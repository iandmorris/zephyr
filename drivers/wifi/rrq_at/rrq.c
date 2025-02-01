/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_rrq_at

#define RRQ_MTU		2048 /* TODO - what should this be set to? */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wifi_rrq_at, CONFIG_WIFI_LOG_LEVEL);

#include <zephyr/drivers/gpio.h>

#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/conn_mgr/connectivity_wifi_mgmt.h>

#include "rrq_offload.h"

struct rrq_config {
#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	const struct gpio_dt_spec reset;
#endif
};

struct rrq_data {
	struct net_if *net_iface;
};

static const struct rrq_config rrq_driver_config = {
#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	.reset = GPIO_DT_SPEC_INST_GET(0, reset_gpios),
#endif
};

struct rrq_data rrq_driver_data;

static int rrq_mgmt_scan(const struct device *dev,
			 struct wifi_scan_params *params,
			 scan_result_cb_t cb)
{
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
	int ret = 0;

	ret = rrq_reset(dev);

	return ret;
}
