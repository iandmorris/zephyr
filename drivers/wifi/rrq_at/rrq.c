/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_rrq_at

#define RRQ_MTU					2048 /* TODO - what should this be set to? */
#define MODEM_RING_BUF_SIZE		CONFIG_WIFI_RRQ_AT_MODEM_RING_BUF_SIZE
#define MODEM_RECV_MAX_BUF		CONFIG_WIFI_RRQ_AT_MODEM_RX_BUF_COUNT
#define MODEM_RECV_BUF_SIZE		CONFIG_WIFI_RRQ_AT_MODEM_RX_BUF_SIZE

#define RRQ_CMD_WFSCAN 			"AT+WFSCAN"
#define RRQ_SCAN_TIMEOUT		K_SECONDS(10)

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wifi_rrq_at, CONFIG_WIFI_LOG_LEVEL);

#include <zephyr/drivers/gpio.h>

#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/conn_mgr/connectivity_wifi_mgmt.h>

#include "modem_context.h" // TODO zephyr paths?
#include "modem_iface_uart.h"
#include "modem_cmd_handler.h"

#include "rrq_offload.h"

NET_BUF_POOL_DEFINE(modem_recv_pool, MODEM_RECV_MAX_BUF, MODEM_RECV_BUF_SIZE,
		    0, NULL);

K_KERNEL_STACK_DEFINE(rrq_rx_stack,
		      CONFIG_WIFI_RRQ_AT_RX_STACK_SIZE);
struct k_thread rrq_rx_thread;

K_KERNEL_STACK_DEFINE(rrq_workq_stack,
	CONFIG_WIFI_RRQ_AT_WORKQ_STACK_SIZE);

struct rrq_config {
#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	const struct gpio_dt_spec reset;
#endif
};

struct rrq_data {
	struct net_if *net_iface;

	/* todo - put all of this in a struct called rrq_modem outside of this one? */
	struct modem_context context;
	struct modem_iface_uart_data iface_data;
	uint8_t modem_buf[MODEM_RING_BUF_SIZE];
	struct modem_cmd_handler_data cmd_handler_data;
	uint8_t cmd_match_buf[MODEM_RECV_BUF_SIZE];

	/* todo - put all of this in a struct called rrq_work ? */
	struct k_work_q workq;
	//struct k_work init_work;
	//struct k_work_delayable ip_addr_work;
	struct k_work scan_work;
	//struct k_work connect_work;
	//struct k_work disconnect_work;
	//struct k_work iface_status_work;
	//struct k_work mode_switch_work;
	//struct k_work dns_work;

	scan_result_cb_t scan_cb;

	struct k_sem sem_response;
};

static const struct rrq_config rrq_driver_config = {
#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	.reset = GPIO_DT_SPEC_INST_GET(0, reset_gpios),
#endif
};

static inline int rrq_cmd_send(struct rrq_data *data,
	const struct modem_cmd *handlers,
	size_t handlers_len, const char *buf,
	k_timeout_t timeout)
{
	return modem_cmd_send(&data->context.iface, &data->context.cmd_handler,
				handlers, handlers_len, buf, &data->sem_response,
				timeout);
}

/*
 * Modem Response Command Handlers
 */
MODEM_CMD_DEFINE(on_cmd_ok)
{
	return 0;
}

MODEM_CMD_DEFINE(on_cmd_error)
{
	return 0;
}

