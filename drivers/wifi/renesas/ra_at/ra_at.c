/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_ra_at

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wifi_ra_at, CONFIG_WIFI_LOG_LEVEL);

#define RA_AT_MTU				2048 /* TODO - what should this be set to? */
#define MODEM_RING_BUF_SIZE		CONFIG_WIFI_RA_AT_MODEM_RING_BUF_SIZE
#define MODEM_RECV_MAX_BUF		CONFIG_WIFI_RA_AT_MODEM_RX_BUF_COUNT
#define MODEM_RECV_BUF_SIZE		CONFIG_WIFI_RA_AT_MODEM_RX_BUF_SIZE

#define SCAN_RSP_BUF_SIZE		1024 /* Should this be a config option, should it be related to something else, MTU? related to max ssid's in config or scan request? */

#define RA_AT_CMD_WFSCAN		"AT+WFSCAN"
#define RA_AT_CMD_VER			"AT+VER"
#define RA_AT_SCAN_TIMEOUT		K_SECONDS(10)

#include <stdlib.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/conn_mgr/connectivity_wifi_mgmt.h>

#include "modem_context.h"
#include "modem_iface_uart.h"
#include "modem_cmd_handler.h"

#include "ra_at_socket_offload.h"

NET_BUF_POOL_DEFINE(modem_recv_pool, MODEM_RECV_MAX_BUF, MODEM_RECV_BUF_SIZE,
		    0, NULL);

K_KERNEL_STACK_DEFINE(ra_at_rx_stack,
		      CONFIG_WIFI_RA_AT_RX_STACK_SIZE);
struct k_thread ra_at_rx_thread;

K_KERNEL_STACK_DEFINE(ra_at_workq_stack,
	CONFIG_WIFI_RA_AT_WORKQ_STACK_SIZE);

struct ra_at_config {
#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	const struct gpio_dt_spec reset;
#endif
};

struct ra_at_data {
	struct net_if *net_iface;

	/* todo - put all of this in a struct called ra_modem outside of this one? */
	struct modem_context context;
	struct modem_iface_uart_data iface_data;
	uint8_t modem_buf[MODEM_RING_BUF_SIZE];
	struct modem_cmd_handler_data cmd_handler_data;
	uint8_t cmd_match_buf[MODEM_RECV_BUF_SIZE];

	/* todo - put all of this in a struct called ra_work ? */
	struct k_work_q workq;
	struct k_work init_work;
	//struct k_work_delayable ip_addr_work;
	struct k_work scan_work;
	struct k_work get_version_work;
	//struct k_work connect_work;
	//struct k_work disconnect_work;
	//struct k_work iface_status_work;
	//struct k_work mode_switch_work;
	//struct k_work dns_work;

	struct wifi_version *version;

	scan_result_cb_t scan_cb;
	uint8_t scan_rsp_buf[SCAN_RSP_BUF_SIZE];
	char *last;
	uint16_t scan_max_bss_cnt;

	struct k_sem sem_response;
	struct k_sem get_version_sem;
	struct k_sem sem_if_up;

	uint8_t drv_version[32];
};

static const struct ra_at_config ra_driver_config = {
#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	.reset = GPIO_DT_SPEC_INST_GET(0, reset_gpios),
#endif
};

static inline int ra_cmd_send(struct ra_at_data *data,
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
	struct ra_at_data *dev = CONTAINER_OF(data, struct ra_at_data,
		cmd_handler_data);

	modem_cmd_handler_set_error(data, 0);
	k_sem_give(&dev->sem_response);

	return 0;
}

MODEM_CMD_DEFINE(on_cmd_error)
{
	return 0;
}

