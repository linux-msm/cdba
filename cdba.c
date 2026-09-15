/*
 * Copyright (c) 2016-2018, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <alloca.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "cdba.h"
#include "circ_buf.h"
#include "list.h"

#define TX_DATA_CHUNK_SIZE 2048

static bool quit;
static bool fastboot_repeat;
static bool fastboot_done;
static bool fastboot_continue;
static bool edl_pending;

static int status_fd = -1;

static const char *fastboot_file;

struct edl_file {
	struct list_head node;

	char *target;
	char *filename;
};

static struct list_head edl_files = LIST_INIT(edl_files);

struct tx_item {
	struct list_head node;

	uint8_t type;
	uint16_t len;

	int fd;
	char *upload_name;
	size_t upload_size;
	bool upload_eof;

	uint8_t payload[];
};

static struct list_head tx_queue = LIST_INIT(tx_queue);

enum upload_type {
	UPLOAD_NONE,
	UPLOAD_FASTBOOT,
	UPLOAD_EDL,
	UPLOAD_MIXED,
};

struct upload_progress {
	bool enabled;
	bool active;

	size_t total_bytes;
	size_t sent_bytes;
	size_t line_len;
	unsigned int tty_width;
	enum upload_type type;
	char name[64];

	struct timeval last_update;
};

static struct upload_progress upload_progress;

static void upload_progress_render(bool force);

static const char *upload_type_name(enum upload_type type)
{
	switch (type) {
	case UPLOAD_FASTBOOT:
		return "fastboot";
	case UPLOAD_EDL:
		return "edl";
	case UPLOAD_MIXED:
		return "mixed";
	default:
		return "upload";
	}
}

static bool is_upload_msg(uint8_t type)
{
	return type == MSG_FASTBOOT_DOWNLOAD || type == MSG_EDL_DOWNLOAD;
}

static enum upload_type msg_upload_type(uint8_t type)
{
	if (type == MSG_FASTBOOT_DOWNLOAD)
		return UPLOAD_FASTBOOT;
	if (type == MSG_EDL_DOWNLOAD)
		return UPLOAD_EDL;

	return UPLOAD_NONE;
}

static const char *path_basename(const char *path)
{
	const char *base;

	base = strrchr(path, '/');
	if (!base || !base[1])
		return path;

	return base + 1;
}

static void upload_progress_clear_line(void)
{
	char spaces[160];
	size_t to_clear;

	if (!upload_progress.enabled || !upload_progress.line_len)
		return;

	to_clear = MIN(upload_progress.line_len, sizeof(spaces));
	memset(spaces, ' ', to_clear);
	write(STDERR_FILENO, "\r", 1);
	write(STDERR_FILENO, spaces, to_clear);
	write(STDERR_FILENO, "\r", 1);
	upload_progress.line_len = 0;
}

static void upload_progress_write(int fd, const void *buf, size_t len)
{
	upload_progress_clear_line();
	write(fd, buf, len);
	upload_progress_render(true);
}

static void upload_progress_render(bool force)
{
	struct timeval now;
	unsigned long elapsed_us;
	char bar[40];
	char line[192];
	double percent;
	double sent_mb;
	double total_mb;
	size_t filled;
	size_t empty;
	size_t line_len;
	int line_n;
	int bar_n;

	if (!upload_progress.enabled || !upload_progress.active || !upload_progress.total_bytes)
		return;

	if (!force && upload_progress.last_update.tv_sec) {
		gettimeofday(&now, NULL);
		elapsed_us = (now.tv_sec - upload_progress.last_update.tv_sec) * 1000000 +
			     (now.tv_usec - upload_progress.last_update.tv_usec);
		if (elapsed_us < 100000)
			return;
	}

	percent = (double)upload_progress.sent_bytes / upload_progress.total_bytes;
	if (percent > 1.0)
		percent = 1.0;

	filled = percent * (sizeof(bar) - 1);
	empty = (sizeof(bar) - 1) - filled;

	bar_n = snprintf(bar, sizeof(bar), "%.*s%.*s",
			 (int)filled,
			 "#######################################",
			 (int)empty,
			 "---------------------------------------");
	if (bar_n < 0)
		return;

	sent_mb = (double)upload_progress.sent_bytes / 1000000.0;
	total_mb = (double)upload_progress.total_bytes / 1000000.0;

	line_n = snprintf(line, sizeof(line), "%s %6.2f%% [%s] %.2f/%.2f MB",
			  upload_progress.name[0] ? upload_progress.name :
			  upload_type_name(upload_progress.type),
			  percent * 100.0,
			  bar,
			  sent_mb, total_mb);
	if (line_n < 0)
		return;

	line_len = MIN((size_t)line_n, sizeof(line) - 1);
	if (line_len > upload_progress.tty_width - 1)
		line_len = upload_progress.tty_width - 1;

	write(STDERR_FILENO, "\r", 1);
	write(STDERR_FILENO, line, line_len);
	if (upload_progress.line_len > line_len) {
		size_t tail = upload_progress.line_len - line_len;
		char spaces_tail[160];

		tail = MIN(tail, sizeof(spaces_tail));
		memset(spaces_tail, ' ', tail);
		write(STDERR_FILENO, spaces_tail, tail);
	}
	upload_progress.line_len = line_len;

	gettimeofday(&upload_progress.last_update, NULL);
}

static void upload_progress_finish(void)
{
	if (!upload_progress.active)
		return;

	upload_progress_render(true);
	if (upload_progress.line_len)
		write(STDERR_FILENO, "\n", 1);

	upload_progress.active = false;
	upload_progress.type = UPLOAD_NONE;
	upload_progress.sent_bytes = 0;
	upload_progress.total_bytes = 0;
	upload_progress.line_len = 0;
	upload_progress.name[0] = '\0';
	upload_progress.last_update = (struct timeval){ 0 };
}

static void upload_progress_start(const struct tx_item *item)
{
	const char *name = item->upload_name;
	size_t max_name;

	upload_progress.active = true;
	upload_progress.type = msg_upload_type(item->type);
	upload_progress.sent_bytes = 0;
	upload_progress.total_bytes = item->upload_size;
	upload_progress.line_len = 0;
	upload_progress.last_update = (struct timeval){ 0 };

	if (!name || !name[0])
		name = upload_type_name(upload_progress.type);

	max_name = sizeof(upload_progress.name) - 1;
	snprintf(upload_progress.name, sizeof(upload_progress.name), "%.*s",
		 (int)max_name, name);
}

static bool upload_progress_is_same_file(const struct tx_item *item)
{
	const char *name = item->upload_name;

	if (!name || !name[0])
		name = upload_type_name(msg_upload_type(item->type));

	return upload_progress.active &&
	       upload_progress.type == msg_upload_type(item->type) &&
	       !strcmp(upload_progress.name, name);
}

static void upload_progress_sent(const struct tx_item *item)
{
	if (!is_upload_msg(item->type))
		return;

	if (!upload_progress_is_same_file(item)) {
		if (upload_progress.active)
			upload_progress_finish();
		upload_progress_start(item);
	}

	if (item->len) {
		upload_progress.sent_bytes += item->len;
		upload_progress_render(false);
	}

	if (item->upload_eof)
		upload_progress_finish();
}

static void upload_progress_init(void)
{
	struct winsize w = { };

	if (!isatty(STDERR_FILENO))
		return;

	if (!ioctl(STDERR_FILENO, TIOCGWINSZ, &w) && w.ws_col > 0)
		upload_progress.tty_width = w.ws_col;
	else
		upload_progress.tty_width = 80;

	upload_progress.tty_width = MIN(upload_progress.tty_width, 120);
	upload_progress.enabled = upload_progress.tty_width >= 60;
}

static struct termios *tty_unbuffer(void)
{
	static struct termios orig_tios;
	struct termios tios;
	int ret;

	ret = tcgetattr(STDIN_FILENO, &orig_tios);
	if (ret < 0) {
		/* stdin is not a tty */
		if (errno == ENOTTY)
			return NULL;
		err(1, "unable to retrieve tty tios");
	}

	memcpy(&tios, &orig_tios, sizeof(struct termios));
	tios.c_lflag &= ~(ICANON | ECHO | ISIG);
	tios.c_iflag &= ~(ISTRIP | IGNCR | ICRNL | INLCR | IXOFF | IXON);
	tios.c_cc[VTIME] = 0;
	tios.c_cc[VMIN] = 1;
	ret = tcsetattr(STDIN_FILENO, TCSANOW, &tios);
	if (ret)
		err(1, "unable to update tty tios");

	return &orig_tios;
}

