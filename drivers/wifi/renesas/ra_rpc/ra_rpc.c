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

static int ra_rpc_mgmt_scan(const struct device *dev,
	struct wifi_scan_params *params,
	scan_result_cb_t cb)
{
	struct ra_rpc_data *data = dev->data;

	//enum wifi_scan_type scan_type;
	/** Bitmap of bands to be scanned.
	 *  Refer to ::wifi_frequency_bands for bit position of each band.
	 */
	//uint8_t bands;
	/** Active scan dwell time (in ms) on a channel.
	 */
	//uint16_t dwell_time_active;
	/** Passive scan dwell time (in ms) on a channel.
	 */
	//uint16_t dwell_time_passive;
	/** Array of SSID strings to scan.
	 */
	//const char *ssids[WIFI_MGMT_SCAN_SSID_FILT_MAX];
	/** Specifies the maximum number of scan results to return. These results would be the
	 * BSSIDS with the best RSSI values, in all the scanned channels. This should only be
	 * used to limit the number of returned scan results, and cannot be counted upon to limit
	 * the scan time, since the underlying Wi-Fi chip might have to scan all the channels to
	 * find the max_bss_cnt number of APs with the best signal strengths. A value of 0
	 * signifies that there is no restriction on the number of scan results to be returned.
	 */
	//uint16_t max_bss_cnt;
	/** Channel information array indexed on Wi-Fi frequency bands and channels within that
	 * band.
	 * E.g. to scan channel 6 and 11 on the 2.4 GHz band, channel 36 on the 5 GHz band:
	 * @code{.c}
	 *     chan[0] = {WIFI_FREQ_BAND_2_4_GHZ, 6};
	 *     chan[1] = {WIFI_FREQ_BAND_2_4_GHZ, 11};
	 *     chan[2] = {WIFI_FREQ_BAND_5_GHZ, 36};
	 * @endcode
	 *
	 *  This list specifies the channels to be __considered for scan__. The underlying
	 *  Wi-Fi chip can silently omit some channels due to various reasons such as channels
	 *  not conforming to regulatory restrictions etc. The invoker of the API should
	 *  ensure that the channels specified follow regulatory rules.
	 */
	//struct wifi_band_channel band_chan[WIFI_MGMT_SCAN_CHAN_MAX_MANUAL];

	if (data->scan_cb != NULL) {
		return -EINPROGRESS;
	}

	if (!net_if_is_carrier_ok(data->net_iface)) {
		return -EIO;
	}

	data->scan_cb = cb;
	data->scan_max_bss_cnt = params->max_bss_cnt;

	k_work_submit_to_queue(&data->workq, &data->scan_work);

	return 0;
}

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

static void ra_rpc_mgmt_scan_work(struct k_work *work)
{
	struct ra_rpc_data *dev;
	struct wifi_scan_result entry;
	WIFIScanResult_t *results;
	WIFIReturnCode_t ret;
	int i;

	dev = CONTAINER_OF(work, struct ra_rpc_data, scan_work);

	if (dev->scan_max_bss_cnt == 0) {
		dev->scan_max_bss_cnt = CONFIG_WIFI_RA_RPC_MAX_BBS_COUNT;
	}

	results = malloc(dev->scan_max_bss_cnt * sizeof(WIFIScanResult_t));
	if (results != NULL) {
		memset(results, 0, dev->scan_max_bss_cnt * sizeof(WIFIScanResult_t));

		ret = WIFI_Scan(results, dev->scan_max_bss_cnt);

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
				if (entry.channel > 14) {
					entry.band = WIFI_FREQ_BAND_5_GHZ;
				}
/*
	struct wifi_scan_result {
		uint8_t ssid[WIFI_SSID_MAX_LEN + 1];			// DONE
		uint8_t ssid_length;							// DONE
		uint8_t band;									// DONE
		uint8_t channel;								// DONE
		enum wifi_security_type security;				// DONE
		enum wifi_wpa3_enterprise_type wpa3_ent_type;
		enum wifi_mfp_options mfp;
		int8_t rssi;									// DONE
		uint8_t mac[WIFI_MAC_ADDR_LEN];					// DONE
		uint8_t mac_length;								// DONE
	};
*/
				dev->scan_cb(dev->net_iface, 0, &entry);
				k_yield();
			}
		} else {
			// TODO - pass back an error code?
			dev->scan_cb(dev->net_iface, -1, NULL);
		}
		free(results);
	}
	dev->scan_cb = NULL;
}

