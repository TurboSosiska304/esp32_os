#include "telnet_service.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "network_service.h"
#include "shell.h"

#define TELNET_PORT 23
#define TELNET_LINE_LENGTH 96
#define TELNET_BACKLOG 1

static const char *const TAG = "telnet";

static void telnet_write(void *context, const char *text)
{
    const int socket_fd = *(const int *)context;
    size_t remaining = strlen(text);
    const char *cursor = text;
    while (remaining > 0U) {
        const int sent = send(socket_fd, cursor, remaining, 0);
        if (sent <= 0) {
            return;
        }
        cursor += sent;
        remaining -= (size_t)sent;
    }
}

static void telnet_handle_client(int client_fd)
{
    char line[TELNET_LINE_LENGTH];
    size_t line_length = 0;
    bool negotiation = false;
    const shell_session_t session = {.write = telnet_write, .context = &client_fd};

    shell_print_banner(&session);
    telnet_write(&client_fd, "mos> ");
    for (;;) {
        char input[32];
        const int received = recv(client_fd, input, sizeof(input), 0);
        if (received <= 0) {
            return;
        }
        for (int index = 0; index < received; ++index) {
            const unsigned char character = (unsigned char)input[index];
            if (negotiation) {
                negotiation = false;
                continue;
            }
            if (character == 255U) {
                negotiation = true;
                continue;
            }
            if (character == '\r') {
                continue;
            }
            if (character == '\n') {
                line[line_length] = '\0';
                shell_execute_line(&session, line);
                line_length = 0;
                telnet_write(&client_fd, "mos> ");
                continue;
            }
            if ((character == '\b' || character == 127U) && line_length > 0U) {
                --line_length;
                telnet_write(&client_fd, "\b \b");
                continue;
            }
            if (character >= 32U && character < 127U && line_length < sizeof(line) - 1U) {
                line[line_length++] = (char)character;
                char echo[2] = {(char)character, '\0'};
                telnet_write(&client_fd, echo);
            }
        }
    }
}

static void telnet_task(void *argument)
{
    (void)argument;
    for (;;) {
        while (!network_service_is_connected()) {
            micro_os_sleep_ms(500);
        }

        const int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (listen_fd < 0) {
            ESP_LOGE(TAG, "Cannot create socket: errno %d", errno);
            micro_os_sleep_ms(1000);
            continue;
        }
        const int reuse_address = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address));
        const struct sockaddr_in address = {
            .sin_family = AF_INET,
            .sin_port = htons(TELNET_PORT),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };
        if (bind(listen_fd, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
            listen(listen_fd, TELNET_BACKLOG) != 0) {
            ESP_LOGE(TAG, "Cannot listen on port %d: errno %d", TELNET_PORT, errno);
            close(listen_fd);
            micro_os_sleep_ms(1000);
            continue;
        }

        ESP_LOGI(TAG, "Telnet shell listening on TCP port %d", TELNET_PORT);
        struct sockaddr_in client_address;
        socklen_t address_length = sizeof(client_address);
        const int client_fd = accept(listen_fd, (struct sockaddr *)&client_address, &address_length);
        if (client_fd >= 0) {
            ESP_LOGI(TAG, "Client connected from %s", inet_ntoa(client_address.sin_addr));
            telnet_handle_client(client_fd);
            close(client_fd);
            ESP_LOGI(TAG, "Client disconnected");
        }
        close(listen_fd);
    }
}

micro_os_status_t telnet_service_start(void)
{
    const micro_os_task_config_t task_config = {
        .name = "telnet",
        .entry = telnet_task,
        .argument = NULL,
        .stack_size_bytes = 6144,
        .priority = 2,
    };
    micro_os_task_id_t task_id;
    const micro_os_status_t status = micro_os_task_create(&task_config, &task_id);
    if (status == MICRO_OS_OK) {
        ESP_LOGI(TAG, "Telnet service started (task %u)", (unsigned)task_id);
    }
    return status;
}