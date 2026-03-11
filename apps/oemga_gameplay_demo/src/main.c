#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/services/nus.h>
#include <nrfx_saadc.h>
#include <nrfx_timer.h>
#include <helpers/nrfx_gppi.h>
#include <zephyr/drivers/i2c.h>

#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "i2c_driver.h"
#include "oemga_model.h"
#include "headmotion.h" 

LOG_MODULE_REGISTER(Ble_EMG, LOG_LEVEL_INF);

#define I2C_NODE DT_NODELABEL(mysensor)

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */
#define SAADC_SAMPLE_INTERVAL_US 500   /* 2 kHz */
#define SAADC_BUFFER_SIZE        32    /* ISR buffer size */
#define MODEL_WINDOW_SIZE        64    /* TinyEMGCNNv1 expects 64 samples */
#define TIMER_INSTANCE_NUMBER    22

/* GPIOs */
static const struct gpio_dt_spec fr_pin   = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), ad8233_fr_gpios);
static const struct gpio_dt_spec sdn_pin  = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), ad8233_sdn_gpios);
static const struct gpio_dt_spec acdc_pin = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), ad8233_acdc_gpios);
static const struct gpio_dt_spec led      = GPIO_DT_SPEC_GET(DT_ALIAS(my_led), gpios);

/* SAADC / Timer */
#define NRF_SAADC_INPUT_AIN3 NRF_PIN_PORT_TO_PIN_NUMBER(7U, 1)
static nrfx_saadc_channel_t channel = NRFX_SAADC_DEFAULT_CHANNEL_SE(NRF_SAADC_INPUT_AIN3, 0);
const nrfx_timer_t timer_instance = NRFX_TIMER_INSTANCE(TIMER_INSTANCE_NUMBER);

/* Double-buffering for SAADC */
static int16_t buffer_pool[2][SAADC_BUFFER_SIZE];
static uint32_t buf_index = 0;

/* Thread sync */
K_MSGQ_DEFINE(data_msgq, sizeof(int16_t *), 2, 4);
static volatile bool ble_connected = false;

#define AD8233_SETTLE_MS 5000

static int64_t ad8233_power_on_time_ms = 0;
static bool settle_done = false;

/* rolling model window of 64 samples */
static float model_window[MODEL_WINDOW_SIZE];
static uint32_t window_fill = 0;

/* ============================================================================
 * UNIFIED BLE PAYLOAD (22 Bytes)
 * ============================================================================ */
struct __packed ble_inference_packet {
    uint8_t emg_class;
    uint8_t imu_class;     /* <-- NEW IMU BYTE */
    uint32_t latency_us;
    float scores[OEMGA_OUTPUT_CLASSES];
};

/* ============================================================================
 * BLE SETUP
 * ============================================================================ */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
    } else {
        LOG_INF("Connected");
        ble_connected = true;
        if (gpio_is_ready_dt(&led)) {
            gpio_pin_set_dt(&led, 0);
        }
        nrfx_timer_enable(&timer_instance);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason %u)", reason);
    ble_connected = false;
    if (gpio_is_ready_dt(&led)) {
        gpio_pin_set_dt(&led, 1);
    }
    nrfx_timer_disable(&timer_instance);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

static struct bt_nus_cb nus_cb = {
    .received = NULL,
    .sent = NULL,
};

static void bt_ready(void)
{
    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }
    LOG_INF("Bluetooth initialized");

    err = bt_nus_init(&nus_cb);
    if (err) {
        LOG_ERR("Failed to init NUS (err %d)", err);
        return;
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
    } else {
        LOG_INF("Advertising started");
    }
}

/* ============================================================================
 * ADC & ISR
 * ============================================================================ */
static void saadc_event_handler(nrfx_saadc_evt_t const *p_event)
{
    int16_t *filled_buffer_ptr;

    switch (p_event->type) {
    case NRFX_SAADC_EVT_READY:
        if (ble_connected) {
            nrfx_timer_enable(&timer_instance);
        }
        break;

    case NRFX_SAADC_EVT_BUF_REQ:
        nrfx_saadc_buffer_set(buffer_pool[(buf_index++) % 2], SAADC_BUFFER_SIZE);
        break;

    case NRFX_SAADC_EVT_DONE:
        filled_buffer_ptr = (int16_t *)(p_event->data.done.p_buffer);
        (void)k_msgq_put(&data_msgq, &filled_buffer_ptr, K_NO_WAIT);
        break;

    default:
        break;
    }
}

