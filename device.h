#ifndef __DEVICE_H__
#define __DEVICE_H__

struct device;
struct cdb_assist;
struct fastboot;
struct fastboot_ops;

struct device *device_open(const char *board, struct fastboot_ops *fastboot_ops);
int device_power_on(struct device *device);
int device_power_off(struct device *device);

int device_enter_fastboot(struct device *device);

void device_print_status(struct device *device);
void device_vbus(struct device *device, bool enable);
int device_write(struct device *device, const void *buf, size_t len);
void device_break(struct device *device);

void device_set_bootimg(struct device *device, int fd);
const char *device_get_serial(struct device *device);
void device_trigger_fastboot(struct device *device, bool enable);
void device_boot(struct device *device, const void *data, size_t len);

#endif
