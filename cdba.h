#ifndef __CDBA_H__
#define __CDBA_H__

#include <stdint.h>

#define __packed __attribute__((packed))

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

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
	MSG_FASTBOOT_PRESENT,
	MSG_FASTBOOT_DOWNLOAD,
	MSG_FASTBOOT_BOOT,
	MSG_STATUS_UPDATE,
	MSG_VBUS_ON,
	MSG_VBUS_OFF,
	MSG_FASTBOOT_REBOOT,
	MSG_SEND_BREAK,
	MSG_LIST_DEVICES,
	MSG_BOARD_INFO,
	MSG_FASTBOOT_CONTINUE,
	MSG_CAPTURE_IMAGE,
};

#endif
