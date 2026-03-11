#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * OEMGA NN LAYERS - v1 minimal backend
 *
 * Assumptions:
 * - int8 activations
 * - int8 weights
 * - int32 bias
 * - per-output-channel requant params:
 *     out_mult_q31 : int32
 *     out_shift    : int8
 * - Conv1d layout:
 *     input  = [Cin, L]
 *     weight = [Cout, Cin, K]
 *     output = [Cout, Lout]
 *
 * The generated code currently assumes:
 * - stride = 1 for conv
 * - symmetric padding
 * - groups = 1
 * - N = 1 batch only
 * ============================================================ */

#ifndef OEMGA_RESTRICT
  #if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
    #define OEMGA_RESTRICT restrict
  #else
    #define OEMGA_RESTRICT
  #endif
#endif

#ifndef OEMGA_ALIGN16
  #if defined(__GNUC__) || defined(__clang__)
    #define OEMGA_ALIGN16 __attribute__((aligned(16)))
  #else
    #define OEMGA_ALIGN16
  #endif
#endif

/* ============================================================
 * Helpers
 * ============================================================ */

static inline int8_t oemga_sat_s8(int32_t x) {
    if (x > 127) return 127;
    if (x < -128) return -128;
    return (int8_t)x;
}

static inline int32_t oemga_rounding_divide_by_pot(int32_t x, int shift) {
    if (shift <= 0) return x;
    const int32_t mask = (1 << shift) - 1;
    const int32_t remainder = x & mask;
    const int32_t threshold = (mask >> 1) + ((x < 0) ? 1 : 0);
    return (x >> shift) + (remainder > threshold);
}

/*
 * Requantization:
 *   y_int8 ~= acc_int32 * mult_q31 / 2^(31 + shift)
 *
 * This follows the usual fixed-point style:
 *   scaled = round((acc * mult_q31) / 2^31)
 *   y      = round(scaled / 2^shift)
 */
static inline int32_t oemga_requantize_q31(int32_t acc, int32_t mult_q31, int shift) {
    int64_t prod = (int64_t)acc * (int64_t)mult_q31;

    /* rounding divide by 2^31 */
    int64_t scaled = (prod + (1LL << 30)) >> 31;

    if (shift > 0) {
        const int64_t offset = 1LL << (shift - 1);
        if (scaled >= 0) {
            scaled = (scaled + offset) >> shift;
        } else {
            scaled = -(((-scaled) + offset) >> shift);
        }
    }

    if (scaled > 2147483647LL) scaled = 2147483647LL;
    if (scaled < -2147483648LL) scaled = -2147483648LL;
    return (int32_t)scaled;
}

static inline void dequant_s8_to_f32(
    const int8_t* OEMGA_RESTRICT input,
    float* OEMGA_RESTRICT output,
    int count,
    float scale
) {
    for (int i = 0; i < count; i++) {
        output[i] = ((float)input[i]) * scale;
    }
}

/* ============================================================
 * ReLU
 * ============================================================ */

static inline void relu_s8(
    const int8_t* OEMGA_RESTRICT input,
    int8_t* OEMGA_RESTRICT output,
    int count
) {
    for (int i = 0; i < count; i++) {
        int8_t x = input[i];
        output[i] = (x < 0) ? 0 : x;
    }
}

/* ============================================================
 * MaxPool1d
 *
 * Layout:
 *   input  = [channels, length]
 *   output = [channels, out_length]
 * ============================================================ */

static inline void maxpool1d_s8(
    const int8_t* OEMGA_RESTRICT input,
    int8_t* OEMGA_RESTRICT output,
    int channels,
    int length,
    int kernel,
    int stride
) {
    const int out_length = ((length - kernel) / stride) + 1;

    for (int c = 0; c < channels; c++) {
        const int8_t* in_c = &input[c * length];
        int8_t* out_c = &output[c * out_length];

        for (int i = 0; i < out_length; i++) {
            int start = i * stride;
            int8_t vmax = in_c[start];

            for (int k = 1; k < kernel; k++) {
                int8_t v = in_c[start + k];
                if (v > vmax) vmax = v;
            }

            out_c[i] = vmax;
        }
    }
}

