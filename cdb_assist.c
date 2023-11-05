/*
 * Copyright (c) 2016-2018, Linaro Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
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

struct cdb_assist {
	char serial[9];

	int control_tty;

	struct termios control_tios;
	struct termios target_tios;

	struct cdb_assist *next;

	/* parser */
	unsigned state;
	unsigned num[2];
	char key[32];
	unsigned key_idx;
	bool voltage;

	/* state */
	unsigned current_actual;
	unsigned current_set;
	unsigned voltage_actual;
	unsigned voltage_set;

	bool vbat;
	bool btn[3];
	bool vbus;
	unsigned vref;
};

enum {
	STATE_,
	STATE_key,
	STATE_key_bool,
	STATE_key_value,
	STATE_key_o,
	STATE_key_of,
	STATE_key_num,
	STATE_key_num_m,
	STATE_num,
	STATE_num_m,
	STATE_num_mX,
	STATE_num_mX_,
	STATE_num_num_m,
};

static void cdb_set_voltage(struct cdb_assist *cdb, unsigned mV);

static void cdb_parser_bool(struct cdb_assist *cdb, const char *key, bool set)
{
	static const char *sz_keys[] = { "vbat", "btn1", "btn2", "btn3", "vbus" };
	int i;

	for (i = 0; i < 5; i++)
		if (strcmp(key, sz_keys[i]) == 0)
			break;
	
	switch (i) {
	case 0:
		cdb->vbat = set;
		break;
	case 1:
	case 2:
	case 3:
		cdb->btn[i-1] = set;
		break;
	case 4:
		cdb->vbus = set;
		break;
	}
}
static void cdb_parser_current(struct cdb_assist *cdb, unsigned set, unsigned actual)
{
	cdb->current_actual = actual;
	cdb->current_set = set;
}

static void cdb_parser_voltage(struct cdb_assist *cdb, unsigned set, unsigned actual)
{
	cdb->voltage_actual = actual;
	cdb->voltage_set = set;
}
			
static void cdb_parser_vref(struct cdb_assist *cdb, unsigned vref)
{
	cdb->vref = vref;
}

static void cdb_parser_push(struct cdb_assist *cdb, char ch)
{
	switch (cdb->state) {
	case STATE_:
		if (isdigit(ch)) {
			cdb->num[0] = ch - '0';
			cdb->state = STATE_num;
		} else if (isalpha(ch)) {
			cdb->key[0] = ch;
			cdb->key_idx = 1;
			cdb->state = STATE_key;
		}
		break;
	case STATE_num:
		if (isdigit(ch)) {
			cdb->num[0] *= 10;
			cdb->num[0] += ch - '0';
		} else if (ch == 'm') {
			cdb->state = STATE_num_m;
		} else {
			cdb->state = STATE_;
		}
		break;
	case STATE_num_m:
		if (ch == 'v') {
			cdb->voltage = true;
			cdb->state = STATE_num_mX;
		} else if (ch == 'a') {
			cdb->voltage = false;
			cdb->state = STATE_num_mX;
		} else {
			cdb->state = STATE_;
		}
		break;
	case STATE_num_mX:
		if (ch == '/') {
			cdb->num[1] = 0;
			cdb->state = STATE_num_mX_;
		} else {
			cdb->state = STATE_;
		}
		break;
	case STATE_num_mX_:
		if (isdigit(ch)) {
			cdb->num[1] *= 10;
			cdb->num[1] += ch - '0';
		} else if (ch == 'm') {
			cdb->state = STATE_num_num_m;
		} else {
			cdb->state = STATE_;
		}
		break;
	case STATE_num_num_m:
		if (ch == 'v' && cdb->voltage)
			cdb_parser_voltage(cdb, cdb->num[0], cdb->num[1]);
		else if (ch == 'a' && !cdb->voltage)
			cdb_parser_current(cdb, cdb->num[0], cdb->num[1]);

		cdb->state = STATE_;
		break;
	case STATE_key:
		if (isalnum(ch)) {
			cdb->key[cdb->key_idx++] = ch;
		} else if (ch == ':') {
			cdb->key[cdb->key_idx] = '\0';
			cdb->state = STATE_key_bool;
		} else if (ch == '=') {
			cdb->key[cdb->key_idx] = '\0';
			cdb->state = STATE_key_value;
		} else {
			cdb->state = STATE_;
		}
		break;
	case STATE_key_bool:
		if (ch == 'o')
			cdb->state = STATE_key_o;
		else
			cdb->state = STATE_;
		break;
	case STATE_key_o:
		if (ch == 'f') {
			cdb->state = STATE_key_of;
		} else if (ch == 'n') {
			cdb_parser_bool(cdb, cdb->key, true);
			cdb->state = STATE_;
		} else {
			cdb->state = STATE_;
		}
		break;
	case STATE_key_of:
		if (ch == 'f')
			cdb_parser_bool(cdb, cdb->key, false);
		cdb->state = STATE_;	
		break;
	case STATE_key_value:
		if (isdigit(ch)) {
			cdb->num[0] = ch - '0';
			cdb->state = STATE_key_num;
		} else {
			cdb->state = STATE_;
		}
		break;
	case STATE_key_num:
		if (isdigit(ch)) {
			cdb->num[0] *= 10;
			cdb->num[0] += ch - '0';
		} else if (ch == 'm') {
			cdb->state = STATE_key_num_m;
		} else {
			cdb->state = STATE_;
		}
		break;
	case STATE_key_num_m:
		if (ch == 'v')
			cdb_parser_vref(cdb, cdb->num[0]);
		cdb->state = STATE_;
		break;
	}
}

