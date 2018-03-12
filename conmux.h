#ifndef __CONMUX_H__
#define __CONMUX_H__

#include "device.h"

struct conmux;

void *conmux_open(struct device *dev);
int conmux_power_on(struct device *dev);
int conmux_power_off(struct device *dev);
int conmux_write(struct device *dev, const void *buf, size_t len);

#endif
