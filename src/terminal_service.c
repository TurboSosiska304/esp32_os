#include "terminal_service.h"

#include <stdio.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "shell.h"

#define TERMINAL_LINE_LENGTH 96
#define TERMINAL_UART UART_NUM_0

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
        uint8_t character;
        const int received = uart_read_bytes(TERMINAL_UART, &character, 1, pdMS_TO_TICKS(20));
        if (received == 1) {
            if (character == '\r' || character == '\n') {
                if (line_length == 0U) {
                    continue;
                }
                line[line_length] = '\0';
                terminal_write(NULL, "\r\n");
                shell_execute_line(&session, line);
                line_length = 0;
                terminal_write(NULL, "mos> ");
            } else if ((character == '\b' || character == 127U) && line_length > 0U) {
                --line_length;
            } else if (character >= 32U && character < 127U && line_length < sizeof(line) - 1U) {
                line[line_length++] = (char)character;
            }
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