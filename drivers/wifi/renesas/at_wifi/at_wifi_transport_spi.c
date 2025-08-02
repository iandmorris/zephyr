/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_at_wifi_spi

#include <zephyr/kernel.h>

#include "at_wifi.h"

int at_wifi_transport_init(struct at_wifi_data *data)
{
	return 0;
}

int at_wifi_transport_rx_wait(struct at_wifi_data *data, k_timeout_t timeout)
{
	return 0;
}