/* ============================================================================
 * HARDWARE INIT
 * ============================================================================ */
static void ad8233_init(void)
{
    if (!gpio_is_ready_dt(&fr_pin) ||
        !gpio_is_ready_dt(&sdn_pin) ||
        !gpio_is_ready_dt(&acdc_pin)) {
        LOG_ERR("AD8233 GPIOs not ready");
        return;
    }

    gpio_pin_configure_dt(&acdc_pin, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&sdn_pin, GPIO_OUTPUT_ACTIVE);

    k_busy_wait(200);

    gpio_pin_configure_dt(&fr_pin, GPIO_OUTPUT_ACTIVE);

    k_msleep(500);
    k_msleep(10);
    ad8233_power_on_time_ms = k_uptime_get();
    settle_done = false;
}

static void configure_hw(void)
{
    nrfx_timer_config_t timer_config = NRFX_TIMER_DEFAULT_CONFIG(1000000);
    nrfx_timer_init(&timer_instance, &timer_config, NULL);

    uint32_t ticks = nrfx_timer_us_to_ticks(&timer_instance, SAADC_SAMPLE_INTERVAL_US);
    nrfx_timer_extended_compare(&timer_instance,
                                NRF_TIMER_CC_CHANNEL0,
                                ticks,
                                NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK,
                                false);

    IRQ_CONNECT(DT_IRQN(DT_NODELABEL(adc)),
                DT_IRQ(DT_NODELABEL(adc), priority),
                nrfx_isr,
                nrfx_saadc_irq_handler,
                0);

    nrfx_saadc_init(DT_IRQ(DT_NODELABEL(adc), priority));

    channel.channel_config.gain = NRF_SAADC_GAIN1_4;
    nrfx_saadc_channels_config(&channel, 1);

    nrfx_saadc_adv_config_t adv_config = NRFX_SAADC_DEFAULT_ADV_CONFIG;
    nrfx_saadc_advanced_mode_set(BIT(0),
                                 NRF_SAADC_RESOLUTION_12BIT,
                                 &adv_config,
                                 saadc_event_handler);

    nrfx_saadc_buffer_set(buffer_pool[0], SAADC_BUFFER_SIZE);
    nrfx_saadc_buffer_set(buffer_pool[1], SAADC_BUFFER_SIZE);
    nrfx_saadc_mode_trigger();

    uint8_t ch1, ch2;
    nrfx_gppi_channel_alloc(&ch1);
    nrfx_gppi_channel_alloc(&ch2);

    nrfx_gppi_channel_endpoints_setup(
        ch1,
        nrfx_timer_compare_event_address_get(&timer_instance, NRF_TIMER_CC_CHANNEL0),
        nrf_saadc_task_address_get(NRF_SAADC, NRF_SAADC_TASK_SAMPLE));

    nrfx_gppi_channel_endpoints_setup(
        ch2,
        nrf_saadc_event_address_get(NRF_SAADC, NRF_SAADC_EVENT_END),
        nrf_saadc_task_address_get(NRF_SAADC, NRF_SAADC_TASK_START));

    nrfx_gppi_channels_enable(BIT(ch1) | BIT(ch2));
}

/* ============================================================================
 * IMU MLC SETUP AND INIT
 * ============================================================================ */
