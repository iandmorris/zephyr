/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_ra_rpc

#include <stdlib.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wifi_ra_rpc, CONFIG_WIFI_LOG_LEVEL);

#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/conn_mgr/connectivity_wifi_mgmt.h>
#include <erpc_client_setup.h>
#include <erpc_transport_setup.h>
#include <erpc_mbf_setup.h>

#include "ra_rpc.h"
#include "c_wifi_client.h"

K_KERNEL_STACK_DEFINE(ra_rpc_workq_stack,
		      CONFIG_WIFI_RA_RPC_WORKQ_STACK_SIZE);

struct ra_rpc_data ra_rpc_driver_data;

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

static int ra_rpc_mgmt_scan(const struct device *dev,
	struct wifi_scan_params *params,
	scan_result_cb_t cb)
{
	int err;
	struct ra_rpc_data *data = dev->data;

	LOG_DBG("ra_rpc_mgmt_scan");
	LOG_DBG("type: %d cb: 0x%x", params->scan_type, (int)data->scan_cb);

	if (data->scan_cb != NULL) {
		return -EINPROGRESS;
	}

	if (!net_if_is_carrier_ok(data->net_iface)) {
		return -EIO;
	}

	data->scan_cb = cb;
	data->scan_max_bss_cnt = params->max_bss_cnt;

	err = k_work_submit_to_queue(&data->workq, &data->scan_work);

	LOG_DBG("k_work_submit_to_queue - err: %d", err);

	return 0;
}

static void ra_rpc_mgmt_scan_work(struct k_work *work)
{
	struct ra_rpc_data *dev;
	struct wifi_scan_result entry;
	WIFIScanResult_t *results;
	WIFIReturnCode_t ret;
	int i;

	LOG_DBG("ra_rpc_mgmt_scan_work");

	dev = CONTAINER_OF(work, struct ra_rpc_data, scan_work);

	if (dev->scan_max_bss_cnt == 0) {
		dev->scan_max_bss_cnt = CONFIG_WIFI_RA_RPC_MAX_BBS_COUNT;
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

				// TODO - replace magic value...
				if (entry.channel > 14) {
					entry.band = WIFI_FREQ_BAND_5_GHZ;
				}

				dev->scan_cb(dev->net_iface, 0, &entry);
				k_yield();
			}
		} else {
			// TODO - pass back an error code?
			dev->scan_cb(dev->net_iface, -1, NULL);
		}
		free(results);
	}

	dev->scan_cb(dev->net_iface, 0, NULL);
	dev->scan_cb = NULL;
}

static int ra_rpc_mgmt_connect(const struct device *dev,
	struct wifi_connect_req_params *params)
{
	int err;
	struct ra_rpc_data *data = dev->data;

	LOG_DBG("ra_rpc_mgmt_connect");

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

	err = k_work_submit_to_queue(&data->workq, &data->connect_work);

	LOG_DBG("k_work_submit_to_queue - err: %d", err);

	return 0;
}

static void ra_rpc_mgmt_connect_work(struct k_work *work)
{
	struct ra_rpc_data *dev;
	WIFIReturnCode_t ret;
	int status = 0;

	dev = CONTAINER_OF(work, struct ra_rpc_data, connect_work);

	LOG_DBG("ra_rpc_mgmt_connect_work");
	LOG_DBG("ucSSID: %s", dev->drv_nwk_params.ucSSID);
	LOG_DBG("ucSSIDLength: %d", dev->drv_nwk_params.ucSSIDLength);
	LOG_DBG("xSecurity: %d", dev->drv_nwk_params.xSecurity);
	LOG_DBG("xWPA.ucLength: %d", dev->drv_nwk_params.xPassword.xWPA.ucLength);
	LOG_DBG("ucChannel: %d", dev->drv_nwk_params.ucChannel);

	ret = WIFI_ConnectAP(&dev->drv_nwk_params);

	LOG_DBG("WIFI_ConnectAP: %d", ret);

	if (ret) {
		status = -1;
	}
	wifi_mgmt_raise_connect_result_event(dev->net_iface, status);
}

