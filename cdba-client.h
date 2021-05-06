#ifndef __CDBA_CLIENT_H__
#define __CDBA_CLIENT_H__

#include <stdio.h>
#include <stdint.h>

#include "list.h"

#define __packed __attribute__((packed))

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

struct work {
        void (*fn)(struct work *work, int ssh_stdin);

        struct list_head node;
};

struct msg {
	uint8_t type;
	uint16_t len;
	uint8_t data[];
} __packed;

enum {
	MSG_SELECT_BOARD = 1,
	MSG_CONSOLE,
	MSG_HARDRESET,
	MSG_POWER_ON,
	MSG_POWER_OFF,
	MSG_FASTBOOT_REBOOT, // Messages handled by cdba.c
	MSG_FASTBOOT_PRESENT,
	MSG_FASTBOOT_DOWNLOAD,
	MSG_STATUS_UPDATE,
	MSG_VBUS_ON,
	MSG_VBUS_OFF,
	MSG_FASTBOOT_BOOT,
	MSG_SEND_BREAK,
	MSG_LIST_DEVICES,
	MSG_BOARD_INFO,
};

#define MAX_CDBA_CLIENT_VERBS 2
enum cdba_client_verbs {
	CDBA_LIST = 0,
	CDBA_INFO = 1,
};

void cdba_usage(const char *__progname);
int cdba_handle_opt(int argc, const char *argv);
void cdba_handle_verb(int argc, char **argv, int optind, const char *board);
int cdba_handle_message(struct msg *msg);
void cdba_end(void);

void cdba_client_usage(void);
void cdba_client_request_select_board(const char *board, uint8_t device_standalone, uint8_t device_lock);
void cdba_client_request_power_on(void);
void cdba_client_request_power_off(void);
int cdba_client_reached_timeout(void);
int cdba_client_main(int argc, char **argv, const char *opts);

#endif
