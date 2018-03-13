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

#include "bad.h"
#include "cdb_assist.h"

struct cdb_assist {
	char serial[9];

	char control_uart[20];
	char target_uart[20];

	int control_tty;
	int target_tty;

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

static int readat(int dir, const char *name, char *buf, size_t len)
{
	ssize_t n;
	int fd;
	int ret = 0;

	fd = openat(dir, name, O_RDONLY);
	if (fd < 0)
		return fd;

	n = read(fd, buf, len - 1);
	if (n < 0) {
		warn("failed to read %s", name);
		ret = -EINVAL;
		goto close_fd;
	}
	buf[n] = '\0';

	buf[strcspn(buf, "\n")] = '\0';

close_fd:
	close(fd);
	return ret;
}

static struct cdb_assist *enumerate_cdb_assists()
{
	struct cdb_assist *cdb;
	struct cdb_assist *all = NULL;
	struct cdb_assist *last = NULL;
	struct dirent *de;
	char interface[30];
	char serial[9];
	DIR *dir;
	int tty;
	int fd;
	int ret;

	tty = open("/sys/class/tty", O_DIRECTORY);
	if (tty < 0)
		err(1, "failed to open /sys/class/tty");

	dir = fdopendir(tty);
	if (!dir)
		err(1, "failed to opendir /sys/class/tty");

	while ((de = readdir(dir)) != NULL) {
		if (strncmp(de->d_name, "ttyACM", 6) != 0)
			continue;

		fd = openat(tty, de->d_name, O_DIRECTORY);
		if (fd < 0)
			continue;

		ret = readat(fd, "../../interface", interface, sizeof(interface));
		if (ret < 0)
			goto close_fd;

		ret = readat(fd, "../../../serial", serial, sizeof(serial));
		if (ret < 0)
			goto close_fd;

		for (cdb = all; cdb; cdb = cdb->next) {
			if (strcmp(cdb->serial, serial) == 0)
				break;
		}

		if (!cdb) {
			cdb = calloc(1, sizeof(*cdb));

			strcpy(cdb->serial, serial);

			if (last) {
				last->next = cdb;
				last = cdb;
			} else {
				last = cdb;
				all = cdb;
			}
		}

		if (strcmp(interface, "Control UART") == 0)
			strcpy(cdb->control_uart, de->d_name);
		else if (strcmp(interface, "Target UART") == 0)
			strcpy(cdb->target_uart, de->d_name);
		else
			errx(1, "tty is neither control nor target\n");

close_fd:
		close(fd);
	}

	closedir(dir);
	close(tty);

	return all;
}

static struct cdb_assist *cdb_assist_find(const char *serial)
{
	struct cdb_assist *cdb;
	struct cdb_assist *all;

	all = enumerate_cdb_assists();

	for (cdb = all; cdb; cdb = cdb->next) {
		if (strcmp(cdb->serial, serial) == 0)
			return cdb;
	}

