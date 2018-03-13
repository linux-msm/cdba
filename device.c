#include <sys/stat.h>

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "device.h"
#include "fastboot.h"
#include "list.h"

#define ARRAY_SIZE(x) ((sizeof(x)/sizeof((x)[0])))

static struct list_head devices = LIST_INIT(devices);

void device_add(struct device *device)
{
	list_add(&devices, &device->node);
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
	if (device->set_active)
		fastboot_set_active(device->fastboot, "a");
	fastboot_download(device->fastboot, data, len);
	device->boot(device);
}
