#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#include <zephyr/drivers/i2c.h>
#include "lsm6dsox_reg.h"

#define IMU_ADDRESS                   0x6A
#define BOOT_TIME                     15 //ms
#define LP_I2C_TRANS_TIMEOUT_CYCLES   5000
#define LP_I2C_TRANS_WAIT_FOREVER     -1

int platform_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len);
int platform_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len);

#endif