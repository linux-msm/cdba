#ifndef __CDB_ASSIST_H__
#define __CDB_ASSIST_H__

#include <stdbool.h>

struct cdb_assist;

struct cdb_assist *cdb_assist_open(const char *serial);
void cdb_assist_close(struct cdb_assist *cdb);

void cdb_power(struct cdb_assist *cdb, bool on);
void cdb_vbus(struct cdb_assist *cdb, bool on);
void cdb_gpio(struct cdb_assist *cdb, int gpio, bool on);
int cdb_target_write(struct cdb_assist *cdb, const void *buf, size_t len);
void cdb_target_break(struct cdb_assist *cdb);
unsigned int cdb_vref(struct cdb_assist *cdb);
void cdb_assist_print_status(struct cdb_assist *cdb);
void cdb_set_voltage(struct cdb_assist *cdb, unsigned mV);

#endif