	return NULL;
}

static int tty_open(const char *tty, struct termios *old)
{
	struct termios tios;
	char buf[80] = "/dev/";
	int ret;
	int fd;

	strcat(buf, tty);
	fd = open(buf, O_RDWR | O_NOCTTY | O_EXCL);
	if (fd < 0)
		err(1, "unable to open \"%s\"", tty);

	ret = tcgetattr(fd, old);
	if (ret < 0)
		err(1, "unable to retrieve \"%s\" tios", tty);

	memset(&tios, 0, sizeof(tios));
	tios.c_cflag = B115200 | CRTSCTS | CS8 | CLOCAL | CREAD;
	tios.c_iflag = IGNPAR;
	tios.c_oflag = 0;

	tcflush(fd, TCIFLUSH);

	ret = tcsetattr(fd, TCSANOW, &tios);
	if (ret < 0)
		err(1, "unable to update \"%s\" tios", tty);

	return fd;
}

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

static int cdb_assist_target_data(int fd, void *data)
{
	struct msg hdr;
	char buf[128];
	ssize_t n;

	n = read(fd, buf, sizeof(buf));
	if (n < 0)
		return n;

	hdr.type = MSG_CONSOLE;
	hdr.len = n;
	write(STDOUT_FILENO, &hdr, sizeof(hdr));
	write(STDOUT_FILENO, buf, n);

	return 0;
}

static int cdb_ctrl_write(struct cdb_assist *cdb, const char *buf, size_t len)
{
	return write(cdb->control_tty, buf, len);
}

void *cdb_assist_open(struct device *dev)
{
	const char *serial = dev->cdb_serial;
	struct cdb_assist *cdb;
	int ret;

	cdb = cdb_assist_find(serial);
	if (!cdb) {
		fprintf(stderr, "unable to find cdb assist with serial %s\n", serial);
		return NULL;
	}

	cdb->control_tty = tty_open(cdb->control_uart, &cdb->control_tios);
	if (cdb->control_tty < 0)
		return NULL;

	cdb->target_tty = tty_open(cdb->target_uart, &cdb->target_tios);
	if (cdb->target_tty < 0)
		return NULL;

	watch_add_readfd(cdb->control_tty, cdb_assist_ctrl_data, cdb);
	watch_add_readfd(cdb->target_tty, cdb_assist_target_data, cdb);

	ret = cdb_ctrl_write(cdb, "vpabc", 5);
	if (ret < 0)
		return NULL;

	cdb_set_voltage(cdb, dev->voltage);

	return cdb;
}

void cdb_assist_close(struct cdb_assist *cdb)
{
	int ret;

	tcflush(cdb->control_tty, TCIFLUSH);

	ret = tcsetattr(cdb->target_tty, TCSAFLUSH, &cdb->target_tios);
	if (ret < 0)
		warn("unable to restore tios of \"%s\"", cdb->target_uart);

	close(cdb->control_tty);
	close(cdb->target_tty);
}

static void cdb_power(struct cdb_assist *cdb, bool on)
{
	const char cmd[] = "pP";

	cdb_ctrl_write(cdb, &cmd[on], 1);
}

void cdb_vbus(struct cdb_assist *cdb, bool on)
{
	const char cmd[] = "vV";

	cdb_ctrl_write(cdb, &cmd[on], 1);
}

int cdb_assist_power_on(struct device *dev)
{
	struct cdb_assist *cdb = dev->cdb;

	cdb_power(cdb, true);
	cdb_gpio(cdb, 0, true);
	usleep(500000);
	cdb_gpio(cdb, 0, false);

	return 0;
}

int cdb_assist_power_off(struct device *dev)
{
	struct cdb_assist *cdb = dev->cdb;

	cdb_vbus(cdb, false);
	cdb_power(cdb, false);

	if (dev->pshold_shutdown) {
		cdb_gpio(cdb, 2, true);
		sleep(2);
		cdb_gpio(cdb, 2, false);
	}

	return 0;
}

void cdb_assist_vbus(struct device *dev, bool on)
{
	cdb_vbus(dev->cdb, on);
}

void cdb_gpio(struct cdb_assist *cdb, int gpio, bool on)
{
	const char *cmd[] = { "aA", "bB", "cC" };
	cdb_ctrl_write(cdb, &cmd[gpio][on], 1);
}

int cdb_target_write(struct device *dev, const void *buf, size_t len)
{
	struct cdb_assist *cdb = dev->cdb;

	return write(cdb->target_tty, buf, len);
}

void cdb_target_break(struct cdb_assist *cdb)
{
	tcsendbreak(cdb->target_tty, 0);
}

unsigned int cdb_vref(struct cdb_assist *cdb)
{
	return cdb->vref;
}

void cdb_assist_print_status(struct device *dev)
{
	struct cdb_assist *cdb = dev->cdb;
	struct msg hdr;
	char buf[128];
	int n;

	n = sprintf(buf, "%dmV %dmA%s%s%s%s%s ref: %dmV",
			 cdb->voltage_set,
			 cdb->current_actual,
			 cdb->vbat ? " vbat" : "",
			 cdb->vbus ? " vbus" : "",
			 cdb->btn[0] ? " btn1" : "",
			 cdb->btn[1] ? " btn2" : "",
			 cdb->btn[2] ? " btn3" : "",
			 cdb->vref);

	hdr.type = MSG_STATUS_UPDATE;
	hdr.len = n;
	write(STDOUT_FILENO, &hdr, sizeof(hdr));
	write(STDOUT_FILENO, buf, n);
}

void cdb_set_voltage(struct cdb_assist *cdb, unsigned mV)
{
	char buf[20];
	int n;

	n = sprintf(buf, "u%d\r\n", mV);
	cdb_ctrl_write(cdb, buf, n);
}
