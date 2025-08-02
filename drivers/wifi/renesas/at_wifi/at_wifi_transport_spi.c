/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_at_wifi_spi

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>

#include "at_wifi.h"

static K_SEM_DEFINE(sem_int, 0, 1);

static struct spi_dt_spec bus = SPI_DT_SPEC_INST_GET(0, SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_WORD_SET(8), 0);
static struct gpio_dt_spec int_gpio = GPIO_DT_SPEC_INST_GET(0, int_gpios);
static struct gpio_callback int_gpio_cb;

static void int_gpio_handler(const struct device *dev_gpio,
					struct gpio_callback *cb, uint32_t pins)
{
	k_sem_give(&sem_int);
}

int at_wifi_transport_init(struct at_wifi_data *data)
{
	int ret;

	if (!gpio_is_ready_dt(&int_gpio)) {
		return -EIO;
	}

	ret = gpio_pin_configure_dt(&int_gpio, GPIO_INPUT);
	if (ret) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&int_gpio, GPIO_INT_EDGE_FALLING);
	if (ret) {
		return ret;
	}

	gpio_init_callback(&int_gpio_cb, int_gpio_handler, BIT(int_gpio.pin));

	ret = gpio_add_callback(int_gpio.port, &int_gpio_cb);
	if (ret) {
		return ret;
	}

	return 0;
}

int at_wifi_transport_rx_wait(struct at_wifi_data *data, k_timeout_t timeout)
{
	int ret;
	struct spi_buf buf[1];
	struct spi_buf_set rx;
	uint8_t header[8];

	ret = k_sem_take(&sem_int, timeout);
	if (ret) {
		return ret;
	}

	buf[0].buf = (void *)header;
	buf[0].len = sizeof(header);

	rx.count = 1;
	rx.buffers = (const struct spi_buf *)&buf;

	ret = spi_read_dt(&bus, &rx);

	return 0;
}
