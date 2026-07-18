#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "micro_os.h"

micro_os_status_t network_service_start(void);
bool network_service_is_connected(void);
void network_service_get_ip(char *buffer, size_t buffer_size);