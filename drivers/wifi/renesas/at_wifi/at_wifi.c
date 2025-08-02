/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "zephyr/kernel.h"

#if defined(CONFIG_WIFI_AT_WIFI_TRANSPORT_UART)
#define DT_DRV_COMPAT renesas_at_wifi_uart
#else
#define DT_DRV_COMPAT renesas_at_wifi_spi
#endif

#include <stdlib.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wifi_at_wifi, CONFIG_WIFI_LOG_LEVEL);

#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/wifi_utils.h>
#include <zephyr/net/conn_mgr/connectivity_wifi_mgmt.h>

#include "at_wifi.h"
#include "at_wifi_offload.h"
#include "at_wifi_transport.h"

#define AT_WIFI_INIT_TIMEOUT    K_SECONDS(10)
#define AT_WIFI_CONNECT_TIMEOUT K_SECONDS(20)
#define AT_WIFI_CMD_TIMEOUT     K_SECONDS(10)
#define AT_WIFI_STATUS_TIMEOUT  K_SECONDS(10)
#define AT_WIFI_SCAN_TIMEOUT    K_SECONDS(20)

/* RX thread structures */
K_KERNEL_STACK_DEFINE(at_wifi_rx_stack, CONFIG_WIFI_AT_WIFI_RX_STACK_SIZE);
struct k_thread at_wifi_rx_thread;

NET_BUF_POOL_DEFINE(mdm_recv_pool, CONFIG_WIFI_AT_WIFI_MODEM_RX_BUF_COUNT,
		    CONFIG_WIFI_AT_WIFI_MODEM_RX_BUF_SIZE, 0, NULL);

K_KERNEL_STACK_DEFINE(at_wifi_workq_stack, CONFIG_WIFI_AT_WIFI_WORKQ_STACK_SIZE);

struct at_wifi_data at_wifi_driver_data;

static enum wifi_frequency_bands wifi_utils_chan_to_band(uint16_t chan)
{
	enum wifi_frequency_bands band;

	if (wifi_utils_validate_chan_2g(chan)) {
		band = WIFI_FREQ_BAND_2_4_GHZ;
	} else if (wifi_utils_validate_chan_5g(chan)) {
		band = WIFI_FREQ_BAND_5_GHZ;
	} else if (wifi_utils_validate_chan_6g(chan)) {
		band = WIFI_FREQ_BAND_6_GHZ;
	} else {
		band = WIFI_FREQ_BAND_UNKNOWN;
	}

	return band;
}

// TODO - can a formula be used instead of switch statement
// TODO - add 5GHz channels or name this 2g and have another for 5g
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

enum wifi_security_type get_sec_from_flags(char *flags)
{
	enum wifi_security_type sec = WIFI_SECURITY_TYPE_UNKNOWN;

	// LOG_DBG("flags: %s", flags);

	if (*flags++ == '[') {
		if (strncmp(flags, "WPA", sizeof("WPA") - 1) == 0) {
			if (strncmp(flags, "WPA2-PSK", sizeof("WPA2-PSK") - 1) == 0) {
				sec = WIFI_SECURITY_TYPE_PSK;
			} else {
				sec = WIFI_SECURITY_TYPE_WPA_PSK;
			}
		} else if (strncmp(flags, "WEP", sizeof("WEP") - 1) == 0) {
			sec = WIFI_SECURITY_TYPE_WEP;
		} else {
			sec = WIFI_SECURITY_TYPE_NONE;
		}
		// TODO - handle more security types...
	}

	return sec;
}

#if defined(CONFIG_WIFI_AT_WIFI_SOC_RA6WX)
/*
 * Handler for +WFSCAN command. As this command can have a variable number of
 * parameters parsing is performed here, rather than by the modem subsystem.
 *
 * RA6Wx scan response is formatted as follows:
 *	+WFSCAN:[NO]<\t>[SSID]<\t>[SIGNAL]<\t>[CH]<\t>[SECURITY<LF>
 *			<no><\t><ssid><\t><signal><\t><ch><\t><security><LF>
 *			...
 *			<no><\t><ssid><\t><signal><\t><ch><\t><security><LF>
 *			<CR><LF>OK<CR><LF>
 */
