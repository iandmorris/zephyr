/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_WIFI_RRQ_OFFLOAD_H_
#define ZEPHYR_INCLUDE_DRIVERS_WIFI_RRQ_OFFLOAD_H_

#ifdef __cplusplus
extern "C" {
#endif

int rrq_offload_init(struct net_if *iface);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_WIFI_RRQ_OFFLOAD_H_ */
