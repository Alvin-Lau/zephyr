/* Bosch BMG160 gyro driver, trigger implementation
 *
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
 *
 * Datasheet:
 * http://ae-bst.resource.bosch.com/media/_tech/media/datasheets/BST-BMG160-DS000-09.pdf
 */

#include <nanokernel.h>
#include <sensor.h>

#include "sensor_bmg160.h"

extern struct bmg160_device_data bmg160_data;

static void bmg160_gpio_callback(struct device *port, struct gpio_callback *cb,
				 uint32_t pin)
{
	struct bmg160_device_data *bmg160 =
		CONTAINER_OF(cb, struct bmg160_device_data, gpio_cb);

#if defined(CONFIG_BMG160_TRIGGER_OWN_FIBER)
	nano_isr_sem_give(&bmg160->trig_sem);
#elif defined(CONFIG_BMG160_TRIGGER_GLOBAL_FIBER)
	nano_work_submit(&bmg160->work);
#endif
}

static int bmg160_anymotion_set(struct device *dev,
				sensor_trigger_handler_t handler)
{
	struct bmg160_device_data *bmg160 = dev->driver_data;
	uint8_t anymotion_en = 0;

	if (handler) {
		anymotion_en = BMG160_ANY_EN_X |
			       BMG160_ANY_EN_Y |
			       BMG160_ANY_EN_Z;
	}

	if (bmg160_update_byte(dev, BMG160_REG_ANY_EN,
			       BMG160_ANY_EN_MASK, anymotion_en) < 0) {
		return -EIO;
	}

	bmg160->anymotion_handler = handler;

	return 0;
}

static int bmg160_drdy_set(struct device *dev, sensor_trigger_handler_t handler)
{
	struct bmg160_device_data *bmg160 = dev->driver_data;

	if (bmg160_update_byte(dev, BMG160_REG_INT_EN0,
			       BMG160_DATA_EN,
			       handler ? BMG160_DATA_EN : 0) < 0) {
		return -EIO;
	}

	bmg160->drdy_handler = handler;

	return 0;
}

int bmg160_slope_config(struct device *dev, enum sensor_attribute attr,
			const struct sensor_value *val)
{
	struct bmg160_device_data *bmg160 = dev->driver_data;

	if (attr == SENSOR_ATTR_SLOPE_TH) {
		uint16_t any_th_dps, range_dps;
		uint8_t any_th_reg_val;

		if (val->type != SENSOR_VALUE_TYPE_INT_PLUS_MICRO) {
			return -EINVAL;
		}

		any_th_dps = sensor_rad_to_degrees(val);
		range_dps = BMG160_SCALE_TO_RANGE(bmg160->scale);
		any_th_reg_val = any_th_dps * 2000 / range_dps;

		/* the maximum slope depends on selected range */
		if (any_th_dps > range_dps / 16) {
			return -ENOTSUP;
		}

		return bmg160_write_byte(dev, BMG160_REG_THRES,
					 any_th_dps & BMG160_THRES_MASK);
	} else if (attr == SENSOR_ATTR_SLOPE_DUR) {
		if (val->type != SENSOR_VALUE_TYPE_INT) {
			return -EINVAL;
		}

		/* slope duration can be 4, 8, 12 or 16 samples */
		if (val->val1 != 4 && val->val1 != 8 &&
		    val->val1 != 12 && val->val1 != 16) {
			return -ENOTSUP;
		}

		return bmg160_write_byte(dev, BMG160_REG_ANY_EN,
			   (val->val1 << BMG160_ANY_DURSAMPLE_POS) &
			    BMG160_ANY_DURSAMPLE_MASK);
	}

	return -ENOTSUP;
}

int bmg160_trigger_set(struct device *dev,
		       const struct sensor_trigger *trig,
		       sensor_trigger_handler_t handler)
{
	if (trig->type == SENSOR_TRIG_DELTA) {
		return bmg160_anymotion_set(dev, handler);
	} else if (trig->type == SENSOR_TRIG_DATA_READY) {
		return bmg160_drdy_set(dev, handler);
	}

	return -ENOTSUP;
}

static int bmg160_handle_anymotion_int(struct device *dev)
{
	struct bmg160_device_data *bmg160 = dev->driver_data;
	struct sensor_trigger any_trig = {
		.type = SENSOR_TRIG_DELTA,
		.chan = SENSOR_CHAN_GYRO_ANY,
	};

	if (bmg160->anymotion_handler) {
		bmg160->anymotion_handler(dev, &any_trig);
	}

	return 0;
}

