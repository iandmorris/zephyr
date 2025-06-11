/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_WIFI_RENESAS_RA_ERPC_H_
#define ZEPHYR_INCLUDE_DRIVERS_WIFI_RENESAS_RA_ERPC_H_

#include <zephyr/net/wifi_mgmt.h>
#include "wifi_common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ra_erpc_data {
	const struct device *bus;
	struct net_if *net_iface;
	scan_result_cb_t scan_cb;
	uint16_t scan_max_bss_cnt;
	struct WIFINetworkParams_t drv_nwk_params;

	struct k_work_q workq;
	struct k_work scan_work;
	struct k_work connect_work;
	struct k_work disconnect_work;
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_WIFI_RENESAS_RA_ERPC_H_ */
