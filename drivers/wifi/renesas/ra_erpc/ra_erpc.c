/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "zephyr/kernel.h"
#define DT_DRV_COMPAT renesas_ra_erpc

#include <stdlib.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wifi_ra_erpc, CONFIG_WIFI_LOG_LEVEL);

#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/wifi_utils.h>
#include <zephyr/net/conn_mgr/connectivity_wifi_mgmt.h>
#include <zephyr/net/net_ip.h>
#include <erpc_client_setup.h>
#include <erpc_server_setup.h>
#include <erpc_transport_setup.h>
#include <erpc_mbf_setup.h>
#include <erpc_arbitrated_client_setup.h>
#include <c_wifi_host_to_ra_client.h>
#include <c_wifi_ra_to_host_server.h>

#include "ra_erpc.h"
#include "ra_erpc_socket_offload.h"

K_THREAD_STACK_DEFINE(ra_erpc_server_thread_stack, 
			  CONFIG_WIFI_RA_ERPC_SERVER_THREAD_STACK_SIZE);
static struct k_thread ra_erpc_server_thread_data;

K_KERNEL_STACK_DEFINE(ra_erpc_workq_stack,
		      CONFIG_WIFI_RA_ERPC_WORKQ_STACK_SIZE);

// TODO can this be static?
struct ra_erpc_data ra_erpc_driver_data;

static inline enum wifi_security_type drv_to_wifi_mgmt_sec(int drv_security_type)
{
	switch (drv_security_type) {
	case eWiFiSecurityOpen:
		return WIFI_SECURITY_TYPE_NONE;
	case eWiFiSecurityWEP:
		return WIFI_SECURITY_TYPE_WEP;
	case eWiFiSecurityWPA:
		return WIFI_SECURITY_TYPE_WPA_PSK;
	case eWiFiSecurityWPA2:
		return WIFI_SECURITY_TYPE_PSK;
	case eWiFiSecurityWPA2_ent:
		return WIFI_SECURITY_TYPE_PSK_SHA256;
	case eWiFiSecurityWPA3:
		return WIFI_SECURITY_TYPE_SAE;
	default:
		return WIFI_SECURITY_TYPE_UNKNOWN;
	}
}

static inline enum WIFISecurity_t wifi_mgmt_to_drv_sec(int wifi_mgmt_security_type)
{
	switch (wifi_mgmt_security_type) {
	case WIFI_SECURITY_TYPE_NONE:
		return eWiFiSecurityOpen;
	case WIFI_SECURITY_TYPE_WEP:
		return eWiFiSecurityWEP;
	case WIFI_SECURITY_TYPE_WPA_PSK:
		return eWiFiSecurityWPA;
	case WIFI_SECURITY_TYPE_PSK:
		return eWiFiSecurityWPA2;
	case WIFI_SECURITY_TYPE_PSK_SHA256:
		return eWiFiSecurityWPA2_ent;
	case  WIFI_SECURITY_TYPE_SAE:
		return eWiFiSecurityWPA3;
	default:
		return eWiFiSecurityNotSupported;
	}
}

static int ra_erpc_mgmt_scan(const struct device *dev,
	struct wifi_scan_params *params,
	scan_result_cb_t cb)
{
	struct ra_erpc_data *data = dev->data;

	LOG_DBG("ra_erpc_mgmt_scan");
	LOG_DBG("type: %d cb: 0x%x", params->scan_type, (int)data->scan_cb);

	if (data->scan_cb != NULL) {
		return -EINPROGRESS;
	}

	if (!net_if_is_carrier_ok(data->net_iface)) {
		return -EIO;
	}

	data->scan_cb = cb;
	data->scan_max_bss_cnt = params->max_bss_cnt;
	data->state = WIFI_STATE_SCANNING;

	k_work_submit_to_queue(&data->workq, &data->scan_work);

	return 0;
}

