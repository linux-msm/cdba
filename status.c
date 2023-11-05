#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cdba-server.h"
#include "status.h"

static const char *sz_units[] = {
	[STATUS_MV] = "mv",
	[STATUS_MA] = "ma",
	[STATUS_GPIO] = "gpio",
};

static void status_get_ts(struct timespec *ts)
{
	static struct timespec t0;
	struct timespec t;

	if (!t0.tv_sec && !t0.tv_nsec)
		clock_gettime(CLOCK_MONOTONIC, &t0);

	clock_gettime(CLOCK_MONOTONIC, &t);

	if (t.tv_nsec < t0.tv_nsec) {
		ts->tv_sec = t.tv_sec - t0.tv_sec - 1;
		ts->tv_nsec = 1000000000 + (t.tv_nsec - t0.tv_nsec);
	} else {
		ts->tv_sec = t.tv_sec - t0.tv_sec;
		ts->tv_nsec = t.tv_nsec - t0.tv_nsec;
	}
}

void status_send_values(const char *id, struct status_value *values)
{
	struct status_value *value;
	struct timespec ts;
	char chunk[32];
	char buf[256];
	size_t len;
	size_t n;

	status_get_ts(&ts);

	len = snprintf(buf, sizeof(buf), "{\"ts\":%ld.%03ld, \"%s\":{ ", ts.tv_sec, ts.tv_nsec / 1000000, id);

	for (value = values; value->unit; value++) {
		if (value != values) {
			if (len + 3 >= sizeof(buf)) {
				warnx("status message overflow");
				return;
			}

			strcpy(buf + len, ", ");
			len += 2;
		}

		n = snprintf(chunk, sizeof(chunk), "\"%s\": %u", sz_units[value->unit], value->value);

		if (len + n + 1>= sizeof(buf)) {
			warnx("status message overflow");
			return;
		}

		strcpy(buf + len, chunk);
		len += n;
	}

	if (len + 4 >= sizeof(buf)) {
		warnx("status message overflow");
		return;
	}

	strcpy(buf + len, "}}\n");
	len += 3;

	cdba_send_buf(MSG_STATUS_UPDATE, len, buf);
}
