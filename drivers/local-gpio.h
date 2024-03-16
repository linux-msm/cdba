/*
 * Copyright (c) 2023, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LOCAL_GPIO_H_
#define _LOCAL_GPIO_H_

enum {
	GPIO_POWER = 0,			// Power input enable
	GPIO_FASTBOOT_KEY,		// Usually volume key
	GPIO_POWER_KEY,			// Key to power the device
	GPIO_USB_DISCONNECT,		// Simulate main USB connection
	GPIO_COUNT
};

struct local_gpio_options {
	struct {
		char *chip;
		bool present;
		unsigned int offset;
		bool active_low;
	} gpios[GPIO_COUNT];
};

struct local_gpio {
	struct local_gpio_options *options;
	struct {
		void *chip;
		void *line;
	} gpios[GPIO_COUNT];
};

int local_gpio_init(struct local_gpio *local_gpio);
int local_gpio_set_value(struct local_gpio *local_gpio, unsigned int gpio, bool on);

#endif /* _LOCAL_GPIO_H_ */
