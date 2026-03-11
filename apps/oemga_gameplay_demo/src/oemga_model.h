#pragma once
    #include <stdint.h>

    #ifdef __cplusplus
    extern "C" {
    #endif

    #define OEMGA_INPUT_LENGTH 64
    #define OEMGA_INPUT_CHANNELS 1
    #define OEMGA_OUTPUT_CLASSES 4

    void oemga_forward_int8(const int8_t* input_q, int8_t* output_q);
    void oemga_forward_f32(const float* input_f, float* output_f, int input_len);

    #ifdef __cplusplus
    }
    #endif
    