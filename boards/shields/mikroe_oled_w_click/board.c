/*
 * Copyright (c) 2025 Ian Morris
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>

static int board_init(void)
{
	const struct gpio_dt_spec cs =
		GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), cs_gpios);

	/*
	 * When using the I2C interface, the SPI chip select (CS) input
	 * must be driven low. This would normally be taken care of in
	 * hardware but this shield connects the CS input to the MikroBus
	 * header which in turn is connected to the host MCU and so must
	 * be driven low here.
	 */
	if (!gpio_is_ready_dt(&cs)) {
		return -ENODEV;
	}

	/* SPI chip select (CS) is active low */
	(void)gpio_pin_configure_dt(&cs, GPIO_OUTPUT_ACTIVE);

	return 0;
}

SYS_INIT(board_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
