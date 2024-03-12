/*
 * Copyright (c) 2021, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "cdba-server.h"
#include "device.h"
#include "status.h"
#include "tty.h"
#include "watch.h"

enum qcomlt_parse_state {
	STATE_,
	STATE_num,
	STATE_num_m,
	STATE_num_mV,
	STATE_num_mV_num,
	STATE_num_mV_num_m,
	STATE_err,
};

struct qcomlt_dbg {
	int fd;
	struct termios orig_tios;

	enum qcomlt_parse_state parse_state;
	unsigned long mv;
	unsigned long ma;
};

static void *qcomlt_dbg_open(struct device *dev)
{
	struct qcomlt_dbg *dbg;

	dev->has_power_key = true;

	dbg = calloc(1, sizeof(*dbg));

	dbg->fd = tty_open(dev->control_dev, &dbg->orig_tios);
	if (dbg->fd < 0)
		err(1, "failed to open %s", dev->control_dev);

	// fprintf(stderr, "qcomlt_dbg_open()\n");
	write(dbg->fd, "brpu", 4);

	return dbg;
}

static int qcomlt_dbg_power(struct device *dev, bool on)
{
	struct qcomlt_dbg *dbg = dev->cdb;

	// fprintf(stderr, "qcomlt_dbg_power(%d)\n", on);
	return write(dbg->fd, &("pP"[on]), 1);
}

static void qcomlt_dbg_usb(struct device *dev, bool on)
{
	struct qcomlt_dbg *dbg = dev->cdb;

	// fprintf(stderr, "qcomlt_dbg_usb(%d)\n", on);
	write(dbg->fd, &("uU"[on]), 1);
}

static void qcomlt_dbg_key(struct device *dev, int key, bool asserted)
{
	struct qcomlt_dbg *dbg = dev->cdb;

	// fprintf(stderr, "qcomlt_dbg_key(%d, %d)\n", key, asserted);

	switch (key) {
	case DEVICE_KEY_FASTBOOT:
		write(dbg->fd, &("rR"[asserted]), 1);
		break;
	case DEVICE_KEY_POWER:
		write(dbg->fd, &("bB"[asserted]), 1);
		break;
	}
}

static int qcomlt_dbg_ctrl_data(int fd, void *data)
{
	struct qcomlt_dbg *dbg = data;
	struct status_value dc[] = {
		{
			.unit = STATUS_MV,
		},
		{
			.unit = STATUS_MA,
		},
		{}
	};
	char buf[64];
	ssize_t i;
	ssize_t n;
	char ch;

	n = read(fd, buf, sizeof(buf));
	if (n < 0)
		return n;

	for (i = 0; i < n; i++) {
		ch = buf[i];

                /*
		 * The control data consists of a stream in the format:
		 *   <number>mV <number>mA
		 *
		 * The stream might be split in arbitrary ways across reads, so
		 * a parser is used instead of sscanf().
		 * In the initial state any non-digits are ignored, if a parse
		 * error occurs thereafter all characters until 'A' are
		 * dropped, with the result that any unexpected control
		 * messages are ignored.
		 */
		switch (dbg->parse_state) {
		case STATE_:
			if (isdigit(ch)) {
				dbg->mv = ch - '0';
				dbg->parse_state = STATE_num;
			}
			break;
		case STATE_num:
			if (isdigit(ch)) {
				dbg->mv *= 10;
				dbg->mv += ch - '0';
			} else if (ch == 'm') {
				dbg->parse_state = STATE_num_m;
			} else {
				dbg->parse_state = STATE_err;
			}
			break;
		case STATE_num_m:
			if (ch == 'V')
				dbg->parse_state = STATE_num_mV;
			else
				dbg->parse_state = STATE_err;
			break;
		case STATE_num_mV:
			if (isdigit(ch)) {
				dbg->ma = ch - '0';
				dbg->parse_state = STATE_num_mV_num;
			} else if (!isspace(ch)) {
				dbg->parse_state = STATE_err;
			}
			break;
		case STATE_num_mV_num:
			if (isdigit(ch)) {
				dbg->ma *= 10;
				dbg->ma += ch - '0';
			} else if (ch == 'm') {
				dbg->parse_state = STATE_num_mV_num_m;
			} else {
				dbg->parse_state = STATE_err;
			}
			break;
		case STATE_num_mV_num_m:
			if (ch == 'A') {
				/* Parser found a match, report it */
				dc[0].value = dbg->mv;
				dc[1].value = dbg->ma;

				status_send_values("dc", dc);
			} else {
				dbg->parse_state = STATE_err;
			}
			break;
		case STATE_err:
			if (ch == 'A')
				dbg->parse_state = STATE_;
			break;
		}
	}

	return 0;
}

static void qcomlt_dbg_request_status(void *data)
{
	struct qcomlt_dbg *dbg = data;

	write(dbg->fd, "s", 1);

	watch_timer_add(200, qcomlt_dbg_request_status, dbg);
}

static void qcomlt_dbg_status_enable(struct device *dev)
{
	struct qcomlt_dbg *dbg = dev->cdb;

	watch_add_readfd(dbg->fd, qcomlt_dbg_ctrl_data, dbg);
	watch_timer_add(200, qcomlt_dbg_request_status, dbg);
}

const struct control_ops qcomlt_dbg_ops = {
	.open = qcomlt_dbg_open,
	.power = qcomlt_dbg_power,
	.usb = qcomlt_dbg_usb,
	.key = qcomlt_dbg_key,
	.status_enable = qcomlt_dbg_status_enable,
};
