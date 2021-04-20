#ifndef __CDBA_SERVER_STANDALONE_H__
#define __CDBA_SERVER_STANDALONE_H__

#include "device.h"

int cdba_server_standalone_create(struct device *device);
int cdba_server_standalone_end(struct device *device);

void cdba_server_standalone_device_power(struct device *device, bool on);

#endif
