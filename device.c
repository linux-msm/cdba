#include <sys/stat.h>

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cdb_assist.h"
#include "conmux.h"
#include "device.h"
#include "fastboot.h"

#define ARRAY_SIZE(x) ((sizeof(x)/sizeof((x)[0])))

static void device_fastboot_boot(struct device *device);
static void device_fastboot_flash_reboot(struct device *device);

static struct device devices[] = {
};

struct device *device_open(const char *board,
			   struct fastboot_ops *fastboot_ops)
{
	struct device *device = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(devices); i++) {
		if (strcmp(devices[i].board, board) == 0) {
			device = &devices[i];
			break;
		}
	}

	if (!device)
		return NULL;

	assert(device->open);

	device->cdb = device->open(device);
	if (!device->cdb)
		errx(1, "failed to open device controller");

	device->fastboot = fastboot_open(device->serial, fastboot_ops, NULL);

	return device;
}

int device_power_on(struct device *device)
{
	if (!device)
		return 0;

	assert(device->power_on);

	device->power_on(device);

	return 0;
}

int device_power_off(struct device *device)
{
	if (!device)
		return 0;

	assert(device->power_off);

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

static void device_fastboot_boot(struct device *device)
{
	fastboot_boot(device->fastboot);
}

static void device_fastboot_flash_reboot(struct device *device)
{
	fastboot_flash(device->fastboot, "boot");
	fastboot_reboot(device->fastboot);
}

void device_boot(struct device *device, const void *data, size_t len)
{
	if (device->set_active)
		fastboot_set_active(device->fastboot, "a");
	fastboot_download(device->fastboot, data, len);
	device->boot(device);
}
