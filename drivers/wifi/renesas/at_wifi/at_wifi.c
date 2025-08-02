/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "zephyr/kernel.h"

#if defined(CONFIG_WIFI_AT_WIFI_TRANSPORT_UART)
#define DT_DRV_COMPAT renesas_at_wifi_uart
#endif

#include <stdlib.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wifi_at_wifi, CONFIG_WIFI_LOG_LEVEL);

#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/wifi_utils.h>
#include <zephyr/net/conn_mgr/connectivity_wifi_mgmt.h>

#include "at_wifi.h"
#include "at_wifi_socket_offload.h"

#define AT_WIFI_INIT_TIMEOUT		K_SECONDS(10)
#define AT_WIFI_CONNECT_TIMEOUT		K_SECONDS(20)

/* RX thread structures */
K_KERNEL_STACK_DEFINE(at_wifi_rx_stack, CONFIG_WIFI_AT_WIFI_RX_STACK_SIZE);
struct k_thread at_wifi_rx_thread;

NET_BUF_POOL_DEFINE(mdm_recv_pool, MDM_RECV_MAX_BUF, MDM_RECV_BUF_SIZE,
		    0, NULL);

K_KERNEL_STACK_DEFINE(at_wifi_workq_stack, CONFIG_WIFI_AT_WIFI_WORKQ_STACK_SIZE);

// TODO can this be static?
struct at_wifi_data at_wifi_driver_data;

static void at_wifi_init_work(struct k_work *work)
{
	struct at_wifi_data *dev;
	int ret;

	static const struct setup_cmd setup_cmds[] = {
		SETUP_CMD_NOHANDLE("AT+VER"),
		SETUP_CMD_NOHANDLE("AT+SDKVER"),
		SETUP_CMD_NOHANDLE("AT+WFMAC=?"),
	};

	dev = CONTAINER_OF(work, struct at_wifi_data, init_work);

	ret = modem_cmd_handler_setup_cmds(&dev->mctx.iface,
					   &dev->mctx.cmd_handler, setup_cmds,
					   ARRAY_SIZE(setup_cmds),
					   &dev->sem_response,
					   AT_WIFI_INIT_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("Init failed %d", ret);
		return;
	}

	net_if_set_link_addr(dev->net_iface, dev->mac_addr,
			     sizeof(dev->mac_addr), NET_LINK_ETHERNET);

	LOG_INF("AT Wi-Fi ready");

	/* L1 network layer (physical layer) is up */
	net_if_carrier_on(dev->net_iface);

	k_sem_give(&dev->sem_if_up);
}

MODEM_CMD_DEFINE(on_cmd_init_done)
{
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data,
					    cmd_handler_data);

	LOG_DBG("mode: %s", argv[0]);

	k_sem_give(&dev->sem_if_ready);

	if (net_if_is_carrier_ok(dev->net_iface)) {
		net_if_dormant_on(dev->net_iface);
		net_if_carrier_off(dev->net_iface);
		LOG_ERR("Unexpected reset");
	}

	k_work_submit_to_queue(&dev->workq, &dev->init_work);

	return 0;
}

MODEM_CMD_DEFINE(on_cmd_version)
{
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data,
					    cmd_handler_data);

	if (argv[0]) {
		if (strlen(argv[0]) < sizeof(dev->ver_str)) {
			strcpy(dev->ver_str, argv[0]);
		}
	}

	return 0;
}

MODEM_CMD_DEFINE(on_cmd_sdk_version)
{
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data,
					    cmd_handler_data);

	if (argv[0]) {
		if (strlen(argv[0]) < sizeof(dev->sdk_ver_str)) {
			strcpy(dev->sdk_ver_str, argv[0]);
		}
	}

	return 0;
}

MODEM_CMD_DEFINE(on_cmd_get_mac_addr)
{
	int ret;
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data,
					    cmd_handler_data);

	ret = net_bytes_from_str(dev->mac_addr, sizeof(dev->mac_addr), argv[0]);
	if (ret) {
		LOG_ERR("Failed to parse MAC address");
	}

	return 0;
}

static const struct modem_cmd unsol_cmds[] = {
	MODEM_CMD("+INIT:DONE,", on_cmd_init_done, 1U, ""),
	MODEM_CMD("+VER:", on_cmd_version, 1U, ""),
	MODEM_CMD("+SDKVER:", on_cmd_sdk_version, 1U, ""),
	MODEM_CMD("+WFMAC:", on_cmd_get_mac_addr, 1U, ""),
};

MODEM_CMD_DEFINE(on_cmd_ok)
{
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data,
					    cmd_handler_data);

	modem_cmd_handler_set_error(data, 0);
	k_sem_give(&dev->sem_response);

	return 0;
}

