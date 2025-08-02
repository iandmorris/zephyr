/*
 * Copyright (c) 2025 Ian Morris
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_WIFI_RENESAS_AT_WIFI_TRANSPORT_H_
#define ZEPHYR_INCLUDE_DRIVERS_WIFI_RENESAS_AT_WIFI_TRANSPORT_H_

#include <zephyr/kernel.h>

#include "at_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

int at_wifi_transport_init(struct at_wifi_data *data);
int at_wifi_transport_rx_wait(const struct at_wifi_data *data, k_timeout_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_WIFI_RENESAS_AT_WIFI_TRANSPORT_H_ */
