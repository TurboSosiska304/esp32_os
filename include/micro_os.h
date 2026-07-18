#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** The initial version of the ESP32-S3 microkernel API. */
#define MICRO_OS_VERSION "0.1.0"
#define MICRO_OS_WAIT_FOREVER UINT32_MAX

typedef enum {
    MICRO_OS_OK = 0,
    MICRO_OS_ERROR_INVALID_ARGUMENT = -1,
    MICRO_OS_ERROR_NO_MEMORY = -2,
    MICRO_OS_ERROR_LIMIT_REACHED = -3,
    MICRO_OS_ERROR_TIMEOUT = -4,
    MICRO_OS_ERROR_STATE = -5,
} micro_os_status_t;

typedef uint16_t micro_os_task_id_t;
typedef void (*micro_os_task_entry_t)(void *argument);

typedef struct micro_os_queue micro_os_queue_t;
typedef struct micro_os_mutex micro_os_mutex_t;

typedef struct {
    const char *name;
    micro_os_task_entry_t entry;
    void *argument;
    uint32_t stack_size_bytes;
    uint8_t priority;
} micro_os_task_config_t;

typedef struct {
    micro_os_task_id_t id;
    const char *name;
    uint8_t priority;
    uint32_t stack_free_bytes;
    bool suspended;
} micro_os_task_info_t;

typedef struct {
    uint16_t task_count;
    size_t heap_free_bytes;
    size_t heap_minimum_free_bytes;
} micro_os_system_info_t;

micro_os_status_t micro_os_init(void);
micro_os_status_t micro_os_task_create(const micro_os_task_config_t *config, micro_os_task_id_t *task_id);
micro_os_status_t micro_os_task_get_info(micro_os_task_id_t task_id, micro_os_task_info_t *info);
micro_os_status_t micro_os_task_set_priority(micro_os_task_id_t task_id, uint8_t priority);
micro_os_status_t micro_os_task_suspend(micro_os_task_id_t task_id);
micro_os_status_t micro_os_task_resume(micro_os_task_id_t task_id);
micro_os_status_t micro_os_get_system_info(micro_os_system_info_t *info);

uint64_t micro_os_uptime_ms(void);
void micro_os_sleep_ms(uint32_t duration_ms);

void *micro_os_malloc(size_t size);
void micro_os_free(void *memory);

micro_os_queue_t *micro_os_queue_create(size_t item_size, size_t capacity);
void micro_os_queue_destroy(micro_os_queue_t *queue);
micro_os_status_t micro_os_queue_send(micro_os_queue_t *queue, const void *item, uint32_t timeout_ms);
micro_os_status_t micro_os_queue_receive(micro_os_queue_t *queue, void *item, uint32_t timeout_ms);

micro_os_mutex_t *micro_os_mutex_create(void);
void micro_os_mutex_destroy(micro_os_mutex_t *mutex);
micro_os_status_t micro_os_mutex_lock(micro_os_mutex_t *mutex, uint32_t timeout_ms);
micro_os_status_t micro_os_mutex_unlock(micro_os_mutex_t *mutex);