#ifndef NEWOS_SIMPLE_CONFIG_H
#define NEWOS_SIMPLE_CONFIG_H

#include <stddef.h>

typedef int (*SimpleConfigVisitor)(const char *key, const char *value, void *context);

int simple_config_parse_file(const char *path, SimpleConfigVisitor visitor, void *context);

#endif
