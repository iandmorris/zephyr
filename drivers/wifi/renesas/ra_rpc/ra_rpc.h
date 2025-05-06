/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_WIFI_RENESAS_RA_RPC_H_
#define ZEPHYR_INCLUDE_DRIVERS_WIFI_RENESAS_RA_RPC_H_

#include <zephyr/net/wifi_mgmt.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ra_rpc_data {
	const struct device *bus;
	struct net_if *net_iface;
	scan_result_cb_t scan_cb;
	uint16_t scan_max_bss_cnt;

	struct k_work_q workq;
	struct k_work scan_work;
};

int ra_rpc_socket_offload_init(struct net_if *iface);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_WIFI_RENESAS_RA_RPC_H_ */
