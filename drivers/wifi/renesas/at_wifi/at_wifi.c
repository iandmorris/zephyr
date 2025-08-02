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
#define AT_WIFI_CMD_TIMEOUT			K_SECONDS(10)
#define AT_WIFI_STATUS_TIMEOUT		K_SECONDS(10)
#define AT_WIFI_SCAN_TIMEOUT		K_SECONDS(20)

/* RX thread structures */
K_KERNEL_STACK_DEFINE(at_wifi_rx_stack, CONFIG_WIFI_AT_WIFI_RX_STACK_SIZE);
struct k_thread at_wifi_rx_thread;

NET_BUF_POOL_DEFINE(mdm_recv_pool, MDM_RECV_MAX_BUF, MDM_RECV_BUF_SIZE,
		    0, NULL);

K_KERNEL_STACK_DEFINE(at_wifi_workq_stack, CONFIG_WIFI_AT_WIFI_WORKQ_STACK_SIZE);

static struct at_wifi_data at_wifi_driver_data;

static uint8_t wifi_chan_to_band(uint16_t chan)
{
	uint8_t band;

	if (wifi_utils_validate_chan_2g(chan)) {
		band = WIFI_FREQ_BAND_2_4_GHZ;
	} else if (wifi_utils_validate_chan_5g(chan)) {
		band = WIFI_FREQ_BAND_5_GHZ;
	} else {
		band = WIFI_FREQ_BAND_UNKNOWN;
	}

	return band;
}

static uint8_t wifi_freq_to_chan(int frequency)
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

//	LOG_DBG("flags: %s", flags);

	if (strncmp(flags, "[OPEN]", sizeof("[OPEN]") - 1) == 0) {
		sec = WIFI_SECURITY_TYPE_NONE;
	} else if (strncmp(flags, "[WPS]", sizeof("[WPS]") - 1) == 0) {
		sec = WIFI_SECURITY_TYPE_NONE; // TODO fix this
	} else if (strncmp(flags, "[WPA2-EAP-CCMP]", sizeof("[WPA2-EAP-CCMP]") - 1) == 0) {
		sec = WIFI_SECURITY_TYPE_WPA_PSK;
	} else {
		sec = WIFI_SECURITY_TYPE_UNKNOWN;
	}

/*
 TODO - map these to ephyr types:

[00:00:07.280,000] <dbg> wifi_at_wifi: get_sec_from_flags: flags: [WPA2-PSK-CCMP][WPS][ESS]
[00:00:07.281,000] <dbg> wifi_at_wifi: get_sec_from_flags: flags: [WPA2-PSK-CCMP][WPS][ESS]
[00:00:07.281,000] <dbg> wifi_at_wifi: get_sec_from_flags: flags: [WPA2-PSK-CCMP][WPS][ESS]
[00:00:07.285,000] <dbg> wifi_at_wifi: get_sec_from_flags: flags: [WPA2-EAP-CCMP][ESS]
[00:00:07.291,000] <dbg> wifi_at_wifi: get_sec_from_flags: flags: [WPA-EAP-CCMP][WPA2-EAP-CCMP][ESS]
*/

	return sec;
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

static void at_wifi_init_work(struct k_work *work)
{
	struct at_wifi_data *dev;
	int ret;

	static const struct setup_cmd setup_cmds[] = {
		SETUP_CMD("AT+VER", "+VER:", on_cmd_version, 1U, ""),
		SETUP_CMD("AT+SDKVER", "+SDKVER:", on_cmd_sdk_version, 1U, ""),
		SETUP_CMD("AT+WFMAC=?", "+WFMAC:", on_cmd_get_mac_addr, 1U, ""),
		SETUP_CMD_NOHANDLE("AT+WFMODE=0"),
		SETUP_CMD_NOHANDLE("AT+WFCC=US"),
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
	}

	k_work_submit_to_queue(&dev->workq, &dev->init_work);

	return 0;
}

static int cmd_nwk_join_parse(struct at_wifi_data *dev,
			     struct net_buf *buf, uint16_t len,
			     int *data_offset, long *data_len)
{
	return 0;
}

/*
 * Handler for +WFJAP command. As this command can have a variable number of
 * parameters parsing is performed here, rather than by the modem subsystem.
 *
 * Response Type 1: +WFJAP:1,'xxxx',a.a.a.a
 * Response Type 1: +WFJAP:1,'xxxx',e,f
 *
 * where a.a.a.a is an IPv4 address
 * where 'xxxx' is an SSID
 */
