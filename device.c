#include <sys/stat.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cdb_assist.h"
#include "device.h"
#include "fastboot.h"

#define ARRAY_SIZE(x) ((sizeof(x)/sizeof((x)[0])))

static void device_fastboot_boot(struct device *device);
static void device_fastboot_flash_reboot(struct device *device);

struct device {
	char *board;
	char *cdb_serial;
	char *name;
	char *serial;
	unsigned voltage;
	bool tickle_mmc;
	bool pshold_shutdown;
	struct fastboot *fastboot;

	void (*boot)(struct device *);

	struct cdb_assist *cdb;
};

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

	device->cdb = cdb_assist_open(device->cdb_serial);
	if (!device->cdb)
		errx(1, "failed to open cdb assist");

	cdb_set_voltage(device->cdb, device->voltage);

	device->fastboot = fastboot_open(device->serial, fastboot_ops, NULL);

	return device;
}

int device_power_on(struct device *device)
{
	if (!device)
		return 0;

	cdb_power(device->cdb, true);
	cdb_gpio(device->cdb, 0, true);
	usleep(500000);
	cdb_gpio(device->cdb, 0, false);

	return 0;
}

int device_power_off(struct device *device)
{
	if (!device)
		return 0;

	cdb_vbus(device->cdb, false);
	cdb_power(device->cdb, false);

	if (device->pshold_shutdown) {
		cdb_gpio(device->cdb, 2, true);
		sleep(2);
		cdb_gpio(device->cdb, 2, false);
	}

	return 0;
}

void device_print_status(struct device *device)
{
	cdb_assist_print_status(device->cdb);
}

void device_vbus(struct device *device, bool enable)
{
	cdb_vbus(device->cdb, enable);
}

int device_write(struct device *device, const void *buf, size_t len)
{
	if (!device)
		return 0;

	return cdb_target_write(device->cdb, buf, len);
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
	fastboot_download(device->fastboot, data, len);
	device->boot(device);
}
