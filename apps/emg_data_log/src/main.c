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
#include "i2c_driver.h"
#include <math.h>

LOG_MODULE_REGISTER(Ble_EMG, LOG_LEVEL_INF);

#define I2C_NODE DT_NODELABEL(mysensor)
/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */
#define SAADC_SAMPLE_INTERVAL_US 500   // 2000Hz (0.5ms)
#define SAADC_BUFFER_SIZE        48  
#define TIMER_INSTANCE_NUMBER    22
#define BLE_MAX_PAYLOAD          200   // Max bytes per NUS packet

/* GPIOs */
static const struct gpio_dt_spec fr_pin     = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), ad8233_fr_gpios);
static const struct gpio_dt_spec sdn_pin    = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), ad8233_sdn_gpios);
static const struct gpio_dt_spec acdc_pin   = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), ad8233_acdc_gpios);
static const struct gpio_dt_spec led        = GPIO_DT_SPEC_GET(DT_ALIAS(my_led), gpios);

/* SAADC / Timer */
#define NRF_SAADC_INPUT_AIN3 NRF_PIN_PORT_TO_PIN_NUMBER(7U, 1)
static nrfx_saadc_channel_t channel = NRFX_SAADC_DEFAULT_CHANNEL_SE(NRF_SAADC_INPUT_AIN3, 0);
const nrfx_timer_t timer_instance = NRFX_TIMER_INSTANCE(TIMER_INSTANCE_NUMBER);

/* Double Buffering */
static int16_t buffer_pool[2][SAADC_BUFFER_SIZE];
static uint32_t buf_index = 0;

/* Thread Sync */
K_MSGQ_DEFINE(data_msgq, sizeof(int16_t *), 2, 4);
static volatile bool ble_connected = false;


/* ============================================================================
 * BLE SETUP
 * ============================================================================ */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL), // Requires CONFIG_BT_NUS=y
};

static void connected(struct bt_conn *conn, uint8_t err) {
    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
    } else {
        LOG_INF("Connected!");
        ble_connected = true;
        gpio_pin_set_dt(&led, 0);
        nrfx_timer_enable(&timer_instance); // Start sampling
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    LOG_INF("Disconnected (reason %u)", reason);
    ble_connected = false;
    gpio_pin_set_dt(&led, 1);
    nrfx_timer_disable(&timer_instance); // Stop sampling
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};


/* ============================================================================
 * ADC & ISR
 * ============================================================================ */
static void saadc_event_handler(nrfx_saadc_evt_t const * p_event) {
    int16_t *filled_buffer_ptr;
    switch (p_event->type) {
        case NRFX_SAADC_EVT_READY:
            if(ble_connected) nrfx_timer_enable(&timer_instance);
            break;                        
        case NRFX_SAADC_EVT_BUF_REQ:
            nrfx_saadc_buffer_set(buffer_pool[(buf_index++) % 2], SAADC_BUFFER_SIZE);
            break;
        case NRFX_SAADC_EVT_DONE:
            filled_buffer_ptr = (int16_t *)(p_event->data.done.p_buffer);
            // Send pointer to main thread (don't process here!)
            k_msgq_put(&data_msgq, &filled_buffer_ptr, K_NO_WAIT);
            break;
        default: break;
    }
}

/* ============================================================================
 * HARDWARE INIT
 * ============================================================================ */
void ad8233_init(void){
  /* 0. Safety Check: Verify device drivers are ready */
    if (!gpio_is_ready_dt(&fr_pin) || 
        !gpio_is_ready_dt(&sdn_pin) || 
        !gpio_is_ready_dt(&acdc_pin)) {
        return; // Handle error appropriately in production
    }

    /* 1. Set AC/DC BAR to GND */
    // GPIO_OUTPUT_INACTIVE sets the pin to logical 0 (GND/Low) immediately
    gpio_pin_configure_dt(&acdc_pin, GPIO_OUTPUT_INACTIVE);

    /* 2. Set SDN to High (Sensor ON) */
    // GPIO_OUTPUT_ACTIVE sets the pin to logical 1 (High) immediately
    gpio_pin_configure_dt(&sdn_pin, GPIO_OUTPUT_ACTIVE);

    /* 3. Wait 200us */
    k_busy_wait(200);

    /* 4. Set Fast Restore (FR) to High */
    gpio_pin_configure_dt(&fr_pin, GPIO_OUTPUT_ACTIVE);

    /* 5. Wait 500ms */
    k_msleep(500);
    
    /* 7. Wait 10ms */
    k_msleep(10);
}