MODEM_CMD_DIRECT_DEFINE(on_cmd_nwk_join)
{
	//int err;
	//int data_offset;
	//long data_len;

	//int result;
	//struct in_addr ip_addr;
	//struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data,
	//				    cmd_handler_data);

	//LOG_DBG("argv[0]: %s", argv[0]);

	//result = atoi(argv[0]);

	LOG_DBG("len: %d", len);
	//LOG_DBG("data: %s", data->rx_buf);

	//err = cmd_nwk_join_parse(dev, data->rx_buf, len, &data_offset, &data_len);
	//if (err) {
	//	if (err == -EAGAIN) {
	//		return -EAGAIN;
	//	}
	//	return len;
	//}

	//if (data_offset + data_len > net_buf_frags_len(data->rx_buf)) {
	//	return -EAGAIN;
	//}

	/*
		if (result == 1) {
		result = 0;
		net_addr_pton(AF_INET, argv[2], &ip_addr);
		net_if_ipv4_addr_add(dev->net_iface, &ip_addr, NET_ADDR_DHCP, 0);
		net_if_dormant_off(dev->net_iface);
	} else {
		result = -1;
		net_if_dormant_on(dev->net_iface);
	}

	wifi_mgmt_raise_connect_result_event(dev->net_iface, result);
	k_sem_give(&dev->sem_connecting);
*/
	return len;
}

MODEM_CMD_DEFINE(on_cmd_nwk_ip)
{
	struct in_addr ip_addr;
	struct in_addr gw_addr;
	struct in_addr netmask;
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data,
					    cmd_handler_data);

	net_addr_pton(AF_INET, argv[1], &ip_addr);
	net_if_ipv4_addr_add(dev->net_iface, &ip_addr, NET_ADDR_DHCP, 0);
	net_addr_pton(AF_INET, argv[2], &netmask);
	net_if_ipv4_set_netmask_by_addr(dev->net_iface, &ip_addr, &netmask);
	net_addr_pton(AF_INET, argv[3], &gw_addr);
	net_if_ipv4_set_gw(dev->net_iface, &gw_addr);

	return 0;
}

MODEM_CMD_DEFINE(on_cmd_disconnected)
{
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data,
					    cmd_handler_data);

	net_if_dormant_on(dev->net_iface);
	wifi_mgmt_raise_disconnect_result_event(dev->net_iface, 0);

	return 0;
}

static const struct modem_cmd unsol_cmds[] = {
	MODEM_CMD("+INIT:DONE,", on_cmd_init_done, 1U, ""),
	MODEM_CMD("+WFDAP:", on_cmd_disconnected, 1U, ""),
	MODEM_CMD_DIRECT("+WFJAP:", on_cmd_nwk_join),
};

MODEM_CMD_DEFINE(on_cmd_ok)
{
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data,
					    cmd_handler_data);

	modem_cmd_handler_set_error(data, 0);
	k_sem_give(&dev->sem_response);

	return 0;
}

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
	struct at_wifi_data *data = dev->data;

	LOG_DBG("");

	data->scan_cb = cb;
	data->scan_max_bss_cnt = params->max_bss_cnt;
	data->last = NULL;

	k_work_submit_to_queue(&data->workq, &data->scan_work);

	return 0;
}


	/*
	   Response is formatted as follows:
	   +WFSCAN:<bssid><\t><frequency><\t><signalstrength><\t><flag><\t><ssid><LF>
			   <bssid><\t><frequency><\t><signalstrength><\t><flag><\t><ssid><LF>
			   ...
			   <bssid><\t><frequency><\t><signalstrength><\t><flag><\t><ssid><LF>
			   <CR><LF>OK<CR><LF>
	*/

	/*
 * Handler for +WFJAP command. As this command can have a variable number of
 * parameters parsing is performed here, rather than by the modem subsystem.
 *
 * Response Type 1: +WFJAP:1,'xxxx',a.a.a.a
 * Response Type 1: +WFJAP:1,'xxxx',e,f
 *
 * where a.a.a.a is an IPv4 address
 * where 'xxxx' is an SSID
 */