MODEM_CMD_DIRECT_DEFINE(on_cmd_scan_rsp)
{
	char *next;
	char *curr;
	char *end;
	char header[] = "+WFSCAN:[NO]\t[SSID]\t[SIGNAL]\t[CH]\t[SECURITY]\n";
	static struct wifi_scan_result res;
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data, modem.cmd_handler_data);

	len = net_buf_linearize(dev->scan.rsp_buf, sizeof(dev->scan.rsp_buf) - 1, data->rx_buf, 0,
				sizeof(dev->scan.rsp_buf) - 1);

	/* Wait until response parameters are avilable */
	if (len <= sizeof(header) - 1) {
		return -EAGAIN;
	}

	/* Find next parameter to parse */
	if (dev->scan.last_field == NULL) {
		curr = &dev->scan.rsp_buf[sizeof(header) - 1];
	} else {
		curr = dev->scan.last_field;
	}

	/* NULL terminate what's in the buffer so string functions can be used */
	end = &dev->scan.rsp_buf[len];
	*end = '\0';

	while (*curr != '\r') {
		// TODO - what about response with no results..?
		memset(&res, 0, sizeof(struct wifi_scan_result));

		/* Find end of first parameter (entry number) so we know it's
		   all in the buffer before attempting to parse it. */
		next = strchr(curr, '\t');
		if (next == NULL) {
			return -EAGAIN;
		}
		*next = '\0';

		LOG_DBG("curr: %s", curr);
	}
}
#else
/*
 * Handler for +WFSCAN command. As this command can have a variable number of
 * parameters parsing is performed here, rather than by the modem subsystem.
 *
 * DA16xxx scan response is formatted as follows:
 *	+WFSCAN:<bssid><\t><frequency><\t><signalstrength><\t><flag><\t><ssid><LF>
 *			<bssid><\t><frequency><\t><signalstrength><\t><flag><\t><ssid><LF>
 *			...
 *			<bssid><\t><frequency><\t><signalstrength><\t><flag><\t><ssid><LF>
 *			<CR><LF>OK<CR><LF>
 */
MODEM_CMD_DIRECT_DEFINE(on_cmd_scan_rsp)
{
	int err;
	int frequency;
	char *next;
	char *curr;
	char *end;
	static struct wifi_scan_result res;
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data, modem.cmd_handler_data);

	len = net_buf_linearize(dev->scan.rsp_buf, sizeof(dev->scan.rsp_buf) - 1, data->rx_buf, 0,
				sizeof(dev->scan.rsp_buf) - 1);

	/* Wait until response parameters are avilable */
	if (len <= sizeof("+WFSCAN:") - 1) {
		return -EAGAIN;
	}

	/* Find next parameter to parse */
	if (dev->scan.last_field == NULL) {
		curr = &dev->scan.rsp_buf[sizeof("+WFSCAN:") - 1];
	} else {
		curr = dev->scan.last_field;
	}

	/* NULL terminate what's in the buffer so string functions can be used */
	end = &dev->scan.rsp_buf[len];
	*end = '\0';

	while (*curr != '\r') {
		// TODO - what about response with no results..?
		memset(&res, 0, sizeof(struct wifi_scan_result));

		/* Find end of first parameter (bssid) so we know it's
		   all in the buffer before attempting to parse it. */
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

		/* Find end of next parameter (frequency) so we know it's
		   all in the buffer before attempting to parse it. */
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
		res.band = wifi_utils_chan_to_band(res.channel);

		/* Find end of next parameter (RSSI) so we know it's
		   all in the buffer before attempting to parse it. */
		curr = next + 1;
		next = strchr(curr, '\t');
		if (next == NULL) {
			return -EAGAIN;
		}
		*next = '\0';

		/* Extract signal strength */
		res.rssi = strtol(curr, NULL, 10);

		/* Find end of next parameter (flags) so we know it's
		   all in the buffer before attempting to parse it. */
		curr = next + 1;
		next = strchr(curr, '\t');
		if (next == NULL) {
			return -EAGAIN;
		}
		*next = '\0';

		/* Parse flags parameter */
		res.security = get_sec_from_flags(curr);

		/* Find end of last parameter (ssid) so we know it's
		   all in the buffer before attempting to parse it. */
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

		// TODO - check for max ssid's, might be passed with original scan request...?
		if (dev->scan.cb) {
			dev->scan.cb(dev->net_iface, 0, &res);
		}

		/* Remember how far we got so we don't parse the same line again */
		dev->scan.last_field = next + 1;

		/* Are there any more bytes left in the buffer */
		if ((end - next) == 0) {
			return -EAGAIN;
		}

		/* Move to the next line (if there is one) */
		curr = next + 1;
	}

	/* Don't consume the OK part of the response so that the caller can parse it */
	return len - (end - curr);
}
#endif

#if defined(CONFIG_WIFI_AT_WIFI_SOC_RA6WX)
/*
 * Handler for +WFSTAT command. As this command can have a variable number of
 * parameters parsing is performed here, rather than by the modem subsystem.
 */
