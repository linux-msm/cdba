#ifndef __CDBA_STANDALONE_H__
#define __CDBA_STANDALONE_H__

#include <stdbool.h>

#include "cdba-client.h"
#include "device.h"

enum cdba_verbs {
	CDBA_CONSOLE = MAX_CDBA_CLIENT_VERBS,
	CDBA_POWER_ON,
	CDBA_POWER_OFF,
};

void cdba_server_standalone_device_power(struct device *device, bool on);

#endif // __CDBA_STANDALONE_H__