static void configure_hw(void) {
    // Timer
    nrfx_timer_config_t timer_config = NRFX_TIMER_DEFAULT_CONFIG(1000000);
    nrfx_timer_init(&timer_instance, &timer_config, NULL);
    uint32_t ticks = nrfx_timer_us_to_ticks(&timer_instance, SAADC_SAMPLE_INTERVAL_US);
    nrfx_timer_extended_compare(&timer_instance, NRF_TIMER_CC_CHANNEL0, ticks, NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, false);

    // SAADC
    IRQ_CONNECT(DT_IRQN(DT_NODELABEL(adc)), DT_IRQ(DT_NODELABEL(adc), priority), nrfx_isr, nrfx_saadc_irq_handler, 0);
    nrfx_saadc_init(DT_IRQ(DT_NODELABEL(adc), priority));
    channel.channel_config.gain = NRF_SAADC_GAIN1_4;
    nrfx_saadc_channels_config(&channel, 1);
    
    nrfx_saadc_adv_config_t adv_config = NRFX_SAADC_DEFAULT_ADV_CONFIG;
    nrfx_saadc_advanced_mode_set(BIT(0), NRF_SAADC_RESOLUTION_12BIT, &adv_config, saadc_event_handler);
    nrfx_saadc_buffer_set(buffer_pool[0], SAADC_BUFFER_SIZE);
    nrfx_saadc_buffer_set(buffer_pool[1], SAADC_BUFFER_SIZE);
    nrfx_saadc_mode_trigger();

    // PPI (Connect Timer -> ADC)
    uint8_t ch1, ch2;
    nrfx_gppi_channel_alloc(&ch1);
    nrfx_gppi_channel_alloc(&ch2);
    nrfx_gppi_channel_endpoints_setup(ch1, nrfx_timer_compare_event_address_get(&timer_instance, NRF_TIMER_CC_CHANNEL0), nrf_saadc_task_address_get(NRF_SAADC, NRF_SAADC_TASK_SAMPLE));
    nrfx_gppi_channel_endpoints_setup(ch2, nrf_saadc_event_address_get(NRF_SAADC, NRF_SAADC_EVENT_END), nrf_saadc_task_address_get(NRF_SAADC, NRF_SAADC_TASK_START));
    nrfx_gppi_channels_enable(BIT(ch1) | BIT(ch2));
}

/* Main Example --------------------------------------------------------------*/
void lsm6dsox_self_test(void *handle)
{
  const struct i2c_dt_spec *dev_i2c_ptr = (const struct i2c_dt_spec *)handle;
  static stmdev_ctx_t dev_ctx;
  uint8_t whoamI;
  uint8_t rst;
  /* Initialize mems driver interface */
  dev_ctx.write_reg = platform_write;
  dev_ctx.read_reg = platform_read;
  dev_ctx.mdelay = NULL;
  dev_ctx.handle = (void *)dev_i2c_ptr;
  /* Wait sensor boot time */
  k_msleep(BOOT_TIME);
  /* Check device ID */
  lsm6dsox_device_id_get(&dev_ctx, &whoamI);
  printk("whoami reg is %x\n\r", whoamI);

  if (whoamI != LSM6DSOX_ID)
    while (1);

  /* Restore default configuration */
  lsm6dsox_reset_set(&dev_ctx, PROPERTY_ENABLE);
  do {
    lsm6dsox_reset_get(&dev_ctx, &rst);
  } while (rst);
  /* Disable I3C interface */
  lsm6dsox_i3c_disable_set(&dev_ctx, LSM6DSOX_I3C_DISABLE);
  /* Disable XL. */
  lsm6dsox_xl_data_rate_set(&dev_ctx, LSM6DSOX_XL_ODR_OFF);
  /* Disable GYRO. */
  lsm6dsox_gy_data_rate_set(&dev_ctx, LSM6DSOX_GY_ODR_OFF);
  /* 3. Disable High-Performance Mode (Required for ULP) */
  lsm6dsox_xl_power_mode_set(&dev_ctx, LSM6DSOX_ULTRA_LOW_POWER_MD);
  /* Enable Block Data Update */
  lsm6dsox_block_data_update_set(&dev_ctx, PROPERTY_ENABLE);
  /* Set Output Data Rate */
//  lsm6dsox_xl_data_rate_set(&dev_ctx, LSM6DSOX_XL_ODR_12Hz5);
//   /* Set full scale */
//   lsm6dsox_xl_full_scale_set(&dev_ctx, LSM6DSOX_4g);
  /* Wait stable output */
  k_msleep(100);
  // read_data();
}