static int ra_rpc_mgmt_disconnect(const struct device *dev)
{
	int err;
	struct ra_rpc_data *data = dev->data;

	LOG_DBG("ra_rpc_mgmt_disconnect");

	err = k_work_submit_to_queue(&data->workq, &data->disconnect_work);

	LOG_DBG("k_work_submit_to_queue - err: %d", err);

	return 0;
}

static void ra_rpc_mgmt_disconnect_work(struct k_work *work)
{
	int status = 0;
	struct ra_rpc_data *dev;
	WIFIReturnCode_t ret;

	LOG_DBG("ra_rpc_mgmt_disconnect_work");

	dev = CONTAINER_OF(work, struct ra_rpc_data, disconnect_work);

	ret = WIFI_Disconnect();

	LOG_DBG("WIFI_Disconnect: %d", ret);

	if (ret != eWiFiSuccess) {
		status = -1;
	}

	wifi_mgmt_raise_disconnect_result_event(dev->net_iface, status);
}

static int ra_rpc_mgmt_iface_status(const struct device *dev,
	struct wifi_iface_status *status)
{
	WIFIReturnCode_t ret;
	WIFIConnectionInfo_t conn_info;
	struct ra_rpc_data *data = dev->data;

	LOG_DBG("ra_rpc_mgmt_iface_status");
	LOG_DBG("net_if_is_carrier_ok: %d", net_if_is_carrier_ok(data->net_iface));

	memset(status, 0, sizeof(struct wifi_iface_status));

	if (!net_if_is_carrier_ok(data->net_iface)) {
		status->state = WIFI_STATE_INTERFACE_DISABLED;
		return 0;
	}

	//ret = WIFI_IsConnected(NULL);
	LOG_DBG("WIFI_IsConnected: %d", ret);

	if (ret != eWiFiSuccess) {
		status->state = WIFI_STATE_DISCONNECTED;
		return 0;
	}

	ret = WIFI_GetConnectionInfo(&conn_info);
	LOG_DBG("WIFI_IsConnected: %d", ret);

	if (ret == eWiFiSuccess) {
		status->state = WIFI_STATE_ASSOCIATED;
		status->ssid_len = MIN(conn_info.ucSSIDLength, sizeof(status->ssid));
		memcpy(status->ssid, conn_info.ucSSID, status->ssid_len);
		status->channel = conn_info.ucChannel;
		memcpy(status->bssid, conn_info.ucBSSID, sizeof(status->bssid));
		// TODO status structure contains more elements
	}

	return 0;
}

#if 0
static void ra_rpc_mgmt_iface_status_work(struct k_work *work)
{
	WIFIReturnCode_t ret;
	WIFIConnectionInfo_t conn_info;
	struct ra_rpc_data *dev;

	LOG_DBG("ra_rpc_mgmt_iface_status_work");

	dev = CONTAINER_OF(work, struct ra_rpc_data, iface_status_work);

	ret = WIFI_GetConnectionInfo(&conn_info);

	LOG_DBG("WIFI_GetConnectionInfo: %d", ret);

	if (ret == eWiFiSuccess) {
		dev->wifi_status->channel = conn_info.ucChannel;
		memcpy(dev->wifi_status->bssid, conn_info.ucBSSID, WIFI_MAC_ADDR_LEN);
		if (conn_info.ucSSIDLength < WIFI_SSID_MAX_LEN) {
			dev->wifi_status->ssid_len = conn_info.ucSSIDLength;
			memcpy(dev->wifi_status->ssid, conn_info.ucSSID, dev->wifi_status->ssid_len);
		}
	}
}
#endif

static int ra_rpc_mgmt_reg_domain(const struct device *dev,
	struct wifi_reg_domain *reg_domain)
{
	return 0;
}