/* Handler: ERROR */
MODEM_CMD_DEFINE(on_cmd_error)
{
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data,
					    cmd_handler_data);

	LOG_DBG("error: %s", argv[0]);

	modem_cmd_handler_set_error(data, -EIO);
	k_sem_give(&dev->sem_response);

	return 0;
}

static const struct modem_cmd response_cmds[] = {
	MODEM_CMD("OK", on_cmd_ok, 0U, ""),
	MODEM_CMD("ERROR:", on_cmd_error, 1U, ""),
};

static void at_wifi_rx(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct at_wifi_data *data = p1;

	while (true) {
		modem_iface_uart_rx_wait(&data->mctx.iface, K_FOREVER);
		modem_cmd_handler_process(&data->mctx.cmd_handler, &data->mctx.iface);

		k_yield();
	}
}

static int at_wifi_reset(const struct device *dev)
{
	struct at_wifi_data *data = dev->data;
	int ret = -EAGAIN;

	if (net_if_is_carrier_ok(data->net_iface)) {
		net_if_carrier_off(data->net_iface);
	}

	ret = modem_cmd_send(&data->mctx.iface, &data->mctx.cmd_handler,
				     NULL, 0, "AT+RESTART", &data->sem_if_ready,
				     K_MSEC(CONFIG_WIFI_AT_WIFI_RESET_TIMEOUT));
	if (ret < 0) {
		LOG_ERR("Failed to reset device: %d", ret);
		return -EAGAIN;
	}

	LOG_INF("Waiting for interface to come up");

	ret = k_sem_take(&data->sem_if_up, AT_WIFI_INIT_TIMEOUT);
	if (ret == -EAGAIN) {
		LOG_ERR("Timeout waiting for interface");
	}

	return ret;
}

static int at_wifi_mgmt_scan(const struct device *dev,
	struct wifi_scan_params *params,
	scan_result_cb_t cb)
{
	/*
		AT+WFSCAN    - For an active scan
		AT+WFPSCAN   }
		AT+WFPCDTMIN } For a passive scan
		AT+WFCDTMAX  }
		AT+WFPSTOP   }
	 */
	return 0;
}

static void at_wifi_mgmt_connect_work(struct k_work *work)
{
	int ret;
	struct at_wifi_data *dev;

	dev = CONTAINER_OF(work, struct at_wifi_data, connect_work);

	ret = modem_cmd_send(&dev->mctx.iface, &dev->mctx.cmd_handler,
			      NULL, 0, dev->conn_cmd, &dev->sem_response,
			      AT_WIFI_CONNECT_TIMEOUT);

	memset(dev->conn_cmd, 0, sizeof(dev->conn_cmd));

	if (ret < 0) {
		net_if_dormant_on(dev->net_iface);
	} else {
		wifi_mgmt_raise_connect_result_event(dev->net_iface, 0);
		net_if_dormant_off(dev->net_iface);
	}
}

static int at_wifi_mgmt_connect(const struct device *dev,
	struct wifi_connect_req_params *params)
{
	/*
		AT+WFJAP
		AT+WFJAPBSSID - If BSSID supplied by caller
	 */

	struct at_wifi_data *data = dev->data;

	//if (!net_if_is_carrier_ok(data->net_iface) ||
	//    !net_if_is_admin_up(data->net_iface)) {
	//	return -EIO;
	//}

	snprintk(data->conn_cmd, sizeof(data->conn_cmd), "AT+WJAP=TP-LINK_1218,4,2,74512829");

	k_work_submit_to_queue(&data->workq, &data->connect_work);

	return 0;
}

static int at_wifi_mgmt_disconnect(const struct device *dev)
{
	/*
		AT+WFQAP
	 */
	return 0;
}

int at_wifi_mgmt_iface_status(const struct device *dev,
			     struct wifi_iface_status *status)
{
	/*
		AT+WFSTA
		AT+WFRSSI
		AT+WFSTAT
	*/
	return 0;
}

int at_wifi_mgmt_get_version(const struct device *dev,
				 struct wifi_version *params)
{
	struct at_wifi_data *data = dev->data;

	/* Version information has been read during the initialization process */
	params->drv_version = data->ver_str;
	params->fw_version = data->sdk_ver_str;

	return 0;
}

static enum offloaded_net_if_types at_wifi_offload_get_type(void)
{
	return L2_OFFLOADED_NET_IF_TYPE_WIFI;
}

static void at_wifi_iface_init(struct net_if *iface)
{
	at_wifi_socket_offload_init(iface);

	/* Not currently connected to a network */
	net_if_dormant_on(iface);
}