MODEM_CMD_DIRECT_DEFINE(on_cmd_wfscan)
{
	struct rrq_data *dev = CONTAINER_OF(data, struct rrq_data,
		cmd_handler_data);
	struct wifi_scan_result res = { 0 };
	size_t match_len;


	match_len = net_buf_data_match(data->rx_buf, 0, "+WFSCAN:", sizeof("+WFSCAN:") - 1);

	if (match_len == (sizeof("+WFSCAN:") - 1))
	{
		if (dev->scan_cb) {
			dev->scan_cb(dev->net_iface, 0, &res);
		}
	}
//	size_t net_buf_data_match	(	const struct net_buf *	buf,
//		size_t	offset,
//		const void *	data,
//		size_t	len )


		/* Iterate over the table, and call the scan_result callback. */
		//while (index < simplelink_data.num_results_or_err) {
		//	z_simplelink_get_scan_result(index, &scan_result);
		//	simplelink_data.cb(simplelink_data.iface, 0,
		//			   &scan_result);
			/* Yield, to ensure notifications get delivered:  */
		//	k_yield();
		//	index++;
		//}

		/* Sending a NULL entry indicates e/o results, and
		 * triggers the NET_EVENT_WIFI_SCAN_DONE event:
		 */
		//simplelink_data.cb(simplelink_data.iface, 0, NULL);

		/** SSID */
		//uint8_t ssid[WIFI_SSID_MAX_LEN + 1];
		/** SSID length */
		//uint8_t ssid_length;
		/** Frequency band */
		//uint8_t band;
		/** Channel */
		//uint8_t channel;
		/** Security type */
		//enum wifi_security_type security;
		/** WPA3 enterprise type */
		//enum wifi_wpa3_enterprise_type wpa3_ent_type;
		/** MFP options */
		//enum wifi_mfp_options mfp;
		/** RSSI */
		//int8_t rssi;
		/** BSSID */
		//uint8_t mac[WIFI_MAC_ADDR_LEN];
		/** BSSID length */
		//uint8_t mac_length;

	//if (dev->scan_cb) {
	//	dev->scan_cb(dev->net_iface, 0, &res);
	//}

	/*
	struct esp_data *dev = CONTAINER_OF(data, struct esp_data,
					    cmd_handler_data);
	struct wifi_scan_result res = { 0 };
	char cwlap_buf[sizeof("\"0\",\"\",-100,\"xx:xx:xx:xx:xx:xx\",12") +
		       WIFI_SSID_MAX_LEN * 2 + 1];
	char *ecn;
	char *ssid;
	char *mac;
	char *channel;
	long rssi;
	long ecn_id;
	int err;

	len = net_buf_linearize(cwlap_buf, sizeof(cwlap_buf) - 1,
				data->rx_buf, 0, sizeof(cwlap_buf) - 1);
	cwlap_buf[len] = '\0';

	char *str = &cwlap_buf[sizeof("+CWJAP:(") - 1];
	char *str_end = cwlap_buf + len;

	err = esp_pull_raw(&str, str_end, &ecn);
	if (err) {
		return err;
	}

	ecn_id = strtol(ecn, NULL, 10);
	if (ecn_id == 0) {
		res.security = WIFI_SECURITY_TYPE_NONE;
	} else {
		res.security = WIFI_SECURITY_TYPE_PSK;
	}

	err = esp_pull_quoted(&str, str_end, &ssid);
	if (err) {
		return err;
	}

	err = esp_pull_long(&str, str_end, &rssi);
	if (err) {
		return err;
	}

	if (strlen(ssid) > WIFI_SSID_MAX_LEN) {
		return -EBADMSG;
	}

	res.ssid_length = MIN(sizeof(res.ssid), strlen(ssid));
	memcpy(res.ssid, ssid, res.ssid_length);

	res.rssi = rssi;

	if (IS_ENABLED(CONFIG_WIFI_ESP_AT_SCAN_MAC_ADDRESS)) {
		err = esp_pull_quoted(&str, str_end, &mac);
		if (err) {
			return err;
		}

		res.mac_length = WIFI_MAC_ADDR_LEN;
		if (net_bytes_from_str(res.mac, sizeof(res.mac), mac) < 0) {
			LOG_ERR("Invalid MAC address");
			res.mac_length = 0;
		}
	}

	err = esp_pull_raw(&str, str_end, &channel);
	if (err) {
		return err;
	}

	res.channel = strtol(channel, NULL, 10);

	if (dev->scan_cb) {
		dev->scan_cb(dev->net_iface, 0, &res);
	}

	return str - cwlap_buf;
*/
	return 0;
}

static const struct modem_cmd response_cmds[] = {
	MODEM_CMD("OK", on_cmd_ok, 0U, ""), /* 3GPP */
	MODEM_CMD("ERROR", on_cmd_error, 0U, ""), /* 3GPP */
};

MODEM_CMD_DEFINE(on_cmd_wifi_connected)
{
	return 0;
}

MODEM_CMD_DEFINE(on_cmd_wifi_disconnected)
{
	return 0;
}

