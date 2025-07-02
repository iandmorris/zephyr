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

#include "ra_erpc.h"
#include "ra_erpc_socket_offload.h"
#include "c_wifi_host_to_ra_client.h"
#include "c_wifi_ra_to_host_server.h"

#define SERVER_THREAD_STACK_SIZE 1024
#define SERVER_THREAD_PRIORITY 5

erpc_server_t server;
void erpc_server_thread(void *arg1, void *arg2, void *arg3);
void erpc_client_error_handler(erpc_status_t err, uint32_t functionID);

K_THREAD_STACK_DEFINE(erpc_server_stack_area, SERVER_THREAD_STACK_SIZE);
static struct k_thread erpc_server_thread_data;

K_KERNEL_STACK_DEFINE(ra_erpc_workq_stack,
		      CONFIG_WIFI_RA_ERPC_WORKQ_STACK_SIZE);

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
	int err;
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

	err = k_work_submit_to_queue(&data->workq, &data->scan_work);

	LOG_DBG("k_work_submit_to_queue - err: %d", err);

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
			// TODO - pass back an error code?
			dev->scan_cb(dev->net_iface, -1, NULL);
		}
		free(results);
	}

	dev->scan_cb(dev->net_iface, 0, NULL);
	dev->scan_cb = NULL;
}

static int ra_erpc_mgmt_connect(const struct device *dev,
	struct wifi_connect_req_params *params)
{
	int err;
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

	err = k_work_submit_to_queue(&data->workq, &data->connect_work);

	LOG_DBG("k_work_submit_to_queue - err: %d", err);

	return 0;
}

static void ra_erpc_mgmt_connect_work(struct k_work *work)
{
	struct ra_erpc_data *dev;
	WIFIReturnCode_t ret;
	//WIFIIPConfiguration_t ip_config;
	//struct in_addr ip_addr;
	//struct in_addr gw_addr;
	//struct in_addr netmask;
	int status = 0;

	dev = CONTAINER_OF(work, struct ra_erpc_data, connect_work);

	LOG_DBG("ra_erpc_mgmt_connect_work");
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

	// TODO - this is not working at present, just returns 0.0.0.0 for all IP addresses...
	//ret = WIFI_GetIPInfo(&ip_config);

	//LOG_DBG("WIFI_GetIPInfo: %d", ret);

	//if (ret == eWiFiSuccess) {
	//	memcpy(ip_addr.s_addr, &ip_config.xIPAddress.ulAddress, sizeof(ip_addr.s_addr));
	//	memcpy(gw_addr.s_addr, &ip_config.xGateway.ulAddress, sizeof(gw_addr.s_addr));
	//	memcpy(netmask.s_addr, &ip_config.xNetMask.ulAddress, sizeof(netmask.s_addr));

	//	net_if_ipv4_addr_add(dev->net_iface, &ip_addr, NET_ADDR_DHCP, 0);
	//	net_if_ipv4_set_gw(dev->net_iface, &gw_addr);
	//	net_if_ipv4_set_netmask_by_addr(dev->net_iface, &ip_addr, &netmask);
	//}

	wifi_mgmt_raise_connect_result_event(dev->net_iface, status);
	// TODO - don't do this if not connected
	net_if_dormant_off(dev->net_iface);
}

static int ra_erpc_mgmt_disconnect(const struct device *dev)
{
	int err;
	struct ra_erpc_data *data = dev->data;

	LOG_DBG("ra_erpc_mgmt_disconnect");

	err = k_work_submit_to_queue(&data->workq, &data->disconnect_work);

	LOG_DBG("k_work_submit_to_queue - err: %d", err);

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
		status = -1;
	}

	wifi_mgmt_raise_disconnect_result_event(dev->net_iface, status);
	net_if_dormant_on(dev->net_iface);
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

static const struct wifi_mgmt_ops ra_erpc_mgmt_ops = {
	.scan		   		= ra_erpc_mgmt_scan,
	.connect	   		= ra_erpc_mgmt_connect,
	.disconnect	   		= ra_erpc_mgmt_disconnect,
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
    erpc_mbf_t message_buffer_factory;
    erpc_transport_t arbitrator;
    erpc_client_t client_manager;
    erpc_service_t service;

	struct ra_erpc_data *data = dev->data;

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

    client_manager = erpc_arbitrated_client_init(transport, message_buffer_factory, &arbitrator);
	if (client_manager == NULL) {
		LOG_ERR("Failed to initialize eRPC client");
		return -ENODEV;
	}

    // Init eRPC client interface
    erpc_client_set_error_handler(client_manager, erpc_client_error_handler);
	initwifi_client(client_manager);

    // Init eRPC server interface
    server = erpc_server_init(arbitrator, message_buffer_factory);    
    service = create_wifi_async_service();
    /* Add custom service implementation to the server */
    erpc_add_service_to_server(server, service);

    k_thread_create(&erpc_server_thread_data, 
                    erpc_server_stack_area, 
                    SERVER_THREAD_STACK_SIZE,
                    erpc_server_thread,
                    NULL, NULL, NULL,  // arguments
                    SERVER_THREAD_PRIORITY, 0,       // priority and options
                    K_NO_WAIT);        // start immediately

	data->net_iface = NET_IF_GET(Z_DEVICE_DT_DEV_ID(DT_DRV_INST(0)), 0);

	return 0;
}

void erpc_client_error_handler(erpc_status_t err, uint32_t functionID)
{
    if(err > 0) {
        // see wifi_interface.hpp for list of function IDs
        LOG_ERR("eRPC client error. err=%d, functionID=%d\n", err, functionID);
    }
}     

// This thread polls the server interface (e.g. has RA6W1 called `async` function)
void erpc_server_thread(void *arg1, void *arg2, void *arg3) {
    while (1) {
        // TODO should we use erpc_server_run() instead? 
        erpc_status_t err = erpc_server_poll(server);
        if (err != kErpcStatus_Success)
        {
            LOG_ERR("erpc_server_poll() error=%d\n", err);
        }
    }
}

void ra_erpc_server_event_handler(const ra_erp_server_event_t * event)
{
    LOG_DBG("ra_erpc_server_event_handler. event_id=%d\n", event->event_id);
    switch(event->event_id) {
        case eNetworkInterfaceIPAssigned:
        {
            struct in_addr addr;
            memcpy(&addr, &event->event_data.data, sizeof(event->event_data.data));
            char buf[INET_ADDRSTRLEN ];
            if (net_addr_ntop(AF_INET, &addr, buf, sizeof(buf))) {
                LOG_DBG("IPv4 addr = %s\n", buf);
            }
            break;
        }
        default:
            break;
    }
}