MODEM_CMD_DIRECT_DEFINE(on_cmd_get_version)
{
	struct ra_at_data *dev = CONTAINER_OF(data, struct ra_at_data,
		cmd_handler_data);

	struct wifi_version *version = dev->version;

	char version_buf[sizeof("+VER:FRTOS-GEN01-01-xxxxxxxxxx-xxxxxx") + 1];

	LOG_INF("on_cmd_get_version: rxd %d of %d", data->rx_buf->len, sizeof(version_buf));

	len = net_buf_linearize(version_buf, sizeof(version_buf) - 1,
				data->rx_buf, 0, sizeof(version_buf) - 1);
	version_buf[len - 5] = '\0';

	LOG_INF("on_cmd_get_version: %d %s", len, version_buf);

	if (len < (sizeof(version_buf) - 1)) {
		return -EAGAIN;
	}

	strcpy(dev->drv_version, &version_buf[sizeof("+VER:") - 1]);

	k_sem_give(&dev->get_version_sem);

	return sizeof(version_buf) - 5;
}

static uint8_t freq_to_chan(int frequency)
{
	uint8_t channel = 0;

	switch (frequency) {
		case 2412:
			channel = 1;
			break;
		case 2417:
			channel = 2;
			break;
		case 2422:
			channel = 3;
			break;
		case 2427:
			channel = 4;
			break;
		case 2432:
			channel = 5;
			break;
		case 2437:
			channel = 6;
			break;
		case 2442:
			channel = 7;
			break;
		case 2447:
			channel = 8;
			break;
		case 2452:
			channel = 9;
			break;
		case 2457:
			channel = 10;
			break;
		case 2462:
			channel = 11;
			break;
		case 2467:
			channel = 12;
			break;
		case 2472:
			channel = 13;
			break;
		case 2484:
			channel = 14;
			break;
		default:
			break;
	}

	return channel;
}

enum wifi_security_type get_sec_from_flags (char *flags)
{
	enum wifi_security_type sec;

	if (strncmp(flags, "[OPEN]", sizeof("[OPEN]") - 1) == 0) {
		sec = WIFI_SECURITY_TYPE_NONE;
	} else if (strncmp(flags, "[WPS]", sizeof("[WPS]") - 1) == 0) {
		sec = WIFI_SECURITY_TYPE_NONE; // TODO fix this
	} else if (strncmp(flags, "[WPA2-EAP-CCMP]", sizeof("[WPA2-EAP-CCMP]") - 1) == 0) {
		sec = WIFI_SECURITY_TYPE_WPA_PSK;
	} else {
		sec = WIFI_SECURITY_TYPE_UNKNOWN;
	}

	return sec;
}

