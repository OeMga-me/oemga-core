#include <stdint.h>
#include "oemga_model.h"
#include "oemga_weights.h"
#include "nn_layers.h"

static int8_t buf0[512] OEMGA_ALIGN16;
static int8_t buf1[512] OEMGA_ALIGN16;
static int8_t scratch0[288] OEMGA_ALIGN16;

void oemga_forward_int8(const int8_t* input_q, int8_t* output_q) {
    for (int i = 0; i < OEMGA_INPUT_LENGTH * OEMGA_INPUT_CHANNELS; i++) buf0[i] = input_q[i];

    // conv0
    conv1d_s8(buf0, conv0_weight_q, conv0_bias_q, buf1, 1, 8, 64, 5, 2, conv0_out_mult_q31, conv0_out_shift, scratch0);
    relu_s8(buf1, buf0, 512);
    maxpool1d_s8(buf0, buf1, 8, 64, 2, 2);
    // conv1
    conv1d_s8(buf1, conv1_weight_q, conv1_bias_q, buf0, 8, 16, 32, 5, 2, conv1_out_mult_q31, conv1_out_shift, scratch0);
    relu_s8(buf0, buf1, 512);
    maxpool1d_s8(buf1, buf0, 16, 32, 2, 2);
    // reshape0: view only
    linear_s8(buf0, fc0_weight_q, fc0_bias_q, buf1, 256, 32, fc0_out_mult_q31, fc0_out_shift);
    relu_s8(buf1, buf0, 32);
    linear_s8(buf0, fc1_weight_q, fc1_bias_q, buf1, 32, 4, fc1_out_mult_q31, fc1_out_shift);
    // output_alias0: view only

    for (int i = 0; i < OEMGA_OUTPUT_CLASSES; i++) output_q[i] = buf1[i];
}

void oemga_forward_f32(const float* input_f, float* output_f, int input_len) {
    for (int i = 0; i < input_len; i++) {
        float scaled = input_f[i] / OEMGA_INPUT_SCALE;
        int32_t q = (int32_t)(scaled >= 0 ? (scaled + 0.5f) : (scaled - 0.5f));
        if (q > 127) q = 127;
        if (q < -128) q = -128;
        buf0[i] = (int8_t)q;
    }
    int8_t out_q[OEMGA_OUTPUT_CLASSES] = {0};
    oemga_forward_int8(buf0, out_q);
    dequant_s8_to_f32(out_q, output_f, OEMGA_OUTPUT_CLASSES, OEMGA_FINAL_OUT_SCALE);
}
