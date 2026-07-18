#include "display_service.h"

#include <stdio.h>

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

#define DISPLAY_WIDTH 280
#define DISPLAY_HEIGHT 240
#define DISPLAY_SPI_CLOCK_HZ (40 * 1000 * 1000)
#define DISPLAY_QUEUE_LENGTH 4
#define DISPLAY_BACKGROUND 0x0841
#define DISPLAY_FOREGROUND 0xFFFF
#define DISPLAY_ACCENT 0x07E0
#define DISPLAY_WARNING 0xFD20

typedef struct {
    uint32_t sequence;
    uint64_t uptime_ms;
} display_event_t;

static const char *const TAG = "display";
static esp_lcd_panel_handle_t s_panel;
static micro_os_queue_t *s_events;
static uint16_t s_line_buffer[DISPLAY_WIDTH];

static const uint8_t DIGITS[10][5] = {
    {0x1E, 0x21, 0x21, 0x21, 0x1E}, {0x00, 0x22, 0x3F, 0x20, 0x00},
    {0x32, 0x29, 0x29, 0x29, 0x26}, {0x12, 0x21, 0x25, 0x25, 0x1A},
    {0x0C, 0x0A, 0x09, 0x3F, 0x08}, {0x17, 0x25, 0x25, 0x25, 0x19},
    {0x1E, 0x25, 0x25, 0x25, 0x18}, {0x01, 0x39, 0x05, 0x03, 0x01},
    {0x1A, 0x25, 0x25, 0x25, 0x1A}, {0x06, 0x29, 0x29, 0x29, 0x1E},
};

static void display_rectangle(int x, int y, int width, int height, uint16_t color)
{
    if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > DISPLAY_WIDTH || y + height > DISPLAY_HEIGHT) {
        return;
    }

    for (int column = 0; column < width; ++column) {
        s_line_buffer[column] = color;
    }
    for (int row = y; row < y + height; ++row) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, x, row, x + width, row + 1, s_line_buffer));
    }
}

static void display_fill(uint16_t color)
{
    display_rectangle(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, color);
}

static void display_digit(int x, int y, uint8_t digit, uint8_t scale, uint16_t color)
{
    if (digit > 9U) {
        return;
    }
    for (int column = 0; column < 5; ++column) {
        for (int row = 0; row < 7; ++row) {
            if ((DIGITS[digit][column] & (1U << row)) != 0U) {
                display_rectangle(x + column * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void display_number(int x, int y, uint32_t value, uint8_t scale, uint16_t color)
{
    char text[11];
    snprintf(text, sizeof(text), "%lu", (unsigned long)value);
    for (size_t index = 0; text[index] != '\0'; ++index) {
        display_digit(x + (int)(index * 6U * scale), y, (uint8_t)(text[index] - '0'), scale, color);
    }
}

static void display_draw_dashboard(uint32_t sequence, uint64_t uptime_ms)
{
    display_fill(DISPLAY_BACKGROUND);
    display_rectangle(0, 0, DISPLAY_WIDTH, 7, DISPLAY_ACCENT);
    display_rectangle(16, 32, 208, 2, DISPLAY_FOREGROUND);
    display_rectangle(16, 132, 208, 2, DISPLAY_FOREGROUND);
    display_number(28, 52, sequence, 8, DISPLAY_FOREGROUND);
    display_number(28, 154, (uint32_t)(uptime_ms / 1000U), 8, DISPLAY_WARNING);
    display_rectangle(16, 252, 208, 10, DISPLAY_ACCENT);
}

static void display_task(void *argument)
{
    micro_os_queue_t *events = argument;
    display_draw_dashboard(0, 0);

    for (;;) {
        display_event_t event;
        if (micro_os_queue_receive(events, &event, MICRO_OS_WAIT_FOREVER) == MICRO_OS_OK) {
            display_draw_dashboard(event.sequence, event.uptime_ms);
        }
    }
}

static void display_backlight_init(void)
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 12000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t channel_config = {
        .gpio_num = 8,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_7,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 1023,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
}

micro_os_status_t display_service_start(void)
{
    if (s_events != NULL) {
        return MICRO_OS_ERROR_STATE;
    }

    const spi_bus_config_t bus_config = {
        .sclk_io_num = 12,
        .mosi_io_num = 11,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * 40 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = 18,
        .cs_gpio_num = 10,
        .pclk_hz = DISPLAY_SPI_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io));

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = 17,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_config, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, 0, 20));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    display_backlight_init();

    s_events = micro_os_queue_create(sizeof(display_event_t), DISPLAY_QUEUE_LENGTH);
    if (s_events == NULL) {
        return MICRO_OS_ERROR_NO_MEMORY;
    }

    const micro_os_task_config_t task_config = {
        .name = "display",
        .entry = display_task,
        .argument = s_events,
        .stack_size_bytes = 6144,
        .priority = 3,
    };
    micro_os_task_id_t task_id;
    const micro_os_status_t status = micro_os_task_create(&task_config, &task_id);
    if (status == MICRO_OS_OK) {
        ESP_LOGI(TAG, "ST7789V3 display service started (task %u)", (unsigned)task_id);
    }
    return status;
}

micro_os_status_t display_service_publish_heartbeat(uint32_t sequence, uint64_t uptime_ms)
{
    if (s_events == NULL) {
        return MICRO_OS_ERROR_STATE;
    }
    const display_event_t event = {.sequence = sequence, .uptime_ms = uptime_ms};
    return micro_os_queue_send(s_events, &event, 0);
}