static void tty_reset(struct termios *orig_tios)
{
	int ret;

	if (!orig_tios)
		return;

	tcflush(STDIN_FILENO, TCIFLUSH);
	ret = tcsetattr(STDIN_FILENO, TCSANOW, orig_tios);
	if (ret < 0)
		warn("unable to reset tty tios");
}

static int fork_ssh(const char *host, const char *cmd, int *pipes)
{
	int piped_stdin[2];
	int piped_stdout[2];
	int piped_stderr[2];
	pid_t pid;
	int flags;
	int i;

	pipe(piped_stdin);
	pipe(piped_stdout);
	pipe(piped_stderr);

	pid = fork();
	switch(pid) {
	case -1:
		err(1, "failed to fork");
	case 0:
		dup2(piped_stdin[0], STDIN_FILENO);
		dup2(piped_stdout[1], STDOUT_FILENO);
		dup2(piped_stderr[1], STDERR_FILENO);

		close(piped_stdin[0]);
		close(piped_stdin[1]);

		close(piped_stdout[0]);
		close(piped_stdout[1]);

		close(piped_stderr[0]);
		close(piped_stderr[1]);

		if (host) {
			execlp("ssh", "ssh",
			       "-o", "ServerAliveInterval=30",
			       "-o", "ServerAliveCountMax=3",
			       host, cmd, NULL);
			err(1, "launching ssh failed");
		} else {
			execlp(cmd, cmd, NULL);
			err(1, "launching cdba-server failed");
		}
	default:
		close(piped_stdin[0]);
		close(piped_stdout[1]);
		close(piped_stderr[1]);
	}

	pipes[0] = piped_stdin[1];
	pipes[1] = piped_stdout[0];
	pipes[2] = piped_stderr[0];

	for (i = 0; i < 3; i++) {
		flags = fcntl(pipes[i], F_GETFL, 0);
		fcntl(pipes[i], F_SETFL, flags | O_NONBLOCK);
	}

	return 0;
}

