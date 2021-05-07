/*
 * Copyright (c) 2021, Linaro Ltd.
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

#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>

#include "cdba.h"

int verb = CDBA_BOOT;
extern bool auto_power_on;
extern struct list_head work_items;
extern bool cdba_client_quit;

static const char *cdba_opts = "R";

static bool fastboot_repeat;
static bool fastboot_done;
static const char *fastboot_file;

struct fastboot_download_work {
	struct work work;

	void *data;
	size_t offset;
	size_t size;
};

static void fastboot_work_fn(struct work *_work, int ssh_stdin)
{
	struct fastboot_download_work *work = container_of(_work, struct fastboot_download_work, work);
	struct msg *msg;
	size_t left;
	ssize_t n;

	left = MIN(2048, work->size - work->offset);

	msg = alloca(sizeof(*msg) + left);
	msg->type = MSG_FASTBOOT_DOWNLOAD;
	msg->len = left;
	memcpy(msg->data, work->data + work->offset, left);

	n = write(ssh_stdin, msg, sizeof(*msg) + msg->len);
	if (n < 0 && errno == EAGAIN) {
		list_add(&work_items, &_work->node);
		return;
	} else if (n < 0) {
		err(1, "failed to write fastboot message");
	}

	work->offset += msg->len;

	/* We've sent the entire image, and a zero length packet */
	if (!msg->len)
		free(work);
	else
		list_add(&work_items, &_work->node);
}

static void request_fastboot_files(void)
{
	struct fastboot_download_work *work;
	struct stat sb;
	int fd;

	work = calloc(1, sizeof(*work));
	work->work.fn = fastboot_work_fn;

	fd = open(fastboot_file, O_RDONLY);
	if (fd < 0)
		err(1, "failed to open \"%s\"", fastboot_file);

	fstat(fd, &sb);

	work->size = sb.st_size;
	work->data = malloc(work->size);
	read(fd, work->data, work->size);
	close(fd);

	list_add(&work_items, &work->work.node);
}

void cdba_usage(const char *__progname)
{
	fprintf(stderr, "usage: %s -b <board> -h <host> [-t <timeout>] "
			"[-T <inactivity-timeout>] boot.img\n",
			__progname);
}

int cdba_handle_opt(int opt, const char *optarg)
{
	switch (opt) {
	case 'R':
		fastboot_repeat = true;
		return 0;
	}

	return 1;
}

void cdba_handle_verb(int argc, char **argv, int optind, const char *board)
{
	struct stat sb;

	switch (verb) {
	case CDBA_BOOT:
		if (optind >= argc || !board)
			cdba_client_usage();

		fastboot_file = argv[argc - 1];
		if (lstat(fastboot_file, &sb))
			err(1, "unable to read \"%s\"", fastboot_file);
		if (!S_ISREG(sb.st_mode) && !S_ISLNK(sb.st_mode))
			errx(1, "\"%s\" is not a regular file", fastboot_file);

		cdba_client_request_select_board(board, 0, 1);
		break;
	}
}

int cdba_handle_message(struct msg *msg)
{
	int rc = 0;

	switch (msg->type) {
	case MSG_SELECT_BOARD:
		// printf("======================================== MSG_SELECT_BOARD\n");
		cdba_client_request_power_on();
		break;
	case MSG_HARDRESET:
		break;
	case MSG_POWER_ON:
		// printf("======================================== MSG_POWER_ON\n");
		break;
	case MSG_POWER_OFF:
		// printf("======================================== MSG_POWER_OFF\n");
		if (auto_power_on) {
			sleep(2);
			cdba_client_request_power_on();
		}
		break;
	case MSG_FASTBOOT_PRESENT:
		if (*(uint8_t*)msg->data) {
			// printf("======================================== MSG_FASTBOOT_PRESENT(on)\n");
			if (!fastboot_done || fastboot_repeat)
				request_fastboot_files();
			else
				cdba_client_quit = true;
		} else {
			fastboot_done = true;
			// printf("======================================== MSG_FASTBOOT_PRESENT(off)\n");
		}
		break;
	case MSG_FASTBOOT_DOWNLOAD:
		// printf("======================================== MSG_FASTBOOT_DOWNLOAD\n");
		break;
	case MSG_FASTBOOT_BOOT:
		// printf("======================================== MSG_FASTBOOT_BOOT\n");
		break;
	default:
		rc = -1;
		break;
	}

	return rc;
}

void cdba_end(void)
{
	if (verb == CDBA_BOOT)
		printf("Waiting for ssh to finish\n");
}

int cdba_client_reached_timeout(void)
{
	return fastboot_done ? 110 : 2;
}

int main(int argc, char **argv)
{
	return cdba_client_main(argc, argv, cdba_opts);
}
