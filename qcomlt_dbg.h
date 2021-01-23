#ifndef __QCOMLT_DBG_H__
#define __QCOMLT_DBG_H__

#include "device.h"

void *qcomlt_dbg_open(struct device *dev);
int qcomlt_dbg_power(struct device *dev, bool on);
void qcomlt_dbg_usb(struct device *dev, bool on);
void qcomlt_dbg_key(struct device *dev, int key, bool asserted);

#endif
