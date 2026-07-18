#pragma once

#include <stdint.h>

#include "micro_os.h"

/** Starts the ST7789V3 display service. It must be called once after micro_os_init(). */
micro_os_status_t display_service_start(void);

/** Publishes current system state to the display service without accessing SPI from the caller. */
micro_os_status_t display_service_publish_heartbeat(uint32_t sequence, uint64_t uptime_ms);