static ssize_t cdba_tx_one(int fd, struct tx_item *item)
{
	struct iovec iov[2];
	struct msg msg;
	void *buf;
	ssize_t n;

	msg.type = item->type;
	msg.len = item->len;

	iov[0].iov_base = &msg;
	iov[0].iov_len = sizeof(msg);

	if (item->fd != -1 && item->len) {
		buf = alloca(item->len);
		n = read(item->fd, buf, item->len);
		if (n != item->len)
			err(1, "failed to read %u bytes from file", item->len);

		iov[1].iov_base = buf;
		iov[1].iov_len = item->len;
	} else if (item->fd != -1) {
		close(item->fd);
	} else {
		iov[1].iov_base = item->payload;
		iov[1].iov_len = item->len;
	}

	return writev(fd, iov, item->len ? 2 : 1);
}

static void cdba_queue_data(int type, size_t len, const void *buf)
{
	struct tx_item *item;

	item = calloc(1, sizeof(*item) + len);
	item->type = type;
	item->len = len;
	item->fd = -1;
	if (len)
		memcpy(item->payload, buf, len);

	list_append(&tx_queue, &item->node);
}

static void cdba_queue_fd(int type, size_t len, int fd,
			  const char *upload_name, size_t upload_size,
			  bool upload_eof)
{
	struct tx_item *item;

	item = calloc(1, sizeof(*item) + len);
	item->type = type;
	item->len = len;
	item->fd = fd;
	if (upload_name)
		item->upload_name = strdup(upload_name);
	item->upload_size = upload_size;
	item->upload_eof = upload_eof;

	list_append(&tx_queue, &item->node);
}

