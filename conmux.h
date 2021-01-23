#ifndef __CONMUX_H__
#define __CONMUX_H__

#include "device.h"

struct conmux;

void *conmux_open(struct device *dev);
int conmux_power(struct device *dev, bool on);
int conmux_write(struct device *dev, const void *buf, size_t len);

#endif
