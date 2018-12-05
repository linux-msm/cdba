#ifndef __ALPACA_H__
#define __ALPACA_H__

#include "device.h"

struct alpaca;

void *alpaca_open(struct device *dev);
int alpaca_power_on(struct device *dev);
int alpaca_power_off(struct device *dev);
int alpaca_write(struct device *dev, const void *buf, size_t len);

#endif