static void ra_erpc_mgmt_scan_work(struct k_work *work)
{
	struct ra_erpc_data *dev;
	struct wifi_scan_result entry;
	WIFIScanResult_t *results;
	WIFIReturnCode_t ret;
	int i;

	LOG_DBG("ra_erpc_mgmt_scan_work");

	dev = CONTAINER_OF(work, struct ra_erpc_data, scan_work);

	if (dev->scan_max_bss_cnt == 0) {
		dev->scan_max_bss_cnt = CONFIG_WIFI_RA_ERPC_MAX_BBS_COUNT;
	}

	results = malloc(dev->scan_max_bss_cnt * sizeof(WIFIScanResult_t));
	if (results != NULL) {
		memset(results, 0, dev->scan_max_bss_cnt * sizeof(WIFIScanResult_t));

		ret = WIFI_Scan(results, dev->scan_max_bss_cnt);

		LOG_DBG("WIFI_Scan: %d", ret);

		if ((ret == eWiFiSuccess) || (ret == eWiFiTimeout)) {
			for (i = 0; (results[i].ucSSIDLength != 0) && (i < dev->scan_max_bss_cnt); i++) {
				memset(&entry, 0, sizeof(struct wifi_scan_result));

				if (results[i].ucSSIDLength <= WIFI_SSID_MAX_LEN) {
					/* Tab character signifies hidden SSID */
					if (results[i].ucSSID[0] != '\t') {
						entry.ssid_length = results[i].ucSSIDLength;
						memcpy(entry.ssid, results[i].ucSSID, entry.ssid_length);
					}
				}

				entry.security = drv_to_wifi_mgmt_sec(results[i].xSecurity);
				entry.channel = results[i].ucChannel;
				entry.rssi = results[i].cRSSI;
				entry.mac_length = WIFI_MAC_ADDR_LEN;
				memcpy(entry.mac, results[i].ucBSSID, entry.mac_length);

				if (wifi_utils_validate_chan_2g(entry.channel)) {
					entry.band = WIFI_FREQ_BAND_2_4_GHZ;
				} else if (wifi_utils_validate_chan_5g(entry.channel)) {
					entry.band = WIFI_FREQ_BAND_5_GHZ;
				} else {
					entry.band = WIFI_FREQ_BAND_UNKNOWN;
				}

				dev->scan_cb(dev->net_iface, 0, &entry);
				k_yield();
			}
		} else {
			// TODO - pass back an enumerated error code?
			dev->scan_cb(dev->net_iface, -1, NULL);
		}
		free(results);
	}

	dev->scan_cb(dev->net_iface, 0, NULL);
	dev->scan_cb = NULL;
	dev->state = WIFI_STATE_DISCONNECTED;
}

static int ra_erpc_mgmt_connect(const struct device *dev,
	struct wifi_connect_req_params *params)
{
	struct ra_erpc_data *data = dev->data;

	LOG_DBG("ra_erpc_mgmt_connect");

	memset(&data->drv_nwk_params, 0, sizeof(sizeof(WIFINetworkParams_t)));

	data->drv_nwk_params.ucSSIDLength = MIN(params->ssid_length,
		sizeof(data->drv_nwk_params.ucSSID));
	memcpy(data->drv_nwk_params.ucSSID, params->ssid,
		data->drv_nwk_params.ucSSIDLength);
	data->drv_nwk_params.xSecurity = wifi_mgmt_to_drv_sec(params->security);
	// TODO - copy parameters based on security type
	data->drv_nwk_params.xPassword.xWPA.ucLength = MIN(params->psk_length,
		sizeof(data->drv_nwk_params.xPassword.xWPA.cPassphrase));
	memcpy(data->drv_nwk_params.xPassword.xWPA.cPassphrase, params->psk,
		data->drv_nwk_params.xPassword.xWPA.ucLength);
	data->drv_nwk_params.ucChannel = params->channel;

	k_work_submit_to_queue(&data->workq, &data->connect_work);

	return 0;
}

static void ra_erpc_mgmt_connect_work(struct k_work *work)
{
	struct ra_erpc_data *dev;
	WIFIReturnCode_t ret;
	int status;

	dev = CONTAINER_OF(work, struct ra_erpc_data, connect_work);

	LOG_DBG("ra_erpc_mgmt_connect_work");
	LOG_DBG("ucSSID: %s", dev->drv_nwk_params.ucSSID);
	LOG_DBG("ucSSIDLength: %d", dev->drv_nwk_params.ucSSIDLength);
	LOG_DBG("xSecurity: %d", dev->drv_nwk_params.xSecurity);
	LOG_DBG("xWPA.ucLength: %d", dev->drv_nwk_params.xPassword.xWPA.ucLength);
	LOG_DBG("ucChannel: %d", dev->drv_nwk_params.ucChannel);

	ret = WIFI_ConnectAP(&dev->drv_nwk_params);

	LOG_DBG("WIFI_ConnectAP: %d", ret);

	if (ret == eWiFiSuccess) {
		dev->state = WIFI_STATE_COMPLETED;
		status = WIFI_STATUS_CONN_SUCCESS;
		net_if_dormant_off(dev->net_iface);
	} else {
		status = WIFI_STATUS_CONN_FAIL;
	}

	wifi_mgmt_raise_connect_result_event(dev->net_iface, status);
}

