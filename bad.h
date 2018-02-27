#ifndef __BAD_H__
#define __BAD_H__

#include <stdbool.h>
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
};

void watch_add_readfd(int fd, int (*cb)(int, void*), void *data);
int watch_add_quit(int (*cb)(int, void*), void *data);
int watch_add_timer(void (*cb)(void*), void *data, unsigned interval, bool repeat);
void watch_quit(void);
int watch_run(void);

#endif
