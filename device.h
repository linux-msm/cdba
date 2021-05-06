#ifndef __DEVICE_H__
#define __DEVICE_H__

#include <termios.h>
#include "list.h"

struct cdb_assist;
struct fastboot;
struct fastboot_ops;

struct device {
	char *board;
	char *control_dev;
	char *console_dev;
	char *name;
	char *serial;
	char *description;
	unsigned voltage;
	bool tickle_mmc;
	bool usb_always_on;
	struct fastboot *fastboot;
	unsigned int fastboot_key_timeout;
	int state;
	bool has_power_key;
	int standalone;
	int locked;

	void (*boot)(struct device *);

	void *(*open)(struct device *dev);
	void (*close)(struct device *dev);
	int (*power)(struct device *dev, bool on);
	void (*usb)(struct device *dev, bool on);
	void (*print_status)(struct device *dev);
	int (*write)(struct device *dev, const void *buf, size_t len);
	void (*fastboot_key)(struct device *dev, bool on);
	void (*key)(struct device *device, int key, bool asserted);

	void (*send_break)(struct device *dev);
	bool set_active;

	void *cdb;

	int console_fd;
	struct termios console_tios;

	int standalone_channel;

	struct list_head node;
};

void device_add(struct device *device);

struct device *device_open(const void *msg, struct fastboot_ops *fastboot_ops);
void device_close(struct device *dev);
int device_power(struct device *device, bool on);

void device_print_status(struct device *device);
void device_usb(struct device *device, bool on);
int device_write(struct device *device, const void *buf, size_t len);

void device_boot(struct device *device, const void *data, size_t len);

void device_fastboot_boot(struct device *device);
void device_fastboot_flash_reboot(struct device *device);
void device_send_break(struct device *device);
void device_list_devices(void);
void device_info(const void *data, size_t dlen);

enum {
	DEVICE_KEY_FASTBOOT,
	DEVICE_KEY_POWER,
};

#endif
