#ifndef __ALPACA_H__
#define __ALPACA_H__

#include "device.h"

struct alpaca;

void *alpaca_open(struct device *dev);
int alpaca_power(struct device *dev, bool on);
void alpaca_fastboot_key(struct device *dev, bool on);

#endif
