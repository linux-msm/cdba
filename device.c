/*
 * Copyright (c) 2016-2018, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
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
#include <fcntl.h>
#include <syslog.h>

#include "cdba-server.h"
#include "device.h"
#include "fastboot.h"
#include "list.h"
#include "ppps.h"
#include "status-cmd.h"
#include "watch.h"

#define ARRAY_SIZE(x) ((sizeof(x)/sizeof((x)[0])))

#define device_has_control(_dev, _op) \
	((_dev)->control_ops && (_dev)->control_ops->_op)

#define device_control(_dev, _op, ...) \
	(_dev)->control_ops->_op((_dev) , ## __VA_ARGS__)

#define device_has_console(_dev, _op) \
	((_dev)->console_ops && (_dev)->console_ops->_op)

#define device_console(_dev, _op, ...) \
	(_dev)->console_ops->_op((_dev) , ## __VA_ARGS__)

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
	if (n >= (int)sizeof(lock))
		errx(1, "failed to build lockfile path");

	fd = open(lock, O_RDONLY | O_CREAT, 0666);
	if (fd >= 0)
		close(fd);

	fd = open(lock, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		err(1, "failed to open lockfile %s", lock);

	while (1) {
		char c;

		n = flock(fd, LOCK_EX | LOCK_NB);
		if (!n)
			return;

		warnx("board is in use, waiting...");

		sleep(3);

		/* check that connection isn't gone */
		if (read(STDIN_FILENO, &c, 1) == 0)
			errx(1, "connection is gone");
	}
}

static bool device_check_access(struct device *device,
				const char *username)
{
	struct device_user *user;

	if (!device->users)
		return true;

	if (!username)
		return false;

	list_for_each_entry(user, device->users, node) {
		if (!strcmp(user->username, username))
			return true;
	}

	return false;
}

static int device_power_off(struct device *device);

struct device *device_open(const char *board,
			   const char *username)
{
	struct device *device;

	list_for_each_entry(device, &devices, node) {
		if (!strcmp(device->board, board))
			goto found;
	}

	syslog(LOG_INFO, "user %s asked for non-existing board %s", username, board);
	return NULL;

found:
	if (!device_check_access(device, username)) {
		syslog(LOG_INFO, "user %s access denied to the board %s", username, board);

		return NULL;
	}

	syslog(LOG_INFO, "user %s opening board %s", username, board);

	assert(device->console_ops);
	assert(device->console_ops->open);
	assert(device->console_ops->write);

	device_lock(device);

	if (device_has_control(device, open)) {
		device->cdb = device_control(device, open);
		if (!device->cdb)
			errx(1, "failed to open device controller");
	}

	device->console = device_console(device, open);
	if (!device->console)
		errx(1, "failed to open device console");

	/*
	 * Power off before opening fastboot. Otherwise if the device is
	 * already in the fastboot state, CDBA will detect it, then power up
	 * procedure will restart the device causing fastboot to disappear and
	 * appear again. This will cause CDBA to exit, ending up with the
	 * unbreakable fastboot-reset-second_fastboot-quit cycle.
	 * */
	if (device->power_always_on) {
		device_power_off(device);
		sleep(2);
	}

	if (device->usb_always_on)
		device_usb(device, true);

	return device;
}

static void device_impl_power(struct device *device, bool on)
{
	device_control(device, power, on);
}

void device_key(struct device *device, int key, bool asserted)
{
	if (device_has_control(device, key))
		device_control(device, key, key, asserted);
}

enum {
	DEVICE_STATE_START,
	DEVICE_STATE_CONNECT,
	DEVICE_STATE_PRESS,
	DEVICE_STATE_RELEASE_PWR,
	DEVICE_STATE_RELEASE_FASTBOOT,
	DEVICE_STATE_RUNNING,
};

static void device_tick(void *data)
{
	struct device *device = data;

	switch (device->state) {
	case DEVICE_STATE_START:
		/* Make sure power key is not engaged */
		if (device->fastboot_key_timeout)
			device_key(device, DEVICE_KEY_FASTBOOT, true);
		if (device->has_power_key)
			device_key(device, DEVICE_KEY_POWER, false);

		device->state = DEVICE_STATE_CONNECT;
		watch_timer_add(10, device_tick, device);
		break;
	case DEVICE_STATE_CONNECT:
		/* Connect power and USB */
		device_impl_power(device, true);
		device_usb(device, true);

		if (device->has_power_key) {
			device->state = DEVICE_STATE_PRESS;
			watch_timer_add(250, device_tick, device);
		} else if (device->fastboot_key_timeout) {
			device->state = DEVICE_STATE_RELEASE_FASTBOOT;
			watch_timer_add(device->fastboot_key_timeout * 1000, device_tick, device);
		} else {
			device->state = DEVICE_STATE_RUNNING;
		}
		break;
	case DEVICE_STATE_PRESS:
		/* Press power key */
		device_key(device, DEVICE_KEY_POWER, true);

		device->state = DEVICE_STATE_RELEASE_PWR;
		watch_timer_add(100, device_tick, device);
		break;
	case DEVICE_STATE_RELEASE_PWR:
		/* Release power key */
		device_key(device, DEVICE_KEY_POWER, false);

		if (device->fastboot_key_timeout) {
			device->state = DEVICE_STATE_RELEASE_FASTBOOT;
			watch_timer_add(device->fastboot_key_timeout * 1000, device_tick, device);
		} else {
			device->state = DEVICE_STATE_RUNNING;
		}
		break;
	case DEVICE_STATE_RELEASE_FASTBOOT:
		device_key(device, DEVICE_KEY_FASTBOOT, false);
		device->state = DEVICE_STATE_RUNNING;
		break;
	}
}