static const struct modem_cmd unsol_cmds[] = {
	MODEM_CMD("WIFI CONNECTED", on_cmd_wifi_connected, 0U, ""),
	MODEM_CMD("WIFI DISCONNECT", on_cmd_wifi_disconnected, 0U, ""),
//	MODEM_CMD("WIFI GOT IP", on_cmd_got_ip, 0U, ""),
//	MODEM_CMD("0,CONNECT", on_cmd_connect, 0U, ""),
//	MODEM_CMD("1,CONNECT", on_cmd_connect, 0U, ""),
//	MODEM_CMD("2,CONNECT", on_cmd_connect, 0U, ""),
//	MODEM_CMD("3,CONNECT", on_cmd_connect, 0U, ""),
//	MODEM_CMD("4,CONNECT", on_cmd_connect, 0U, ""),
//	MODEM_CMD("0,CLOSED", on_cmd_closed, 0U, ""),
//	MODEM_CMD("1,CLOSED", on_cmd_closed, 0U, ""),
//	MODEM_CMD("2,CLOSED", on_cmd_closed, 0U, ""),
//	MODEM_CMD("3,CLOSED", on_cmd_closed, 0U, ""),
//	MODEM_CMD("4,CLOSED", on_cmd_closed, 0U, ""),
//	MODEM_CMD("busy s...", on_cmd_busy_sending, 0U, ""),
//	MODEM_CMD("busy p...", on_cmd_busy_processing, 0U, ""),
//	MODEM_CMD("ready", on_cmd_ready, 0U, ""),
//#if defined(CONFIG_WIFI_ESP_AT_FETCH_VERSION)
//	MODEM_CMD("AT version:", on_cmd_at_version, 1U, ""),
//	MODEM_CMD("SDK version:", on_cmd_sdk_version, 1U, ""),
//	MODEM_CMD("Compile time", on_cmd_compile_time, 1U, ""),
//	MODEM_CMD("Bin version:", on_cmd_bin_version, 1U, ""),
//#endif
//	MODEM_CMD_DIRECT("+IPD", on_cmd_ipd),
};

struct rrq_data rrq_driver_data;

static void rrq_mgmt_scan_work(struct k_work *work)
{
	struct rrq_data *dev;
	int ret;
	static const struct modem_cmd cmds[] = {
		MODEM_CMD_DIRECT("+WFSCAN:", on_cmd_wfscan),
	};

	dev = CONTAINER_OF(work, struct rrq_data, scan_work);

	//ret = esp_mode_flags_set(dev, EDF_STA_LOCK);
	//if (ret < 0) {
	//	goto out;
	//}
	ret = rrq_cmd_send(dev,
			   cmds, ARRAY_SIZE(cmds),
			   RRQ_CMD_WFSCAN,
			   RRQ_SCAN_TIMEOUT);
	//esp_mode_flags_clear(dev, EDF_STA_LOCK);
	LOG_DBG("RRQ Wi-Fi scan: cmd = %s", RRQ_CMD_WFSCAN);

	if (ret < 0) {
		LOG_ERR("Failed to scan: ret %d", ret);
	}

out:
	dev->scan_cb(dev->net_iface, 0, NULL);
	dev->scan_cb = NULL;
}

static int rrq_mgmt_scan(const struct device *dev,
			 struct wifi_scan_params *params,
			 scan_result_cb_t cb)
{
	struct rrq_data *data = dev->data;

	ARG_UNUSED(params);

	if (data->scan_cb != NULL) {
		return -EINPROGRESS;
	}

	if (!net_if_is_carrier_ok(data->net_iface)) {
		return -EIO;
	}

	data->scan_cb = cb;

	k_work_submit_to_queue(&data->workq, &data->scan_work);

	return 0;
}

static int rrq_mgmt_connect(const struct device *dev,
			    struct wifi_connect_req_params *params)
{
	return 0;
}

static int rrq_mgmt_disconnect(const struct device *dev)
{
	return 0;
}

static int rrq_mgmt_ap_enable(const struct device *dev,
			      struct wifi_connect_req_params *params)
{
	return 0;
}

static int rrq_mgmt_ap_disable(const struct device *dev)
{
	return 0;
}

static int rrq_mgmt_iface_status(const struct device *dev,
				 struct wifi_iface_status *status)
{
	return 0;
}

static void rrq_iface_init(struct net_if *iface)
{
	rrq_offload_init(iface);

	/* Not currently connected to a network */
	net_if_dormant_on(iface);
}

static enum offloaded_net_if_types rrq_offload_get_type(void)
{
	return L2_OFFLOADED_NET_IF_TYPE_WIFI;
}
// TODO - check if we have all available functions in the struct defined
static const struct wifi_mgmt_ops rrq_mgmt_ops = {
	.scan		   = rrq_mgmt_scan,
	.connect	   = rrq_mgmt_connect,
	.disconnect	   = rrq_mgmt_disconnect,
	.ap_enable	   = rrq_mgmt_ap_enable,
	.ap_disable	   = rrq_mgmt_ap_disable,
	.iface_status  = rrq_mgmt_iface_status,
};

