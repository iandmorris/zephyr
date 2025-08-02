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

/* Document how this matches with MTU of DA16xxx/RA6Wx */
#define WIFI_AT_WIFI_MTU					4096
/* Should this be a config option, should it be related to something else, MTU? related to max ssid's in config or scan request? */
#define WIFI_AT_WIFI_SCAN_RSP_BUF_SIZE		1024
#define WIFI_AT_WIFI_VER_LEN_MAX			64
#define WIFI_AT_WIFI_SDK_VER_LEN_MAX		64
#define WIFI_AT_WIFI_SOCKETS_MAX			4

// TODO - this needs to be updated to match AT command
#define _CWJAP  "CWJAP"
#define CONN_CMD_MAX_LEN (sizeof("AT+"_CWJAP"=\"\",\"\"") + \
			  WIFI_SSID_MAX_LEN * 2 + WIFI_PSK_MAX_LEN * 2)

enum at_wifi_chip_id {
	AT_WIFI_CHIP_ID_UNKNOWN = 0,
	AT_WIFI_CHIP_ID_DA16200,
	AT_WIFI_CHIP_ID_DA16600,
	AT_WIFI_CHIP_ID_RA6W1,
	AT_WIFI_CHIP_ID_RA6W2,
};

// TODO - can we just use the uart struct, or maybe there is a generic one?
struct modem_iface_spi_data {
	struct ring_buf rx_rb;
	struct k_sem rx_sem;
};

struct at_wifi_modem {
	struct modem_context ctx;
	struct modem_cmd_handler_data cmd_handler_data;
	// TODO - add conditional compilation to include only the struct needed based on the transport type
	struct modem_iface_uart_data iface_data;
	struct modem_iface_spi_data iface_spi_data;
	uint8_t iface_rb_buf[CONFIG_WIFI_AT_WIFI_MODEM_RING_BUF_SIZE];
	uint8_t cmd_match_buf[CONFIG_WIFI_AT_WIFI_MODEM_RX_BUF_SIZE];
};

struct at_wifi_scan {
	scan_result_cb_t cb;
	uint8_t rsp_buf[WIFI_AT_WIFI_SCAN_RSP_BUF_SIZE];
	char *last_field;
	uint16_t max_bss_cnt;
};

struct at_wifi_info {
	enum at_wifi_chip_id chip_id;
	char ver_str[WIFI_AT_WIFI_VER_LEN_MAX];
	char sdk_ver_str[WIFI_AT_WIFI_SDK_VER_LEN_MAX];
	// TODO - use macro for length?
	uint8_t mac_addr[6];
	int rssi;
};

struct at_wifi_socket {
	atomic_t flags;
	struct net_context *context;
	sa_family_t family;
	enum net_sock_type type;
	enum net_ip_protocol ip_proto;
	bool in_use;
	uint8_t cid;

	struct sockaddr src;
	struct sockaddr dst;
};

struct at_wifi_data {
	struct net_if *net_iface;
	struct wifi_iface_status *wifi_status;

	struct at_wifi_info info;
	struct at_wifi_modem modem;
	struct at_wifi_scan scan;
	struct at_wifi_socket sockets[WIFI_AT_WIFI_SOCKETS_MAX];
	struct at_wifi_socket *sock_connect;

	struct k_work_q workq;
	struct k_work init_work;
	struct k_work scan_work;
	struct k_work connect_work;

	struct k_sem sem_response;
	struct k_sem sem_if_up;
	struct k_sem sem_connecting;

	char conn_cmd[CONN_CMD_MAX_LEN];
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_WIFI_RENESAS_AT_WIFI_H_ */