void lsm6dsox_mlc(stmdev_ctx_t *dev_ctx, const struct i2c_dt_spec *dev_i2c_ptr) 
{
    lsm6dsox_pin_int1_route_t pin_int1_route;
    lsm6dsox_emb_sens_t emb_sens;
    uint8_t whoamI, rst;
    uint32_t i;
    
    dev_ctx->write_reg = platform_write;
    dev_ctx->read_reg = platform_read;
    dev_ctx->mdelay = NULL;
    dev_ctx->handle = (void *)dev_i2c_ptr;
    
    k_msleep(BOOT_TIME);
    lsm6dsox_device_id_get(dev_ctx, &whoamI);

    if (whoamI != LSM6DSOX_ID) {
        while (1);
    }

    lsm6dsox_reset_set(dev_ctx, PROPERTY_ENABLE);

    do {
        lsm6dsox_reset_get(dev_ctx, &rst);
    } while (rst);

    for ( i = 0; i < (sizeof(headmotionnew_conf_0) / sizeof(struct mems_conf_op) ); i++ ) {
        lsm6dsox_write_reg(dev_ctx, headmotionnew_conf_0[i].address,
                           (uint8_t *)&headmotionnew_conf_0[i].data, 1);
    }

    lsm6dsox_embedded_sens_get(dev_ctx, &emb_sens);
    lsm6dsox_embedded_sens_off(dev_ctx);
    k_msleep(10);
    
    lsm6dsox_xl_data_rate_set(dev_ctx, LSM6DSOX_XL_ODR_OFF);
    lsm6dsox_gy_data_rate_set(dev_ctx, LSM6DSOX_GY_ODR_OFF);
    lsm6dsox_i3c_disable_set(dev_ctx, LSM6DSOX_I3C_DISABLE);
    lsm6dsox_block_data_update_set(dev_ctx, PROPERTY_ENABLE);
    lsm6dsox_xl_full_scale_set(dev_ctx, LSM6DSOX_4g);
    
    lsm6dsox_pin_int1_route_get(dev_ctx, &pin_int1_route);
    pin_int1_route.mlc1 = PROPERTY_ENABLE;
    lsm6dsox_pin_int1_route_set(dev_ctx, pin_int1_route);
    
    lsm6dsox_int_notification_set(dev_ctx, LSM6DSOX_BASE_PULSED_EMB_LATCHED);
    lsm6dsox_embedded_sens_set(dev_ctx, &emb_sens);
    lsm6dsox_xl_data_rate_set(dev_ctx, LSM6DSOX_XL_ODR_52Hz);
}

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================ */
static void append_samples_to_window(const int16_t *src, uint32_t count)
{
    if (count > MODEL_WINDOW_SIZE) {
        src += (count - MODEL_WINDOW_SIZE);
        count = MODEL_WINDOW_SIZE;
    }

    if (window_fill < MODEL_WINDOW_SIZE) {
        uint32_t to_copy = MIN(count, MODEL_WINDOW_SIZE - window_fill);
        for (uint32_t i = 0; i < to_copy; i++) {
            model_window[window_fill + i] = (float)src[i];
        }
        window_fill += to_copy;
        return;
    }

    if (count < MODEL_WINDOW_SIZE) {
        memmove(&model_window[0],
                &model_window[count],
                (MODEL_WINDOW_SIZE - count) * sizeof(model_window[0]));
        for (uint32_t i = 0; i < count; i++) {
            model_window[MODEL_WINDOW_SIZE - count + i] = (float)src[i];
        }
    } else {
        for (uint32_t i = 0; i < MODEL_WINDOW_SIZE; i++) {
            model_window[i] = (float)src[i];
        }
    }
}

static uint8_t argmax4(const float *scores)
{
    uint8_t idx = 0;
    float best = scores[0];

    for (uint8_t i = 1; i < OEMGA_OUTPUT_CLASSES; i++) {
        if (scores[i] > best) {
            best = scores[i];
            idx = i;
        }
    }
    return idx;
}

static uint8_t debounce_prediction(uint8_t pred)
{
    static uint8_t stable_pred = 0;
    static uint8_t smile_count = 0;

    if (pred == 3) {
        if (smile_count < 255) {
            smile_count++;
        }
        if (smile_count >= 2) {
            stable_pred = 3;
        }
    } else {
        smile_count = 0;
        stable_pred = pred;
    }

    return stable_pred;
}

static void send_inference_ble(uint8_t emg_class,
                               uint8_t imu_class,
                               uint32_t latency_us,
                               const float *scores)
{
    struct ble_inference_packet pkt;
    int err;

    pkt.emg_class = emg_class;
    pkt.imu_class = imu_class;
    pkt.latency_us = latency_us;
    memcpy(pkt.scores, scores, sizeof(pkt.scores));

    err = bt_nus_send(NULL, (const uint8_t *)&pkt, sizeof(pkt));
    if (err) {
        LOG_WRN("bt_nus_send failed (%d)", err);
    }
}