/* Since you don't need inputs, we define an empty callback struct */
static struct bt_nus_cb nus_cb = {
    .received = NULL,
    .sent = NULL,
};

static void bt_ready(void) {
    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }
    LOG_INF("Bluetooth initialized");

    // Initialize the Nordic UART Service
    err = bt_nus_init(&nus_cb);
    if (err) {
        LOG_ERR("Failed to init NUS (err: %d)", err);
        return;
    }

    // Start Advertising (Connectable + Name)
    err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
    }
}

/* ============================================================================
 * MAIN
 * ============================================================================ */
int main(void) {
    if (gpio_is_ready_dt(&led)) {
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    }
    static const struct i2c_dt_spec dev_i2c = I2C_DT_SPEC_GET(I2C_NODE);
	if(!device_is_ready(dev_i2c.bus)) {
		printk("I2C bus %s is not ready!\n\r", dev_i2c.bus->name);
		return -1;
	}
    lsm6dsox_self_test((void *)&dev_i2c);
    ad8233_init();
    configure_hw();
    bt_ready(); // Start Advertising

    int16_t *current_buffer;
    uint8_t *byte_ptr; 
    uint32_t bytes_remaining;
    uint32_t chunk_size;

    LOG_INF("BLE AD8233 Streamer Ready. Waiting for connection...");

    while (1) {
        // Wait for buffer from ISR
        if (k_msgq_get(&data_msgq, &current_buffer, K_FOREVER) == 0) {

            if (!ble_connected) continue;
            
            // --- NEW: RANGE MONITORING BLOCK ---
            int16_t min_val = 4095;
            int16_t max_val = 0;
            uint32_t avg_acc = 0;

            for (int k = 0; k < SAADC_BUFFER_SIZE; k++) {
                if (current_buffer[k] < min_val) min_val = current_buffer[k];
                if (current_buffer[k] > max_val) max_val = current_buffer[k];
                avg_acc += current_buffer[k];
            }
            int16_t current_avg = (int16_t)(avg_acc / SAADC_BUFFER_SIZE);

            // Every second, print the "Health" of your signal
            // LOG_INF("RAW SIGNAL -> Min: %d | Max: %d | Avg (Baseline): %d", 
            //         min_val, max_val, current_avg);
            // PREPARE TO SEND
            bytes_remaining = SAADC_BUFFER_SIZE * sizeof(int16_t);
            byte_ptr = (uint8_t *)current_buffer;

            while(bytes_remaining > 0 && ble_connected) {
                // Determine packet size (Max 240, or whatever is left)
                chunk_size = (bytes_remaining > BLE_MAX_PAYLOAD) ? BLE_MAX_PAYLOAD : bytes_remaining;

                // Send via Nordic UART Service
                int err = bt_nus_send(NULL, byte_ptr, chunk_size);
                
                if (err) {
                    // If buffer is full, wait a tiny bit and retry
                    k_usleep(100); 
                } else {
                    // Success! Advance pointers
                    byte_ptr += chunk_size;
                    bytes_remaining -= chunk_size;
                }
            }
        }
    }
    return 0;
}
