/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_WIFI_RENESAS_AT_WIFI_OFFLOAD_H_
#define ZEPHYR_INCLUDE_DRIVERS_WIFI_RENESAS_AT_WIFI_OFFLOAD_H_

#include <zephyr/net/wifi_mgmt.h>

#ifdef __cplusplus
extern "C" {
#endif

int at_wifi_offload_init(struct net_if *iface);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_WIFI_RENESAS_AT_WIFI_OFFLOAD_H_ */
