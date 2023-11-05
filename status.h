#ifndef __STATUS_H__
#define __STATUS_H__

#include <stdlib.h>

enum status_unit {
	STATUS_EOF,
	STATUS_MV,
	STATUS_MA,
	STATUS_GPIO,
};

struct status_value {
	enum status_unit unit;
	unsigned int value;
};

void status_send_values(const char *id, struct status_value *values);

#endif
