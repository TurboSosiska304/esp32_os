#include "terminal_service.h"

#include <stdio.h>

#include "esp_log.h"
#include "shell.h"

#define TERMINAL_LINE_LENGTH 96

static const char *const TAG = "terminal";

static void terminal_write(void *context, const char *text)
{
    (void)context;
    fputs(text, stdout);
    fflush(stdout);
}

static void terminal_task(void *argument)
{
    (void)argument;
    char line[TERMINAL_LINE_LENGTH];
    size_t line_length = 0;
    const shell_session_t session = {.write = terminal_write, .context = NULL};

    shell_print_banner(&session);
    terminal_write(NULL, "mos> ");
    for (;;) {
        const int character = fgetc(stdin);
        if (character == EOF) {
            micro_os_sleep_ms(20);
            continue;
        }
        if (character == '\r' || character == '\n') {
            if (line_length == 0U) {
                continue;
            }
            line[line_length] = '\0';
            shell_execute_line(&session, line);
            line_length = 0;
            terminal_write(NULL, "mos> ");
        } else if ((character == '\b' || character == 127) && line_length > 0U) {
            --line_length;
        } else if (character >= 32 && character < 127 && line_length < sizeof(line) - 1U) {
            line[line_length++] = (char)character;
        }
    }
}

micro_os_status_t terminal_service_start(void)
{
    const micro_os_task_config_t task_config = {
        .name = "terminal",
        .entry = terminal_task,
        .argument = NULL,
        .stack_size_bytes = 4096,
        .priority = 3,
    };
    micro_os_task_id_t task_id;
    const micro_os_status_t status = micro_os_task_create(&task_config, &task_id);
    if (status == MICRO_OS_OK) {
        ESP_LOGI(TAG, "USB serial terminal started (task %u)", (unsigned)task_id);
    }
    return status;
}