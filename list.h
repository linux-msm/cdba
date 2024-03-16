/*
 * Copyright (c) 2016, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __LIST_H__
#define __LIST_H__

#include <stdbool.h>
#include <stddef.h>

#define container_of(ptr, type, member) ({ \
		typeof(((type *)0)->member)*__mptr = (ptr);  \
		(type *)(void *)((char *)__mptr - offsetof(type, member)); \
		})

struct list_head {
	struct list_head *prev;
	struct list_head *next;
};

#define LIST_INIT(list) { &(list), &(list) }

static inline void list_init(struct list_head *list)
{
	list->prev = list->next = list;
}

static inline bool list_empty(struct list_head *list)
{
	return list->next == list;
}

static inline void list_add(struct list_head *list, struct list_head *item)
{
	struct list_head *prev = list->prev;

	item->next = list;
	item->prev = prev;

	prev->next = list->prev = item;
}

static inline void list_del(struct list_head *item)
{
	item->prev->next = item->next;
	item->next->prev = item->prev;
}

#define list_for_each(item, list) \
	for (item = (list)->next; item != list; item = item->next)

#define list_for_each_safe(item, next, list) \
	for (item = (list)->next, next = item->next; item != list; item = next, next = item->next)

#define list_entry(item, type, member) \
	container_of(item, type, member)

#define list_entry_first(list, type, member) \
	container_of((list)->next, type, member)

#define list_entry_next(item, member) \
	container_of((item)->member.next, typeof(*(item)), member)

#define list_for_each_entry(item, list, member) \
	for (item = list_entry_first(list, typeof(*(item)), member); \
	     &item->member != list; \
	     item = list_entry_next(item, member))

#define list_for_each_entry_safe(item, next, list, member) \
	for (item = list_entry_first(list, typeof(*(item)), member), \
	     next = list_entry_next(item, member); \
	     &item->member != list; \
	     item = next, \
	     next = list_entry_next(item, member)) \

#endif