static void cdba_queue(int type)
{
	cdba_queue_data(type, 0, NULL);
}

static void cdba_send_key(int fd, int key, uint8_t state)
{
	struct key_press press = {
		.key = key,
		.state = state,
	};

	cdba_queue_data(MSG_KEY_PRESS, sizeof(press), &press);
}

static void cdba_toggle_key(int fd, int key, bool key_state[DEVICE_KEY_COUNT])
{
	key_state[key] = !key_state[key];
	cdba_send_key(fd, key, key_state[key]);
}

static int tty_callback(int *ssh_fds)
{
	static bool key_state[DEVICE_KEY_COUNT];
	static const char ctrl_a = 0x1;
	static bool special;
	char buf[32];
	ssize_t k;
	ssize_t n;

	n = read(STDIN_FILENO, buf, sizeof(buf));
	if (n <= 0)
		return n;

	for (k = 0; k < n; k++) {
		if (buf[k] == ctrl_a) {
			special = true;
		} else if (special) {
			switch (buf[k]) {
			case 'q':
				quit = true;
				break;
			case 'P':
				cdba_queue(MSG_POWER_ON);
				break;
			case 'p':
				cdba_queue(MSG_POWER_OFF);
				break;
			case 's':
				cdba_queue(MSG_STATUS_UPDATE);
				break;
			case 'V':
				cdba_queue(MSG_VBUS_ON);
				break;
			case 'v':
				cdba_queue(MSG_VBUS_OFF);
				break;
			case 'a':
				cdba_queue_data(MSG_CONSOLE, 1, &ctrl_a);
				break;
			case 'B':
				cdba_queue(MSG_SEND_BREAK);
				break;
			case 'o':
				cdba_send_key(ssh_fds[0], DEVICE_KEY_POWER, KEY_PRESS_PULSE);
				break;
			case 'O':
				cdba_toggle_key(ssh_fds[0], DEVICE_KEY_POWER, key_state);
				break;
			case 'f':
				cdba_send_key(ssh_fds[0], DEVICE_KEY_FASTBOOT, KEY_PRESS_PULSE);
				break;
			case 'F':
				cdba_toggle_key(ssh_fds[0], DEVICE_KEY_FASTBOOT, key_state);
				break;
			}

			special = false;
		} else {
			cdba_queue_data(MSG_CONSOLE, 1, buf + k);
		}
	}

	return n;
}

/**
 * request_board_list() - Queue a request for a boards list
 */
static void request_board_list(void)
{
	cdba_queue(MSG_LIST_DEVICES);
}

/**
 * request_board_info() - Queue a request for a specific "board"
 * @board: identifier of the board
 *
 * Note that @board is assumed to be alive until the message has been queued,
 * and will not be freed.
 */
static void request_board_info(const char *board)
{
	cdba_queue_data(MSG_BOARD_INFO, strlen(board) + 1, board);
}

/**
 * request_select_board() - Queue a request for a specific "board"
 * @board: identifier of the board
 *
 * Note that @board is assumed to be alive until the message has been queued,
 * and will not be freed.
 */
static void request_select_board(const char *board)
{
	cdba_queue_data(MSG_SELECT_BOARD, strlen(board) + 1, board);
}

/**
 * request_power_on() - Queue a request to power on the selected board
 */
static void request_power_on(void)
{
	uint8_t mode;

	if (edl_pending)
		mode = MSG_POWER_ON_EDL;
	else if (fastboot_file)
		mode = MSG_POWER_ON_FASTBOOT;
	else
		mode = MSG_POWER_ON_NORMAL;

	cdba_queue_data(MSG_POWER_ON, 1, &mode);
}

/**
 * request_power_off() - Queue a request to power off the selected board
 */
static void request_power_off(void)
{
	cdba_queue(MSG_POWER_OFF);
}

