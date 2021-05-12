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

#include "cdba-standalone.h"

int lock = 1;
int verb = -1;
extern bool cdba_client_quit;
static bool selected_board = false;

static const char *cdba_standalone_opts = "a:e:p:";

void cdba_usage(const char *__progname)
{
	fprintf(stderr, "usage: %s -b <board> -h <host> [-t <timeout>] "
			"[-T <inactivity-timeout>] <-a console> [-e on/off]\n",
			__progname);
	fprintf(stderr, "usage: %s -b <board> -h <host> [-t <timeout>] "
			"[-T <inactivity-timeout>] <-p <on/off> [-e on/off]>\n",
			__progname);
}

int cdba_handle_opt(int opt, const char *optarg)
{
	switch (opt) {
	case 'a':
		if (strcmp(optarg, "console") == 0)
			verb = CDBA_CONSOLE;
		else
			cdba_client_usage();

		return 0;
	case 'e':
		if (strcmp(optarg, "on") == 0)
			lock = 1;
		else if (strcmp(optarg, "off") == 0)
			lock = 0;
		else
			cdba_client_usage();
		return 0;
	case 'p':
		if (strcmp(optarg, "on") == 0)
			verb = CDBA_POWER_ON;
		else if (strcmp(optarg, "off") == 0)
			verb = CDBA_POWER_OFF;
		else
			cdba_client_usage();

		return 0;
	}

	return 1;
}

void cdba_handle_verb(int argc, char **argv, int optind, const char *board)
{
	if (!board || verb == -1)
		cdba_client_usage();

	switch (verb) {
	case CDBA_CONSOLE:
		cdba_client_request_select_board(board, 1, lock);
		break;
	case CDBA_POWER_ON:
	case CDBA_POWER_OFF:
		cdba_client_request_select_board(board, 1, lock);
		break;
	}
}

int cdba_handle_message(struct msg *msg)
{
	int rc = 0;

	switch (msg->type) {
        case MSG_SELECT_BOARD:
		selected_board = true;
		switch (verb) {
		case CDBA_CONSOLE:
			break;
		case CDBA_POWER_ON:
			cdba_client_request_power_on();
			break;
		case CDBA_POWER_OFF:
			cdba_client_request_power_off();
			break;
		}
	case MSG_POWER_ON:
	case MSG_POWER_OFF:
		break;
	}

	return rc;
}

void cdba_end(void)
{	if (verb == CDBA_CONSOLE)
		printf("Waiting for ssh to finish\n");
	else if (selected_board)
		cdba_client_quit = true;
}

int cdba_client_reached_timeout(void)
{
	return 2;
}

int main(int argc, char **argv)
{
	return cdba_client_main(argc, argv, cdba_standalone_opts);
}