static enum offloaded_net_if_types ra_rpc_offload_get_type(void)
{
	return L2_OFFLOADED_NET_IF_TYPE_WIFI;
}

static void ra_rpc_iface_init(struct net_if *iface)
{
	ra_rpc_socket_offload_init(iface);

	/* Not currently connected to a network */
	net_if_dormant_on(iface);
}

static const struct wifi_mgmt_ops ra_rpc_mgmt_ops = {
	.scan		   		= ra_rpc_mgmt_scan,
	.connect	   		= ra_rpc_mgmt_connect,
	.disconnect	   		= ra_rpc_mgmt_disconnect,
	.iface_status  		= ra_rpc_mgmt_iface_status,
	.reg_domain         = ra_rpc_mgmt_reg_domain,
#ifdef CONFIG_WIFI_RA_RPC_SOFTAP_SUPPORT
	.ap_enable     		= NULL,
	.ap_disable    		= NULL,
	.ap_sta_disconnect 	= NULL,
	.ap_config_params   = NULL,
#endif
};

static const struct net_wifi_mgmt_offload ra_rpc_api = {
	.wifi_iface.iface_api.init = ra_rpc_iface_init,
	.wifi_iface.get_type = ra_rpc_offload_get_type,
	.wifi_mgmt_api = &ra_rpc_mgmt_ops,
};

static int ra_rpc_init(const struct device *dev);

NET_DEVICE_DT_INST_OFFLOAD_DEFINE(0, ra_rpc_init, NULL,
	&ra_rpc_driver_data, NULL,
	CONFIG_WIFI_INIT_PRIORITY, &ra_rpc_api,
	1500); // TODO - create a macro or config option for this?

CONNECTIVITY_WIFI_MGMT_BIND(Z_DEVICE_DT_DEV_ID(DT_DRV_INST(0)));

static int ra_rpc_init(const struct device *dev)
{
    erpc_transport_t transport;
    erpc_mbf_t message_buffer_factory;
    erpc_client_t client_manager;
	struct ra_rpc_data *data = dev->data;

	data->bus = DEVICE_DT_GET(DT_INST_BUS(0));

	if (!device_is_ready(data->bus)) {
		LOG_ERR("Bus device is not ready");
		return -ENODEV;
	}

	k_work_init(&data->scan_work, ra_rpc_mgmt_scan_work);
	k_work_init(&data->connect_work, ra_rpc_mgmt_connect_work);
	k_work_init(&data->disconnect_work, ra_rpc_mgmt_disconnect_work);
	//k_work_init(&data->iface_status_work, ra_rpc_mgmt_iface_status_work);

	/* Initialize the work queue */
	k_work_queue_start(&data->workq, ra_rpc_workq_stack,
			   K_KERNEL_STACK_SIZEOF(ra_rpc_workq_stack),
			   K_PRIO_COOP(CONFIG_WIFI_RA_RPC_WORKQ_THREAD_PRIORITY),
			   NULL);
	k_thread_name_set(&data->workq.thread, "ra_rpc_workq");

    /* Initialize the eRPC client infrastructure */
    transport = erpc_transport_zephyr_uart_init(data->bus);
	if (transport == NULL) {
		LOG_ERR("Failed to initialize eRPC transport");
		return -ENODEV;
	}

    message_buffer_factory = erpc_mbf_dynamic_init();
	if (message_buffer_factory == NULL) {
		LOG_ERR("Failed to initialize eRPC message buffer factory");
		return -ENODEV;
	}

	client_manager = erpc_client_init(transport, message_buffer_factory);
	if (client_manager == NULL) {
		LOG_ERR("Failed to initialize eRPC client");
		return -ENODEV;
	}

	initwifi_client(client_manager);

	data->net_iface = NET_IF_GET(Z_DEVICE_DT_DEV_ID(DT_DRV_INST(0)), 0);

	return 0;
}
