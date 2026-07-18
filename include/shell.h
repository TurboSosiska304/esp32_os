#pragma once

#include <stddef.h>

typedef void (*shell_write_fn_t)(void *context, const char *text);

typedef struct {
    shell_write_fn_t write;
    void *context;
} shell_session_t;

void shell_print_banner(const shell_session_t *session);
void shell_execute_line(const shell_session_t *session, char *line);