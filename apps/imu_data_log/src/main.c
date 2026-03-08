#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/services/nus.h>
#include <zephyr/drivers/i2c.h>
#include "i2c_driver.h" // Assuming this contains your platform_write/read and ST driver includes

LOG_MODULE_REGISTER(Ble_IMU_Stream, LOG_LEVEL_INF);

#define I2C_NODE DT_NODELABEL(mysensor)

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */
#define ACCEL_SAMPLES_PER_PACKET 10  // Send 10 samples (X,Y,Z) per BLE notification
#define BLE_PAYLOAD_SIZE (ACCEL_SAMPLES_PER_PACKET * 3 * sizeof(int16_t)) // 60 bytes

static const struct gpio_dt_spec sdn_pin = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), ad8233_sdn_gpios);
static const struct gpio_dt_spec led     = GPIO_DT_SPEC_GET(DT_ALIAS(my_led), gpios);

static volatile bool ble_connected = false;

/* ============================================================================
 * BLE SETUP
 * ============================================================================ */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

static void connected(struct bt_conn *conn, uint8_t err) {
    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
    } else {
        LOG_INF("Connected!");
        ble_connected = true;
        gpio_pin_set_dt(&led, 0);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    LOG_INF("Disconnected (reason %u)", reason);
    ble_connected = false;
    gpio_pin_set_dt(&led, 1);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

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
    bt_nus_init(&nus_cb);
    bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
    LOG_INF("Bluetooth initialized and advertising");
}

/* ============================================================================
 * IMU INITIALIZATION
 * ============================================================================ */
void lsm6dsox_init_for_data_collection(stmdev_ctx_t *dev_ctx, const struct i2c_dt_spec *dev_i2c_ptr)
{
    uint8_t whoamI, rst;

    dev_ctx->write_reg = platform_write;
    dev_ctx->read_reg = platform_read;
    dev_ctx->mdelay = NULL;
    dev_ctx->handle = (void *)dev_i2c_ptr;

    k_msleep(10); // Boot time

    lsm6dsox_device_id_get(dev_ctx, &whoamI);
    if (whoamI != LSM6DSOX_ID) {
        LOG_ERR("LSM6DSOX not found! WHOAMI: 0x%02x", whoamI);
        while (1) { k_msleep(1000); }
    }

    /* Reset device */
    lsm6dsox_reset_set(dev_ctx, PROPERTY_ENABLE);
    do {
        lsm6dsox_reset_get(dev_ctx, &rst);
    } while (rst);

    lsm6dsox_i3c_disable_set(dev_ctx, LSM6DSOX_I3C_DISABLE);
    lsm6dsox_block_data_update_set(dev_ctx, PROPERTY_ENABLE);

    /* Disable Gyro to save power */
    lsm6dsox_gy_data_rate_set(dev_ctx, LSM6DSOX_GY_ODR_OFF);

    /* Turn ON Accelerometer: 52Hz, ±4g */
    lsm6dsox_xl_data_rate_set(dev_ctx, LSM6DSOX_XL_ODR_52Hz);
    lsm6dsox_xl_full_scale_set(dev_ctx, LSM6DSOX_4g);

    LOG_INF("LSM6DSOX Initialized for Accel 52Hz");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */
int main(void) {
    if (gpio_is_ready_dt(&led)) {
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    }

    // Shut down the AD8233 to prevent it from drawing power or floating
    if (gpio_is_ready_dt(&sdn_pin)) {
        gpio_pin_configure_dt(&sdn_pin, GPIO_OUTPUT_INACTIVE); 
    }

    static const struct i2c_dt_spec dev_i2c = I2C_DT_SPEC_GET(I2C_NODE);
    if(!device_is_ready(dev_i2c.bus)) {
        LOG_ERR("I2C bus %s is not ready!", dev_i2c.bus->name);
        return -1;
    }

    static stmdev_ctx_t dev_ctx;
    lsm6dsox_init_for_data_collection(&dev_ctx, &dev_i2c);
    
    bt_ready();

    int16_t accel_buffer[ACCEL_SAMPLES_PER_PACKET * 3]; // Stores X, Y, Z
    uint8_t sample_count = 0;
    uint8_t data_ready;

    LOG_INF("Waiting for BLE connection to stream IMU data...");

    while (1) {
        if (!ble_connected) {
            k_msleep(100);
            continue;
        }

        // Check if new data is available in the IMU registers
        lsm6dsox_xl_flag_data_ready_get(&dev_ctx, &data_ready);

        if (data_ready) {
            // Read X, Y, Z into the current slot in the buffer
            lsm6dsox_acceleration_raw_get(&dev_ctx, &accel_buffer[sample_count * 3]);
            sample_count++;

            // Once we have 10 samples, blast it over BLE
            if (sample_count >= ACCEL_SAMPLES_PER_PACKET) {
                int err = bt_nus_send(NULL, (uint8_t *)accel_buffer, BLE_PAYLOAD_SIZE);
                if (err) {
                    LOG_WRN("BLE send failed, buffer full? (err %d)", err);
                }
                sample_count = 0; 
            }
        }

        // 52Hz means a new sample every ~19.2ms. Sleep 5ms to avoid spamming I2C bus.
        k_msleep(5); 
    }
    return 0;
}