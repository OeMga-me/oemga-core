#include "i2c_driver.h"

/*
 * @brief  Write generic device register (platform dependent)
 */
int platform_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len)
{
    const struct i2c_dt_spec *dev_i2c_ptr = (const struct i2c_dt_spec *)handle;

    /* Use Zephyr's burst write.
     * It writes 'reg' first, then follows with 'len' bytes from 'bufp'.
     * This avoids creating a temporary buffer on the stack.
     */
    int ret = i2c_burst_write_dt(dev_i2c_ptr, reg, bufp, len);

    if (ret != 0) {
        printk("I2C Write failed! Addr: 0x%02x, Reg: 0x%02x, Err: %d\n", 
               dev_i2c_ptr->addr, reg, ret);
    }

    return ret;
}

/*
 * @brief  Read generic device register (platform dependent)
 */
int platform_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len)
{
    const struct i2c_dt_spec *dev_i2c_ptr = (const struct i2c_dt_spec *)handle;

    /* Use Zephyr's burst read.
     * CRITICAL: This performs a "Write (Reg) + Repeated Start + Read (Data)" transaction.
     * Your previous code sent a STOP condition between write and read, 
     * which causes most sensors to reset the register pointer.
     */
    int ret = i2c_burst_read_dt(dev_i2c_ptr, reg, bufp, len);

    if (ret != 0) {
        printk("I2C Read failed! Addr: 0x%02x, Reg: 0x%02x, Err: %d\n", 
               dev_i2c_ptr->addr, reg, ret);
    }

    return ret;
}