/*
 * Copyright (c) 2023, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>

/* local-gpio implementation for libgpiod major version 1 */

#include "local-gpio.h"

#include <gpiod.h>

int local_gpio_init(struct local_gpio *local_gpio)
{
	int i;

	for (i = 0; i < GPIO_COUNT; ++i) {
		struct gpiod_line_request_config cfg;

		if (!local_gpio->options->gpios[i].present)
			continue;

		local_gpio->gpios[i].chip =
			gpiod_chip_open_lookup(local_gpio->options->gpios[i].chip);
		if (!local_gpio->gpios[i].chip) {
			err(1, "Unable to open gpiochip '%s'",
			    local_gpio->options->gpios[i].chip);
			return -1;
		}

		cfg.consumer = "cdba";
		cfg.request_type = GPIOD_LINE_REQUEST_DIRECTION_OUTPUT;
		cfg.flags = 0;

		if (local_gpio->options->gpios[i].active_low)
			cfg.flags = GPIOD_LINE_REQUEST_FLAG_ACTIVE_LOW;

		local_gpio->gpios[i].line = gpiod_chip_get_line(local_gpio->gpios[i].chip,
							local_gpio->options->gpios[i].offset);

		if (!local_gpio->gpios[i].line) {
			err(1, "Unable to find gpio %d offset %u",
			    i, local_gpio->options->gpios[i].offset);
			return -1;
		}

		if (gpiod_line_request(local_gpio->gpios[i].line, &cfg, 0))  {
			err(1, "Unable to request gpio %d offset %u",
			    i, local_gpio->options->gpios[i].offset);
			return -1;
		}
	}

	return 0;
}

int local_gpio_set_value(struct local_gpio *local_gpio, unsigned int gpio, bool on)
{
	return gpiod_line_set_value(local_gpio->gpios[gpio].line, on);
}