/* ============================================================
 * Conv1d scratch helper
 *
 * Makes zero-padded contiguous input:
 *   in_pad shape = [Cin, L + 2*padding]
 * ============================================================ */

static inline void oemga_make_padded_input_s8(
    const int8_t* OEMGA_RESTRICT input,
    int8_t* OEMGA_RESTRICT in_pad,
    int in_channels,
    int length,
    int padding
) {
    const int Lp = length + 2 * padding;

    for (int ic = 0; ic < in_channels; ic++) {
        int8_t* dst = &in_pad[ic * Lp];
        const int8_t* src = &input[ic * length];

        for (int i = 0; i < Lp; i++) {
            dst[i] = 0;
        }
        for (int i = 0; i < length; i++) {
            dst[padding + i] = src[i];
        }
    }
}

/* ============================================================
 * Conv1d
 *
 * Layout:
 *   input  = [Cin, L]
 *   weight = [Cout, Cin, K]
 *   bias   = [Cout]
 *   output = [Cout, L]   (for stride=1, same-length style with padding)
 *
 * Current v1 assumptions:
 * - stride = 1
 * - groups = 1
 * - dilation = 1
 * - output length == input length
 * ============================================================ */

static inline void conv1d_s8(
    const int8_t* OEMGA_RESTRICT input,
    const int8_t* OEMGA_RESTRICT weight,
    const int32_t* OEMGA_RESTRICT bias,
    int8_t* OEMGA_RESTRICT output,
    int in_ch,
    int out_ch,
    int length,
    int kernel_size,
    int padding,
    const int32_t* OEMGA_RESTRICT out_mult_q31,
    const int8_t* OEMGA_RESTRICT out_shift,
    int8_t* OEMGA_RESTRICT scratch
) {
    const int Lp = length + 2 * padding;
    const int w_ic_stride = kernel_size;
    const int w_oc_stride = in_ch * kernel_size;

    oemga_make_padded_input_s8(input, scratch, in_ch, length, padding);

    for (int oc = 0; oc < out_ch; oc++) {
        const int8_t* w_oc = &weight[oc * w_oc_stride];
        const int32_t b = bias ? bias[oc] : 0;
        const int32_t mult = out_mult_q31[oc];
        const int sh = (int)out_shift[oc];
        int8_t* out_oc = &output[oc * length];

        for (int x = 0; x < length; x++) {
            int32_t acc = b;

            for (int ic = 0; ic < in_ch; ic++) {
                const int8_t* in_ic = &scratch[ic * Lp + x];
                const int8_t* w_ic = &w_oc[ic * w_ic_stride];

                for (int k = 0; k < kernel_size; k++) {
                    acc += (int32_t)in_ic[k] * (int32_t)w_ic[k];
                }
            }

            int32_t y = oemga_requantize_q31(acc, mult, sh);
            out_oc[x] = oemga_sat_s8(y);
        }
    }
}

/* ============================================================
 * Linear
 *
 * Layout:
 *   input  = [in_features]
 *   weight = [out_features, in_features]
 *   bias   = [out_features]
 *   output = [out_features]
 * ============================================================ */

static inline void linear_s8(
    const int8_t* OEMGA_RESTRICT input,
    const int8_t* OEMGA_RESTRICT weight,
    const int32_t* OEMGA_RESTRICT bias,
    int8_t* OEMGA_RESTRICT output,
    int in_features,
    int out_features,
    const int32_t* OEMGA_RESTRICT out_mult_q31,
    const int8_t* OEMGA_RESTRICT out_shift
) {
    for (int of = 0; of < out_features; of++) {
        const int8_t* w_row = &weight[of * in_features];
        int32_t acc = bias ? bias[of] : 0;

        for (int i = 0; i < in_features; i++) {
            acc += (int32_t)input[i] * (int32_t)w_row[i];
        }

        int32_t y = oemga_requantize_q31(acc, out_mult_q31[of], (int)out_shift[of]);
        output[of] = oemga_sat_s8(y);
    }
}

#ifdef __cplusplus
}
#endif