/**
 * request_fastboot_continue() - Queue a request to issue a fastboot continue
 */
static void request_fastboot_continue(void)
{
	cdba_queue(MSG_FASTBOOT_CONTINUE);
}

/**
 * request_fastboot_files() - Queue the fastboot download (and boot) of fastboot_file
 */
static void request_fastboot_files(void)
{
	struct stat sb;
	size_t offset;
	size_t len;
	int fd;

	fd = open(fastboot_file, O_RDONLY);
	if (fd < 0)
		err(1, "failed to open \"%s\"", fastboot_file);

	fstat(fd, &sb);

	for (offset = 0; offset < sb.st_size; offset += TX_DATA_CHUNK_SIZE) {
		len = MIN(TX_DATA_CHUNK_SIZE, sb.st_size - offset);
		cdba_queue_fd(MSG_FASTBOOT_DOWNLOAD, len, fd,
			      path_basename(fastboot_file), sb.st_size, false);
	}
	cdba_queue_fd(MSG_FASTBOOT_DOWNLOAD, 0, fd,
		      path_basename(fastboot_file), sb.st_size, true);
}

static void edl_submit_one(struct edl_file *edl)
{
	struct stat sb;
	size_t offset;
	size_t len;
	int fd;

	fd = open(edl->filename, O_RDONLY);
	if (fd < 0)
		err(1, "failed to open \"%s\" for EDL flashing", edl->filename);

	fstat(fd, &sb);

	for (offset = 0; offset < sb.st_size; offset += TX_DATA_CHUNK_SIZE) {
		len = MIN(TX_DATA_CHUNK_SIZE, sb.st_size - offset);
		cdba_queue_fd(MSG_EDL_DOWNLOAD, len, fd,
			      path_basename(edl->filename), sb.st_size, false);
	}
	cdba_queue_fd(MSG_EDL_DOWNLOAD, 0, fd,
		      path_basename(edl->filename), sb.st_size, true);

	cdba_queue_data(MSG_EDL_WRITE, strlen(edl->target) + 1, edl->target);
}

static void handle_edl_present(uint8_t present)
{
	struct edl_file *edl;

	if (present) {
		if (!edl_pending) {
			fprintf(stderr, "device entered EDL unexpectedly, do we have a ramdump?\n");
			quit = true;
			return;
		}

		list_for_each_entry(edl, &edl_files, node)
			edl_submit_one(edl);

		cdba_queue(MSG_EDL_RESET);

		edl_pending = false;
	}
}

static void handle_status_update(const void *data, size_t len)
{
	if (status_fd < 0)
		return;

	write(status_fd, data, len);
}

static void status_pipe_open(const char *path)
{
	int ret;
	int fd;

	ret = mkfifo(path, 0600);
	if (ret < 0 && errno != EEXIST)
		err(1, "failed to create fifo %s", path);

	fd = open(path, O_RDWR | O_NONBLOCK);
	if (fd < 0)
		err(1, "failed to open fifo %s", path);

	status_fd = fd;

	/* Queue a MSG_STATUS_UPDATE request to start the status flow */
	cdba_queue(MSG_STATUS_UPDATE);
}

static void handle_list_devices(const void *data, size_t len)
{
	char *board;

	if (!len) {
		quit = true;
		return;
	}

	board = alloca(len + 1);
	memcpy(board, data, len);
	board[len] = '\n';
	upload_progress_write(STDOUT_FILENO, board, len + 1);
}

static void handle_board_info(const void *data, size_t len)
{
	char *info;

	info = alloca(len + 1);
	memcpy(info, data, len);
	info[len] = '\n';
	upload_progress_write(STDOUT_FILENO, info, len + 1);

	quit = true;
}

static int power_cycles = -1;
static bool received_power_off;
static bool reached_timeout;

