/*
 * Copyright 2024 Ian Morris
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>

#define LOG_LEVEL CONFIG_BT_HCI_DRIVER_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_hci_da1453x);

#define DT_DRV_COMPAT renesas_bt_hci_da1453x

int bt_hci_transport_setup(const struct device *h4)
{
	int err = 0;

#if DT_INST_NODE_HAS_PROP(0, bt_reset_gpios)
	char c;
	struct gpio_dt_spec bt_reset = GPIO_DT_SPEC_GET(DT_DRV_INST(0), bt_reset_gpios);

	if (!gpio_is_ready_dt(&bt_reset)) {
		LOG_ERR("Error: failed to configure bt_reset %s pin %d",
			bt_reset.port->name, bt_reset.pin);
		return -EIO;
	}

	/* Set bt_reset as output driving high which will reset the DA1453x  */
	err = gpio_pin_configure_dt(&bt_reset, GPIO_OUTPUT_HIGH);
	if (err) {
		LOG_ERR("Error %d: failed to configure bt_reset %s pin %d",
			err, bt_reset.port->name, bt_reset.pin);
		return err;
	}

	/* Release the DA1453x from reset */
	err = gpio_pin_set_dt(&bt_reset, 0);
	if (err) {
		return err;
	}

	/* Wait for the DA1453x to boot */
	k_sleep(K_MSEC(50));

	/* Drain bytes */
	while (h4 && uart_fifo_read(h4, &c, 1)) {
		continue;
	}
#endif /* DT_INST_NODE_HAS_PROP(0, bt_reset_gpios) */

	return err;
}