MODEM_CMD_DIRECT_DEFINE(on_cmd_wifi_state)
{
	char *curr;
	char *next;
	char *value;
	char wfstat_buf[256]; // TODO - use macro or create dummy string of max length
	struct wifi_iface_status *status;

	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data, modem.cmd_handler_data);

	len = net_buf_linearize(wfstat_buf, sizeof(wfstat_buf) - 1, data->rx_buf, 0,
				sizeof(wfstat_buf) - 1);

	status = dev->wifi_status;

	/* Wait until parameters are avilable */
	if (len <= sizeof("+WFSTAT:") - 1) {
		return -EAGAIN;
	}

	// TODO - update parsing to handle parameters returned if not connect...
	dev->wifi_status->state = WIFI_STATE_COMPLETED;

	/* NULL terminate what's in the buffer so string functions can be used */
	wfstat_buf[len] = '\0';
	curr = &wfstat_buf[sizeof("+WFSTAT:") - 1];

	/* Find end of first parameter (interface name) so we know it's
	   all in the buffer before attempting to parse it. */
	next = strchr(curr, '\n');
	if (next == NULL) {
		return -EAGAIN;
	}
	*next = '\0';

	/* Not using the interface name at present */

	/* Find end of next parameter (ssid length) so we know it's
	   all in the buffer before attempting to parse it. */
	curr = next + 1;
	next = strchr(curr, ' ');
	if (next == NULL) {
		return -EAGAIN;
	}
	*next = '\0';

	/* Extract the SSID length value */
	value = curr + (sizeof("SSID_LENGTH:") - 1);
	status->ssid_len = atoi(value);

	/* Find end of next parameter (ssid) so we know it's
	   all in the buffer before attempting to parse it. */
	curr = next + 1;
	next = strchr(curr, ' ');
	if (next == NULL) {
		return -EAGAIN;
	}
	*next = '\0';

	/* Extract the SSID value */
	value = curr + (sizeof("SSID:") - 1);
	memcpy(status->ssid, value, status->ssid_len);

	/* Find end of next parameter (bssid) so we know it's
	   all in the buffer before attempting to parse it. */
	curr = next + 1;
	next = strchr(curr, ' ');
	if (next == NULL) {
		return -EAGAIN;
	}
	*next = '\0';

	/* Extract the BSSID value */
	value = curr + (sizeof("BSSID:") - 1);
	net_bytes_from_str(status->bssid, sizeof(status->bssid), value);

	/* Find end of next parameter (security) so we know it's
	   all in the buffer before attempting to parse it. */
	curr = next + 1;
	next = strstr(curr, ") ");
	if (next == NULL) {
		return -EAGAIN;
	}
	*(next + 1) = '\0';

	/* Extract the security value */
	value = curr + (sizeof("SECURITY:") - 1);

	/* Find end of next parameter (channel) so we know it's
	   all in the buffer before attempting to parse it. */
	curr = next + 2;
	next = strchr(curr, '\n');
	if (next == NULL) {
		return -EAGAIN;
	}
	*next = '\0';

	/* Extract the security value */
	value = curr + (sizeof("CHANNEL:") - 1);
	status->channel = atoi(value);
	status->band = wifi_utils_chan_to_band(status->channel);

	return next - wfstat_buf;
}
#else
/*
 * Handler for +WFSTAT command. As this command can have a variable number of
 * parameters parsing is performed here, rather than by the modem subsystem.
 */