static int ra_erpc_mgmt_disconnect(const struct device *dev)
{
	struct ra_erpc_data *data = dev->data;

	LOG_DBG("ra_erpc_mgmt_disconnect");

	k_work_submit_to_queue(&data->workq, &data->disconnect_work);

	return 0;
}

static void ra_erpc_mgmt_disconnect_work(struct k_work *work)
{
	int status = 0;
	struct ra_erpc_data *dev;
	WIFIReturnCode_t ret;

	LOG_DBG("ra_erpc_mgmt_disconnect_work");

	dev = CONTAINER_OF(work, struct ra_erpc_data, disconnect_work);

	ret = WIFI_Disconnect();

	LOG_DBG("WIFI_Disconnect: %d", ret);

	if (ret != eWiFiSuccess) {
		// TODO - there an enumerated error code that can be used here?
		status = -1;
	}

	dev->state = WIFI_STATE_DISCONNECTED;

	wifi_mgmt_raise_disconnect_result_event(dev->net_iface, status);
	net_if_dormant_on(dev->net_iface);
}

int ra_erpc_mgmt_iface_status(const struct device *dev,
			     struct wifi_iface_status *status)
{
	struct ra_erpc_data *data = dev->data;

	LOG_DBG("ra_erpc_mgmt_iface_status");
	
	status->state = data->state;
	// TODO use MIN macro to ensure not too long?
	status->ssid_len = data->drv_nwk_params.ucSSIDLength;
	memcpy(status->ssid, data->drv_nwk_params.ucSSID, status->ssid_len);

	// TODO - populate the rest of the status structure
#if 0
struct wifi_iface_status {
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
	int current_phy_tx_rate;
};

#endif

	return 0;
}

static enum offloaded_net_if_types ra_erpc_offload_get_type(void)
{
	return L2_OFFLOADED_NET_IF_TYPE_WIFI;
}

static void ra_erpc_iface_init(struct net_if *iface)
{
	ra_erpc_socket_offload_init(iface);

	/* Not currently connected to a network */
	net_if_dormant_on(iface);
}

static void ra_erpc_client_error_handler(erpc_status_t err, uint32_t func_id)
{
	if (err > 0) {
		/* See wifi_interface.hpp for list of function id's */
		LOG_ERR("eRPC client error - err: %d func_id: %d\n", err, func_id);
	}
}

static void ra_erpc_server_thread(void *arg1, void *unused1, void *unused2) 
{
	struct ra_erpc_data *data = (struct ra_erpc_data *)arg1;

	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);

	while (1) {
		// TODO should we use erpc_server_run() instead? 
		erpc_status_t err = erpc_server_poll(data->erpc_server);
		if (err != kErpcStatus_Success) {
			LOG_ERR("erpc_server_poll - err: %d\n", err);
		}
	}
}

void ra_erpc_server_event_handler(const ra_erp_server_event_t * event)
{
    LOG_DBG("ra_erpc_server_event_handler - event_id: %d", event->event_id);

    switch (event->event_id) {
        case eNetworkInterfaceIPAssigned:
        {
			// TODO - handle address type, get rid of warnings, basically clean this up!
			net_if_ipv4_addr_add(ra_erpc_driver_data.net_iface,
				 &event->event_data.xConfig.xIPAddress.ulAddress[0], NET_ADDR_DHCP, 0);
			net_if_ipv4_set_gw(ra_erpc_driver_data.net_iface, 
				 &event->event_data.xConfig.xGateway.ulAddress[0]);
			net_if_ipv4_set_netmask_by_addr(ra_erpc_driver_data.net_iface, 
				 &event->event_data.xConfig.xIPAddress.ulAddress[0], 
				 &event->event_data.xConfig.xNetMask.ulAddress[0]);
            break;
        }
        default:
            break;
    }
}

