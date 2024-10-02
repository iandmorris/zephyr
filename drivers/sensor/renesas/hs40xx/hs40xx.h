/*
 * Copyright (c) 2024 Ian Morris
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SENSOR_HS40XX_HS40XX_H_
#define ZEPHYR_DRIVERS_SENSOR_HS40XX_HS40XX_H_

struct hs40xx_data {
	int16_t t_sample;
	uint16_t rh_sample;
	uint32_t id;
};

struct hs40xx_config {
	struct i2c_dt_spec bus;
};

#endif /*  ZEPHYR_DRIVERS_SENSOR_HS40XX_HS40XX_H_ */