/* ============================================================================
 * MAIN
 * ============================================================================ */
int main(void)
{
    if (gpio_is_ready_dt(&led)) {
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    }

    static const struct i2c_dt_spec dev_i2c = I2C_DT_SPEC_GET(I2C_NODE);
    if (!device_is_ready(dev_i2c.bus)) {
        printk("I2C bus %s is not ready!\n\r", dev_i2c.bus->name);
        return -1;
    }

    static stmdev_ctx_t dev_ctx;
    lsm6dsox_mlc(&dev_ctx, &dev_i2c);
    ad8233_init();
    configure_hw();
    bt_ready();

    LOG_INF("BLE EMG & IMU classifier ready. Waiting for connection...");

    int16_t *current_buffer;
    uint8_t current_imu_gesture = 4; /* Default to IDLE (4) */

    while (1) {
        /* This blocks exactly 16ms waiting for ADC. Perfect metronome. */
        if (k_msgq_get(&data_msgq, &current_buffer, K_FOREVER) != 0) {
            continue;
        }

        if (!ble_connected) {
            continue;
        }

        /* 1. AD8233 Settle Check */
        if (!settle_done) {
            int64_t elapsed_ms = k_uptime_get() - ad8233_power_on_time_ms;
            if (elapsed_ms < AD8233_SETTLE_MS) {
                continue;
            }
            settle_done = true;
            LOG_INF("AD8233 settle complete. Starting pipeline...");
        }

        /* 2. EMG Rolling Window */
        append_samples_to_window(current_buffer, SAADC_BUFFER_SIZE);
        if (window_fill < MODEL_WINDOW_SIZE) {
            continue;
        }

        /* 3. EMG Preprocessing */
        float input_f[OEMGA_INPUT_LENGTH];
        float output_f[OEMGA_OUTPUT_CLASSES];
        float window_mean = 0.0f;

        for (int i = 0; i < OEMGA_INPUT_LENGTH; i++) {
            window_mean += model_window[i];
        }
        window_mean /= (float)OEMGA_INPUT_LENGTH;

        for (int i = 0; i < OEMGA_INPUT_LENGTH; i++) {
            input_f[i] = model_window[i] - window_mean;
        }

        float rms = 0.0f;
        for (int i = 0; i < OEMGA_INPUT_LENGTH; i++) {
            rms += input_f[i] * input_f[i];
        }
        rms = sqrtf(rms / (float)OEMGA_INPUT_LENGTH);

        /* 4. EMG Inference */
        uint32_t start_cyc = k_cycle_get_32();
        oemga_forward_f32(input_f, output_f, OEMGA_INPUT_LENGTH);
        uint32_t end_cyc = k_cycle_get_32();
        uint32_t latency_us = k_cyc_to_us_floor32(end_cyc - start_cyc);

        uint8_t pred = argmax4(output_f);
        const float idle_rms_thresh = 18.0f;
        if (rms < idle_rms_thresh) {
            pred = 0;
        }
        pred = debounce_prediction(pred);

        /* ============================================================
         * 5. IMU MLC Polling (Non-blocking check)
         * ============================================================ */
        lsm6dsox_all_sources_t status;
        lsm6dsox_all_sources_get(&dev_ctx, &status);

        if (status.mlc1) {
            uint8_t mlc_out[8];
            lsm6dsox_mlc_out_get(&dev_ctx, mlc_out);
            uint8_t new_imu_gesture = mlc_out[0];

            if (new_imu_gesture != current_imu_gesture) {
                switch(new_imu_gesture) {
                    case 1: LOG_INF(">>> IMU GESTURE: HEAD UP [0x01]"); break;
                    case 2: LOG_INF(">>> IMU GESTURE: HEAD DOWN [0x02]"); break;
                    case 3: LOG_INF(">>> IMU GESTURE: HEAD FORWARD [0x03]"); break;
                    case 4: LOG_INF(">>> IMU STATE: IDLE [0x04]"); break;
                    default: LOG_WRN("Unknown MLC Code: %02X", new_imu_gesture); break;
                }
                current_imu_gesture = new_imu_gesture;
            }
        }

        /* 6. Unified BLE Send (22 bytes) */
        send_inference_ble(pred, current_imu_gesture, latency_us, output_f);
    }

    return 0;
}