static const struct wifi_mgmt_ops at_wifi_mgmt_ops = {
	.scan		   		= at_wifi_mgmt_scan,
	.connect	   		= at_wifi_mgmt_connect,
	.disconnect	   		= at_wifi_mgmt_disconnect,
	.iface_status		= at_wifi_mgmt_iface_status,
	.get_version        = at_wifi_mgmt_get_version,
#ifdef CONFIG_WIFI_AT_WIFI_SOFTAP_SUPPORT
	.ap_enable     		= NULL,
	.ap_disable    		= NULL,
	.ap_sta_disconnect 	= NULL,
	.ap_config_params   = NULL,
#endif
};

static const struct net_wifi_mgmt_offload at_wifi_api = {
	.wifi_iface.iface_api.init = at_wifi_iface_init,
	.wifi_iface.get_type = at_wifi_offload_get_type,
	.wifi_mgmt_api = &at_wifi_mgmt_ops,
};

static int at_wifi_init(const struct device *dev);

NET_DEVICE_DT_INST_OFFLOAD_DEFINE(0, at_wifi_init, NULL,
	&at_wifi_driver_data, NULL,
	CONFIG_WIFI_INIT_PRIORITY, &at_wifi_api,
	WIFI_AT_WIFI_MTU);

CONNECTIVITY_WIFI_MGMT_BIND(Z_DEVICE_DT_DEV_ID(DT_DRV_INST(0)));

static int at_wifi_init(const struct device *dev)
{
	int ret = 0;
	struct at_wifi_data *data = dev->data;

	LOG_DBG("initializing...");

	k_sem_init(&data->sem_response, 0, 1);
	k_sem_init(&data->sem_if_ready, 0, 1);
	k_sem_init(&data->sem_if_up, 0, 1);

	k_work_init(&data->init_work, at_wifi_init_work);
	k_work_init(&data->connect_work, at_wifi_mgmt_connect_work);

	k_work_queue_start(&data->workq, at_wifi_workq_stack,
			   K_KERNEL_STACK_SIZEOF(at_wifi_workq_stack),
			   K_PRIO_COOP(CONFIG_WIFI_AT_WIFI_WORKQ_THREAD_PRIORITY),
			   NULL);
	k_thread_name_set(&data->workq.thread, "at_wifi_workq");

	const struct modem_cmd_handler_config cmd_handler_config = {
		.match_buf = &data->cmd_match_buf[0],
		.match_buf_len = sizeof(data->cmd_match_buf),
		.buf_pool = &mdm_recv_pool,
		.alloc_timeout = K_NO_WAIT,
		.eol = "\r\n",
		.user_data = NULL,
		.response_cmds = response_cmds,
		.response_cmds_len = ARRAY_SIZE(response_cmds),
		.unsol_cmds = unsol_cmds,
		.unsol_cmds_len = ARRAY_SIZE(unsol_cmds),
	};

	ret = modem_cmd_handler_init(&data->mctx.cmd_handler, &data->cmd_handler_data,
				     &cmd_handler_config);
	if (ret < 0) {
		return ret;
	}

	const struct modem_iface_uart_config uart_config = {
		.rx_rb_buf = &data->iface_rb_buf[0],
		.rx_rb_buf_len = sizeof(data->iface_rb_buf),
		.dev = DEVICE_DT_GET(DT_INST_BUS(0)),
//		.hw_flow_control = DT_PROP(ESP_BUS, hw_flow_control),
	};

	/* The context must be registered before the serial port is initialised. */
	data->mctx.driver_data = data;
	ret = modem_context_register(&data->mctx);
	if (ret < 0) {
		LOG_ERR("Error registering modem context: %d", ret);
		return ret;
	}

	ret = modem_iface_uart_init(&data->mctx.iface, &data->iface_data, &uart_config);
	if (ret < 0) {
		return ret;
	}

	k_thread_create(&at_wifi_rx_thread, at_wifi_rx_stack,
			K_KERNEL_STACK_SIZEOF(at_wifi_rx_stack),
			at_wifi_rx,
			data, NULL, NULL,
			K_PRIO_COOP(CONFIG_WIFI_AT_WIFI_RX_THREAD_PRIORITY), 0,
			K_NO_WAIT);
	k_thread_name_set(&at_wifi_rx_thread, "at_wifi_rx");

	/* Retrieve associated network interface so asynchronous messages can be processed early */
	data->net_iface = NET_IF_GET(Z_DEVICE_DT_DEV_ID(DT_DRV_INST(0)), 0);

	at_wifi_reset(dev);

	return 0;
}
