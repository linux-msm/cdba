/*
 * Copyright (c) 2016-2018, Linaro Ltd.
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
 */
#include <sys/file.h>
#include <sys/stat.h>

#include <assert.h>
#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cdba-server.h"
#include "device.h"
#include "fastboot.h"
#include "console.h"
#include "list.h"

#define ARRAY_SIZE(x) ((sizeof(x)/sizeof((x)[0])))

static struct list_head devices = LIST_INIT(devices);

void device_add(struct device *device)
{
	list_add(&devices, &device->node);
}

static void device_lock(struct device *device)
{
	char lock[PATH_MAX];
	int fd;
	int n;

	n = snprintf(lock, sizeof(lock), "/tmp/cdba-%s.lock", device->board);
	if (n >= sizeof(lock))
		errx(1, "failed to build lockfile path");

	fd = open(lock, O_RDONLY | O_CREAT | O_CLOEXEC, 0666);
	if (fd < 0)
		err(1, "failed to open lockfile %s", lock);

	n = flock(fd, LOCK_EX | LOCK_NB);
	if (!n)
		return;

	warnx("board is in use, waiting...");

	n = flock(fd, LOCK_EX);
	if (n < 0)
		err(1, "failed to lock lockfile %s", lock);
}

struct device *device_open(const char *board,
			   struct fastboot_ops *fastboot_ops)
{
	struct device *device;

	list_for_each_entry(device, &devices, node) {
		if (!strcmp(device->board, board))
			goto found;
	}

	return NULL;

found:
	assert(device->open || device->console_dev);

	device_lock(device);

	if (device->open) {
		device->cdb = device->open(device);
		if (!device->cdb)
			errx(1, "failed to open device controller");
	}

	if (device->console_dev)
		console_open(device);

	device->fastboot = fastboot_open(device->serial, fastboot_ops, NULL);

	return device;
}

static void device_release_fastboot_key(void *data)
{
	struct device *device = data;

	device->fastboot_key(device, false);
}

int device_power_on(struct device *device)
{
	if (!device || !device->power_on)
		return 0;

	if (device->fastboot_key_timeout)
		device->fastboot_key(device, true);

	device->power_on(device);

	if (device->fastboot_key_timeout)
		watch_timer_add(device->fastboot_key_timeout * 1000, device_release_fastboot_key, device);

	return 0;
}

int device_power_off(struct device *device)
{
	if (!device || !device->power_off)
		return 0;

	device->power_off(device);

	return 0;
}

void device_print_status(struct device *device)
{
	if (device->print_status)
		device->print_status(device);
}

void device_vbus(struct device *device, bool enable)
{
	if (device->vbus)
		device->vbus(device, enable);
}

int device_write(struct device *device, const void *buf, size_t len)
{
	if (!device)
		return 0;

	assert(device->write);

	return device->write(device, buf, len);
}

void device_fastboot_boot(struct device *device)
{
	fastboot_boot(device->fastboot);
}

void device_fastboot_flash_reboot(struct device *device)
{
	fastboot_flash(device->fastboot, "boot");
	fastboot_reboot(device->fastboot);
}

void device_boot(struct device *device, const void *data, size_t len)
{
	warnx("booting the board...");
	if (device->set_active)
		fastboot_set_active(device->fastboot, "a");
	fastboot_download(device->fastboot, data, len);
	device->boot(device);
}

void device_send_break(struct device *device)
{
	if (device->send_break)
		device->send_break(device);
}

void device_list_devices(void)
{
	struct device *device;
	struct msg hdr;
	size_t len;
	char buf[80];

	list_for_each_entry(device, &devices, node) {
		if (device->name)
			len = snprintf(buf, sizeof(buf), "%-20s %s", device->board, device->name);
		else
			len = snprintf(buf, sizeof(buf), "%s", device->board);

		hdr.type = MSG_LIST_DEVICES;
		hdr.len = len;
		write(STDOUT_FILENO, &hdr, sizeof(hdr));
		write(STDOUT_FILENO, buf, len);
	}

	hdr.type = MSG_LIST_DEVICES;
	hdr.len = 0;
	write(STDOUT_FILENO, &hdr, sizeof(hdr));
}

void device_info(const void *data, size_t dlen)
{
	struct device *device;
	struct msg hdr;
	char *description;
	size_t len = 0;

	list_for_each_entry(device, &devices, node) {
		if (!strncmp(device->board, data, dlen) && device->description) {
			description = device->description;
			len = strlen(device->description);
			break;
		}
	}

	hdr.type = MSG_BOARD_INFO;
	hdr.len = len;
	write(STDOUT_FILENO, &hdr, sizeof(hdr));
	if (len)
		write(STDOUT_FILENO, description, len);
}