MODEM_CMD_DIRECT_DEFINE(on_cmd_wifi_state)
{
	char *curr;
	char *next;
	char *value;
	char wfstat_buf[256]; // TODO - use macro or create dummy string of max length
	struct wifi_iface_status *status;

	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data, modem.cmd_handler_data);

	len = net_buf_linearize(wfstat_buf, sizeof(wfstat_buf) - 1, data->rx_buf, 0,
				sizeof(wfstat_buf) - 1);

	status = dev->wifi_status;

	/* Wait until parameters are avilable */
	if (len <= sizeof("+WFSTAT:") - 1) {
		return -EAGAIN;
	}

	// TODO - update parsing to handle parameters returned if not connect...

	/* NULL terminate what's in the buffer so string functions can be used */
	wfstat_buf[len] = '\0';
	curr = &wfstat_buf[sizeof("+WFSTAT:") - 1];

	/* Find end of first parameter (interface name) so we know it's
	   all in the buffer before attempting to parse it. */
	next = strchr(curr, '\n');
	if (next == NULL) {
		return -EAGAIN;
	}
	*next = '\0';

	/* Not using the interface name at present */

	/* Find end of next parameter (mac address) so we know it's
	   all in the buffer before attempting to parse it. */
	curr = next + 1;
	next = strchr(curr, '\n');
	if (next == NULL) {
		return -EAGAIN;
	}
	*next = '\0';

	/* Not using the MAC address at present */

	/* Find end of next parameter (bssid) so we know it's
	   all in the buffer before attempting to parse it. */
	curr = next + 1;
	next = strchr(curr, '\n');
	if (next == NULL) {
		return -EAGAIN;
	}
	*next = '\0';

	/* Extract the BSSID value */
	value = curr + (sizeof("bssid=") - 1);
	net_bytes_from_str(status->bssid, sizeof(status->bssid), value);

	/* Find end of next parameter (ssid) so we know it's
	   all in the buffer before attempting to parse it. */
	curr = next + 1;
	next = strchr(curr, '\n');
	if (next == NULL) {
		return -EAGAIN;
	}
	*next = '\0';

	/* Extract the SSID value */
	value = curr + (sizeof("ssid=") - 1);
	status->ssid_len = MIN(strlen(value), sizeof(status->ssid));
	memcpy(status->ssid, value, status->ssid_len);

	/* Find end of next parameter (id) so we know it's
	   all in the buffer before attempting to parse it. */
	curr = next + 1;
	next = strchr(curr, '\n');
	if (next == NULL) {
		return -EAGAIN;
	}
	*next = '\0';

	/* Not using the ID value at present */

	/* Find end of next parameter (mode) so we know it's
	   all in the buffer before attempting to parse it. */
	curr = next + 1;
	next = strchr(curr, '\n');
	if (next == NULL) {
		return -EAGAIN;
	}
	*next = '\0';

	/* Extract the mode value */
	value = curr + (sizeof("mode=") - 1);
	if (strcmp(value, "STATION") == 0) {
		dev->wifi_status->iface_mode = WIFI_MODE_INFRA;
	} else {
		dev->wifi_status->iface_mode = WIFI_MODE_AP;
	}

	/* Find end of next parameter (key_mgmt) so we know it's
	   all in the buffer before attempting to parse it. */
	curr = next + 1;
	next = strchr(curr, '\n');
	if (next == NULL) {
		return -EAGAIN;
	}
	*next = '\0';

	/* Extract the key_mgmt value */
	value = curr + (sizeof("key_mgmt=") - 1);
	if (strcmp(value, "WPA2-PSK") == 0) {
		status->security = WIFI_SECURITY_TYPE_PSK;
	}

	// TODO - parse more security types...

	/* Find end of next parameter (pairwise_cipher) so we know it's
	   all in the buffer before attempting to parse it. */
	curr = next + 1;
	next = strchr(curr, '\n');
	if (next == NULL) {
		return -EAGAIN;
	}
	*next = '\0';

	/* Not using the pairwise_cipher value at present */

	/* Find end of next parameter (group_cipher) so we know it's
	   all in the buffer before attempting to parse it. */
	curr = next + 1;
	next = strchr(curr, '\n');
	if (next == NULL) {
		return -EAGAIN;
	}
	*next = '\0';

	/* Not using the group_cipher value at present */

	/* Find end of next parameter (channel) so we know it's
	   all in the buffer before attempting to parse it. */
	curr = next + 1;
	next = strchr(curr, '\n');
	if (next == NULL) {
		return -EAGAIN;
	}
	*next = '\0';

	/* Extract the channel value */
	value = curr + (sizeof("channel=") - 1);
	status->channel = atoi(value);
	status->band = wifi_utils_chan_to_band(status->channel);

	/* Find end of next parameter (wpa_state) so we know it's
	   all in the buffer before attempting to parse it. */
	curr = next + 1;
	next = strchr(curr, '\n');
	if (next == NULL) {
		return -EAGAIN;
	}
	*next = '\0';

	/* Extract the wpa_state value */
	value = curr + (sizeof("wpa_state=") - 1);
	if (strcmp(value, "COMPLETED") == 0) {
		dev->wifi_status->state = WIFI_STATE_COMPLETED;
	}

	// TODO - parse other state values...

	return next - wfstat_buf;
}
#endif

MODEM_CMD_DEFINE(on_cmd_version)
{
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data, modem.cmd_handler_data);

	LOG_DBG("version: %s", argv[0]);

	if (strlen(argv[0]) < sizeof(dev->info.ver_str)) {
		strcpy(dev->info.ver_str, argv[0]);
	}

	return 0;
}

MODEM_CMD_DEFINE(on_cmd_wifi_rssi)
{
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data, modem.cmd_handler_data);

	LOG_DBG("rssi: %s", argv[0]);

	dev->info.rssi = atoi(argv[0]);

	return 0;
}

MODEM_CMD_DEFINE(on_cmd_sdk_version)
{
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data, modem.cmd_handler_data);

	if (strlen(argv[0]) < sizeof(dev->info.sdk_ver_str)) {
		strcpy(dev->info.sdk_ver_str, argv[0]);
	}

	return 0;
}

MODEM_CMD_DEFINE(on_cmd_get_mac_addr)
{
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data, modem.cmd_handler_data);

	net_bytes_from_str(dev->info.mac_addr, sizeof(dev->info.mac_addr), argv[0]);

	return 0;
}

MODEM_CMD_DEFINE(on_cmd_get_chip_name)
{
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data, modem.cmd_handler_data);

	// TODO - check these chip names on all devices
	if (strcmp(argv[0], "DA16200") == 0) {
		dev->info.chip_id = AT_WIFI_CHIP_ID_DA16200;
	} else if (strcmp(argv[0], "DA16600") == 0) {
		dev->info.chip_id = AT_WIFI_CHIP_ID_DA16600;
		// TODO - the following might need updating depending on the resolution of
		// WIRCON-30823
	} else if (strcmp(argv[0], "RA6W1-RRQ61001") == 0) {
		dev->info.chip_id = AT_WIFI_CHIP_ID_RA6W1;
	} else if (strcmp(argv[0], "RA6W2") == 0) {
		dev->info.chip_id = AT_WIFI_CHIP_ID_RA6W2;
	} else {
		dev->info.chip_id = AT_WIFI_CHIP_ID_UNKNOWN;
	}

	LOG_DBG("chipname: %s", argv[0]);
	LOG_DBG("chip id: %d", dev->info.chip_id);

	return 0;
}

