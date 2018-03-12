#ifndef __CDB_ASSIST_H__
#define __CDB_ASSIST_H__

#include <stdbool.h>

#include "device.h"

struct cdb_assist;

void *cdb_assist_open(struct device *dev);
void cdb_assist_close(struct cdb_assist *cdb);

int cdb_assist_power_on(struct device *dev);
int cdb_assist_power_off(struct device *dev);
void cdb_assist_vbus(struct device *dev, bool on);
void cdb_gpio(struct cdb_assist *cdb, int gpio, bool on);
int cdb_target_write(struct device *dev, const void *buf, size_t len);
void cdb_target_break(struct cdb_assist *cdb);
unsigned int cdb_vref(struct cdb_assist *cdb);
void cdb_assist_print_status(struct device *dev);
void cdb_set_voltage(struct cdb_assist *cdb, unsigned mV);

#endif
