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
#include "i2c_driver.h" 
#include "headmotion.h" 

LOG_MODULE_REGISTER(Ble_MLC_Stream, LOG_LEVEL_INF);

#define I2C_NODE DT_NODELABEL(mysensor)

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


/* Main Example --------------------------------------------------------------*/
void lsm6dsox_mlc(stmdev_ctx_t *dev_ctx, const struct i2c_dt_spec *dev_i2c_ptr) 
{
  /* Variable declaration */
  lsm6dsox_pin_int1_route_t pin_int1_route;
  lsm6dsox_emb_sens_t emb_sens;
  uint8_t mlc_out[8];
  uint8_t whoamI, rst;
  uint32_t i;
  /* Initialize mems driver interface */
   dev_ctx->write_reg = platform_write;
    dev_ctx->read_reg = platform_read;
    dev_ctx->mdelay = NULL;
    dev_ctx->handle = (void *)dev_i2c_ptr;
  /* Wait sensor boot time */
  k_msleep(BOOT_TIME);
  /* Check device ID */
  lsm6dsox_device_id_get(dev_ctx, &whoamI);

  if (whoamI != LSM6DSOX_ID)
    while (1);

  /* Restore default configuration */
  lsm6dsox_reset_set(dev_ctx, PROPERTY_ENABLE);

  do {
    lsm6dsox_reset_get(dev_ctx, &rst);
  } while (rst);

  /* Start Machine Learning Core configuration */
  for ( i = 0; i < (sizeof(headmotionnew_conf_0) /
                    sizeof(struct mems_conf_op) ); i++ ) {
    lsm6dsox_write_reg(dev_ctx, headmotionnew_conf_0[i].address,
                       (uint8_t *)&headmotionnew_conf_0[i].data, 1);
  }

  /* End Machine Learning Core configuration */
  /* At this point the device is ready to run but if you need you can also
   * interact with the device but taking in account the MLC configuration.
   *
   * For more information about Machine Learning Core tool please refer
   * to AN5259 "LSM6DSOX: Machine Learning Core".
   */
  /* Turn off embedded features */
  lsm6dsox_embedded_sens_get(dev_ctx, &emb_sens);
  lsm6dsox_embedded_sens_off(dev_ctx);
  k_msleep(10);
  /* Turn off Sensors */
  lsm6dsox_xl_data_rate_set(dev_ctx, LSM6DSOX_XL_ODR_OFF);
  lsm6dsox_gy_data_rate_set(dev_ctx, LSM6DSOX_GY_ODR_OFF);
  /* Disable I3C interface */
  lsm6dsox_i3c_disable_set(dev_ctx, LSM6DSOX_I3C_DISABLE);
  /* Enable Block Data Update */
  lsm6dsox_block_data_update_set(dev_ctx, PROPERTY_ENABLE);
  /* Set full scale */
  lsm6dsox_xl_full_scale_set(dev_ctx, LSM6DSOX_4g);
  /* Route signals on interrupt pin 1 */
  lsm6dsox_pin_int1_route_get(dev_ctx, &pin_int1_route);
  pin_int1_route.mlc1 = PROPERTY_ENABLE;
  lsm6dsox_pin_int1_route_set(dev_ctx, pin_int1_route);
  /* Configure interrupt pin mode notification */
  lsm6dsox_int_notification_set(dev_ctx,
                                LSM6DSOX_BASE_PULSED_EMB_LATCHED);
  /* Enable embedded features */
  lsm6dsox_embedded_sens_set(dev_ctx, &emb_sens);
  /* Set Output Data Rate.
   * Selected data rate have to be equal or greater with respect
   * with MLC data rate.
   */
  lsm6dsox_xl_data_rate_set(dev_ctx, LSM6DSOX_XL_ODR_52Hz);

}

/* ============================================================================
 * MAIN
 * ============================================================================ */
int main(void) {
    if (gpio_is_ready_dt(&led)) {
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    }

    // Shut down the AD8233 
    if (gpio_is_ready_dt(&sdn_pin)) {
        gpio_pin_configure_dt(&sdn_pin, GPIO_OUTPUT_INACTIVE); 
    }

    static const struct i2c_dt_spec dev_i2c = I2C_DT_SPEC_GET(I2C_NODE);
    if(!device_is_ready(dev_i2c.bus)) {
        LOG_ERR("I2C bus %s is not ready!", dev_i2c.bus->name);
        return -1;
    }

    static stmdev_ctx_t dev_ctx;
    lsm6dsox_mlc(&dev_ctx, &dev_i2c);
    bt_ready();

    // Start last_gesture at an impossible value (99) so it forces a print on the very first loop
    uint8_t current_gesture = 99;

    LOG_INF("Waiting for BLE connection to stream gestures...");
    lsm6dsox_all_sources_t status;
   uint8_t last_gesture = 0;
    uint8_t mlc_out[8]; // Buffer for MLC output status registers

    LOG_INF("MLC Loop Started. Listening for head movements...");

    while (1) {
        /* 1. Check if the Machine Learning Core has a new result */
        lsm6dsox_all_sources_get(&dev_ctx, &status);

        if (status.mlc1) {
            /* 2. Get the specific output (Class 1, 2, 3, or 4) */
            lsm6dsox_mlc_out_get(&dev_ctx, mlc_out);
            uint8_t current_gesture = mlc_out[0];

            /* 3. Only act if the gesture has actually changed */
            if (current_gesture != last_gesture) {
                
                // Log to the local serial console
                switch(current_gesture) {
                    case 1: LOG_INF(">>> GESTURE: HEAD UP [0x01]"); break;
                    case 2: LOG_INF(">>> GESTURE: HEAD DOWN [0x02]"); break;
                    case 3: LOG_INF(">>> GESTURE: HEAD FORWARD [0x03]"); break;
                    case 4: LOG_INF(">>> STATE: IDLE [0x04]"); break;
                    default: LOG_WRN("Unknown MLC Code: %02X", current_gesture); break;
                }

                /* 4. Send the 1-byte gesture code via Bluetooth */
                if (ble_connected) {
                    int err = bt_nus_send(NULL, &current_gesture, 1);
                    if (err) {
                        LOG_WRN("BLE Send Failed (err %d)", err);
                    }
                }

                last_gesture = current_gesture;
            }
        }

        /* 5. Small sleep to prevent I2C bus congestion. 
           The MLC internal ODR is 52Hz, so 20ms polling is plenty. */
        k_msleep(20);
    }
    return 0;
}