MODEM_CMD_DEFINE(on_cmd_nwk_ip)
{
	struct in_addr ip_addr;
	struct in_addr gw_addr;
	struct in_addr netmask;
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data, modem.cmd_handler_data);

	net_addr_pton(AF_INET, argv[1], &ip_addr);
	net_if_ipv4_addr_add(dev->net_iface, &ip_addr, NET_ADDR_DHCP, 0);
	net_addr_pton(AF_INET, argv[2], &netmask);
	net_if_ipv4_set_netmask_by_addr(dev->net_iface, &ip_addr, &netmask);
	net_addr_pton(AF_INET, argv[3], &gw_addr);
	net_if_ipv4_set_gw(dev->net_iface, &gw_addr);

	return 0;
}

/*
 * Handler for +WFJAP command. As this command can have a variable number of
 * parameters parsing is performed here, rather than by the modem subsystem.
 *
 * Response Type 1: +WFJAP:#,'xxxx',a.a.a.a
 * Response Type 2: +WFJAP:#
 *
 * where # is the result
 * where 'xxxx' is an SSID
 * where a.a.a.a is an IPv4 address
 */
MODEM_CMD_DIRECT_DEFINE(on_cmd_join_ap)
{
	int op_result;
	char *curr;
	char *next;
	struct in_addr ip_addr;
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data, modem.cmd_handler_data);

	char cwjap_buf[sizeof("+WFJAP:#,'',255.255.255.255") + WIFI_SSID_MAX_LEN];

	len = net_buf_linearize(cwjap_buf, sizeof(cwjap_buf) - 1, data->rx_buf, 0,
				sizeof(cwjap_buf) - 1);

	/* Result parameter is first and is either '1' or '0'. Wait for this, if
	   it is '0' nothing follows. If it is '1' then further fields are present. */
	if (len <= sizeof("+WFJAP:#") - 1) {
		return -EAGAIN;
	}
	curr = &cwjap_buf[sizeof("+WFJAP:") - 1];

	/* NULL terminate what's in the buffer so string functions can be used */
	cwjap_buf[len] = '\0';

	op_result = atoi(curr);
	if (op_result == 1) {
		/* Find start of next parameter (ssid) */
		next = strchr(curr, ',');
		if (next == NULL) {
			return -EAGAIN;
		}
		*next = '\0';

		/* Find end of next parameter (ssid) so we know it's
		   all in the buffer before attempting to parse it. */
		curr = next + 1;
		next = strchr(curr, ',');
		if (next == NULL) {
			return -EAGAIN;
		}
		*next = '\0';

		// TODO - waht to do with SSID?

		/* Find end of next parameter (address) so we know it's
		   all in the buffer before attempting to parse it. */
		curr = next + 1;
		next = strchr(curr, '\r');
		if (next == NULL) {
			return -EAGAIN;
		}
		*next = '\0';

		net_addr_pton(AF_INET, curr, &ip_addr);
		net_if_ipv4_addr_add(dev->net_iface, &ip_addr, NET_ADDR_DHCP, 0);
		net_if_dormant_off(dev->net_iface);
		op_result = WIFI_STATUS_CONN_SUCCESS;
	} else {
		net_if_dormant_on(dev->net_iface);
		op_result = WIFI_STATUS_CONN_FAIL;
	}

	wifi_mgmt_raise_connect_result_event(dev->net_iface, op_result);
	k_sem_give(&dev->sem_connecting);

	return len;
}

MODEM_CMD_DEFINE(on_cmd_init_done)
{
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data, modem.cmd_handler_data);

	LOG_DBG("mode: %s", argv[0]);

	if (net_if_is_carrier_ok(dev->net_iface)) {
		net_if_dormant_on(dev->net_iface);
		net_if_carrier_off(dev->net_iface);
	}

	k_work_submit_to_queue(&dev->workq, &dev->init_work);

	return 0;
}

MODEM_CMD_DEFINE(on_cmd_disconnected)
{
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data, modem.cmd_handler_data);

	net_if_dormant_on(dev->net_iface);
	wifi_mgmt_raise_disconnect_result_event(dev->net_iface, 0);

	return 0;
}

static const struct modem_cmd unsol_cmds[] = {
	MODEM_CMD_DIRECT("+WFJAP:", on_cmd_join_ap),
	MODEM_CMD("+INIT:DONE,", on_cmd_init_done, 1U, ""),
	MODEM_CMD("+WFDAP:", on_cmd_disconnected, 1U, ""),
};

MODEM_CMD_DEFINE(on_cmd_ok)
{
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data, modem.cmd_handler_data);

	modem_cmd_handler_set_error(data, 0);
	k_sem_give(&dev->sem_response);

	return 0;
}

MODEM_CMD_DEFINE(on_cmd_error)
{
	struct at_wifi_data *dev = CONTAINER_OF(data, struct at_wifi_data, modem.cmd_handler_data);

	LOG_DBG("error: %s", argv[0]);

	modem_cmd_handler_set_error(data, -EIO);
	k_sem_give(&dev->sem_response);

	return 0;
}