static int ra_rpc_mgmt_connect(const struct device *dev,
	struct wifi_connect_req_params *params)
{
#if 0
struct WIFINetworkParamsExt_t
{
    WIFINetworkParams_t 				  xNetworkParams; ///< Basic configuration
    WIFIEnterpriseNetParams_t 			  xEntNetParams; 	///< Enterprise security configuration
    WIFIBand_t 							  ucBand;         ///< Band to configure
    e_ra_eprc_wifi_phy_mode_ext_t 		  ucWiFi_mode;    ///< Wi-Fi PHY mode to configure (a, b, g ,n, ...)
    bool 								  hidden_ssid;    ///< AP is using hidden SSID
    WIFIPmf_t 							  pmf;            ///< Protected Management Frame mode
    uint8[k_wificonfigMAX_SAE_GROUPS_LEN] sae_groups;		///< String contains all the WPA3 sae group ids
}

struct wifi_connect_req_params {
	/** SSID */
	const uint8_t *ssid;
	/** SSID length */
	uint8_t ssid_length; /* Max 32 */
	/** Pre-shared key */
	const uint8_t *psk;
	/** Pre-shared key length */
	uint8_t psk_length; /* Min 8 - Max 64 */
	/** SAE password (same as PSK but with no length restrictions), optional */
	const uint8_t *sae_password;
	/** SAE password length */
	uint8_t sae_password_length; /* No length restrictions */
	/** Frequency band */
	uint8_t band;
	/** Channel */
	uint8_t channel;
	/** Security type */
	enum wifi_security_type security;
	/** MFP options */
	enum wifi_mfp_options mfp;
	/** BSSID */
	uint8_t bssid[WIFI_MAC_ADDR_LEN];
	/** Connect timeout in seconds, SYS_FOREVER_MS for no timeout */
	int timeout;
	/** anonymous identity */
	const uint8_t *anon_id;
	/** anon_id length, max 64 */
	uint8_t aid_length;
	/** Private key passwd for enterprise mode */
	const uint8_t *key_passwd;
	/** Private key passwd length, max 128 */
	uint8_t key_passwd_length;
	/** private key2 passwd */
	const uint8_t *key2_passwd;
	/** key2 passwd length, max 128 */
	uint8_t key2_passwd_length;
	/** wpa3 enterprise mode */
	enum wifi_wpa3_enterprise_type wpa3_ent_mode;
	/** TLS cipher */
	uint8_t TLS_cipher;
	/** eap version */
	int eap_ver;
	/** Identity for EAP */
	const uint8_t *eap_identity;
	/** eap identity length, max 64 */
	uint8_t eap_id_length;
	/** Password string for EAP. */
	const uint8_t *eap_password;
	/** eap passwd length, max 128 */
	uint8_t eap_passwd_length;
	/** Whether verify peer with CA or not: false-not verify, true-verify. */
	bool verify_peer_cert;
	/** Fast BSS Transition used */
	bool ft_used;
	/** Number of EAP users */
	int nusers;
	/** Number of EAP passwds */
	uint8_t passwds;
	/** User Identities */
	const uint8_t *identities[WIFI_ENT_IDENTITY_MAX_USERS];
	/** User Passwords */
	const uint8_t *passwords[WIFI_ENT_IDENTITY_MAX_USERS];
	/** Hidden SSID configure
	 * 0: disabled (default)
	 * 1: send empty (length=0) SSID in beacon and ignore probe request for broadcast SSID
	 * 2: clear SSID, but keep the original length and ignore probe request for broadcast SSID
	 */
	uint8_t ignore_broadcast_ssid;
	/** Parameter used for frequency band */
	enum wifi_frequency_bandwidths bandwidth;
};

#endif

