#ifndef __WATCH_H__
#define __WATCH_H__

void watch_add_readfd(int fd, int (*cb)(int, void*), void *data);
int watch_add_quit(int (*cb)(int, void*), void *data);
void watch_timer_add(int timeout_ms, void (*cb)(void *), void *data);
void watch_quit(void);
int watch_main_loop(bool (*quit_cb)(void));
int watch_run(void);

#endif