static const struct modem_cmd response_cmds[] = {
	MODEM_CMD("OK", on_cmd_ok, 0U, ""),
	MODEM_CMD("ERROR:", on_cmd_error, 1U, ""),
};

static void at_wifi_init_work(struct k_work *work)
{
	int ret;
	struct at_wifi_data *dev;

	static const struct setup_cmd setup_cmds[] = {
		SETUP_CMD("AT+VER", "+VER:", on_cmd_version, 1U, ""),
		SETUP_CMD("AT+SDKVER", "+SDKVER:", on_cmd_sdk_version, 1U, ""),
		SETUP_CMD("AT+WFMAC=?", "+WFMAC:", on_cmd_get_mac_addr, 1U, ""),
		SETUP_CMD("AT+CHIPNAME", "+CHIPNAME:", on_cmd_get_chip_name, 1U, ""),
		SETUP_CMD_NOHANDLE("AT+WFMODE=0"),
		SETUP_CMD_NOHANDLE("AT+WFCC=US"),
	};

	dev = CONTAINER_OF(work, struct at_wifi_data, init_work);

	ret = modem_cmd_handler_setup_cmds(&dev->modem.ctx.iface, &dev->modem.ctx.cmd_handler,
					   setup_cmds, ARRAY_SIZE(setup_cmds), &dev->sem_response,
					   AT_WIFI_INIT_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("Init failed %d", ret);
		return;
	}

	net_if_set_link_addr(dev->net_iface, dev->info.mac_addr, sizeof(dev->info.mac_addr),
			     NET_LINK_ETHERNET);
	net_if_carrier_on(dev->net_iface);

	k_sem_give(&dev->sem_if_up);
}

static void at_wifi_mgmt_scan_work(struct k_work *work)
{
	int ret;
	struct at_wifi_data *dev;

	static const struct modem_cmd cmd = MODEM_CMD_DIRECT("+WFSCAN:", on_cmd_scan_rsp);

	LOG_DBG("");

	dev = CONTAINER_OF(work, struct at_wifi_data, scan_work);

	ret = modem_cmd_send(&dev->modem.ctx.iface, &dev->modem.ctx.cmd_handler,
			     // TODO - should we use this semaphore or one in scan parse function?
			     &cmd, 1, "AT+WFSCAN", &dev->sem_response, AT_WIFI_SCAN_TIMEOUT);

	dev->scan.cb(dev->net_iface, 0, NULL);
	dev->scan.cb = NULL;
}

