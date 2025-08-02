/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_WIFI_RENESAS_AT_WIFI_H_
#define ZEPHYR_INCLUDE_DRIVERS_WIFI_RENESAS_AT_WIFI_H_

#include <zephyr/net/wifi_mgmt.h>

#include "modem_context.h"
#include "modem_cmd_handler.h"
#include "modem_iface_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_AT_WIFI_MTU		4096
#define MDM_RING_BUF_SIZE		CONFIG_WIFI_AT_WIFI_MODEM_RING_BUF_SIZE
#define MDM_RECV_MAX_BUF		CONFIG_WIFI_AT_WIFI_MODEM_RX_BUF_COUNT
#define MDM_RECV_BUF_SIZE		CONFIG_WIFI_AT_WIFI_MODEM_RX_BUF_SIZE

#define VERSION_LEN_MAX			32
#define SDK_VERSION_LEN_MAX		32

#define _CWJAP  "CWJAP"
#define CONN_CMD_MAX_LEN (sizeof("AT+"_CWJAP"=\"\",\"\"") + \
			  WIFI_SSID_MAX_LEN * 2 + WIFI_PSK_MAX_LEN * 2)

#define SCAN_RSP_BUF_SIZE		1024 /* Should this be a config option, should it be related to something else, MTU? related to max ssid's in config or scan request? */

struct at_wifi_data {
	struct net_if *net_iface;
	struct wifi_iface_status *wifi_status;

	struct modem_context mctx;
	struct modem_cmd_handler_data cmd_handler_data;
	struct modem_iface_uart_data iface_data;
	uint8_t iface_rb_buf[MDM_RING_BUF_SIZE];
	uint8_t cmd_match_buf[MDM_RECV_BUF_SIZE];

	uint8_t mac_addr[6];

	// TODO put all this into a scan response struct?
	scan_result_cb_t scan_cb;
	uint8_t scan_rsp_buf[SCAN_RSP_BUF_SIZE];
	char *last;
	uint16_t scan_max_bss_cnt;

	struct k_work_q workq;
	struct k_work init_work;
	struct k_work scan_work;
	struct k_work connect_work;
	struct k_work iface_status_work;

	struct k_sem sem_response;
	struct k_sem sem_if_ready;
	struct k_sem sem_if_up;
	struct k_sem sem_connecting;
	struct k_sem sem_wifi_status;

	char ver_str[VERSION_LEN_MAX];
	char sdk_ver_str[SDK_VERSION_LEN_MAX];

	char conn_cmd[CONN_CMD_MAX_LEN];
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_WIFI_RENESAS_AT_WIFI_H_ */