MODEM_CMD_DIRECT_DEFINE(on_cmd_wfscan)
{
	int err;
	int frequency;
	char *next;
	char *curr;
	char *end;
	static struct wifi_scan_result res = { 0 };
	struct ra_at_data *dev = CONTAINER_OF(data, struct ra_at_data, cmd_handler_data);

	LOG_INF("on_cmd_wfscan: rxd %d", data->rx_buf->len);

	/*
	   Response is formatted as follows:
	   +WFSCAN:<bssid><\t><frequency><\t><signalstrength><\t><flag><\t><ssid><LF>
			   <bssid><\t><frequency><\t><signalstrength><\t><flag><\t><ssid><LF>
			   ...
			   <bssid><\t><frequency><\t><signalstrength><\t><flag><\t><ssid><LF>
			   <CR><LF>OK<CR><LF>
	*/
	len = net_buf_linearize(dev->scan_rsp_buf, sizeof(dev->scan_rsp_buf) - 1, data->rx_buf, 0, sizeof(dev->scan_rsp_buf) - 1);

	LOG_INF("%s", dev->scan_rsp_buf);

	// TODO what if above won't fit in buffer, what error is returned and waht to do with it?

	/* Wait until response paramters are avilable */
	if (len <= sizeof("+WFSCAN:") - 1) {
		return -EAGAIN;
	}

	/* Find next parameter to parse */
	if (dev->last == NULL) {
		curr = &dev->scan_rsp_buf[sizeof("+WFSCAN:") - 1];
	} else {
		curr = dev->last;
	}

	/* NULL terminate what's in the buffer so string functions can be used */
	//scan_rsp_buf[len] = '\0';
	end = &dev->scan_rsp_buf[len];
	*end = '\0';

	// TODO - what do we do about the 'band' parameter...?
	// TODO - what do we do about the mfp parameter...?

	do {
		// TODO - check we have not received <CR>
		// TODO - what about response with no results..?

		/* Find end of first parameter (bssid) so we know it's all in the buffer */
		next = strchr(curr, '\t');
		if (next == NULL) {
			return -EAGAIN;
		}
		*next = '\0';

		/* Extract bssid from parameter */
		err = net_bytes_from_str(res.mac, sizeof(res.mac), curr);
		if (err) {
			return -EBADMSG;
		}
		res.mac_length = WIFI_MAC_ADDR_LEN;

		/* Find end of next parameter (frequency) so we know it's all in the buffer */
		curr = next + 1;
		next = strchr(curr, '\t');
		if (next == NULL) {
			return -EAGAIN;
		}
		*next = '\0';

		/* Extract frequency */
		frequency = strtol(curr, NULL, 10);

		/* Convert frequency to channel */
		res.channel = freq_to_chan(frequency);

		/* Find end of next parameter (signal strength) so we know it's all in the buffer */
		curr = next + 1;
		next = strchr(curr, '\t');
		if (next == NULL) {
			return -EAGAIN;
		}
		*next = '\0';

		/* Extract signal strength */
		res.rssi = strtol(curr, NULL, 10);

		/* Find end of next parameter (flags) so we know it's all in the buffer */
		curr = next + 1;
		next = strchr(curr, '\t');
		if (next == NULL) {
			return -EAGAIN;
		}
		*next = '\0';

		/* Parse flags parameter */
		res.security = get_sec_from_flags(next);

		/* Find end of next parameter (ssid) so we know it's all in the buffer */
		curr = next + 1;
		next = strchr(curr, '\n');
		if (next == NULL) {
			return -EAGAIN;
		}
		*next = '\0';

		/* Extract ssid (if there is one) */
		res.ssid_length = strlen(curr);
		if (res.ssid_length) {
			strcpy(res.ssid, curr);
		}

		if (dev->scan_cb) {
			dev->scan_cb(dev->net_iface, 0, &res);
		}

		// TODO can we detect the end using the fact the whole buffer is null terminated?
		if (++next == end) {
			return -EAGAIN;
		}

		dev->last = next;

		/* Last response is followed by <CR><LF> */
		// TODO - check for max ssid's, might be passed with original scan request...?
	} while (*next != '\r');

	return len;
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
//	MODEM_CMD("VER", on_cmd_get_version, 0U, ""),
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

struct ra_at_data ra_at_driver_data;

static void ra_rx(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct ra_at_data *data = p1;

	while (true) {
		modem_iface_uart_rx_wait(&data->context.iface, K_FOREVER);
		modem_cmd_handler_process(&data->context.cmd_handler, &data->context.iface);

		/* give up time if we have a solid stream of data */
		k_yield();
	}
}

static int ra_at_mgmt_scan(const struct device *dev,
			 struct wifi_scan_params *params,
			 scan_result_cb_t cb)
{
	struct ra_at_data *data = dev->data;

	ARG_UNUSED(params);

	if (data->scan_cb != NULL) {
		return -EINPROGRESS;
	}

	if (!net_if_is_carrier_ok(data->net_iface)) {
		return -EIO;
	}

	data->scan_cb = cb;
	data->scan_max_bss_cnt = params->max_bss_cnt;
	data->last = NULL;

	k_work_submit_to_queue(&data->workq, &data->scan_work);

	return 0;
}

static void ra_at_mgmt_scan_work(struct k_work *work)
{
	struct ra_at_data *dev;
	int ret;
	static const struct modem_cmd cmds[] = {
		MODEM_CMD_DIRECT("+WFSCAN:", on_cmd_wfscan),
	};

	dev = CONTAINER_OF(work, struct ra_at_data, scan_work);

	ret = ra_cmd_send(dev,
			   cmds, ARRAY_SIZE(cmds),
			   RA_AT_CMD_WFSCAN,
			   RA_AT_SCAN_TIMEOUT);

   LOG_DBG("RA AT Wi-Fi scan: cmd = %s", RA_AT_CMD_WFSCAN);

	if (ret < 0) {
		LOG_ERR("Failed to scan: ret %d", ret);
	}

	dev->scan_cb(dev->net_iface, 0, NULL); // TODO - is this required
	dev->scan_cb = NULL;
}

static int ra_at_mgmt_connect(const struct device *dev,
			    struct wifi_connect_req_params *params)
{
	return 0;
}

static int ra_at_mgmt_disconnect(const struct device *dev)
{
	return 0;
}

static int ra_at_mgmt_iface_status(const struct device *dev,
				 struct wifi_iface_status *status)
{
	return 0;
}

static int ra_at_mgmt_reg_domain(const struct device *dev,
	struct wifi_reg_domain *reg_domain)
{
//	struct wifi_reg_domain {
//	/** Regulatory domain operation */
//	enum wifi_mgmt_op oper;
//	/** Ignore all other regulatory hints over this one, the behavior is
//	 * implementation specific.
//	 */
//	bool force;
//	/** Country code: ISO/IEC 3166-1 alpha-2 */
//	uint8_t country_code[WIFI_COUNTRY_CODE_LEN];
//	/** Number of channels supported */
//	unsigned int num_channels;
//	/** Channels information */
//	struct wifi_reg_chan_info *chan_info;
//};

//enum wifi_mgmt_op {
//	/** Get operation */
//	WIFI_MGMT_GET = 0,
//	/** Set operation */
//	WIFI_MGMT_SET = 1,
//};

//struct wifi_reg_chan_info {
//	/** Center frequency in MHz */
//	unsigned short center_frequency;
//	/** Maximum transmission power (in dBm) */
//	unsigned short max_power:8;
//	/** Is channel supported or not */
//	unsigned short supported:1;
//	/** Passive transmissions only */
//	unsigned short passive_only:1;
//	/** Is a DFS channel */
//	unsigned short dfs:1;
//} __packed;

	return 0;
}

static void ra_at_mgmt_get_version_work(struct k_work *work)
{
	int ret;
	struct ra_at_data *dev;
	static const struct modem_cmd cmds[] = {
		MODEM_CMD_DIRECT("+VER:", on_cmd_get_version),
	};

	dev = CONTAINER_OF(work, struct ra_at_data, get_version_work);

	ret = ra_cmd_send(dev,
			   cmds, ARRAY_SIZE(cmds),
			   RA_AT_CMD_VER,
			   RA_AT_SCAN_TIMEOUT);

	if (ret < 0) {
		LOG_ERR("Failed to scan: ret %d", ret);
	}
}

int ra_at_mgmt_get_version(const struct device *dev, struct wifi_version *params)
{
	struct ra_at_data *data = dev->data;

	memset(params, 0x0, sizeof(*params));

	data->version = params;
	k_sem_init(&data->get_version_sem, 0, 1);

	k_work_submit_to_queue(&data->workq, &data->get_version_work);

	// TODO - is this request as ra_cmd_send waits for response sem....?
	k_sem_take(&data->get_version_sem, K_FOREVER);

	params->drv_version = data->drv_version;

	return 0;
}

static enum offloaded_net_if_types ra_at_offload_get_type(void)
{
	return L2_OFFLOADED_NET_IF_TYPE_WIFI;
}

static void ra_at_iface_init(struct net_if *iface)
{
	ra_at_socket_offload_init(iface);

	/* Not currently connected to a network */
	net_if_dormant_on(iface);
}

static void ra_at_init_work(struct k_work *work)
{
	struct ra_at_data *dev;
	int ret;
	static const struct setup_cmd setup_cmds[] = {
		//SETUP_CMD_NOHANDLE("AT"),
		/* turn off echo */
		//SETUP_CMD_NOHANDLE("ATE0"),
		SETUP_CMD_NOHANDLE("AT+VER"),
	};

	dev = CONTAINER_OF(work, struct ra_at_data, init_work);

	ret = modem_cmd_handler_setup_cmds(&dev->context.iface,
					   &dev->context.cmd_handler, setup_cmds,
					   ARRAY_SIZE(setup_cmds),
					   &dev->sem_response,
					   K_SECONDS(10));
	if (ret < 0) {
		LOG_ERR("Init failed %d", ret);
		return;
	}

	k_sem_give(&dev->sem_if_up);
}

static const struct wifi_mgmt_ops ra_at_mgmt_ops = {
	.scan		   		= ra_at_mgmt_scan,
	.connect	   		= ra_at_mgmt_connect,
	.disconnect	   		= ra_at_mgmt_disconnect,
	.iface_status  		= ra_at_mgmt_iface_status,
	.reg_domain         = ra_at_mgmt_reg_domain,
	.get_version        = ra_at_mgmt_get_version,
#ifdef CONFIG_WIFI_RA_AT_SOFTAP_SUPPORT
	.ap_enable     		= NULL,
	.ap_disable    		= NULL,
	.ap_sta_disconnect 	= NULL,
	.ap_config_params   = NULL,
#endif
};

static const struct net_wifi_mgmt_offload ra_at_api = {
	.wifi_iface.iface_api.init = ra_at_iface_init,
	.wifi_iface.get_type = ra_at_offload_get_type,
	.wifi_mgmt_api = &ra_at_mgmt_ops,
};

static int ra_at_reset(const struct device *dev)
{
#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	const struct ra_at_config *config = dev->config;

	gpio_pin_set_dt(&config->reset, GPIO_OUTPUT_ACTIVE);
	k_sleep(K_MSEC(100));
	gpio_pin_set_dt(&config->reset, GPIO_OUTPUT_INACTIVE);
#endif

	return 0;
}

static int ra_at_init(const struct device *dev);

NET_DEVICE_DT_INST_OFFLOAD_DEFINE(0, ra_at_init, NULL,
				  &ra_at_driver_data, &ra_driver_config,
				  CONFIG_WIFI_INIT_PRIORITY, &ra_at_api,
				  RA_AT_MTU);

CONNECTIVITY_WIFI_MGMT_BIND(Z_DEVICE_DT_DEV_ID(DT_DRV_INST(0)));

static int ra_at_init(const struct device *dev)
{
#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	const struct ra_at_config *config = dev->config;
#endif
	struct ra_at_data *data = dev->data;
	int ret = 0;

	k_sem_init(&data->sem_response, 0, 1);
	k_sem_init(&data->sem_if_up, 0, 1);

	k_work_init(&data->init_work, ra_at_init_work);
	k_work_init(&data->scan_work, ra_at_mgmt_scan_work);
	k_work_init(&data->get_version_work, ra_at_mgmt_get_version_work);

	k_work_queue_start(&data->workq, ra_at_workq_stack,
		K_KERNEL_STACK_SIZEOF(ra_at_workq_stack),
		K_PRIO_COOP(CONFIG_WIFI_RA_AT_WORKQ_THREAD_PRIORITY),
		NULL);
	k_thread_name_set(&data->workq.thread, "ra_at_workq");

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

	k_thread_create(&ra_at_rx_thread, ra_at_rx_stack,
			K_KERNEL_STACK_SIZEOF(ra_at_rx_stack),
			ra_rx,
			data, NULL, NULL,
			K_PRIO_COOP(CONFIG_WIFI_RA_AT_RX_THREAD_PRIORITY), 0,
			K_NO_WAIT);
	k_thread_name_set(&ra_at_rx_thread, "ra_at_rx");

	data->net_iface = NET_IF_GET(Z_DEVICE_DT_DEV_ID(DT_DRV_INST(0)), 0);

	LOG_INF("RESET!!!!!!");

	ret = ra_at_reset(dev);

	//ret = k_sem_take(&data->sem_if_up, K_SECONDS(10));
	//if (ret == -EAGAIN) {
	//	LOG_ERR("Timeout waiting for interface");
	//}

error:
	return ret;
}
