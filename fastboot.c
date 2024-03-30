/*
 * Copyright (c) 2016-2018, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <linux/usbdevice_fs.h>
#include <linux/usb/ch9.h>

#include <sys/ioctl.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libudev.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cdba-server.h"
#include "fastboot.h"
#include "watch.h"

#define MAX_USBFS_BULK_SIZE (16*1024)

struct fastboot {
	const char *serial;

	int fd;
	unsigned ep_in;
	unsigned ep_out;

	const char *dev_path;

	void *data;

	struct fastboot_ops *ops;

	int state;

	struct udev_monitor *mon;
};

enum {
	FASTBOOT_STATE_START,
	FASTBOOT_STATE_OPENED,
	FASTBOOT_STATE_CLOSED,
};

static int fastboot_read(struct fastboot *fb, char *buf, size_t len)
{
	struct usbdevfs_bulktransfer bulk = {0};
	char status[65];
	int n;

	for (;;) {
		bulk.ep = fb->ep_in;
		bulk.len = 64;
		bulk.data = status;
		bulk.timeout = 1000;

		n = ioctl(fb->fd, USBDEVFS_BULK, &bulk);
		if (n < 0) {
			warn("failed to receive usb bulk transfer");
			return -ENXIO;
		}

		status[n] = '\0';

		if (n < 4) {
			warn("malformed response from fastboot");
			return -1;
		}

		if (strncmp(status, "INFO", 4) == 0) {
			fb->ops->info(fb, status + 4, n - 4);
		} else if (strncmp(status, "OKAY", 4) == 0) {
			if (buf) {
				strncpy(buf, status + 4, len);
				buf[len - 1] = '\0';
			}
			return n - 4;
		} else if (strncmp(status, "FAIL", 4) == 0) {
			fprintf(stderr, "%s\n", status + 4);
			return -ENXIO;
		} else if (strncmp(status, "DATA", 4) == 0) {
			return strtol(status + 4, NULL, 16);
		}
	}

	return 0;
}

static int fastboot_write(struct fastboot *fb, const void *data, size_t len)
{
	struct usbdevfs_bulktransfer bulk = {0};
	size_t count = 0;
	char *buf = (char *)data;
	int n;

	do {
		bulk.ep = fb->ep_out;
		bulk.len = MIN(len, MAX_USBFS_BULK_SIZE);
		bulk.data = buf;
		bulk.timeout = 1000;

		n = ioctl(fb->fd, USBDEVFS_BULK, &bulk);
		if (n < 0) {
			warn("failed to send usb bulk transfer");
			return -1;
		}

		buf += n;
		len -= n;
		count += n;
	} while (len > 0);

	return count;
}

static int parse_usb_desc(int usbfd, unsigned *ep_in, unsigned *ep_out)
{
	const struct usb_interface_descriptor *ifc;
	const struct usb_endpoint_descriptor *ept;
	const struct usb_device_descriptor *dev;
	const struct usb_config_descriptor *cfg;
	const struct usb_descriptor_header *hdr;
	unsigned type;
	unsigned out;
	unsigned in;
	unsigned k;
	unsigned l;
	ssize_t n;
	char *ptr;
	char *end;
	char desc[1024];
	int ret;
	int id;

	n = read(usbfd, desc, sizeof(desc));
	if (n < 0)
		return n;

	ptr = desc;
	end = ptr + n;

	dev = (void *)ptr;
	ptr += dev->bLength;
	if (ptr >= end || dev->bDescriptorType != USB_DT_DEVICE)
		return -EINVAL;

	cfg = (void *)ptr;
	ptr += cfg->bLength;
	if (ptr >= end || cfg->bDescriptorType != USB_DT_CONFIG)
		return -EINVAL;

	for (k = 0; k < cfg->bNumInterfaces; k++) {
		if (ptr >= end)
			return -EINVAL;

		do {
			ifc = (void *)ptr;
			if (ifc->bLength < USB_DT_INTERFACE_SIZE)
				return -EINVAL;

			ptr += ifc->bLength;
		} while (ptr < end && ifc->bDescriptorType != USB_DT_INTERFACE);

		in = -1;
		out = -1;

		for (l = 0; l < ifc->bNumEndpoints; l++) {
			if (ptr >= end)
				return -EINVAL;

			do {
				ept = (void *)ptr;
				if (ept->bLength < USB_DT_ENDPOINT_SIZE)
					return -EINVAL;

				ptr += ept->bLength;
			} while (ptr < end && ept->bDescriptorType != USB_DT_ENDPOINT);

			type = ept->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
			if (type != USB_ENDPOINT_XFER_BULK)
				continue;

			if (ept->bEndpointAddress & USB_DIR_IN)
				in = ept->bEndpointAddress;
			else
				out = ept->bEndpointAddress;

			if (ptr >= end)
				break;

			hdr = (void *)ptr;
			if (hdr->bDescriptorType == USB_DT_SS_ENDPOINT_COMP)
				ptr += USB_DT_SS_EP_COMP_SIZE;
		}

		if (ifc->bInterfaceClass != 0xff)
			continue;

		if (ifc->bInterfaceSubClass != 0x42)
			continue;

		if (ifc->bInterfaceProtocol != 0x03)
			continue;

		id = ifc->bInterfaceNumber;
		ret = ioctl(usbfd, USBDEVFS_CLAIMINTERFACE, &id);
		if (ret < 0) {
			warn("failed to claim interface");
			continue;
		}

		*ep_in = in;
		*ep_out = out;

		return 0;
	}

	return -ENOENT;
}

static int handle_fastboot_add(struct fastboot *fastboot, struct udev_device *dev)
{
	const char *dev_path;
	const char *dev_node;
	unsigned ep_out;
	unsigned ep_in;
	int usbfd;
	int ret;

	dev_path = udev_device_get_devpath(dev);
	dev_node = udev_device_get_devnode(dev);

	usbfd = open(dev_node, O_RDWR);
	if (usbfd < 0)
		return usbfd;

	ret = parse_usb_desc(usbfd, &ep_in, &ep_out);
	if (ret < 0) {
		close(usbfd);
		return ret;
	}

	fastboot->ep_in = ep_in;
	fastboot->ep_out = ep_out;
	fastboot->fd = usbfd;
	fastboot->dev_path = strdup(dev_path);

	fastboot->state = FASTBOOT_STATE_OPENED;

	if (fastboot->ops && fastboot->ops->opened)
		fastboot->ops->opened(fastboot, fastboot->data);

	return 0;
}

static int handle_udev_event(int fd, void *data)
{
	struct fastboot *fastboot = data;
	struct udev_device* dev;
	const char *dev_path;
	const char *action;
	const char *serial;

	dev = udev_monitor_receive_device(fastboot->mon);

	action = udev_device_get_action(dev);
	dev_path = udev_device_get_devpath(dev);

	if (!action || !dev_path)
		goto unref_dev;

	if (!strcmp(action, "add")) {
		serial = udev_device_get_sysattr_value(dev, "serial");
		if (!serial || strcmp(serial, fastboot->serial))
			goto unref_dev;

		handle_fastboot_add(fastboot, dev);
	} else if (!strcmp(action, "remove")) {
		if (!fastboot->dev_path || strcmp(dev_path, fastboot->dev_path))
			goto unref_dev;

		close(fastboot->fd);
		fastboot->fd = -1;
		fastboot->dev_path = NULL;

		if (fastboot->ops && fastboot->ops->disconnect)
			fastboot->ops->disconnect(fastboot->data);

		fastboot->state = FASTBOOT_STATE_CLOSED;
	}

unref_dev:
	udev_device_unref(dev);

	return 0;
}

struct fastboot *fastboot_open(const char *serial, struct fastboot_ops *ops, void *data)
{
	struct fastboot *fb;
	struct udev* udev;
	int fd;
	struct udev_enumerate* udev_enum;
	struct udev_list_entry* first, *item;

	udev = udev_new();
	if (!udev)
		err(1, "udev_new() failed");

	fb = calloc(1, sizeof(struct fastboot));
	if (!fb)
		err(1, "failed to allocate fastboot structure");

	fb->serial = serial;
	fb->ops = ops;
	fb->data = data;

	fb->state = FASTBOOT_STATE_START;

	fb->mon = udev_monitor_new_from_netlink(udev, "udev");
	udev_monitor_filter_add_match_subsystem_devtype(fb->mon, "usb", NULL);
	udev_monitor_enable_receiving(fb->mon);

	fd = udev_monitor_get_fd(fb->mon);

	watch_add_readfd(fd, handle_udev_event, fb);

	udev_enum = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(udev_enum, "usb");
	udev_enumerate_add_match_sysattr(udev_enum, "serial", serial);
	udev_enumerate_scan_devices(udev_enum);

	first = udev_enumerate_get_list_entry(udev_enum);
	udev_list_entry_foreach(item, first) {
		const char *path;
		struct udev_device *dev;

		path = udev_list_entry_get_name(item);
		dev = udev_device_new_from_syspath(udev, path);
		handle_fastboot_add(fb, dev);
	}

	udev_enumerate_unref(udev_enum);

	return fb;
}

int fastboot_getvar(struct fastboot *fb, const char *var, char *buf, size_t len)
{
	char cmd[128];
	int n;

	n = snprintf(cmd, sizeof(cmd), "getvar:%s", var);
	fastboot_write(fb, cmd, n);

	return fastboot_read(fb, buf, len);
}

int fastboot_download(struct fastboot *fb, const void *data, size_t len)
{
	size_t xfer;
	ssize_t n;
	size_t offset = 0;
	void *buf;
	char cmd[32];
	int ret = 0;

	buf = malloc(MAX_USBFS_BULK_SIZE);
	if (!buf)
		err(1, "failed to allocate usb scratch buffer");

	n = sprintf(cmd, "download:%08x", (unsigned int)len);
	fastboot_write(fb, cmd, n);

	n = fastboot_read(fb, buf, MAX_USBFS_BULK_SIZE);
	if (n < 0) {
		fprintf(stderr, "remote rejected download request\n");
		return -1;
	}

	while (len > 0) {
		xfer = MIN(len, MAX_USBFS_BULK_SIZE);

		ret = fastboot_write(fb, (const char *)data + offset, xfer);
		if (ret < 0)
			goto out;

		offset += xfer;
		len -= xfer;
	}

	ret = fastboot_read(fb, NULL, 0);

out:
	free(buf);
	return ret;
}

int fastboot_boot(struct fastboot *fb)
{
	char buf[80];
	int n;

	fastboot_write(fb, "boot", 4);

	n = fastboot_read(fb, buf, sizeof(buf));
	if (n >= 0)
		fprintf(stderr, "%s\n", buf);

	return 0;
}

int fastboot_erase(struct fastboot *fb, const char *partition)
{
	char buf[80];
	int n;

	n = sprintf(buf, "erase:%s", partition);
	fastboot_write(fb, buf, n);

	fastboot_read(fb, buf, sizeof(buf));

	return 0;
}

int fastboot_set_active(struct fastboot *fb, const char *active)
{
	char buf[80];
	int n;

	n = sprintf(buf, "set_active:%s", active);
	fastboot_write(fb, buf, n);

	fastboot_read(fb, buf, sizeof(buf));

	return 0;
}

int fastboot_flash(struct fastboot *fb, const char *partition)
{
	char buf[80];
	int n;

	n = sprintf(buf, "flash:%s", partition);
	fastboot_write(fb, buf, n);

	fastboot_read(fb, buf, sizeof(buf));

	return 0;
}

int fastboot_reboot(struct fastboot *fb)
{
	char buf[80];

	fastboot_write(fb, "reboot", 6);

	fastboot_read(fb, buf, sizeof(buf));

	return 0;
}

int fastboot_continue(struct fastboot *fb)
{
	char buf[80];
	int n;

	fastboot_write(fb, "continue", 8);

	n = fastboot_read(fb, buf, sizeof(buf));
	if (n >= 0)
		fprintf(stderr, "%s\n", buf);

	return 0;
}
