#include "terminal_service.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "network_service.h"

#define TERMINAL_LINE_LENGTH 96
#define MICRO_OS_MAX_TASKS 12

static const char *const TAG = "terminal";

static void terminal_show_status(void)
{
    char ip_address[16];
    network_service_get_ip(ip_address, sizeof(ip_address));
    printf("Micro-OS %s\r\n", MICRO_OS_VERSION);
    printf("uptime: %llu ms\r\n", (unsigned long long)micro_os_uptime_ms());
    printf("heap free: %lu bytes\r\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    printf("wifi: %s, ip: %s\r\n", network_service_is_connected() ? "connected" : "offline", ip_address);
}

static void terminal_show_tasks(void)
{
    printf("id  priority  stack-free  name\r\n");
    for (micro_os_task_id_t task_id = 1; task_id <= MICRO_OS_MAX_TASKS; ++task_id) {
        micro_os_task_info_t info;
        if (micro_os_task_get_info(task_id, &info) == MICRO_OS_OK) {
            printf("%u   %u         %lu          %s\r\n", (unsigned)info.id, (unsigned)info.priority,
                   (unsigned long)info.stack_free_bytes, info.name);
        }
    }
}

static void terminal_execute(char *line)
{
    const char *delimiter = " \t\r\n";
    char *context;
    char *command = strtok_r(line, delimiter, &context);
    if (command == NULL) {
        return;
    }
    if (strcmp(command, "help") == 0) {
        printf("commands: help status tasks ip clear reboot\r\n");
    } else if (strcmp(command, "status") == 0) {
        terminal_show_status();
    } else if (strcmp(command, "tasks") == 0) {
        terminal_show_tasks();
    } else if (strcmp(command, "ip") == 0) {
        char ip_address[16];
        network_service_get_ip(ip_address, sizeof(ip_address));
        printf("%s\r\n", ip_address);
    } else if (strcmp(command, "clear") == 0) {
        printf("\033[2J\033[H");
    } else if (strcmp(command, "reboot") == 0) {
        printf("Restarting...\r\n");
        esp_restart();
    } else {
        printf("unknown command: %s\r\n", command);
    }
}

static void terminal_task(void *argument)
{
    (void)argument;
    char line[TERMINAL_LINE_LENGTH];

    printf("\r\nMicro-OS terminal\r\nType 'help' for commands.\r\nmos> ");
    fflush(stdout);
    for (;;) {
        if (fgets(line, sizeof(line), stdin) != NULL) {
            terminal_execute(line);
            printf("mos> ");
            fflush(stdout);
        }
        micro_os_sleep_ms(10);
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