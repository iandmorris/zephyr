/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_at_wifi_spi

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(at_wifi_transport, CONFIG_WIFI_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>

#include "at_wifi.h"

#define CMD_ADDR	0x50080260
#define RSP_ADDR	0x50080258

#define RD_CMD		0xC0
#define WR_CMD		0x80

#define HDR_LEN		8

struct modem_iface_spi_config {
	char *rx_rb_buf;
	size_t rx_rb_buf_len;
	const struct device *dev;
};

static K_HEAP_DEFINE(driver_heap, CONFIG_WIFI_AT_WIFI_HEAP_SIZE);
static K_SEM_DEFINE(sem_int, 0, 1);

static struct spi_dt_spec bus = SPI_DT_SPEC_INST_GET(0, SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8));
static struct gpio_dt_spec int_gpio = GPIO_DT_SPEC_INST_GET(0, int_gpios);
static struct gpio_callback int_gpio_cb;

static void int_gpio_handler(const struct device *dev_gpio,
					struct gpio_callback *cb, uint32_t pins)
{
	k_sem_give(&sem_int);
}

static uint16_t trim_padding_end(uint8_t *buf, uint16_t len, uint8_t pad_byte)
{
	if ((buf == NULL) || (len == 0)) {
		return 0;
	}

    while ((len > 0) && (buf[len - 1] == pad_byte)) {
        len--;
    }

    return len;
}

static int parse_hdr(const uint8_t *hdr, uint32_t *addr, uint8_t *status,
				 uint16_t *len)
{
	*addr = sys_get_be32(hdr);

	// TODO - Looks like length field is little endian, check and raise ticket if required
	//*buf_len = sys_get_be16(rsp_hdr + sizeof(uint32_t));
	*len = hdr[sizeof(uint32_t) + 1];
	*len <<= 8;
	*len |= hdr[sizeof(uint32_t)];

	// TODO - fix magic number?
	*status = hdr[6];

	// TODO - remove magic number
	if ((*len == 0) || (*status != 0x83)) {
		return -EBADMSG;
	}

	return 0;
}

static void build_hdr(uint8_t *buf, uint32_t addr, uint8_t cmd,
				 uint32_t len)
{
	sys_put_be32(addr, buf);
	buf += sizeof(addr);
	*buf = cmd;
	buf += sizeof(cmd);
	sys_put_be24(len, buf);
}

static int read_from_device(uint32_t addr, uint16_t len, uint8_t *data)
{
	int ret;
	struct spi_buf tx_buf[1];
	struct spi_buf rx_buf[1];
	struct spi_buf_set tx;
	struct spi_buf_set rx;
	uint8_t hdr[HDR_LEN];

	build_hdr(hdr, addr, RD_CMD , len);

	tx_buf[0].buf = (void *)hdr;
	tx_buf[0].len = sizeof(hdr);
	tx.count = 1;
	tx.buffers = (const struct spi_buf *)&tx_buf;

	ret = spi_write_dt(&bus, &tx);
	if (ret) {
		return ret;
	}

	rx_buf[0].buf = (void *)data;
	rx_buf[0].len = len;
	rx.count = 1;
	rx.buffers = (const struct spi_buf *)&rx_buf;

	return spi_read_dt(&bus, &rx);
}

static int write_to_device(struct modem_iface *iface, const uint8_t *buf, size_t size)
{
	int ret;
	struct spi_buf tx_buf[3];
	struct spi_buf_set tx;

	uint8_t hdr[HDR_LEN];
	uint8_t pad[3] = {0};

	// TODO - comment why this is needed
	if ((size == 2) && (buf[0] == '\r') && (buf[1] == '\n')) {
		return 0;
	}

	LOG_DBG("data: %s", buf);
	LOG_DBG("pad: %d", size % 4);

	build_hdr(hdr, CMD_ADDR, WR_CMD, size + (size % 4));

	tx_buf[0].buf = (void *)hdr;
	tx_buf[0].len = sizeof(hdr);

	tx_buf[1].buf = (void *)buf;
	tx_buf[1].len = size;

	if (size % 4) {
		tx_buf[2].buf = (void *)pad;
		tx_buf[2].len = size % 4;
		tx.count = 3;
	} else {
		tx.count = 2;
	}

	tx.buffers = (const struct spi_buf *)&tx_buf;

	ret = spi_write_dt(&bus, &tx);

	return ret;
}

