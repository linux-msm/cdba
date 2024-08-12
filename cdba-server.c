/*
 * Copyright (c) 2016-2018, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>

#ifndef CDBA_CAMERA_DISABLED
#include "camera.h"
#endif // CDBA_CAMERA_DISABLED

#include "cdba-server.h"
#include "circ_buf.h"
#include "device.h"
#include "device_parser.h"
#include "fastboot.h"
#include "list.h"
#include "watch.h"

static const char *username;

struct device *selected_device;

static void fastboot_opened(struct fastboot *fb, void *data)
{
	const uint8_t one = 1;

	warnx("fastboot connection opened");

	cdba_send_buf(MSG_FASTBOOT_PRESENT, 1, &one);
}

static void fastboot_info(struct fastboot *fb, const void *buf, size_t len)
{
	fprintf(stderr, "%s\n", (const char *)buf);
}

static void fastboot_disconnect(void *data)
{
	const uint8_t zero = 0;

	cdba_send_buf(MSG_FASTBOOT_PRESENT, 1, &zero);
}

static struct fastboot_ops fastboot_ops = {
	.opened = fastboot_opened,
	.disconnect = fastboot_disconnect,
	.info = fastboot_info,
};

static void msg_select_board(const void *param)
{
	selected_device = device_open(param, username);
	if (!selected_device) {
		fprintf(stderr, "failed to open %s\n", (const char *)param);
		watch_quit();
	}

	device_fastboot_open(selected_device, &fastboot_ops);

	cdba_send(MSG_SELECT_BOARD);
}

static void *fastboot_payload;
static size_t fastboot_size;

static void msg_fastboot_download(const void *data, size_t len)
{
	size_t new_size = fastboot_size + len;
	char *newp;

	newp = realloc(fastboot_payload, new_size);
	if (!newp)
		err(1, "failed too expant fastboot scratch area");

	memcpy(newp + fastboot_size, data, len);

	fastboot_payload = newp;
	fastboot_size = new_size;

	if (!len) {
		device_boot(selected_device, fastboot_payload, fastboot_size);

		cdba_send(MSG_FASTBOOT_DOWNLOAD);
		free(fastboot_payload);
		fastboot_payload = NULL;
		fastboot_size = 0;
	}
}

static void msg_fastboot_continue(void)
{
	device_fastboot_continue(selected_device);
	cdba_send(MSG_FASTBOOT_CONTINUE);
}

#ifdef CDBA_CAMERA_DISABLED
static void capture_image(struct device *device)
{
	fprintf(stderr, "Camera support is not built into the server\n");
}
#else
static void capture_image(struct device *device)
{
	uint8_t *jpeg = NULL;
	size_t size = 0;

	if (!device->video_device)
	{
		fprintf(stderr, "video_device not specified!\n");
		return;
	}

	int ret = camera_capture_jpeg(&jpeg, &size, device->video_device);
	if (ret < 0)
	{
		fprintf(stderr, "Failed to capture image\n");
		return;
	}

	const size_t chunk_size = 1024;
	size_t remaining_size = size;
	const uint8_t *ptr = jpeg;

	while (remaining_size > 0) {
		size_t send_size = remaining_size > chunk_size ? chunk_size : remaining_size;

		cdba_send_buf(MSG_CAPTURE_IMAGE, send_size, ptr);

		ptr += send_size;
		remaining_size -= send_size;
	}

	// Empty message to indicate end of image
	cdba_send_buf(MSG_CAPTURE_IMAGE, 0, NULL);

	free(jpeg);
}
#endif // CDBA_CAMERA_DISABLED

void cdba_send_buf(int type, size_t len, const void *buf)
{
	struct msg msg = {
		.type = type,
		.len = len
	};

	write(STDOUT_FILENO, &msg, sizeof(msg));
	if (len)
		write(STDOUT_FILENO, buf, len);
}

static int handle_stdin(int fd, void *buf)
{
	static struct circ_buf recv_buf = { };
	struct msg *msg;
	struct msg hdr;
	size_t n;
	int ret;

	ret = circ_fill(STDIN_FILENO, &recv_buf);
	if (ret < 0 && errno != EAGAIN) {
		fprintf(stderr, "read %d\n", ret);
		return -1;
	}

	for (;;) {
		n = circ_peak(&recv_buf, &hdr, sizeof(hdr));
		if (n != sizeof(hdr))
			return 0;

		if (CIRC_AVAIL(&recv_buf) < sizeof(*msg) + hdr.len)
			return 0;

		msg = malloc(sizeof(*msg) + hdr.len);
		circ_read(&recv_buf, msg, sizeof(*msg) + hdr.len);

		switch (msg->type) {
		case MSG_CONSOLE:
			device_write(selected_device, msg->data, msg->len);
			break;
		case MSG_FASTBOOT_PRESENT:
			break;
		case MSG_SELECT_BOARD:
			msg_select_board(msg->data);
			break;
		case MSG_HARDRESET:
			// fprintf(stderr, "hard reset\n");
			break;
		case MSG_POWER_ON:
			device_power(selected_device, true);

			cdba_send(MSG_POWER_ON);
			break;
		case MSG_POWER_OFF:
			device_power(selected_device, false);

			cdba_send(MSG_POWER_OFF);
			break;
		case MSG_FASTBOOT_DOWNLOAD:
			msg_fastboot_download(msg->data, msg->len);
			break;
		case MSG_FASTBOOT_BOOT:
			// fprintf(stderr, "fastboot boot\n");
			break;
		case MSG_STATUS_UPDATE:
			device_status_enable(selected_device);
			break;
		case MSG_VBUS_ON:
			device_usb(selected_device, true);
			break;
		case MSG_VBUS_OFF:
			device_usb(selected_device, false);
			break;
		case MSG_SEND_BREAK:
			device_send_break(selected_device);
			break;
		case MSG_LIST_DEVICES:
			device_list_devices(username);
			break;
		case MSG_BOARD_INFO:
			device_info(username, msg->data, msg->len);
			break;
		case MSG_FASTBOOT_CONTINUE:
			msg_fastboot_continue();
			break;
		case MSG_CAPTURE_IMAGE:
			capture_image(selected_device);
			break;
		default:
			fprintf(stderr, "unk %d len %d\n", msg->type, msg->len);
			exit(1);
		}

		free(msg);
	}

	return 0;
}

static void sigpipe_handler(int signo)
{
	watch_quit();
}

static void atexit_handler(void)
{
	syslog(LOG_INFO, "exiting");
}

int main(int argc, char **argv)
{
	int flags;
	int ret;

	signal(SIGPIPE, sigpipe_handler);

	fprintf(stderr, "Starting cdba server\n");

	username = getenv("CDBA_USER");
	if (!username)
		username = getenv("USER");
	if (!username)
		username = "nobody";

	openlog("cdba-server", LOG_PID, LOG_DAEMON);
	atexit(atexit_handler);

	ret = device_parser(".cdba");
	if (ret) {
		ret = device_parser("/etc/cdba");
		if (ret) {
			fprintf(stderr, "device parser: unable to open config file\n");
			exit(1);
		}
	}

	watch_add_readfd(STDIN_FILENO, handle_stdin, NULL);

	flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

	watch_run();

	/* if we got here, stdin/out/err might be not accessible anymore */
	ret = open("/dev/null", O_RDWR);
	if (ret >= 0) {
		close(STDIN_FILENO);
		dup2(ret, STDIN_FILENO);
		close(STDOUT_FILENO);
		dup2(ret, STDOUT_FILENO);
		close(STDERR_FILENO);
		dup2(ret, STDERR_FILENO);
	}

	if (selected_device)
		device_close(selected_device);

	return 0;
}
