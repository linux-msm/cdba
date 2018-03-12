#ifndef __FASTBOOT_H__
#define __FASTBOOT_H__

struct fastboot;

struct fastboot_ops {
	void (*opened)(struct fastboot *, void *);
	void (*disconnect)(void *);
	void (*info)(struct fastboot *, const void *, size_t);
};

struct fastboot *fastboot_open(const char *serial, struct fastboot_ops *ops, void *);
int fastboot_getvar(struct fastboot *fb, const char *var, char *buf, size_t len);
int fastboot_download(struct fastboot *fb, const void *data, size_t len);
int fastboot_boot(struct fastboot *fb);
int fastboot_erase(struct fastboot *fb, const char *partition);
int fastboot_set_active(struct fastboot *fb, const char *active);
int fastboot_flash(struct fastboot *fb, const char *partition);
int fastboot_reboot(struct fastboot *fb);

#endif