static void handle_console(const void *data, size_t len)
{
	static int power_off_chars = 0;
	const char *p = data;
	int i;

	/* Don't process the line by default (power_cycles = -1) */
	for (i = 0; i < len && power_cycles >= 0; i++) {
		if (*p++ == '~') {
			if (power_off_chars++ == 19) {
				received_power_off = true;
				power_off_chars = 0;
			}
		} else {
			power_off_chars = 0;
		}
	}

	upload_progress_write(STDOUT_FILENO, data, len);
}

static bool auto_power_on;

static int handle_message(struct circ_buf *buf)
{
	struct msg *msg;
	struct msg hdr;
	size_t n;

	for (;;) {
		n = circ_peak(buf, &hdr, sizeof(hdr));
		if (n != sizeof(hdr))
			return 0;

		if (CIRC_AVAIL(buf) < sizeof(*msg) + hdr.len)
			return 0;

		// fprintf(stderr, "avail: %zd hdr.len: %d\n", CIRC_AVAIL(buf), hdr.len);

		msg = malloc(sizeof(*msg) + hdr.len);
		circ_read(buf, msg, sizeof(*msg) + hdr.len);

		switch (msg->type) {
		case MSG_SELECT_BOARD:
			// printf("======================================== MSG_SELECT_BOARD\n");
			request_power_on();
			break;
		case MSG_CONSOLE:
			handle_console(msg->data, msg->len);
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
				request_power_on();
			}
			break;
		case MSG_FASTBOOT_PRESENT:
			if (*(uint8_t*)msg->data) {
				// printf("======================================== MSG_FASTBOOT_PRESENT(on)\n");
				if (fastboot_continue) {
					request_fastboot_continue();
					fastboot_continue = false;
				} else if (!fastboot_done || fastboot_repeat) {
					request_fastboot_files();
				} else {
					quit = true;
				}
			}
			break;
		case MSG_EDL_PRESENT:
			handle_edl_present(*(uint8_t *)msg->data);
			break;
		case MSG_FASTBOOT_DOWNLOAD:
			// printf("======================================== MSG_FASTBOOT_DOWNLOAD\n");
			fastboot_done = true;
			break;
		case MSG_FASTBOOT_BOOT:
			// printf("======================================== MSG_FASTBOOT_BOOT\n");
			break;
		case MSG_STATUS_UPDATE:
			handle_status_update(msg->data, msg->len);
			break;
		case MSG_LIST_DEVICES:
			handle_list_devices(msg->data, msg->len);
			break;
		case MSG_BOARD_INFO:
			handle_board_info(msg->data, msg->len);
			return -1;
			break;
		case MSG_FASTBOOT_CONTINUE:
			// printf("======================================== MSG_FASTBOOT_CONTINUE\n");
			fastboot_done = true;
			break;
		default:
			fprintf(stderr, "unk %d len %d\n", msg->type, msg->len);
			return -1;
		}

		free(msg);
	}

	return 0;
}

static struct timeval get_timeout(int sec)
{
	struct timeval delta = { .tv_sec = sec };
	struct timeval now;
	struct timeval tv;

	gettimeofday(&now, NULL);
	timeradd(&now, &delta, &tv);

	return tv;
}