MODEM_CMD_DIRECT_DEFINE(on_cmd_scan_rsp)
{
	int err;
	int frequency;
	char *next;
	char *curr;
	char *end;
	static struct wifi_scan_result res;
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data,
					    cmd_handler_data);

	len = net_buf_linearize(dev->scan_rsp_buf, sizeof(dev->scan_rsp_buf) - 1,
							data->rx_buf, 0, sizeof(dev->scan_rsp_buf) - 1);

	/* Wait until response parameters are avilable */
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
	end = &dev->scan_rsp_buf[len];
	*end = '\0';

	do {
		// TODO - check we have not received <CR>
		// TODO - what about response with no results..?
		memset(&res, 0, sizeof(struct wifi_scan_result));

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
		res.channel = wifi_freq_to_chan(frequency);

		/* Convert channel to band */
		res.band = wifi_chan_to_band(res.channel);

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
		res.security = get_sec_from_flags(curr);

		/* Find end of next parameter (ssid) so we know it's all in the buffer */
		curr = next + 1;
		next = strchr(curr, '\n');
		if (next == NULL) {
			return -EAGAIN;
		}
		*next = '\0';

		res.ssid_length = strlen(curr);
		if ((res.ssid_length == 1) && (*curr == '\t')) {
			/* Hidden SSID */
			res.ssid_length = 0;
		} else {
			strcpy(res.ssid, curr);
		}

		// TODO - don't get this information in the scan results...
		res.mfp = 0;

		if (dev->scan_cb) {
			dev->scan_cb(dev->net_iface, 0, &res);
		}

		// TODO can we detect the end using the fact the whole buffer is null terminated?
		dev->last = ++next;
		if (*next == 0x0d)
		{
			break;
		} else if (next == end) {
			return -EAGAIN;
		}

		/* Last response is followed by <CR><LF> */
		// TODO - check for max ssid's, might be passed with original scan request...?
	} while (*next != '\r');

	return len - sizeof("+WFSCAN:");

	return 0;
}

static void at_wifi_mgmt_scan_work(struct k_work *work)
{
	int ret;
	struct at_wifi_data *dev;

	static const struct modem_cmd cmd = MODEM_CMD_DIRECT("+WFSCAN:", on_cmd_scan_rsp);

	LOG_DBG("");

	dev = CONTAINER_OF(work, struct at_wifi_data, scan_work);

	ret = modem_cmd_send(&dev->mctx.iface, &dev->mctx.cmd_handler,
					     &cmd, 1, "AT+WFSCAN", &dev->sem_response, // TODO - should we use this semaphore or one in scan parse function?
					     AT_WIFI_SCAN_TIMEOUT);

	dev->scan_cb(dev->net_iface, 0, NULL); // TODO - is this required
	dev->scan_cb = NULL;
}

static void at_wifi_mgmt_connect_work(struct k_work *work)
{
	int ret;
	struct at_wifi_data *dev;

	static const struct modem_cmd cmd = MODEM_CMD("+NWIP:", on_cmd_nwk_ip, 4U, ",");

	LOG_DBG("");

	dev = CONTAINER_OF(work, struct at_wifi_data, connect_work);

	k_sem_init(&dev->sem_connecting, 0, 1);

	ret = modem_cmd_send(&dev->mctx.iface, &dev->mctx.cmd_handler,
			      NULL, 0, dev->conn_cmd, &dev->sem_response,
			      AT_WIFI_CMD_TIMEOUT);

	if (ret == 0) {
		LOG_DBG("Connecting...");
		ret = k_sem_take(&dev->sem_connecting, AT_WIFI_CONNECT_TIMEOUT);
		if (ret == 0) {
			LOG_DBG("Requesting IP info...");
			ret = modem_cmd_send(&dev->mctx.iface, &dev->mctx.cmd_handler,
						     &cmd, 1, "AT+NWIP=?", &dev->sem_response,
						     AT_WIFI_CMD_TIMEOUT);
			if (ret < 0) {

			}
			goto out;
		}
	}
	net_if_dormant_on(dev->net_iface);

out:
	memset(dev->conn_cmd, 0, sizeof(dev->conn_cmd));
}

/*
MODEM_CMD_DEFINE(on_cmd_wifi_status)
{
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data,
					    cmd_handler_data);

	LOG_DBG("status: %s", argv[0]);

	dev->wifi_status->iface_mode

	return 0;
}
*/

MODEM_CMD_DEFINE(on_cmd_wifi_state)
{
	//int ret;
	//struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data,
	//				    cmd_handler_data);
	//LOG_DBG();
	//ret = net_bytes_from_str(dev->mac_addr, sizeof(dev->mac_addr), argv[0]);

	return 0;
}

