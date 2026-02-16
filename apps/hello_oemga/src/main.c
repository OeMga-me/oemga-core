#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hello_oemga, LOG_LEVEL_INF);

/* * This sample assumes the standard 'led0' alias is defined in the 
 * Device Tree. On the upstream DVK, this is usually LED 1.
 * We will verify this works even with our Overlay.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
    int ret;

    LOG_INF("booting oemga_alpha...");
    LOG_INF("Board target: %s", CONFIG_BOARD);

    if (!gpio_is_ready_dt(&led)) {
        LOG_ERR("LED device not ready");
        return 0;
    }

    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        return 0;
    }

    while (1) {
        ret = gpio_pin_toggle_dt(&led);
        LOG_INF("Hello OEMGA! LED State: %d", ret);
        k_msleep(1000);
    }
    return 0;
}
