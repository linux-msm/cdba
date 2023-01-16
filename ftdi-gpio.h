#ifndef __FTDI_GPIO_H__
#define __FTDI_GPIO_H__

#include "device.h"

struct ftdi_gpio;

void *ftdi_gpio_open(struct device *dev);
int ftdi_gpio_power(struct device *dev, bool on);
void ftdi_gpio_usb(struct device *dev, bool on);
void ftdi_gpio_key(struct device *dev, int key, bool on);

#endif
