/*
 * Copyright (c) 2024 Ian Morris
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_data_types.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/dsp/print_format.h>

#define DPM_ALIAS(i) DT_ALIAS(_CONCAT(dpm, i))
#define DPM_DEVICE(i, _)                                                      \
	IF_ENABLED(DT_NODE_EXISTS(DPM_ALIAS(i)), (DEVICE_DT_GET(DPM_ALIAS(i)),))

/* Support up to 10 power monitor sensors */
static const struct device *const sensors[] = {LISTIFY(10, DPM_DEVICE, ())};

#define DPM_IODEV(i, _)                                                      \
	IF_ENABLED(DT_NODE_EXISTS(DPM_ALIAS(i)),                                 \
		(SENSOR_DT_READ_IODEV(_CONCAT(dpm_iodev, i), DPM_ALIAS(i),           \
		{SENSOR_CHAN_VOLTAGE, 0},                                            \
		{SENSOR_CHAN_POWER, 0},                                              \
		{SENSOR_CHAN_CURRENT, 0})))

LISTIFY(10, DPM_IODEV, (;));

#define DPM_IODEV_REF(i, _)                                                   \
	COND_CODE_1(DT_NODE_EXISTS(DPM_ALIAS(i)), (CONCAT(&dpm_iodev, i)), (NULL))

static struct rtio_iodev *dpm_iodev[] = { LISTIFY(10, DPM_IODEV_REF, (,)) };

RTIO_DEFINE(dpm_ctx, 1, 1);

int main(void)
{
	int rc;

	for (size_t i = 0; i < ARRAY_SIZE(sensors); i++) {
		if (!device_is_ready(sensors[i])) {
			printk("sensor: device %s not ready.\n", sensors[i]->name);
			return 0;
		}
	}

	while (1) {
		for (size_t i = 0; i < ARRAY_SIZE(sensors); i++) {
			struct device *dev = (struct device *) sensors[i];

			uint8_t buf[128];

			rc = sensor_read(dpm_iodev[i], &dpm_ctx, buf, 128);

			if (rc != 0) {
				printk("%s: sensor_read() failed: %d\n", dev->name, rc);
				return rc;
			}

			const struct sensor_decoder_api *decoder;

			rc = sensor_get_decoder(dev, &decoder);

			if (rc != 0) {
				printk("%s: sensor_get_decode() failed: %d\n", dev->name, rc);
				return rc;
			}

			uint32_t volt_fit = 0;
			struct sensor_q31_data volt_data = {0};

			decoder->decode(buf,
					(struct sensor_chan_spec) {SENSOR_CHAN_VOLTAGE, 0},
					&volt_fit, 1, &volt_data);

			uint32_t power_fit = 0;
			struct sensor_q31_data power_data = {0};

			decoder->decode(buf,
					(struct sensor_chan_spec) {SENSOR_CHAN_POWER, 0},
					&power_fit, 1, &power_data);

			uint32_t curr_fit = 0;
			struct sensor_q31_data curr_data = {0};

			decoder->decode(buf,
					(struct sensor_chan_spec) {SENSOR_CHAN_CURRENT, 0},
					&curr_fit, 1, &curr_data);

			printk("%16s: voltage is %s%d.%d V power is %s%d.%d W current is %s%d.%d A\n", dev->name,
				PRIq_arg(volt_data.readings[0].voltage, 2, volt_data.shift),
				PRIq_arg(power_data.readings[0].power, 2, power_data.shift),
				PRIq_arg(curr_data.readings[0].current, 2, curr_data.shift));
		}
		k_msleep(1000);
	}
	return 0;
}