static void at_wifi_mgmt_connect_work(struct k_work *work)
{
	int ret;
	struct at_wifi_data *dev;

	static const struct modem_cmd cmd = MODEM_CMD("+NWIP:", on_cmd_nwk_ip, 4U, ",");

	LOG_DBG("");

	dev = CONTAINER_OF(work, struct at_wifi_data, connect_work);

	k_sem_init(&dev->sem_connecting, 0, 1);

	ret = modem_cmd_send(&dev->modem.ctx.iface, &dev->modem.ctx.cmd_handler, NULL, 0,
			     dev->conn_cmd, &dev->sem_response, AT_WIFI_CMD_TIMEOUT);

	if (ret == 0) {
		LOG_DBG("Connecting...");
		ret = k_sem_take(&dev->sem_connecting, AT_WIFI_CONNECT_TIMEOUT);
		if (ret == 0) {
			LOG_DBG("Requesting IP info...");
			ret = modem_cmd_send(&dev->modem.ctx.iface, &dev->modem.ctx.cmd_handler,
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

static int at_wifi_mgmt_scan(const struct device *dev, struct wifi_scan_params *params,
			     scan_result_cb_t cb)
{
	struct at_wifi_data *data = dev->data;

	data->scan.cb = cb;
	data->scan.max_bss_cnt = params->max_bss_cnt;
	data->scan.last_field = NULL;

	k_work_submit_to_queue(&data->workq, &data->scan_work);

	return 0;
}

static int at_wifi_mgmt_connect(const struct device *dev, struct wifi_connect_req_params *params)
{
	int ix;
	/*
		AT+WFJAP
		AT+WFJAPBSSID - If BSSID supplied by caller
	 */

	struct at_wifi_data *data = dev->data;

	// if (!net_if_is_carrier_ok(data->net_iface) ||
	//     !net_if_is_admin_up(data->net_iface)) {
	//	return -EIO;
	// }

	/** SSID */
	// const uint8_t params->ssid;
	/** SSID length */
	// uint8_t ssid_length; /* Max 32 */

	if (params->security == WIFI_SECURITY_TYPE_NONE) {
		ix = snprintk(data->conn_cmd, sizeof(data->conn_cmd), "AT+WFJAP=%s,%d",
			      params->ssid, params->security);
	} else { // if (params->security == WIFI_SECURITY_TYPE_WPA_PSK) {
		ix = snprintk(data->conn_cmd, sizeof(data->conn_cmd), "AT+WFJAP=%s,4,1,",
			      params->ssid);
		memcpy(&data->conn_cmd[ix], params->psk, params->psk_length);
	}

	LOG_DBG("%s", data->conn_cmd);

	k_work_submit_to_queue(&data->workq, &data->connect_work);

	/* TODO - should we wait until connection attempt has finished before returning? */

	return 0;
}

static int at_wifi_mgmt_disconnect(const struct device *dev)
{
	int ret;
	struct at_wifi_data *data = dev->data;

	ret = modem_cmd_send(&data->modem.ctx.iface, &data->modem.ctx.cmd_handler, NULL, 0,
			     "AT+WFQAP", &data->sem_response, AT_WIFI_CMD_TIMEOUT);

	if (ret == 0) {
		net_if_dormant_on(data->net_iface);
		wifi_mgmt_raise_disconnect_result_event(data->net_iface, 0);
	}

	return ret;
}

#if 0 // TODO - not populating these values at present...
	/** Link mode, see enum wifi_link_mode */
	enum wifi_link_mode link_mode;
	/** WPA3 enterprise type */
	enum wifi_wpa3_enterprise_type wpa3_ent_type;
	/** MFP options, see enum wifi_mfp_options */
	enum wifi_mfp_options mfp;
	/** DTIM period */
	unsigned char dtim_period;
	/** Beacon interval */
	unsigned short beacon_interval;
	/** is TWT capable? */
	bool twt_capable;
	/** The current 802.11 PHY TX data rate (in Mbps) */
	float current_phy_tx_rate;
#endif
int at_wifi_mgmt_iface_status(const struct device *dev, struct wifi_iface_status *status)
{
	int ret;
	struct at_wifi_data *data = dev->data;

	// TODO - can the following commands be put into an array?
	static const struct modem_cmd wfstat_cmd = MODEM_CMD_DIRECT("+WFSTAT:", on_cmd_wifi_state);

	static const struct modem_cmd wfrssi_cmd = MODEM_CMD("+RSSI:", on_cmd_wifi_rssi, 1U, "");

	memset(status, 0x0, sizeof(struct wifi_iface_status));

	if (!net_if_is_carrier_ok(data->net_iface)) {
		status->state = WIFI_STATE_INTERFACE_DISABLED;
		return 0;
	}

	data->wifi_status = status;

	ret = modem_cmd_send(&data->modem.ctx.iface, &data->modem.ctx.cmd_handler, &wfstat_cmd, 1,
			     "AT+WFSTAT", &data->sem_response, AT_WIFI_CMD_TIMEOUT);
	if (ret) {
		return ret;
	}

	/* We only read the RSSI if we are connected */
	if (data->wifi_status->state == WIFI_STATE_COMPLETED) {
		ret = modem_cmd_send(&data->modem.ctx.iface, &data->modem.ctx.cmd_handler,
				     &wfrssi_cmd, 1, "AT+WFRSSI", &data->sem_response,
				     AT_WIFI_CMD_TIMEOUT);

		if (ret == 0) {
			data->wifi_status->rssi = data->info.rssi;
		}
	}

	return ret;
}

int at_wifi_mgmt_get_version(const struct device *dev, struct wifi_version *params)
{
	struct at_wifi_data *data = dev->data;

	/* Version information should have been read during the initialization
	   process */
	if ((strlen(data->info.sdk_ver_str) == 0) && (strlen(data->info.ver_str) == 0)) {
		/* No version information available */
		return -ENOTSUP;
	} else {
		params->drv_version = data->info.ver_str;
		params->fw_version = data->info.sdk_ver_str;
	}

	return 0;
}

static void at_wifi_rx(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct at_wifi_data *data = p1;

	while (true) {
		at_wifi_transport_rx_wait(data, K_FOREVER);
		modem_cmd_handler_process(&data->modem.ctx.cmd_handler, &data->modem.ctx.iface);

		k_yield();
	}
}

static enum offloaded_net_if_types at_wifi_offload_get_type(void)
{
	return L2_OFFLOADED_NET_IF_TYPE_WIFI;
}

static void at_wifi_iface_init(struct net_if *iface)
{
	at_wifi_offload_init(iface);

	/* Not currently connected to a network */
	net_if_dormant_on(iface);
}

static const struct wifi_mgmt_ops at_wifi_mgmt_ops = {
	.scan = at_wifi_mgmt_scan,
	.connect = at_wifi_mgmt_connect,
	.disconnect = at_wifi_mgmt_disconnect,
	.iface_status = at_wifi_mgmt_iface_status,
	.get_version = at_wifi_mgmt_get_version,
#ifdef CONFIG_WIFI_AT_WIFI_SOFTAP_SUPPORT
	.ap_enable = NULL,
	.ap_disable = NULL,
	.ap_sta_disconnect = NULL,
	.ap_config_params = NULL,
#endif
};

static const struct net_wifi_mgmt_offload at_wifi_api = {
	.wifi_iface.iface_api.init = at_wifi_iface_init,
	.wifi_iface.get_type = at_wifi_offload_get_type,
	.wifi_mgmt_api = &at_wifi_mgmt_ops,
};

static int at_wifi_init(const struct device *dev);

NET_DEVICE_DT_INST_OFFLOAD_DEFINE(0, at_wifi_init, NULL, &at_wifi_driver_data, NULL,
				  CONFIG_WIFI_INIT_PRIORITY, &at_wifi_api, WIFI_AT_WIFI_MTU);

CONNECTIVITY_WIFI_MGMT_BIND(Z_DEVICE_DT_DEV_ID(DT_DRV_INST(0)));

static int at_wifi_init(const struct device *dev)
{
	int ret = 0;
	struct at_wifi_data *data = dev->data;

	LOG_DBG("initializing...");

	memset(data->info.sdk_ver_str, 0, WIFI_AT_WIFI_SDK_VER_LEN_MAX);
	memset(data->info.ver_str, 0, WIFI_AT_WIFI_VER_LEN_MAX);

	k_sem_init(&data->sem_response, 0, 1);
	k_sem_init(&data->sem_if_up, 0, 1);
	k_sem_init(&data->sem_connecting, 0, 1);

	k_work_init(&data->init_work, at_wifi_init_work);
	k_work_init(&data->scan_work, at_wifi_mgmt_scan_work);
	k_work_init(&data->connect_work, at_wifi_mgmt_connect_work);

	k_work_queue_start(&data->workq, at_wifi_workq_stack,
			   K_KERNEL_STACK_SIZEOF(at_wifi_workq_stack),
			   K_PRIO_COOP(CONFIG_WIFI_AT_WIFI_WORKQ_THREAD_PRIORITY), NULL);
	k_thread_name_set(&data->workq.thread, "at_wifi_workq");

	const struct modem_cmd_handler_config cmd_handler_config = {
		.match_buf = &data->modem.cmd_match_buf[0],
		.match_buf_len = sizeof(data->modem.cmd_match_buf),
		.buf_pool = &mdm_recv_pool,
		.alloc_timeout = K_NO_WAIT,
		.eol = "\r\n",
		.user_data = NULL,
		.response_cmds = response_cmds,
		.response_cmds_len = ARRAY_SIZE(response_cmds),
		.unsol_cmds = unsol_cmds,
		.unsol_cmds_len = ARRAY_SIZE(unsol_cmds),
	};

	ret = modem_cmd_handler_init(&data->modem.ctx.cmd_handler, &data->modem.cmd_handler_data,
				     &cmd_handler_config);
	if (ret < 0) {
		return ret;
	}

	data->modem.ctx.driver_data = data;
	ret = modem_context_register(&data->modem.ctx);
	if (ret < 0) {
		LOG_ERR("Error registering modem context: %d", ret);
		return ret;
	}

#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	struct gpio_dt_spec wifi_reset = GPIO_DT_SPEC_GET(DT_DRV_INST(0), reset_gpios);

	if (!gpio_is_ready_dt(&wifi_reset)) {
		LOG_ERR("Error: failed to configure wifi_reset %s pin %d", wifi_reset.port->name,
			wifi_reset.pin);
		return -EIO;
	}

	/* Set wifi_reset as output and activate reset */
	ret = gpio_pin_configure_dt(&wifi_reset, GPIO_OUTPUT_ACTIVE);
	if (ret) {
		LOG_ERR("Error %d: failed to configure wifi_reset %s pin %d", ret,
			wifi_reset.port->name, wifi_reset.pin);
		return ret;
	}

	k_sleep(K_MSEC(DT_INST_PROP_OR(0, reset_assert_duration_ms, 0)));

	/* Release the device from reset */
	ret = gpio_pin_configure_dt(&wifi_reset, GPIO_OUTPUT_INACTIVE);
	if (ret) {
		return ret;
	}

#if DT_INST_NODE_HAS_PROP(0, boot_duration_ms)
	k_sleep(K_MSEC(DT_INST_PROP_OR(0, boot_duration_ms, 0)));
#endif
#endif /* DT_INST_NODE_HAS_PROP(0, reset_gpios) */

	ret = at_wifi_transport_init(data);
	if (ret < 0) {
		LOG_ERR("Error initializing transport: %d", ret);
		return ret;
	}

	k_thread_create(&at_wifi_rx_thread, at_wifi_rx_stack,
			K_KERNEL_STACK_SIZEOF(at_wifi_rx_stack), at_wifi_rx, data, NULL, NULL,
			K_PRIO_COOP(CONFIG_WIFI_AT_WIFI_RX_THREAD_PRIORITY), 0, K_NO_WAIT);
	k_thread_name_set(&at_wifi_rx_thread, "at_wifi_rx");

	data->net_iface = NET_IF_GET(Z_DEVICE_DT_DEV_ID(DT_DRV_INST(0)), 0);

	if (net_if_is_carrier_ok(data->net_iface)) {
		net_if_carrier_off(data->net_iface);
	}

	LOG_INF("Waiting for interface to come up");

	ret = k_sem_take(&data->sem_if_up, AT_WIFI_INIT_TIMEOUT);
	if (ret == -EAGAIN) {
		LOG_ERR("Timeout waiting for interface");
	}

	return ret;
}
