/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#define _GNU_SOURCE

#include <err.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ftdi.h>
#include <libusb.h>
#include <yaml.h>

#include "device.h"
#include "device_parser.h"

#define TOKEN_LENGTH 16384
#define PRE_RESET_DELAY_US 500000
#define POWER_CYCLE_DELAY_US 2000000

struct unoq_options {
	char *description;
	char *vendor;
	char *product;
	char *serial;
	unsigned int index;
	char *devicenode;
};

struct unoq {
	struct unoq_options *options;
	struct ftdi_context *ftdi;
	bool edl_asserted;
	bool direct_usb;
};

static void unoq_set_bitmode(struct unoq *unoq, unsigned char bitmask)
{
	int ret;

	/*
	 * SIO_SET_BITMODE uses one-based FTDI interface numbers, so
	 * interface A is addressed with wIndex 1.  Issue the request via
	 * endpoint zero instead of opening the interface with libftdi, as the
	 * UnoQ CBUS controls share the FTDI device with the serial console.
	 */
	ret = libusb_control_transfer(unoq->ftdi->usb_dev,
		LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		0x0b, bitmask | (BITMODE_CBUS << 8), 1, NULL, 0, 5000);
	if (ret < 0)
		errx(1, "failed to set UnoQ CBUS bitmode 0x%02x (%d)",
		     bitmask, ret);
}

static void unoq_power_on(struct unoq *unoq)
{
	/* CBUS2 drives the open-drain VBUS_POWER_SWITCH_DISABLE_N line.
	 * Keep all CBUS pins as outputs; LOW releases each target signal.
	 */
	unoq_set_bitmode(unoq, 0xf4);
	usleep(POWER_CYCLE_DELAY_US);
	unoq_set_bitmode(unoq, 0xf0);
}

static void unoq_boot_edl(struct unoq *unoq)
{
	unoq_set_bitmode(unoq, 0b01110100);
	usleep(PRE_RESET_DELAY_US);
	unoq_set_bitmode(unoq, 0b01110001);
}

static void unoq_power_off(struct unoq *unoq)
{
	unoq_set_bitmode(unoq, 0xf4);
}

void *unoq_parse_options(struct device_parser *dp)
{
	struct unoq_options *options;
	char value[TOKEN_LENGTH];
	char key[TOKEN_LENGTH];

	options = calloc(1, sizeof(*options));

	if (device_parser_accept(dp, YAML_SCALAR_EVENT, value, TOKEN_LENGTH)) {
		options->description = strdup(value);
		return options;
	}

	device_parser_accept(dp, YAML_MAPPING_START_EVENT, NULL, 0);

	while (device_parser_accept(dp, YAML_SCALAR_EVENT, key, TOKEN_LENGTH)) {
		if (!device_parser_accept(dp, YAML_SCALAR_EVENT, value, TOKEN_LENGTH))
			errx(1, "%s: expected value for \"%s\"", __func__, key);

		if (!strcmp(key, "vendor"))
			options->vendor = strdup(value);
		else if (!strcmp(key, "product"))
			options->product = strdup(value);
		else if (!strcmp(key, "index"))
			options->index = strtoul(value, NULL, 0);
		else if (!strcmp(key, "serial"))
			options->serial = strdup(value);
		else if (!strcmp(key, "devicenode"))
			options->devicenode = strdup(value);
		else
			errx(1, "%s: unknown option \"%s\"", __func__, key);
	}

	device_parser_expect(dp, YAML_MAPPING_END_EVENT, NULL, 0);

	return options;
}

static void *unoq_open(struct device *dev)
{
	struct unoq *unoq;
	int ret;

	unoq = calloc(1, sizeof(*unoq));
	unoq->options = dev->control_options;

	if (!unoq->options->description) {
		if (unoq->options->devicenode)
			asprintf(&unoq->options->description, "d:%s",
				 unoq->options->devicenode);
		else if (unoq->options->vendor &&
			 unoq->options->product &&
			 unoq->options->serial)
			asprintf(&unoq->options->description, "s:%s:%s:%s",
				 unoq->options->vendor,
				 unoq->options->product,
				 unoq->options->serial);
		else if (unoq->options->vendor &&
			 unoq->options->product &&
			 unoq->options->index > 0)
			asprintf(&unoq->options->description, "i:%s:%s:%u",
				 unoq->options->vendor,
				 unoq->options->product,
				 unoq->options->index);
		else if (unoq->options->vendor && unoq->options->product)
			asprintf(&unoq->options->description, "i:%s:%s",
				 unoq->options->vendor,
				 unoq->options->product);
		else
			errx(1, "Incomplete UnoQ FTDI description properties");
	}

	unoq->ftdi = ftdi_new();
	if (!unoq->ftdi)
		errx(1, "failed to allocate UnoQ FTDI context");

	/* Serial-selected Bughoppers only need endpoint zero. Avoid the UART
	 * reset, baud-rate setup and interface claim performed by libftdi open.
	 */
	if (unoq->options->vendor && unoq->options->product && unoq->options->serial) {
		struct ftdi_device_list *devices = NULL;
		struct ftdi_device_list *entry;
		char serial[256];

		ret = ftdi_usb_find_all(unoq->ftdi, &devices,
			strtoul(unoq->options->vendor, NULL, 0),
			strtoul(unoq->options->product, NULL, 0));
		if (ret < 0)
			errx(1, "failed to enumerate UnoQ USB devices (%d)", ret);
		ret = LIBUSB_ERROR_NO_DEVICE;
		for (entry = devices; entry; entry = entry->next) {
			if (ftdi_usb_get_strings(unoq->ftdi, entry->dev, NULL, 0,
					NULL, 0, serial, sizeof(serial)) < 0)
				continue;
			if (strcmp(serial, unoq->options->serial))
				continue;
			ret = libusb_open(entry->dev, &unoq->ftdi->usb_dev);
			break;
		}
		ftdi_list_free(&devices);
		unoq->direct_usb = true;
	} else {
		ret = ftdi_usb_open_string(unoq->ftdi, unoq->options->description);
	}
	if (ret < 0)
		errx(1, "failed to open UnoQ FTDI device '%s' (%d)",
		     unoq->options->description, ret);

	return unoq;
}

static void unoq_close(struct device *dev)
{
	struct unoq *unoq = dev->cdb;

	if (!unoq)
		return;

	if (unoq->ftdi) {
		if (unoq->direct_usb) {
			libusb_close(unoq->ftdi->usb_dev);
			unoq->ftdi->usb_dev = NULL;
		} else {
			ftdi_usb_close(unoq->ftdi);
		}
		ftdi_free(unoq->ftdi);
	}

	free(unoq);
}

static int unoq_power(struct device *dev, bool on)
{
	struct unoq *unoq = dev->cdb;

	if (on) {
		if (unoq->edl_asserted)
			unoq_boot_edl(unoq);
		else
			unoq_power_on(unoq);
	} else {
		unoq_power_off(unoq);
	}

	return 0;
}

static void unoq_key(struct device *dev, int key, bool asserted)
{
	struct unoq *unoq = dev->cdb;

	if (key == DEVICE_KEY_EDL)
		unoq->edl_asserted = asserted;
}

const struct control_ops unoq_ops = {
	.parse_options = unoq_parse_options,
	.open = unoq_open,
	.close = unoq_close,
	.power = unoq_power,
	.key = unoq_key,
};
