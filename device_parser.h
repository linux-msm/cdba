#ifndef __DEVICE_PARSER_H__
#define __DEVICE_PARSER_H__

struct device_parser;

int device_parser_accept(struct device_parser *dp, int type,
			 char *scalar,  size_t scalar_len);
bool device_parser_expect(struct device_parser *dp, int type,
			  char *scalar,  size_t scalar_len);

int device_parser(const char *path);

#endif