bool device_is_running(struct device *device)
{
	return device->state == DEVICE_STATE_RUNNING;
}

static int device_power_on(struct device *device)
{
	if (!device || !device_has_control(device, power))
		return 0;

	device->state = DEVICE_STATE_START;
	device_tick(device);

	return 0;
}

static int device_power_off(struct device *device)
{
	if (!device || !device_has_control(device, power))
		return 0;

	device_control(device, power, false);

	return 0;
}

int device_power(struct device *device, bool on)
{
	if (on)
		return device_power_on(device);
	else
		return device_power_off(device);
}

void device_status_enable(struct device *device)
{
	if (device->status_enabled)
		return;

	if (device_has_control(device, status_enable))
		device_control(device, status_enable);

	if (device->status_cmd)
		status_cmd_open(device);

	device->status_enabled = true;
}

void device_usb(struct device *device, bool on)
{
	if (device->ppps_path)
		ppps_power(device, on);
	else if (device_has_control(device, usb))
		device_control(device, usb, on);
}

int device_write(struct device *device, const void *buf, size_t len)
{
	if (!device)
		return 0;

	return device_console(device, write, buf, len);
}

void device_fastboot_open(struct device *device,
			  struct fastboot_ops *fastboot_ops)
{
	device->fastboot = fastboot_open(device->serial, fastboot_ops, NULL);
}

void device_fastboot_boot(struct device *device)
{
	if (!device->fastboot) {
		fprintf(stderr, "fastboot not opened\n");
		return;
	}
	fastboot_boot(device->fastboot);
}

void device_fastboot_continue(struct device *device)
{
	if (!device->fastboot) {
		fprintf(stderr, "fastboot not opened\n");
		return;
	}
	fastboot_continue(device->fastboot);
}

void device_fastboot_flash_reboot(struct device *device)
{
	if (!device->fastboot) {
		fprintf(stderr, "fastboot not opened\n");
		return;
	}
	fastboot_flash(device->fastboot, "boot");
	fastboot_reboot(device->fastboot);
}

void device_boot(struct device *device, const void *data, size_t len)
{
	warnx("booting the board...");
	if (device->set_active)
		fastboot_set_active(device->fastboot, device->set_active);
	fastboot_download(device->fastboot, data, len);
	device->boot(device);

	if (device->status_enabled && !device->usb_always_on) {
		warnx("disabling USB, use ^A V to enable");
		device_usb(device, false);
	}
}

void device_send_break(struct device *device)
{
	if (device_has_console(device, send_break))
		device_console(device, send_break);
}

void device_list_devices(const char *username)
{
	struct device *device;
	size_t len;
	char buf[80];

	list_for_each_entry(device, &devices, node) {
		if (!device_check_access(device, username))
			continue;

		if (device->name)
			len = snprintf(buf, sizeof(buf), "%-20s %s", device->board, device->name);
		else
			len = snprintf(buf, sizeof(buf), "%s", device->board);

		cdba_send_buf(MSG_LIST_DEVICES, len, buf);
	}

	cdba_send_buf(MSG_LIST_DEVICES, 0, NULL);
}

void device_info(const char *username, const void *data, size_t dlen)
{
	char *description = NULL;
	struct device *device;
	size_t len = 0;

	list_for_each_entry(device, &devices, node) {
		if (strncmp(device->board, data, dlen))
			continue;

		if (!device_check_access(device, username))
			continue;

		if (device->description) {
			description = device->description;
			len = strlen(device->description);
			break;
		}
	}

	cdba_send_buf(MSG_BOARD_INFO, len, description);
}

void device_close(struct device *dev)
{
	if (!dev->usb_always_on)
		device_usb(dev, false);
	if (!dev->power_always_on)
		device_power(dev, false);

	if (device_has_control(dev, close))
		device_control(dev, close);
}