static int cdb_assist_ctrl_data(int fd, void *data)
{
	struct cdb_assist *cdb = data;
	char buf[10];
	ssize_t n;
	ssize_t k;

	n = read(fd, buf, sizeof(buf) - 1);
	if (n < 0)
		return n;

	for (k = 0; k < n; k++)
		cdb_parser_push(cdb, tolower(buf[k]));

	return 0;
}

static int cdb_ctrl_write(struct cdb_assist *cdb, const char *buf, size_t len)
{
	return write(cdb->control_tty, buf, len);
}

static void *cdb_assist_open(struct device *dev)
{
	struct cdb_assist *cdb;
	int ret;

	cdb = calloc(1, sizeof(*cdb));

	cdb->control_tty = tty_open(dev->control_dev, &cdb->control_tios);
	if (cdb->control_tty < 0)
		return NULL;

	watch_add_readfd(cdb->control_tty, cdb_assist_ctrl_data, cdb);

	ret = cdb_ctrl_write(cdb, "vpabc", 5);
	if (ret < 0)
		return NULL;

	cdb_set_voltage(cdb, dev->voltage);

	return cdb;
}

static void cdb_assist_close(struct device *dev)
{
	struct cdb_assist *cdb = dev->cdb;

	tcflush(cdb->control_tty, TCIFLUSH);

	close(cdb->control_tty);
}

static void cdb_power(struct cdb_assist *cdb, bool on)
{
	const char cmd[] = "pP";

	cdb_ctrl_write(cdb, &cmd[on], 1);
}

static void cdb_vbus(struct cdb_assist *cdb, bool on)
{
	const char cmd[] = "vV";

	cdb_ctrl_write(cdb, &cmd[on], 1);
}

static int cdb_assist_power(struct device *dev, bool on)
{
	struct cdb_assist *cdb = dev->cdb;

	cdb_power(cdb, on);

	return 0;
}

static void cdb_assist_usb(struct device *dev, bool on)
{
	cdb_vbus(dev->cdb, on);
}

static void cdb_gpio(struct cdb_assist *cdb, int gpio, bool on)
{
	const char *cmd[] = { "aA", "bB", "cC" };
	cdb_ctrl_write(cdb, &cmd[gpio][on], 1);
}

static void cdb_assist_print_status(void *data)
{
	struct cdb_assist *cdb = data;
	struct status_value vbat[] = {
		{
			.unit = STATUS_MV,
			.value = cdb->voltage_set,
		},
		{
			.unit = STATUS_MA,
			.value = cdb->current_actual,
		},
		{}
	};
	struct status_value vref[] = {
		{
			.unit = STATUS_MV,
			.value = cdb->vref,
		},
		{}
	};

	status_send_values("vbat", vbat);
	status_send_values("vref", vref);
}

static void cdb_assist_status_enable(struct device *dev)
{
	struct cdb_assist *cdb = dev->cdb;

	watch_timer_add(1000, cdb_assist_print_status, cdb);
}

static void cdb_set_voltage(struct cdb_assist *cdb, unsigned mV)
{
	char buf[20];
	int n;

	n = sprintf(buf, "u%u\r\n", mV);
	cdb_ctrl_write(cdb, buf, n);
}

static void cdb_assist_key(struct device *dev, int key, bool asserted)
{
	struct cdb_assist *cdb = dev->cdb;

	switch (key) {
	case DEVICE_KEY_FASTBOOT:
		cdb_gpio(cdb, 1, asserted);
		break;
	case DEVICE_KEY_POWER:
		cdb_gpio(cdb, 0, asserted);
		break;
	}
}

const struct control_ops cdb_assist_ops = {
	.open = cdb_assist_open,
	.close = cdb_assist_close,
	.power = cdb_assist_power,
	.status_enable = cdb_assist_status_enable,
	.usb = cdb_assist_usb,
	.key = cdb_assist_key,
};
