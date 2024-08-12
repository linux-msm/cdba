#ifndef __DEVICE_H__
#define __DEVICE_H__

#include <termios.h>
#include "list.h"

struct cdb_assist;
struct fastboot;
struct fastboot_ops;
struct device;
struct device_parser;

struct control_ops {
	void *(*parse_options)(struct device_parser *dp);
	void *(*open)(struct device *dev);
	void (*close)(struct device *dev);
	int (*power)(struct device *dev, bool on);
	void (*usb)(struct device *dev, bool on);
	void (*key)(struct device *device, int key, bool asserted);
	void (*status_enable)(struct device *dev);
};

struct console_ops {
	void *(*open)(struct device *dev);
	int (*write)(struct device *dev, const void *buf, size_t len);

	void (*send_break)(struct device *dev);
};

struct device {
	char *board;
	char *control_dev;
	void *control_options;
	char *console_dev;
	char *name;
	char *serial;
	char *description;
	char *ppps_path;
	char *ppps3_path;
	struct list_head *users;
	unsigned voltage;
	bool tickle_mmc;
	bool usb_always_on;
	bool power_always_on;

	char *video_device;

	struct fastboot *fastboot;
	unsigned int fastboot_key_timeout;
	int state;
	bool has_power_key;

	bool status_enabled;

	void (*boot)(struct device *);

	const struct control_ops *control_ops;
	const struct console_ops *console_ops;

	const char *set_active;

	void *cdb;
	void *console;

	char *status_cmd;

	struct list_head node;
};

struct device_user {
	const char *username;

	struct list_head node;
};

void device_add(struct device *device);

struct device *device_open(const char *board,
			   const char *username);
void device_close(struct device *dev);
int device_power(struct device *device, bool on);

void device_status_enable(struct device *device);
void device_usb(struct device *device, bool on);
int device_write(struct device *device, const void *buf, size_t len);

void device_boot(struct device *device, const void *data, size_t len);

void device_fastboot_open(struct device *device,
			  struct fastboot_ops *fastboot_ops);
void device_fastboot_boot(struct device *device);
void device_fastboot_flash_reboot(struct device *device);
void device_send_break(struct device *device);
void device_list_devices(const char *username);
void device_info(const char *username, const void *data, size_t dlen);
void device_fastboot_continue(struct device *device);
bool device_is_running(struct device *device);

enum {
	DEVICE_KEY_FASTBOOT,
	DEVICE_KEY_POWER,
};

extern const struct control_ops alpaca_ops;
extern const struct control_ops cdb_assist_ops;
extern const struct control_ops conmux_ops;
extern const struct control_ops ftdi_gpio_ops;
extern const struct control_ops local_gpio_ops;
extern const struct control_ops external_ops;
extern const struct control_ops qcomlt_dbg_ops;
extern const struct control_ops laurent_ops;

extern const struct console_ops conmux_console_ops;
extern const struct console_ops console_ops;

#endif