	struct ra_rpc_data *data = dev->data;

	data->drv_nwk_params = malloc(sizeof(WIFINetworkParams_t));

	if (data->drv_nwk_params) {

		data->drv_nwk_params->xPassword.xWPA.ucLength = MIN(params->psk_length, sizeof(data->drv_nwk_params->xPassword.xWPA.cPassphrase));
		memcpy(data->drv_nwk_params->xPassword.xWPA.cPassphrase, params->psk, data->drv_nwk_params->xPassword.xWPA.ucLength);

		//data->drv_nwk_params->ucBand = params->band;
		data->drv_nwk_params->ucChannel = params->channel;
		data->drv_nwk_params->ucSSIDLength = MIN(params->ssid_length, sizeof(data->drv_nwk_params->ucSSID));
		memcpy(data->drv_nwk_params->ucSSID, params->ssid, data->drv_nwk_params->ucSSIDLength);
		data->drv_nwk_params->xSecurity = wifi_mgmt_to_drv_sec(params->security);

		k_work_submit_to_queue(&data->workq, &data->connect_work);
	}

	// TODO return value based on checking of passed parameters...?
	return 0;
}

static void ra_rpc_mgmt_connect_work(struct k_work *work)
{
	struct ra_rpc_data *dev;
	WIFIReturnCode_t ret;
	int status = 0;

	dev = CONTAINER_OF(work, struct ra_rpc_data, connect_work);

	ret = WIFI_ConnectAP(dev->drv_nwk_params);

	free(dev->drv_nwk_params);

	if (ret) {
		status = -1;
	}
	wifi_mgmt_raise_connect_result_event(dev->net_iface, status);
}

static int ra_rpc_mgmt_disconnect(const struct device *dev)
{
	// TODO - handle return value
	WIFI_Disconnect();

	return 0;
}

static int ra_rpc_mgmt_iface_status(const struct device *dev,
	struct wifi_iface_status *status)
{
	/** Interface state, see enum wifi_iface_state */
	//int state;
	/** Frequency band */
	//enum wifi_frequency_bands band;
	/** Interface mode, see enum wifi_iface_mode */
	//enum wifi_iface_mode iface_mode;
	/** Link mode, see enum wifi_link_mode */
	//enum wifi_link_mode link_mode;
	/** WPA3 enterprise type */
	//enum wifi_wpa3_enterprise_type wpa3_ent_type;
	/** Security type, see enum wifi_security_type */
	//enum wifi_security_type security;
	/** MFP options, see enum wifi_mfp_options */
	//enum wifi_mfp_options mfp;
	/** RSSI */
	//int rssi;
	/** DTIM period */
	//unsigned char dtim_period;
	/** Beacon interval */
	//unsigned short beacon_interval;
	/** is TWT capable? */
	//bool twt_capable;
	/** The current 802.11 PHY TX data rate (in Mbps) */
	//int current_phy_tx_rate;

	//WIFIConnectionInfo_t conn_info;

	//if (WIFI_GetConnectionInfo(&conn_info) == eWiFiSuccess) {
	//	status->channel = conn_info.ucChannel;
	//	memcpy(status->bssid, conn_info.ucBSSID, WIFI_MAC_ADDR_LEN);
	//	if (conn_info.ucSSIDLength < WIFI_SSID_MAX_LEN) {
	//		status->ssid_len = conn_info.ucSSIDLength;
	//		memcpy(status->ssid, conn_info.ucSSID, status->ssid_len);
	//	}
	//}

	return 0;
}

static int ra_rpc_mgmt_reg_domain(const struct device *dev,
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
