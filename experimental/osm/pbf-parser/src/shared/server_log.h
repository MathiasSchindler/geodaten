#ifndef NEWOS_SERVER_LOG_H
#define NEWOS_SERVER_LOG_H

#include <stddef.h>

int server_log_escape_text(const char *input, char *buffer, size_t buffer_size);
int server_log_write(int fd, const char *component, const char *level, const char *message, const char *detail);

#endif
