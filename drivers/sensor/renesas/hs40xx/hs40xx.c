/*
 * Copyright (c) 2024 Ian Morris
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_hs40xx

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>

#include "hs40xx.h"

LOG_MODULE_REGISTER(HS40XX, CONFIG_SENSOR_LOG_LEVEL);

static int hs40xx_read_sample(const struct device *dev, uint16_t *t_sample, uint16_t *rh_sample)
{
	return 0;
}

static int hs40xx_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	return 0;
}

static int hs40xx_channel_get(const struct device *dev, enum sensor_channel chan,
			      struct sensor_value *val)
{
	return 0;
}

static int hs40xx_attr_set(const struct device *dev,
			   enum sensor_channel chan,
			   enum sensor_attribute attr,
			   const struct sensor_value *val)
{
	return 0;
}

static int hs40xx_attr_get(const struct device *dev, enum sensor_channel chan,
			   enum sensor_attribute attr, struct sensor_value *val)
{
	return 0;
}

static const struct sensor_driver_api hs40xx_driver_api = {
	.sample_fetch = hs40xx_sample_fetch,
	.channel_get = hs40xx_channel_get,
	.attr_set = hs40xx_attr_set,
	.attr_get = hs40xx_attr_get,
};


static int hs40xx_init(const struct device *dev)
{
	const struct hs40xx_config *cfg = dev->config;

	if (!i2c_is_ready_dt(&cfg->bus)) {
		LOG_ERR("I2C dev %s not ready", cfg->bus.bus->name);
		return -ENODEV;
	}

	return 0;
}

#define DEFINE_HS40XX(n) \
	static struct hs40xx_data hs40xx_data_##n; \
	static const struct hs40xx_config hs40xx_config_##n = { \
		.bus = I2C_DT_SPEC_INST_GET(n) \
	}; \
	SENSOR_DEVICE_DT_INST_DEFINE(n, hs40xx_init, NULL, \
		&hs40xx_data_##n, &hs40xx_config_##n, POST_KERNEL, \
		CONFIG_SENSOR_INIT_PRIORITY, &hs40xx_driver_api);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_HS40XX)
