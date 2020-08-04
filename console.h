#ifndef __CONSOLE_H__
#define __CONSOLE_H__

#include "device.h"

void console_open(struct device *device);
int console_write(struct device *device, const void *buf, size_t len);

#endif
