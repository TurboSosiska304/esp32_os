#include <inttypes.h>
#include <stdio.h>

#include "display_service.h"
#include "esp_log.h"
#include "micro_os.h"
#include "network_service.h"
#include "terminal_service.h"
#include "telnet_service.h"

static const char *const TAG = "app";

typedef struct {
	uint32_t sequence;
	uint64_t timestamp_ms;
} heartbeat_event_t;

static micro_os_queue_t *s_events;

static void heartbeat_task(void *argument)
{
	micro_os_queue_t *events = argument;
	uint32_t sequence = 0;

	for (;;) {
		const heartbeat_event_t event = {
			.sequence = sequence++,
			.timestamp_ms = micro_os_uptime_ms(),
		};

		if (micro_os_queue_send(events, &event, MICRO_OS_WAIT_FOREVER) != MICRO_OS_OK) {
			ESP_LOGE(TAG, "Failed to publish heartbeat");
		}
		micro_os_sleep_ms(1000);
	}
}

static void monitor_task(void *argument)
{
	micro_os_queue_t *events = argument;

	for (;;) {
		heartbeat_event_t event;
		if (micro_os_queue_receive(events, &event, MICRO_OS_WAIT_FOREVER) == MICRO_OS_OK) {
			ESP_LOGI(TAG, "Event %" PRIu32 " at %" PRIu64 " ms", event.sequence, event.timestamp_ms);
			if (display_service_publish_heartbeat(event.sequence, event.timestamp_ms) != MICRO_OS_OK) {
				ESP_LOGW(TAG, "Display event queue is full");
			}
		}
	}
}

void app_main(void)
{
	if (micro_os_init() != MICRO_OS_OK) {
		ESP_LOGE(TAG, "Micro-OS initialization failed");
		return;
	}

	if (display_service_start() != MICRO_OS_OK) {
		ESP_LOGE(TAG, "Cannot start ST7789V3 display service");
		return;
	}

	if (network_service_start() != MICRO_OS_OK) {
		ESP_LOGE(TAG, "Cannot start Wi-Fi service");
		return;
	}

	if (terminal_service_start() != MICRO_OS_OK) {
		ESP_LOGE(TAG, "Cannot start terminal service");
		return;
	}

	if (telnet_service_start() != MICRO_OS_OK) {
		ESP_LOGE(TAG, "Cannot start Telnet service");
		return;
	}

	s_events = micro_os_queue_create(sizeof(heartbeat_event_t), 8);
	if (s_events == NULL) {
		ESP_LOGE(TAG, "Cannot create event queue");
		return;
	}

	const micro_os_task_config_t heartbeat = {
		.name = "heartbeat",
		.entry = heartbeat_task,
		.argument = s_events,
		.stack_size_bytes = 3072,
		.priority = 5,
	};
	const micro_os_task_config_t monitor = {
		.name = "monitor",
		.entry = monitor_task,
		.argument = s_events,
		.stack_size_bytes = 3072,
		.priority = 4,
	};
	micro_os_task_id_t heartbeat_id;
	micro_os_task_id_t monitor_id;

	if (micro_os_task_create(&heartbeat, &heartbeat_id) != MICRO_OS_OK ||
		micro_os_task_create(&monitor, &monitor_id) != MICRO_OS_OK) {
		ESP_LOGE(TAG, "Cannot start application services");
		return;
	}

	ESP_LOGI(TAG, "Application services started: heartbeat=%u monitor=%u",
			 (unsigned)heartbeat_id, (unsigned)monitor_id);
}