static const struct net_wifi_mgmt_offload rrq_api = {
	.wifi_iface.iface_api.init = rrq_iface_init,
	.wifi_iface.get_type = rrq_offload_get_type,
	.wifi_mgmt_api = &rrq_mgmt_ops,
};

static void rrq_rx(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct rrq_data *data = p1;

	while (true) {
		modem_iface_uart_rx_wait(&data->context.iface, K_FOREVER);
		modem_cmd_handler_process(&data->context.cmd_handler, &data->context.iface);

		/* give up time if we have a solid stream of data */
		k_yield();
	}
}

static int rrq_reset(const struct device *dev)
{
#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	const struct rrq_config *config = dev->config;

	gpio_pin_set_dt(&config->reset, 1);
	k_sleep(K_MSEC(100));
	gpio_pin_set_dt(&config->reset, 0);
#endif

	return 0;
}

static int rrq_init(const struct device *dev);

/* The network device must be instantiated above the init function in order
 * for the struct net_if that the macro declares to be visible inside the
 * function. An `extern` declaration does not work as the struct is static.
 */
NET_DEVICE_DT_INST_OFFLOAD_DEFINE(0, rrq_init, NULL,
				  &rrq_driver_data, &rrq_driver_config,
				  CONFIG_WIFI_INIT_PRIORITY, &rrq_api,
				  RRQ_MTU);

CONNECTIVITY_WIFI_MGMT_BIND(Z_DEVICE_DT_DEV_ID(DT_DRV_INST(0)));

static int rrq_init(const struct device *dev)
{
#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	const struct rrq_config *config = dev->config;
#endif
	struct rrq_data *data = dev->data;
	int ret = 0;

	k_sem_init(&data->sem_response, 0, 1);

	k_work_init(&data->scan_work, rrq_mgmt_scan_work);

	k_work_queue_start(&data->workq, rrq_workq_stack,
		K_KERNEL_STACK_SIZEOF(rrq_workq_stack),
		K_PRIO_COOP(CONFIG_WIFI_RRQ_AT_WORKQ_THREAD_PRIORITY),
		NULL);
	k_thread_name_set(&data->workq.thread, "rrq_workq");

	const struct modem_cmd_handler_config cmd_handler_config = {
		.match_buf = &data->cmd_match_buf[0],
		.match_buf_len = sizeof(data->cmd_match_buf),
		.buf_pool = &modem_recv_pool,
		.alloc_timeout = K_NO_WAIT,
		.eol = "\r\n",
		.user_data = NULL,
		.response_cmds = response_cmds,
		.response_cmds_len = ARRAY_SIZE(response_cmds),
		.unsol_cmds = unsol_cmds,
		.unsol_cmds_len = ARRAY_SIZE(unsol_cmds),
	};

	ret = modem_cmd_handler_init(&data->context.cmd_handler, &data->cmd_handler_data,
				     &cmd_handler_config);
	if (ret < 0) {
		goto error;
	}

	const struct modem_iface_uart_config uart_config = {
		.rx_rb_buf = &data->modem_buf[0],
		.rx_rb_buf_len = sizeof(data->modem_buf),
		.dev = DEVICE_DT_GET(DT_INST_BUS(0)),
		//.hw_flow_control = DT_PROP(ESP_BUS, hw_flow_control), // TODO - fix this...?
	};

	/* The context must be registered before the serial port is initialised. */
	data->context.driver_data = data;
	ret = modem_context_register(&data->context);
	if (ret < 0) {
		LOG_ERR("Error registering modem context: %d", ret);
		goto error;
	}

	ret = modem_iface_uart_init(&data->context.iface, &data->iface_data, &uart_config);
	if (ret < 0) {
		goto error;
	}

#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	ret = gpio_pin_configure_dt(&config->reset, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure %s pin", "reset");
		goto error;
	}
#endif

	k_thread_create(&rrq_rx_thread, rrq_rx_stack,
			K_KERNEL_STACK_SIZEOF(rrq_rx_stack),
			rrq_rx,
			data, NULL, NULL,
			K_PRIO_COOP(CONFIG_WIFI_RRQ_AT_RX_THREAD_PRIORITY), 0,
			K_NO_WAIT);
	k_thread_name_set(&rrq_rx_thread, "rrq_rx");

	data->net_iface = NET_IF_GET(Z_DEVICE_DT_DEV_ID(DT_DRV_INST(0)), 0);

	ret = rrq_reset(dev);

error:
	return ret;
}
