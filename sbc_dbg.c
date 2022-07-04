/*
 * Copyright (c) 2021, Linaro Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * This debug device supports using GPIOs and a TTY on the device running
 * cdba-server, e.g. a Raspberry Pi, Quartz64 or some other Linux SBC
 */

#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <gpiod.h>

#include "sbc_dbg.h"

enum {
	GPIO_PWR = 0,
	GPIO_VOLUP,
	GPIO_VOLDN,
	GPIO_VBUS,

	GPIO_MAX,
#define N_GPIOS GPIO_MAX
};

struct sbc_dbg {
	struct gpiod_line *lines[N_GPIOS];
	struct termios orig_tios;
};

void *sbc_dbg_open(struct device *dev)
{
	struct sbc_dbg *dbg;
	char *c, *start, *end = NULL;
	unsigned long int n;
	int flags = GPIOD_LINE_REQUEST_FLAG_OPEN_SOURCE;
	struct gpiod_chip *chip;
	struct gpiod_chip_iter *iter;
	unsigned long gpio_nums[N_GPIOS];
	int i, rc;
	bool found_any;

	dev->has_power_key = true;

	dbg = calloc(1, sizeof(*dbg));

	start = dev->control_dev;

	for(i = 0; i < N_GPIOS; i++) {
		c = strchr(start, ',');
		if (c)
			*c = '\0';
		n = strtoul(start, &end, 10);
		if (start == end) {
			fprintf(stderr, "Couldn't parse '%s' as a list of GPIOS!\n",
				dev->control_dev);
			return NULL;
		}
		fprintf(stderr, "Got GPIO n %lu\n", n);
		gpio_nums[i] = n;
		start = c + 1;
	}

	if (dev->active_low)
		flags |= GPIOD_LINE_REQUEST_FLAG_ACTIVE_LOW;

	iter = gpiod_chip_iter_new();
	gpiod_foreach_chip_noclose(iter, chip) {
		found_any = false;
		fprintf(stderr, "Checking chip %s\n", gpiod_chip_name(chip));
		for(i = 0; i < N_GPIOS; i++) {
			if (dbg->lines[i])
				continue;
			if (gpio_nums[i] < gpiod_chip_num_lines(chip)) {
				dbg->lines[i] = gpiod_chip_get_line(chip, gpio_nums[i]);
				rc = gpiod_line_request_output_flags(dbg->lines[i], "cdba-server",
					flags, 0);
				if (rc < 0) {
					fprintf(stderr, "Failed to set line %d (%d: %s)\n", i, rc,
						strerror(errno));
					goto err;
				}
				found_any = true;
			} else {
				gpio_nums[i] -= gpiod_chip_num_lines(chip);
			}
		}

		if (!found_any)
			gpiod_chip_close(chip);
	}

	// The vbus line is always active high!
	if (dev->active_low)
		gpiod_line_set_config(dbg->lines[GPIO_VBUS],
			GPIOD_LINE_REQUEST_DIRECTION_OUTPUT, 0, 0);

	gpiod_chip_iter_free_noclose(iter);

	for(i = 0; i < N_GPIOS; i++) {
		if (!dbg->lines[i]) {
			fprintf(stderr, "Failed to find GPIO %d!\n", i);
			goto err;
		}
	}

	return dbg;

err:
	for(i = 0; i < N_GPIOS; i++) {
		if (dbg->lines[i])
			gpiod_line_close_chip(dbg->lines[i]);
	}
	gpiod_chip_iter_free_noclose(iter);
	free(dbg);
	return NULL;
}

static void sbc_dbg_deassert(struct sbc_dbg *dbg)
{
	int i = 0;
	for(; i < N_GPIOS; i++)
		gpiod_line_set_value(dbg->lines[i], 0);
}

/*
 * For phones with a battery in them we need some
 * jank to hard reset the device to power
 * it down
 */
//FIXME: untested
int sbc_dbg_power(struct device *dev, bool on)
{
	struct sbc_dbg *dbg = dev->cdb;
	bool is_on = gpiod_line_get_value(dbg->lines[GPIO_VBUS]);

	fprintf(stderr, "sbc_dbg_power(%d) (is_on: %d)\n", on, is_on);

	sbc_dbg_deassert(dbg);
	/* Make sure buttons are all released */
	sleep(1);
	/*
	 * Release everything and then enable vbus to ensure
	 * the device will be on if it was off before, this is
	 * the only way to get the device into a known state
	 * so that we can perform the correct actions to then
	 * get it into the bootloader.
	 */
	gpiod_line_set_value(dbg->lines[GPIO_VBUS], 1);
	sleep(1);
	gpiod_line_set_value(dbg->lines[GPIO_VBUS], 0);
	/* Do a hard reset (vbus off, hold power+volup for 10 seconds) */
	gpiod_line_set_value(dbg->lines[GPIO_VOLUP], 1);
	gpiod_line_set_value(dbg->lines[GPIO_PWR], 1);
	sleep(10);
	/*
	 * To power off, release before the timer in XBL registers
	 * a power on. To go to fastboot, hold power for slightly longer
	 * and then release it, continuing to hold volup
	 */
	if (on)
		sleep(1);
	gpiod_line_set_value(dbg->lines[GPIO_PWR], 0);
	if (on) {
		sleep(3);
		gpiod_line_set_value(dbg->lines[GPIO_VBUS], 1);
	}
	gpiod_line_set_value(dbg->lines[GPIO_VOLUP], 0);

	return 0;
}

void sbc_dbg_usb(struct device *dev, bool on)
{
	fprintf(stderr, "sbc_dbg_usb(%d)\n", on);

}

void sbc_dbg_key(struct device *dev, int key, bool asserted)
{
	struct sbc_dbg *dbg = dev->cdb;

	fprintf(stderr, "sbc_dbg_key(%d, %d)\n", key, asserted);

	switch (key) {
	case DEVICE_KEY_FASTBOOT:
		//gpiod_line_set_value(dbg->lines[GPIO_VOLUP], 1);
		break;
	case DEVICE_KEY_POWER:
		//gpiod_line_set_value(dbg->lines[GPIO_PWR], 1);
		break;
	}
}

void sbc_dbg_close(struct device *dev)
{
	struct sbc_dbg *dbg = dev->cdb;
	int i;

	for(i = 0; i < N_GPIOS; i++) {
		gpiod_line_close_chip(dbg->lines[i]);
	}
}