static void usage(void)
{
	extern const char *__progname;

	fprintf(stderr, "usage: %s -b <board> [options] "
			"[write <target> <image>]... [boot.img]\n",
			__progname);
	fprintf(stderr, "usage: %s -i -b <board> [connection-options]\n",
			__progname);
	fprintf(stderr, "usage: %s -l [connection-options]\n",
			__progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "connection-options:\n");
	fprintf(stderr, "  -h <host>        connect to remote host using ssh\n");
	fprintf(stderr, "  -S <server>      cdba-server binary or command\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "boot options:\n");
	fprintf(stderr, "  -c <count>       power-cycle count on timeout or power-off marker\n");
	fprintf(stderr, "  -C <count>       power-cycle count on power-off marker only\n");
	fprintf(stderr, "  -R               repeat fastboot boot when fastboot reappears\n");
	fprintf(stderr, "  -s <fifo>        write status updates to fifo\n");
	fprintf(stderr, "  -t <seconds>     total timeout, 0 disables it (default: 600)\n");
	fprintf(stderr, "  -T <seconds>     inactivity timeout\n");
	exit(1);
}

enum {
	CDBA_BOOT,
	CDBA_LIST,
	CDBA_INFO,
};

int main(int argc, char **argv)
{
	bool power_cycle_on_timeout = true;
	struct timeval timeout_inactivity_tv;
	struct timeval timeout_total_tv;
	struct timeval *timeout = NULL;
	struct termios *orig_tios;
	bool watch_stdin = true;
	const char *server_binary = "cdba-server";
	const char *status_pipe = NULL;
	bool bump_inactivity_timer;
	int timeout_inactivity = 0;
	int timeout_total = 600;
	struct tx_item *tx_item;
	struct circ_buf recv_buf = { };
	const char *board = NULL;
	const char *host = NULL;
	struct timeval now;
	struct timeval tv;
	struct stat sb;
	int ssh_fds[3];
	char buf[128];
	fd_set rfds;
	fd_set wfds;
	ssize_t n;
	int nfds;
	int verb = CDBA_BOOT;
	int opt;
	int ret;

	while ((opt = getopt(argc, argv, "b:c:C:h:ilRt:S:s:T:")) != -1) {
		switch (opt) {
		case 'b':
			board = optarg;
			break;
		case 'C':
			power_cycle_on_timeout = false;
			/* FALLTHROUGH */
		case 'c':
			power_cycles = atoi(optarg);
			break;
		case 'h':
			host = optarg;
			break;
		case 'i':
			verb = CDBA_INFO;
			break;
		case 'l':
			verb = CDBA_LIST;
			break;
		case 'R':
			fastboot_repeat = true;
			break;
		case 'S':
			server_binary = optarg;
			break;
		case 's':
			status_pipe = optarg;
			break;
		case 't':
			timeout_total = atoi(optarg);
			break;
		case 'T':
			timeout_inactivity = atoi(optarg);
			break;
		default:
			usage();
		}
	}

	switch (verb) {
	case CDBA_BOOT:
		while (optind < argc && strcmp(argv[optind], "write") == 0) {
			struct edl_file *edl;

			if (optind + 3 > argc)
				usage();

			edl = calloc(1, sizeof(*edl));
			edl->target = argv[optind + 1];
			edl->filename = argv[optind + 2];

			list_append(&edl_files, &edl->node);

			optind += 3;
			edl_pending = true;
		}

		if (optind > argc || !board)
			usage();

		fastboot_file = argv[optind];
		if (!fastboot_file)
			fastboot_continue = true;
		else if (lstat(fastboot_file, &sb))
			err(1, "unable to read \"%s\"", fastboot_file);
		else if (!S_ISREG(sb.st_mode) && !S_ISLNK(sb.st_mode))
			errx(1, "\"%s\" is not a regular file", fastboot_file);

		request_select_board(board);
		break;
	case CDBA_LIST:
		request_board_list();
		break;
	case CDBA_INFO:
		if (!board)
			usage();

		request_board_info(board);
		break;
	}

	if (status_pipe)
		status_pipe_open(status_pipe);

	ret = fork_ssh(host, server_binary, ssh_fds);
	if (ret)
		err(1, "failed to connect to \"%s\"", host);

	upload_progress_init();
	orig_tios = tty_unbuffer();

	timeout_total_tv = get_timeout(timeout_total);
	timeout_inactivity_tv = get_timeout(timeout_inactivity);
	if (timeout_total || timeout_inactivity)
		timeout = &tv;

	while (!quit) {
		if (received_power_off || reached_timeout) {
			if (power_cycles <= 0)
				break;

			if (reached_timeout && !power_cycle_on_timeout)
				break;

			printf("power cycle (%d left)\n", power_cycles);
			fflush(stdout);

			auto_power_on = true;
			power_cycles--;
			received_power_off = false;
			reached_timeout = false;

			request_power_off();

			timeout_inactivity_tv = get_timeout(timeout_inactivity);
		}

		FD_ZERO(&rfds);
		FD_SET(ssh_fds[1], &rfds);
		FD_SET(ssh_fds[2], &rfds);
		nfds = MAX(ssh_fds[1], ssh_fds[2]);

		if (watch_stdin) {
			FD_SET(STDIN_FILENO, &rfds);

			nfds = MAX(nfds, STDIN_FILENO);
		}

		FD_ZERO(&wfds);
		if (!list_empty(&tx_queue))
			FD_SET(ssh_fds[0], &wfds);

		if (timeout) {
			gettimeofday(&now, NULL);
			if (timeout_inactivity && (!timeout_total ||
			    timercmp(&timeout_inactivity_tv, &timeout_total_tv, <))) {
				timersub(&timeout_inactivity_tv, &now, timeout);
			} else {
				timersub(&timeout_total_tv, &now, timeout);
			}
		}
		ret = select(nfds + 1, &rfds, &wfds, NULL, timeout);
#if 0
		printf("select: %d (%c%c%c)\n", ret, FD_ISSET(STDIN_FILENO, &rfds) ? 'X' : '-',
						     FD_ISSET(ssh_fds[1], &rfds) ? 'X' : '-',
						     FD_ISSET(ssh_fds[2], &rfds) ? 'X' : '-');
#endif
		if (ret < 0) {
			err(1, "select");
		} else if (ret == 0) {
			if (timeout_inactivity && timercmp(&timeout_inactivity_tv, &timeout_total_tv, <))
				warnx("timeout due to inactivity");
			else
				warnx("timeout reached");

			reached_timeout = true;
		}

		bump_inactivity_timer = false;

		if (watch_stdin && FD_ISSET(STDIN_FILENO, &rfds)) {
			if (tty_callback(ssh_fds) <= 0)
				watch_stdin = false;
		}

		if (FD_ISSET(ssh_fds[2], &rfds)) {
			n = read(ssh_fds[2], buf, sizeof(buf));
			if (!n) {
				warnx("EOF on stderr");
				break;
			} else if (n < 0 && errno == EAGAIN) {
			       continue;
			} else if (n < 0) {
				warn("received %zd on stderr", n);
				break;
			}

			const char blue[] = "\033[94m";
			const char reset[] = "\033[0m";

			write(STDERR_FILENO, blue, sizeof(blue) - 1);
			write(STDERR_FILENO, buf, n);
			write(STDERR_FILENO, reset, sizeof(reset) - 1);
			upload_progress_render(true);

			bump_inactivity_timer = true;
		}

		if (FD_ISSET(ssh_fds[1], &rfds)) {
			ret = circ_fill(ssh_fds[1], &recv_buf);
			if (ret < 0 && errno != EAGAIN) {
				warn("received %d on stdout", ret);
				break;
			}

			n = handle_message(&recv_buf);
			if (n < 0)
				break;

			bump_inactivity_timer = true;
		}

		if (FD_ISSET(ssh_fds[0], &wfds)) {
			if (!list_empty(&tx_queue)) {
				tx_item = list_entry_first(&tx_queue, struct tx_item, node);
				n = cdba_tx_one(ssh_fds[0], tx_item);
				if (n < 0)
					err(1, "failed to write to SSH pipe");
				upload_progress_sent(tx_item);

				list_del(&tx_item->node);
				free(tx_item->upload_name);
				free(tx_item);

				bump_inactivity_timer = true;
			}
		}

		/* Reset inactivity timeout on activity */
		if (bump_inactivity_timer && timeout_inactivity)
			timeout_inactivity_tv = get_timeout(timeout_inactivity);
	}

	close(ssh_fds[0]);
	close(ssh_fds[1]);
	close(ssh_fds[2]);

	if (verb == CDBA_BOOT)
		printf("Waiting for ssh to finish\n");

	wait(NULL);
	upload_progress_finish();
	upload_progress_clear_line();

	tty_reset(orig_tios);

	if (reached_timeout)
		return fastboot_done ? 110 : 2;

	return (quit || received_power_off) ? 0 : 1;
}
