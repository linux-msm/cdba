#ifndef __SBC_DBG_H__
#define __SBC_DBG_H__

#include "device.h"

void *sbc_dbg_open(struct device *dev);
void sbc_dbg_close(struct device *dev);
int sbc_dbg_power(struct device *dev, bool on);
void sbc_dbg_usb(struct device *dev, bool on);
void sbc_dbg_key(struct device *dev, int key, bool asserted);

#endif
