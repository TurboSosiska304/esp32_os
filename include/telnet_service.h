#pragma once

#include "micro_os.h"

/** Starts a single-client, plaintext Telnet shell on TCP port 23. */
micro_os_status_t telnet_service_start(void);