static int bmg160_handle_dataready_int(struct device *dev)
{
	struct bmg160_device_data *bmg160 = dev->driver_data;
	struct sensor_trigger drdy_trig = {
		.type = SENSOR_TRIG_DATA_READY,
		.chan = SENSOR_CHAN_GYRO_ANY,
	};

	if (bmg160->drdy_handler) {
		bmg160->drdy_handler(dev, &drdy_trig);
	}

	return 0;
}

static void bmg160_handle_int(void *arg)
{
	struct device *dev = (struct device *)arg;
	uint8_t status_int[4];

	if (bmg160_read(dev, BMG160_REG_INT_STATUS0, status_int, 4) < 0) {
		return;
	}

	if (status_int[0] & BMG160_ANY_INT) {
		bmg160_handle_anymotion_int(dev);
	} else {
		bmg160_handle_dataready_int(dev);
	}
}

#ifdef CONFIG_BMG160_TRIGGER_OWN_FIBER
static char __stack bmg160_fiber_stack[CONFIG_BMG160_FIBER_STACK_SIZE];

static void bmg160_fiber_main(int arg1, int unused)
{
	struct device *dev = (struct device *)arg1;
	struct bmg160_device_data *bmg160 = dev->driver_data;

	while (true) {
		nano_fiber_sem_take(&bmg160->trig_sem, TICKS_UNLIMITED);

		bmg160_handle_int(dev);
	}
}
#endif

#ifdef CONFIG_BMG160_TRIGGER_GLOBAL_FIBER
static void bmg160_work_cb(struct nano_work *work)
{
	struct bmg160_device_data *bmg160 =
		CONTAINER_OF(work, struct bmg160_device_data, work);

	bmg160_handle_int(bmg160->dev);
}
#endif

int bmg160_trigger_init(struct device *dev)
{
	struct bmg160_device_config *cfg = dev->config->config_info;
	struct bmg160_device_data *bmg160 = dev->driver_data;

	/* set INT1 pin to: push-pull, active low */
	if (bmg160_write_byte(dev, BMG160_REG_INT_EN1, 0) < 0) {
		SYS_LOG_DBG("Failed to select interrupt pins type.");
		return -EIO;
	}

	/* set interrupt mode to non-latched */
	if (bmg160_write_byte(dev, BMG160_REG_INT_RST_LATCH, 0) < 0) {
		SYS_LOG_DBG("Failed to set the interrupt mode.");
		return -EIO;
	}

	/* map anymotion and high rate interrupts to INT1 pin */
	if (bmg160_write_byte(dev, BMG160_REG_INT_MAP0,
			      BMG160_INT1_ANY | BMG160_INT1_HIGH) < 0) {
		SYS_LOG_DBG("Unable to map interrupts.");
		return -EIO;
	}

	/* map data ready, FIFO and FastOffset interrupts to INT1 pin */
	if (bmg160_write_byte(dev, BMG160_REG_INT_MAP1,
			      BMG160_INT1_DATA | BMG160_INT1_FIFO |
			      BMG160_INT1_FAST_OFFSET) < 0) {
		SYS_LOG_DBG("Unable to map interrupts.");
		return -EIO;
	}

	bmg160->gpio = device_get_binding((char *)cfg->gpio_port);
	if (!bmg160->gpio) {
		SYS_LOG_DBG("Gpio controller %s not found", cfg->gpio_port);
		return -EINVAL;
	}

#if defined(CONFIG_BMG160_TRIGGER_OWN_FIBER)
	nano_sem_init(&bmg160->trig_sem);
	fiber_start(bmg160_fiber_stack, CONFIG_BMG160_FIBER_STACK_SIZE,
		    bmg160_fiber_main, (int)dev, 0, 10, 0);
#elif defined(CONFIG_BMG160_TRIGGER_GLOBAL_FIBER)
	bmg160->work.handler = bmg160_work_cb;
	bmg160->dev = dev;
#endif

	gpio_pin_configure(bmg160->gpio, cfg->int_pin,
			   GPIO_DIR_IN | GPIO_INT | GPIO_INT_EDGE |
			   GPIO_INT_ACTIVE_LOW | GPIO_INT_DEBOUNCE);
	gpio_init_callback(&bmg160->gpio_cb, bmg160_gpio_callback,
			   BIT(cfg->int_pin));
	gpio_add_callback(bmg160->gpio, &bmg160->gpio_cb);
	gpio_pin_enable_callback(bmg160->gpio, cfg->int_pin);

	return 0;
}