static void at_wifi_mgmt_iface_status_work(struct k_work *work)
{
	/*
		AT+WFSTA
		AT+WFRSSI
		AT+WFSTAT
	*/
	struct at_wifi_data *dev;
	int ret;

	LOG_DBG("");

	static const struct setup_cmd query_status_cmds[] = {
//		SETUP_CMD("AT+WFSTA", "", on_cmd_wifi_status, 1U, ""),
		SETUP_CMD("AT+WFSTAT", "", on_cmd_wifi_state, 3U, ","),
	};

	dev = CONTAINER_OF(work, struct at_wifi_data, init_work);

	ret = modem_cmd_handler_setup_cmds(&dev->mctx.iface,
					   &dev->mctx.cmd_handler, query_status_cmds,
					   ARRAY_SIZE(query_status_cmds),
					   &dev->sem_response,
					   AT_WIFI_CMD_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("Query status failed %d", ret);
		return;
	}

	k_sem_give(&dev->sem_wifi_status);

#if 0
	/** Interface state, see enum wifi_iface_state */
	int state;
	/** SSID length */
	unsigned int ssid_len;
	/** SSID */
	char ssid[WIFI_SSID_MAX_LEN + 1];
	/** BSSID */
	char bssid[WIFI_MAC_ADDR_LEN];
	/** Frequency band */
	enum wifi_frequency_bands band;
	/** Channel */
	unsigned int channel;
	/** Interface mode, see enum wifi_iface_mode */
	enum wifi_iface_mode iface_mode;
	/** Link mode, see enum wifi_link_mode */
	enum wifi_link_mode link_mode;
	/** WPA3 enterprise type */
	enum wifi_wpa3_enterprise_type wpa3_ent_type;
	/** Security type, see enum wifi_security_type */
	enum wifi_security_type security;
	/** MFP options, see enum wifi_mfp_options */
	enum wifi_mfp_options mfp;
	/** RSSI */
	int rssi;
	/** DTIM period */
	unsigned char dtim_period;
	/** Beacon interval */
	unsigned short beacon_interval;
	/** is TWT capable? */
	bool twt_capable;
	/** The current 802.11 PHY TX data rate (in Mbps) */
	float current_phy_tx_rate;
#endif

}

static int at_wifi_mgmt_connect(const struct device *dev,
	struct wifi_connect_req_params *params)
{
	int ix;
	/*
		AT+WFJAP
		AT+WFJAPBSSID - If BSSID supplied by caller
	 */

	struct at_wifi_data *data = dev->data;

	//if (!net_if_is_carrier_ok(data->net_iface) ||
	//    !net_if_is_admin_up(data->net_iface)) {
	//	return -EIO;
	//}

	/** SSID */
	//const uint8_t params->ssid;
	/** SSID length */
	//uint8_t ssid_length; /* Max 32 */

	if (params->security == WIFI_SECURITY_TYPE_NONE) {
		ix = snprintk(data->conn_cmd, sizeof(data->conn_cmd), "AT+WFJAP=%s,%d", params->ssid, params->security);
	} else {//if (params->security == WIFI_SECURITY_TYPE_WPA_PSK) {
		ix = snprintk(data->conn_cmd, sizeof(data->conn_cmd), "AT+WFJAP=%s,4,1,", params->ssid);
		memcpy(&data->conn_cmd[ix], params->psk, params->psk_length);
	}

	LOG_DBG("%s", data->conn_cmd);

	k_work_submit_to_queue(&data->workq, &data->connect_work);

	return 0;
}

static int at_wifi_mgmt_disconnect(const struct device *dev)
{
	int ret;
	struct at_wifi_data *data = dev->data;

	LOG_DBG("");

	ret = modem_cmd_send(&data->mctx.iface, &data->mctx.cmd_handler,
				     NULL, 0, "AT+WFQAP", &data->sem_response,
				     AT_WIFI_CMD_TIMEOUT);

	if (ret == 0) {
		net_if_dormant_on(data->net_iface);
		wifi_mgmt_raise_disconnect_result_event(data->net_iface, 0);
	}

	return ret;
}

int at_wifi_mgmt_iface_status(const struct device *dev,
			     struct wifi_iface_status *status)
{
	struct at_wifi_data *data = dev->data;

	LOG_DBG("");

	memset(status, 0x0, sizeof(struct wifi_iface_status));

	if (!net_if_is_carrier_ok(data->net_iface)) {
		status->state = WIFI_STATE_INTERFACE_DISABLED;
		return 0;
	}

	data->wifi_status = status;

	k_work_submit_to_queue(&data->workq, &data->iface_status_work);
	k_sem_take(&data->sem_wifi_status, AT_WIFI_STATUS_TIMEOUT);

	return 0;
}

int at_wifi_mgmt_get_version(const struct device *dev,
				 struct wifi_version *params)
{
	struct at_wifi_data *data = dev->data;

	/* Version information was read during the initialization process */
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
	k_sem_init(&data->sem_connecting, 0, 1);
	k_sem_init(&data->sem_wifi_status, 0, 1);

	k_work_init(&data->init_work, at_wifi_init_work);
	k_work_init(&data->scan_work, at_wifi_mgmt_scan_work);
	k_work_init(&data->connect_work, at_wifi_mgmt_connect_work);
	k_work_init(&data->iface_status_work, at_wifi_mgmt_iface_status_work);

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
