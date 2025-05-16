/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_WIFI_RA_AT_SOCKET_OFFLOAD_H_
#define ZEPHYR_INCLUDE_DRIVERS_WIFI_RA_AT_SOCKET_OFFLOAD_H_

#ifdef __cplusplus
extern "C" {
#endif

int ra_at_socket_offload_init(struct net_if *iface);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_WIFI_RA_AT_SOCKET_OFFLOAD_H_ */
