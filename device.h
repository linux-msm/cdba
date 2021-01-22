#ifndef __DEVICE_H__
#define __DEVICE_H__

#include <termios.h>
#include "list.h"

struct cdb_assist;
struct fastboot;
struct fastboot_ops;

struct device {
	char *board;
	char *cdb_serial;
	char *alpaca_dev;
	char *console_dev;
	char *name;
	char *serial;
	char *description;
	unsigned voltage;
	bool tickle_mmc;
	bool pshold_shutdown;
	bool usb_always_on;
	struct fastboot *fastboot;
	unsigned int fastboot_key_timeout;

	void (*boot)(struct device *);

	void *(*open)(struct device *dev);
	int (*power_on)(struct device *dev);
	int (*power_off)(struct device *dev);
	void (*print_status)(struct device *dev);
	void (*vbus)(struct device *dev, bool on);
	int (*write)(struct device *dev, const void *buf, size_t len);
	void (*fastboot_key)(struct device *dev, bool on);
	void (*send_break)(struct device *dev);
	bool set_active;

	void *cdb;

	int console_fd;
	struct termios console_tios;

	struct list_head node;
};

void device_add(struct device *device);

struct device *device_open(const char *board, struct fastboot_ops *fastboot_ops);
int device_power_on(struct device *device);
int device_power_off(struct device *device);

void device_print_status(struct device *device);
void device_vbus(struct device *device, bool enable);
int device_write(struct device *device, const void *buf, size_t len);

void device_boot(struct device *device, const void *data, size_t len);

void device_fastboot_boot(struct device *device);
void device_fastboot_flash_reboot(struct device *device);
void device_send_break(struct device *device);
void device_list_devices(void);
void device_info(const void *data, size_t dlen);

#endif