static const struct wifi_mgmt_ops ra_erpc_mgmt_ops = {
	.scan		   		= ra_erpc_mgmt_scan,
	.connect	   		= ra_erpc_mgmt_connect,
	.disconnect	   		= ra_erpc_mgmt_disconnect,
	.iface_status		= ra_erpc_mgmt_iface_status,
#ifdef CONFIG_WIFI_RA_ERPC_SOFTAP_SUPPORT
	.ap_enable     		= NULL,
	.ap_disable    		= NULL,
	.ap_sta_disconnect 	= NULL,
	.ap_config_params   = NULL,
#endif
};

static const struct net_wifi_mgmt_offload ra_erpc_api = {
	.wifi_iface.iface_api.init = ra_erpc_iface_init,
	.wifi_iface.get_type = ra_erpc_offload_get_type,
	.wifi_mgmt_api = &ra_erpc_mgmt_ops,
};

static int ra_erpc_init(const struct device *dev);

NET_DEVICE_DT_INST_OFFLOAD_DEFINE(0, ra_erpc_init, NULL,
	&ra_erpc_driver_data, NULL,
	CONFIG_WIFI_INIT_PRIORITY, &ra_erpc_api,
	1500); // TODO - create a macro or config option for this?

CONNECTIVITY_WIFI_MGMT_BIND(Z_DEVICE_DT_DEV_ID(DT_DRV_INST(0)));

static int ra_erpc_init(const struct device *dev)
{
	erpc_transport_t transport;
	erpc_mbf_t mbf;
	erpc_transport_t arbitrator;
	erpc_client_t client_manager;
	erpc_service_t service;

	struct ra_erpc_data *data = dev->data;

	// TODO - Fix this!!!!
	/* Temporary fix to allow RA6W1 to empty UART buffer of incorrect
	   byte(s) it receives when host MCU is reset. */
	k_msleep(1000);

	data->bus = DEVICE_DT_GET(DT_INST_BUS(0));

	if (!device_is_ready(data->bus)) {
		LOG_ERR("Bus device is not ready");
		return -ENODEV;
	}

	k_work_init(&data->scan_work, ra_erpc_mgmt_scan_work);
	k_work_init(&data->connect_work, ra_erpc_mgmt_connect_work);
	k_work_init(&data->disconnect_work, ra_erpc_mgmt_disconnect_work);

	/* Initialize the work queue */
	k_work_queue_start(&data->workq, ra_erpc_workq_stack,
			   K_KERNEL_STACK_SIZEOF(ra_erpc_workq_stack),
			   K_PRIO_COOP(CONFIG_WIFI_RA_ERPC_WORKQ_THREAD_PRIORITY),
			   NULL);
	k_thread_name_set(&data->workq.thread, "ra_erpc_workq");

	/* Initialize the eRPC client infrastructure */
	transport = erpc_transport_zephyr_uart_init((void *)data->bus);
	if (transport == NULL) {
		LOG_ERR("Failed to initialize eRPC transport");
		return -ENODEV;
	}

	mbf = erpc_mbf_dynamic_init();
	if (mbf == NULL) {
		LOG_ERR("Failed to initialize eRPC message buffer factory");
		return -ENODEV;
	}

	client_manager = erpc_arbitrated_client_init(transport, mbf, &arbitrator);
	if (client_manager == NULL) {
		LOG_ERR("Failed to initialize eRPC client");
		return -ENODEV;
	}

	/* Initialize eRPC client interface */
	erpc_client_set_error_handler(client_manager, ra_erpc_client_error_handler);
	initwifi_client(client_manager);

	/* Initialize eRPC server interface */
	data->erpc_server = erpc_server_init(arbitrator, mbf);
	service = create_wifi_async_service();

	/* Add custom service implementation to the server */
	erpc_add_service_to_server(data->erpc_server, service);

	k_thread_create(&ra_erpc_server_thread_data,
					ra_erpc_server_thread_stack,
					CONFIG_WIFI_RA_ERPC_SERVER_THREAD_STACK_SIZE,
					ra_erpc_server_thread,
					data, NULL, NULL,
					CONFIG_WIFI_RA_ERPC_SERVER_THREAD_PRIORITY, 0,
					K_NO_WAIT);

	data->net_iface = NET_IF_GET(Z_DEVICE_DT_DEV_ID(DT_DRV_INST(0)), 0);

	return 0;
}
