#include "shell.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_system.h"
#include "micro_os.h"
#include "network_service.h"

#define SHELL_MAX_TASKS 12U

static void shell_writef(const shell_session_t *session, const char *format, ...)
{
    char buffer[192];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    session->write(session->context, buffer);
}

static bool shell_parse_unsigned(const char *text, unsigned long maximum, unsigned long *value)
{
    char *end;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (*text == '\0' || *end != '\0' || parsed > maximum) {
        return false;
    }
    *value = parsed;
    return true;
}

static void shell_show_status(const shell_session_t *session)
{
    char ip_address[16];
    micro_os_system_info_t info;
    micro_os_get_system_info(&info);
    network_service_get_ip(ip_address, sizeof(ip_address));
    shell_writef(session, "Micro-OS %s\r\nuptime: %llu ms\r\ntasks: %u\r\nheap: %lu B free, %lu B minimum\r\nwifi: %s, ip: %s\r\n",
                 MICRO_OS_VERSION, (unsigned long long)micro_os_uptime_ms(), (unsigned)info.task_count,
                 (unsigned long)info.heap_free_bytes, (unsigned long)info.heap_minimum_free_bytes,
                 network_service_is_connected() ? "connected" : "offline", ip_address);
}

static void shell_show_tasks(const shell_session_t *session)
{
    shell_writef(session, "id  state  priority  stack-free  name\r\n");
    for (micro_os_task_id_t task_id = 1; task_id <= SHELL_MAX_TASKS; ++task_id) {
        micro_os_task_info_t info;
        if (micro_os_task_get_info(task_id, &info) == MICRO_OS_OK) {
            shell_writef(session, "%u   %s  %u         %lu          %s\r\n", (unsigned)info.id,
                         info.suspended ? "stop " : "run  ", (unsigned)info.priority,
                         (unsigned long)info.stack_free_bytes, info.name);
        }
    }
}

void shell_print_banner(const shell_session_t *session)
{
    shell_writef(session, "\r\nMicro-OS terminal\r\nType 'help' for commands.\r\n");
}

void shell_execute_line(const shell_session_t *session, char *line)
{
    char *context;
    char *command = strtok_r(line, " \t\r\n", &context);
    if (command == NULL) {
        return;
    }

    if (strcmp(command, "help") == 0) {
        shell_writef(session, "help status uptime mem tasks ip echo <text> clear reboot\r\n"
                             "prio <id> <0-24> suspend <id> resume <id>\r\n");
    } else if (strcmp(command, "status") == 0) {
        shell_show_status(session);
    } else if (strcmp(command, "uptime") == 0) {
        shell_writef(session, "%llu ms\r\n", (unsigned long long)micro_os_uptime_ms());
    } else if (strcmp(command, "mem") == 0) {
        micro_os_system_info_t info;
        micro_os_get_system_info(&info);
        shell_writef(session, "free: %lu B, minimum: %lu B\r\n", (unsigned long)info.heap_free_bytes,
                     (unsigned long)info.heap_minimum_free_bytes);
    } else if (strcmp(command, "tasks") == 0) {
        shell_show_tasks(session);
    } else if (strcmp(command, "ip") == 0) {
        char ip_address[16];
        network_service_get_ip(ip_address, sizeof(ip_address));
        shell_writef(session, "%s\r\n", ip_address);
    } else if (strcmp(command, "echo") == 0) {
        char *text = strtok_r(NULL, "\r\n", &context);
        shell_writef(session, "%s\r\n", text == NULL ? "" : text);
    } else if (strcmp(command, "clear") == 0) {
        shell_writef(session, "\033[2J\033[H");
    } else if (strcmp(command, "reboot") == 0) {
        shell_writef(session, "Restarting...\r\n");
        esp_restart();
    } else if (strcmp(command, "prio") == 0 || strcmp(command, "suspend") == 0 || strcmp(command, "resume") == 0) {
        char *id_text = strtok_r(NULL, " \t\r\n", &context);
        unsigned long id;
        if (id_text == NULL || !shell_parse_unsigned(id_text, UINT16_MAX, &id)) {
            shell_writef(session, "invalid task id\r\n");
            return;
        }
        micro_os_status_t status;
        if (strcmp(command, "prio") == 0) {
            char *priority_text = strtok_r(NULL, " \t\r\n", &context);
            unsigned long priority;
            if (priority_text == NULL || !shell_parse_unsigned(priority_text, 24, &priority)) {
                shell_writef(session, "usage: prio <id> <0-24>\r\n");
                return;
            }
            status = micro_os_task_set_priority((micro_os_task_id_t)id, (uint8_t)priority);
        } else if (strcmp(command, "suspend") == 0) {
            status = micro_os_task_suspend((micro_os_task_id_t)id);
        } else {
            status = micro_os_task_resume((micro_os_task_id_t)id);
        }
        shell_writef(session, status == MICRO_OS_OK ? "ok\r\n" : "operation failed\r\n");
    } else {
        shell_writef(session, "unknown command: %s\r\n", command);
    }
}