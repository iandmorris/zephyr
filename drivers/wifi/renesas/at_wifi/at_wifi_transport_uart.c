/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_at_wifi_uart

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include "at_wifi.h"

int at_wifi_transport_init(struct at_wifi_data *data)
{
	//struct at_wifi_data *data = dev->data;

	const struct modem_iface_uart_config uart_config = {
		.rx_rb_buf = &data->modem.iface_rb_buf[0],
		.rx_rb_buf_len = sizeof(data->modem.iface_rb_buf),
		.dev = DEVICE_DT_GET(DT_INST_BUS(0)),
//		.hw_flow_control = DT_PROP(ESP_BUS, hw_flow_control),
	};

	return modem_iface_uart_init(&data->modem.ctx.iface,
				&data->modem.iface_data, &uart_config);
}

int at_wifi_transport_rx_wait(struct at_wifi_data *data, k_timeout_t timeout)
{
	return modem_iface_uart_rx_wait(&data->modem.ctx.iface, timeout);
}
