/*
 * Copyright (c) 2016 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <i2c.h>
#include <init.h>
#include <misc/__assert.h>
#include <misc/byteorder.h>
#include <sensor.h>
#include <string.h>

#include "sensor_hts221.h"

static int hts221_channel_get(struct device *dev,
			       enum sensor_channel chan,
			       struct sensor_value *val)
{
	struct hts221_data *drv_data = dev->driver_data;
	int32_t conv_val;

	__ASSERT_NO_MSG(chan == SENSOR_CHAN_TEMP || SENSOR_CHAN_HUMIDITY);

	/*
	 * see "Interpreting humidity and temperature readings" document
	 * for more details
	 */
	if (chan == SENSOR_CHAN_TEMP) {
		conv_val = (int32_t)(drv_data->t1_degc_x8 -
				     drv_data->t0_degc_x8) *
			   (drv_data->t_sample - drv_data->t0_out) /
			   (drv_data->t1_out - drv_data->t0_out) +
			   drv_data->t0_degc_x8;

		/* convert temperature x8 to degrees Celsius */
		val->type = SENSOR_VALUE_TYPE_INT_PLUS_MICRO;
		val->val1 = conv_val / 8;
		val->val2 = (conv_val % 8) * (1000000 / 8);
	} else { /* SENSOR_CHAN_HUMIDITY */
		conv_val = (int32_t)(drv_data->h1_rh_x2 - drv_data->h0_rh_x2) *
			   (drv_data->rh_sample - drv_data->h0_t0_out) /
			   (drv_data->h1_t0_out - drv_data->h0_t0_out) +
			   drv_data->h0_rh_x2;

		/* convert humidity x2 to mili-percent */
		val->type = SENSOR_VALUE_TYPE_INT_PLUS_MICRO;
		val->val1 = conv_val * 500;
		val->val2 = 0;
	}

	return 0;
}

static int hts221_sample_fetch(struct device *dev, enum sensor_channel chan)
{
	struct hts221_data *drv_data = dev->driver_data;
	uint8_t buf[4];

	__ASSERT_NO_MSG(chan == SENSOR_CHAN_ALL);

	if (i2c_burst_read(drv_data->i2c, HTS221_I2C_ADDR,
			   HTS221_REG_DATA_START | HTS221_AUTOINCREMENT_ADDR,
			   buf, 4) < 0) {
		SYS_LOG_ERR("Failed to fetch data sample.");
		return -EIO;
	}

	drv_data->rh_sample = sys_le16_to_cpu(buf[0] | (buf[1] << 8));
	drv_data->t_sample = sys_le16_to_cpu(buf[2] | (buf[3] << 8));

	return 0;
}

static int hts221_read_conversion_data(struct hts221_data *drv_data)
{
	uint8_t buf[16];

	if (i2c_burst_read(drv_data->i2c, HTS221_I2C_ADDR,
			   HTS221_REG_CONVERSION_START |
			   HTS221_AUTOINCREMENT_ADDR, buf, 16) < 0) {
		SYS_LOG_ERR("Failed to read conversion data.");
		return -EIO;
	}

	drv_data->h0_rh_x2 = buf[0];
	drv_data->h1_rh_x2 = buf[1];
	drv_data->t0_degc_x8 = sys_le16_to_cpu(buf[2] | ((buf[5] & 0x3) << 8));
	drv_data->t1_degc_x8 = sys_le16_to_cpu(buf[3] | ((buf[5] & 0xC) << 6));
	drv_data->h0_t0_out = sys_le16_to_cpu(buf[6] | (buf[7] << 8));
	drv_data->h1_t0_out = sys_le16_to_cpu(buf[10] | (buf[11] << 8));
	drv_data->t0_out = sys_le16_to_cpu(buf[12] | (buf[13] << 8));
	drv_data->t1_out = sys_le16_to_cpu(buf[14] | (buf[15] << 8));

	return 0;
}

static struct sensor_driver_api hts221_driver_api = {
#if CONFIG_HTS221_TRIGGER
	.trigger_set = hts221_trigger_set,
#endif
	.sample_fetch = hts221_sample_fetch,
	.channel_get = hts221_channel_get,
};

int hts221_init(struct device *dev)
{
	struct hts221_data *drv_data = dev->driver_data;
	uint8_t id, idx;

	drv_data->i2c = device_get_binding(CONFIG_HTS221_I2C_MASTER_DEV_NAME);
	if (drv_data->i2c == NULL) {
		SYS_LOG_ERR("Could not get pointer to %s device.",
			    CONFIG_HTS221_I2C_MASTER_DEV_NAME);
		return -EINVAL;
	}

	/* check chip ID */
	if (i2c_reg_read_byte(drv_data->i2c, HTS221_I2C_ADDR,
			      HTS221_REG_WHO_AM_I, &id) < 0) {
		SYS_LOG_ERR("Failed to read chip ID.");
		return -EIO;
	}

	if (id != HTS221_CHIP_ID) {
		SYS_LOG_ERR("Invalid chip ID.");
		return -EINVAL;
	}

	/* check if CONFIG_HTS221_ODR is valid */
	for (idx = 0; idx < ARRAY_SIZE(hts221_odr_strings); idx++) {
		if (!strcmp(hts221_odr_strings[idx], CONFIG_HTS221_ODR)) {
			break;
		}
	}

	if (idx == ARRAY_SIZE(hts221_odr_strings)) {
		SYS_LOG_ERR("Invalid ODR value.");
		return -EINVAL;
	}

	if (i2c_reg_write_byte(drv_data->i2c, HTS221_I2C_ADDR, HTS221_REG_CTRL1,
			       (idx + 1) << HTS221_ODR_SHIFT | HTS221_BDU_BIT |
			       HTS221_PD_BIT) < 0) {
		SYS_LOG_ERR("Failed to configure chip.");
		return -EIO;
	}

	if (hts221_read_conversion_data(drv_data) < 0) {
		SYS_LOG_ERR("Failed to read conversion data.");
		return -EINVAL;
	}

#ifdef CONFIG_HTS221_TRIGGER
	if (hts221_init_interrupt(dev) < 0) {
		SYS_LOG_ERR("Failed to initialize interrupt.");
		return -EIO;
	}
#endif

	dev->driver_api = &hts221_driver_api;

	return 0;
}

struct hts221_data hts221_driver;

DEVICE_INIT(hts221, CONFIG_HTS221_NAME, hts221_init, &hts221_driver,
	    NULL, SECONDARY, CONFIG_HTS221_INIT_PRIORITY);