static int read_from_buffer(struct modem_iface *iface,
				 uint8_t *buf, size_t size, size_t *bytes_read)
{
	struct modem_iface_spi_data *data;

	data = (struct modem_iface_spi_data *)(iface->iface_data);

	if (!iface || !iface->iface_data) {
		return -EINVAL;
	}

	if (size == 0) {
		*bytes_read = 0;
		return 0;
	}

	*bytes_read = ring_buf_get(&data->rx_rb, buf, size);

	return 0;
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

	const struct modem_iface_spi_config spi_config = {
		.rx_rb_buf = &data->modem.iface_rb_buf[0],
		.rx_rb_buf_len = sizeof(data->modem.iface_rb_buf),
		.dev = DEVICE_DT_GET(DT_INST_BUS(0)),
	};

	data->modem.ctx.iface.iface_data = &data->modem.iface_spi_data;
	data->modem.ctx.iface.read = read_from_buffer;
	data->modem.ctx.iface.write = write_to_device;

	ring_buf_init(&data->modem.iface_spi_data.rx_rb,
		spi_config.rx_rb_buf_len,
		spi_config.rx_rb_buf);

	// TODO - can we remove this (and the tx semaphore)
	k_sem_init(&data->modem.iface_spi_data.rx_sem, 0, 1);

	return 0;
}

int at_wifi_transport_rx_wait(struct at_wifi_data *data, k_timeout_t timeout)
{
	int ret;
	uint8_t status;
	uint8_t *rsp;
	uint8_t *dst;
	uint16_t len;
	uint32_t addr;
	uint32_t partial_size = 0;
	uint32_t total_size = 0;
	uint8_t delimiter[] = {'\r', '\n'};
	uint8_t hdr[HDR_LEN];

	// TODO - must be a better way to do this...
	struct ring_buf *buf = &data->modem.iface_spi_data.rx_rb;

	// TODO - can we re-use semaphore in modem iface?
	/* Wait for device to signal there is data pending */
	ret = k_sem_take(&sem_int, timeout);
	if (ret) {
		return ret;
	}

	/* Read response header from device, it will tell us how many bytes
	   of data are pending and the address from which they can be read */
	ret = read_from_device(RSP_ADDR, sizeof(hdr), hdr);
	if (ret) {
		return ret;
	}

	ret = parse_hdr(hdr, &addr, &status, &len);
	if (ret) {
		return ret;
	}

	/* Get temporary storage for response data. Ensure there is
	   space to add a delimiter, if required  */
	// TODO - get rid of magic '6'
	rsp = k_heap_aligned_alloc(&driver_heap, sizeof(uint32_t), len + ARRAY_SIZE(delimiter) + 6, K_NO_WAIT);

	if (rsp == NULL) {
		return -ENOMEM;
	}

	// TODO - add comment that reference manual section detailing delay requirements
	k_sleep(K_MSEC(1));

	/* Read response data from the device, using parameters
	   previously obtained from header. */
	ret = read_from_device(addr, len, rsp);
	if (ret) {
		return ret;
	}

	/* Data is 4 byte aligned and padded with zero's at the end if necessary */
	len = trim_padding_end(rsp, len, 0);

	// TODO - add \r\n if not present, check why/if we really need to do this and improve if we do...
	if (rsp[len - 1] != delimiter[0]) {
		rsp[len++] = delimiter[0];
		rsp[len++] = delimiter[1];
	}

	// TODO - add OK, why is RA6W1 not sending this
	// TODO - Do not add if this is an error message
	// TODO - Only add if this is a response to an AT command, figure out how to do this...
	rsp[len++] = '\r';
	rsp[len++] = '\n';
	rsp[len++] = 'O';
	rsp[len++] = 'K';
	rsp[len++] = '\r';
	rsp[len++] = '\n';

	/* Add received data to ring buffer so that it can be parsed by modem module */
	do {
		partial_size = ring_buf_put_claim(buf, &dst, len);
		if (partial_size == 0) {
			break;
		}
		memcpy(dst, rsp + total_size, partial_size);
		total_size += partial_size;
	} while (total_size < len);

	ret = ring_buf_put_finish(buf, total_size);

	k_heap_free(&driver_heap, rsp);

	return ret;
}
