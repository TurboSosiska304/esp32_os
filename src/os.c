#include "micro_os.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define OS_MAX_TASKS 12U
#define OS_MIN_STACK_SIZE 2048U

struct micro_os_queue {
    QueueHandle_t handle;
};

struct micro_os_mutex {
    SemaphoreHandle_t handle;
};

typedef struct {
    bool in_use;
    micro_os_task_id_t id;
    const char *name;
    uint8_t priority;
    TaskHandle_t handle;
} os_task_record_t;

static const char *const TAG = "micro_os";
static bool s_initialized;
static micro_os_task_id_t s_next_task_id = 1;
static os_task_record_t s_tasks[OS_MAX_TASKS];
static portMUX_TYPE s_task_lock = portMUX_INITIALIZER_UNLOCKED;

static TickType_t os_timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == MICRO_OS_WAIT_FOREVER) {
        return portMAX_DELAY;
    }

    return pdMS_TO_TICKS(timeout_ms);
}

static micro_os_status_t micro_os_status_from_freertos(BaseType_t result)
{
    return result == pdPASS ? MICRO_OS_OK : MICRO_OS_ERROR_TIMEOUT;
}

micro_os_status_t micro_os_init(void)
{
    if (s_initialized) {
        return MICRO_OS_ERROR_STATE;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Micro-OS %s initialized", MICRO_OS_VERSION);
    return MICRO_OS_OK;
}

micro_os_status_t micro_os_task_create(const micro_os_task_config_t *config, micro_os_task_id_t *task_id)
{
    if (!s_initialized || config == NULL || config->name == NULL || config->entry == NULL || task_id == NULL) {
        return MICRO_OS_ERROR_INVALID_ARGUMENT;
    }

    if (config->stack_size_bytes < OS_MIN_STACK_SIZE) {
        return MICRO_OS_ERROR_INVALID_ARGUMENT;
    }

    size_t slot = OS_MAX_TASKS;
    taskENTER_CRITICAL(&s_task_lock);
    for (size_t index = 0; index < OS_MAX_TASKS; ++index) {
        if (!s_tasks[index].in_use) {
            slot = index;
            s_tasks[index].in_use = true;
            s_tasks[index].id = s_next_task_id++;
            s_tasks[index].name = config->name;
            s_tasks[index].priority = config->priority;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_task_lock);

    if (slot == OS_MAX_TASKS) {
        return MICRO_OS_ERROR_LIMIT_REACHED;
    }

    const BaseType_t result = xTaskCreate(config->entry, config->name,
                                          config->stack_size_bytes / sizeof(StackType_t),
                                          config->argument, config->priority,
                                          &s_tasks[slot].handle);
    if (result != pdPASS) {
        taskENTER_CRITICAL(&s_task_lock);
        s_tasks[slot].in_use = false;
        taskEXIT_CRITICAL(&s_task_lock);
        return MICRO_OS_ERROR_NO_MEMORY;
    }

    *task_id = s_tasks[slot].id;
    ESP_LOGI(TAG, "Task %u (%s) started", (unsigned)*task_id, config->name);
    return MICRO_OS_OK;
}

micro_os_status_t micro_os_task_get_info(micro_os_task_id_t task_id, micro_os_task_info_t *info)
{
    if (!s_initialized || info == NULL) {
        return MICRO_OS_ERROR_INVALID_ARGUMENT;
    }

    taskENTER_CRITICAL(&s_task_lock);
    for (size_t index = 0; index < OS_MAX_TASKS; ++index) {
        if (s_tasks[index].in_use && s_tasks[index].id == task_id) {
            info->id = s_tasks[index].id;
            info->name = s_tasks[index].name;
            info->priority = s_tasks[index].priority;
            info->stack_free_bytes = uxTaskGetStackHighWaterMark(s_tasks[index].handle) * sizeof(StackType_t);
            info->suspended = eTaskGetState(s_tasks[index].handle) == eSuspended;
            taskEXIT_CRITICAL(&s_task_lock);
            return MICRO_OS_OK;
        }
    }
    taskEXIT_CRITICAL(&s_task_lock);

    return MICRO_OS_ERROR_INVALID_ARGUMENT;
}

static os_task_record_t *micro_os_find_task(micro_os_task_id_t task_id)
{
    for (size_t index = 0; index < OS_MAX_TASKS; ++index) {
        if (s_tasks[index].in_use && s_tasks[index].id == task_id) {
            return &s_tasks[index];
        }
    }
    return NULL;
}

micro_os_status_t micro_os_task_set_priority(micro_os_task_id_t task_id, uint8_t priority)
{
    if (!s_initialized || priority >= configMAX_PRIORITIES) {
        return MICRO_OS_ERROR_INVALID_ARGUMENT;
    }

    taskENTER_CRITICAL(&s_task_lock);
    os_task_record_t *task = micro_os_find_task(task_id);
    if (task != NULL) {
        task->priority = priority;
        vTaskPrioritySet(task->handle, priority);
    }
    taskEXIT_CRITICAL(&s_task_lock);
    return task == NULL ? MICRO_OS_ERROR_INVALID_ARGUMENT : MICRO_OS_OK;
}

micro_os_status_t micro_os_task_suspend(micro_os_task_id_t task_id)
{
    if (!s_initialized) {
        return MICRO_OS_ERROR_STATE;
    }

    taskENTER_CRITICAL(&s_task_lock);
    os_task_record_t *task = micro_os_find_task(task_id);
    if (task != NULL) {
        vTaskSuspend(task->handle);
    }
    taskEXIT_CRITICAL(&s_task_lock);
    return task == NULL ? MICRO_OS_ERROR_INVALID_ARGUMENT : MICRO_OS_OK;
}

micro_os_status_t micro_os_task_resume(micro_os_task_id_t task_id)
{
    if (!s_initialized) {
        return MICRO_OS_ERROR_STATE;
    }

    taskENTER_CRITICAL(&s_task_lock);
    os_task_record_t *task = micro_os_find_task(task_id);
    if (task != NULL) {
        vTaskResume(task->handle);
    }
    taskEXIT_CRITICAL(&s_task_lock);
    return task == NULL ? MICRO_OS_ERROR_INVALID_ARGUMENT : MICRO_OS_OK;
}

micro_os_status_t micro_os_get_system_info(micro_os_system_info_t *info)
{
    if (!s_initialized || info == NULL) {
        return MICRO_OS_ERROR_INVALID_ARGUMENT;
    }

    uint16_t task_count = 0;
    taskENTER_CRITICAL(&s_task_lock);
    for (size_t index = 0; index < OS_MAX_TASKS; ++index) {
        if (s_tasks[index].in_use) {
            ++task_count;
        }
    }
    taskEXIT_CRITICAL(&s_task_lock);

    info->task_count = task_count;
    info->heap_free_bytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    info->heap_minimum_free_bytes = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    return MICRO_OS_OK;
}

uint64_t micro_os_uptime_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000U;
}

void micro_os_sleep_ms(uint32_t duration_ms)
{
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
}

void *micro_os_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

void micro_os_free(void *memory)
{
    heap_caps_free(memory);
}

micro_os_queue_t *micro_os_queue_create(size_t item_size, size_t capacity)
{
    if (item_size == 0U || capacity == 0U) {
        return NULL;
    }

    micro_os_queue_t *queue = micro_os_malloc(sizeof(*queue));
    if (queue == NULL) {
        return NULL;
    }

    queue->handle = xQueueCreate(capacity, item_size);
    if (queue->handle == NULL) {
        micro_os_free(queue);
        return NULL;
    }
    return queue;
}

void micro_os_queue_destroy(micro_os_queue_t *queue)
{
    if (queue != NULL) {
        vQueueDelete(queue->handle);
        micro_os_free(queue);
    }
}

micro_os_status_t micro_os_queue_send(micro_os_queue_t *queue, const void *item, uint32_t timeout_ms)
{
    if (queue == NULL || item == NULL) {
        return MICRO_OS_ERROR_INVALID_ARGUMENT;
    }
    return micro_os_status_from_freertos(xQueueSend(queue->handle, item, os_timeout_to_ticks(timeout_ms)));
}

micro_os_status_t micro_os_queue_receive(micro_os_queue_t *queue, void *item, uint32_t timeout_ms)
{
    if (queue == NULL || item == NULL) {
        return MICRO_OS_ERROR_INVALID_ARGUMENT;
    }
    return micro_os_status_from_freertos(xQueueReceive(queue->handle, item, os_timeout_to_ticks(timeout_ms)));
}

micro_os_mutex_t *micro_os_mutex_create(void)
{
    micro_os_mutex_t *mutex = micro_os_malloc(sizeof(*mutex));
    if (mutex == NULL) {
        return NULL;
    }

    mutex->handle = xSemaphoreCreateMutex();
    if (mutex->handle == NULL) {
        micro_os_free(mutex);
        return NULL;
    }
    return mutex;
}

void micro_os_mutex_destroy(micro_os_mutex_t *mutex)
{
    if (mutex != NULL) {
        vSemaphoreDelete(mutex->handle);
        micro_os_free(mutex);
    }
}

micro_os_status_t micro_os_mutex_lock(micro_os_mutex_t *mutex, uint32_t timeout_ms)
{
    if (mutex == NULL) {
        return MICRO_OS_ERROR_INVALID_ARGUMENT;
    }
    return micro_os_status_from_freertos(xSemaphoreTake(mutex->handle, os_timeout_to_ticks(timeout_ms)));
}

micro_os_status_t micro_os_mutex_unlock(micro_os_mutex_t *mutex)
{
    if (mutex == NULL) {
        return MICRO_OS_ERROR_INVALID_ARGUMENT;
    }
    return micro_os_status_from_freertos(xSemaphoreGive(mutex->handle));
}