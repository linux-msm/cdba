// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
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
#include <strings.h>
#include <unistd.h>

#include "edl.h"
#include "watch.h"

#define MAX_USBFS_BULK_SIZE (16*1024)

enum {
	EDL_STATE_START,
	EDL_STATE_OPENED,
	EDL_STATE_CLOSED,
};

struct edl {
	const char *serial;
	const char *dev_path;

	void *data;

	struct edl_ops *ops;
	void (*present)(bool present);

	int state;

	struct udev_monitor *mon;
};

static int parse_usb_desc(int usbfd)
{
	const struct usb_interface_descriptor *ifc;
	const struct usb_endpoint_descriptor *ept;
	const struct usb_device_descriptor *dev;
	const struct usb_config_descriptor *cfg;
	const struct usb_descriptor_header *hdr;
	unsigned type;
	unsigned k;
	unsigned l;
	ssize_t n;
	char *ptr;
	char *end;
	char desc[1024];

	n = read(usbfd, desc, sizeof(desc));
	if (n < 0)
		return n;

	ptr = desc;
	end = ptr + n;

	dev = (void *)ptr;
	ptr += dev->bLength;
	if (ptr >= end || dev->bDescriptorType != USB_DT_DEVICE)
		return -EINVAL;

	if (dev->idVendor != 0x05c6)
		return -ENOENT;
	if (dev->idProduct != 0x9008)
		return -ENOENT;

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

			if (ptr >= end)
				break;

			hdr = (void *)ptr;
			if (hdr->bDescriptorType == USB_DT_SS_ENDPOINT_COMP)
				ptr += USB_DT_SS_EP_COMP_SIZE;
		}

		if (ifc->bInterfaceClass != 0xff)
			continue;

		if (ifc->bInterfaceSubClass != 0xff)
			continue;

		if (ifc->bInterfaceProtocol != 0xff &&
		    ifc->bInterfaceProtocol != 16 &&
		    ifc->bInterfaceProtocol != 17)
			continue;

		return 0;
	}

	return -ENOENT;
}

/* iProduct carries "..._SN:<serial>", as matched by qdl --serial */
static bool edl_serial_match(struct edl *edl, struct udev_device *dev)
{
	const char *product;
	const char *sn;
	size_t len;

	if (!edl->serial)
		return true;

	product = udev_device_get_sysattr_value(dev, "product");
	if (!product)
		return false;

	sn = strstr(product, "_SN:");
	if (!sn)
		return false;

	sn += strlen("_SN:");
	len = strcspn(sn, "_");

	return len == strlen(edl->serial) && !strncasecmp(sn, edl->serial, len);
}

static int handle_edl_add(struct edl *edl, struct udev_device *dev)
{
	const char *dev_path;
	const char *dev_node;
	int usbfd;
	int ret;

	dev_path = udev_device_get_devpath(dev);
	dev_node = udev_device_get_devnode(dev);
	if (!dev_node)
		return -ENOENT;

	if (!edl_serial_match(edl, dev))
		return -ENOENT;

	usbfd = open(dev_node, O_RDWR);
	if (usbfd < 0)
		return usbfd;

	ret = parse_usb_desc(usbfd);
	if (ret < 0) {
		close(usbfd);
		return ret;
	}

	edl->dev_path = strdup(dev_path);

	if (edl->present)
		edl->present(true);

	return 0;
}

static int handle_udev_event(int fd, void *data)
{
	struct edl *edl = data;
	struct udev_device* dev;
	const char *dev_path;
	const char *action;

	dev = udev_monitor_receive_device(edl->mon);

	action = udev_device_get_action(dev);
	dev_path = udev_device_get_devpath(dev);

	if (!action || !dev_path)
		goto unref_dev;

	if (!strcmp(action, "add")) {
		handle_edl_add(edl, dev);
	} else if (!strcmp(action, "remove")) {
		if (!edl->dev_path || strcmp(dev_path, edl->dev_path))
			goto unref_dev;

		edl->dev_path = NULL;

		if (edl->present)
			edl->present(false);
	}

unref_dev:
	udev_device_unref(dev);

	return 0;
}

struct edl *edl_open(const char *serial, void (*present)(bool present))
{
	struct edl *edl;
	struct udev* udev;
	int fd;
	struct udev_enumerate* udev_enum;
	struct udev_list_entry* first, *item;

	udev = udev_new();
	if (!udev)
		err(1, "udev_new() failed");

	edl = calloc(1, sizeof(struct edl));
	if (!edl)
		err(1, "failed to allocate edl structure");

	edl->serial = serial;
	edl->present = present;

	edl->mon = udev_monitor_new_from_netlink(udev, "udev");
	udev_monitor_filter_add_match_subsystem_devtype(edl->mon, "usb", NULL);
	udev_monitor_enable_receiving(edl->mon);

	fd = udev_monitor_get_fd(edl->mon);

	watch_add_readfd(fd, handle_udev_event, edl);

	udev_enum = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(udev_enum, "usb");
	udev_enumerate_scan_devices(udev_enum);

	first = udev_enumerate_get_list_entry(udev_enum);
	udev_list_entry_foreach(item, first) {
		const char *path;
		struct udev_device *dev;

		path = udev_list_entry_get_name(item);
		dev = udev_device_new_from_syspath(udev, path);
		handle_edl_add(edl, dev);
	}

	udev_enumerate_unref(udev_enum);

	return edl;
}
