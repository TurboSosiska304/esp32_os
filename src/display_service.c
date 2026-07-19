#include "display_service.h"

#include <stdio.h>

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_private/esp_clk.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "network_service.h"

#define DISPLAY_WIDTH 280
#define DISPLAY_HEIGHT 240
#define DISPLAY_SPI_CLOCK_HZ (40 * 1000 * 1000)
#define DISPLAY_QUEUE_LENGTH 4
#define DISPLAY_BACKGROUND 0x0841
#define DISPLAY_FOREGROUND 0xFFFF
#define DISPLAY_ACCENT 0x07E0
#define DISPLAY_WARNING 0xFD20
#define DISPLAY_DIM 0x7BEF

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

static const uint8_t LETTERS[][5] = {
    {0x3F, 0x02, 0x04, 0x02, 0x3F}, /* M */
    {0x00, 0x21, 0x3F, 0x21, 0x00}, /* I */
    {0x1E, 0x21, 0x21, 0x21, 0x12}, /* C */
    {0x3F, 0x09, 0x19, 0x29, 0x06}, /* R */
    {0x1E, 0x21, 0x21, 0x21, 0x1E}, /* O */
    {0x12, 0x25, 0x25, 0x25, 0x18}, /* S */
    {0x3F, 0x05, 0x09, 0x11, 0x3F}, /* N */
    {0x3F, 0x25, 0x25, 0x25, 0x1A}, /* B */
    {0x3F, 0x05, 0x05, 0x05, 0x02}, /* T */
    {0x3F, 0x25, 0x25, 0x25, 0x21}, /* E */
    {0x3F, 0x01, 0x01, 0x01, 0x01}, /* L */
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

static int display_letter_index(char character)
{
    switch (character) {
    case 'M': return 0;
    case 'I': return 1;
    case 'C': return 2;
    case 'R': return 3;
    case 'O': return 4;
    case 'S': return 5;
    case 'N': return 6;
    case 'B': return 7;
    case 'T': return 8;
    case 'E': return 9;
    case 'L': return 10;
    default: return -1;
    }
}

static void display_text(int x, int y, const char *text, uint8_t scale, uint16_t color)
{
    for (size_t offset = 0; text[offset] != '\0'; ++offset) {
        const char character = text[offset];
        const int glyph = display_letter_index(character);
        if (glyph >= 0) {
            for (int column = 0; column < 5; ++column) {
                for (int row = 0; row < 7; ++row) {
                    if ((LETTERS[glyph][column] & (1U << row)) != 0U) {
                        display_rectangle(x + (int)(offset * 6U * scale) + column * scale,
                                          y + row * scale, scale, scale, color);
                    }
                }
            }
        }
    }
}

static uint8_t display_compact_glyph(char character, int row)
{
    static const uint8_t digits[10][5] = {
        {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
        {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 2, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
    };
    static const uint8_t hex_letters[6][5] = {
        {2, 5, 7, 5, 5}, {6, 5, 6, 5, 6}, {3, 4, 4, 4, 3},
        {6, 5, 5, 5, 6}, {7, 4, 6, 4, 7}, {7, 4, 6, 4, 4},
    };
    static const uint8_t labels[9][5] = {
        {7, 5, 5, 5, 7}, {7, 4, 6, 4, 7}, {7, 5, 7, 5, 5},
        {7, 1, 2, 4, 7}, {5, 5, 5, 5, 7}, {5, 5, 5, 5, 2},
        {7, 1, 7, 4, 7}, {5, 5, 7, 5, 5}, {7, 2, 2, 2, 2},
    };

    if (character >= '0' && character <= '9') {
        return digits[character - '0'][row];
    }
    if (character >= 'A' && character <= 'F') {
        return hex_letters[character - 'A'][row];
    }
    switch (character) {
    case 'I': return labels[0][row];
    case 'P': return labels[1][row];
    case 'C': return labels[2][row];
    case 'U': return labels[3][row];
    case 'M': return labels[4][row];
    case 'V': return labels[5][row];
    case 'S': return labels[6][row];
    case 'H': return labels[7][row];
    case 'T': return labels[8][row];
    case ':': return row == 1 || row == 3 ? 2 : 0;
    case '.': return row == 4 ? 2 : 0;
    case '-': return row == 2 ? 7 : 0;
    default: return 0;
    }
}

static void display_compact_text(int x, int y, const char *text, uint8_t scale, uint16_t color)
{
    for (size_t offset = 0; text[offset] != '\0'; ++offset) {
        for (int row = 0; row < 5; ++row) {
            const uint8_t bitmap = display_compact_glyph(text[offset], row);
            for (int column = 0; column < 3; ++column) {
                if ((bitmap & (1U << (2 - column))) != 0U) {
                    display_rectangle(x + (int)(offset * 4U * scale) + column * scale, y + row * scale,
                                      scale, scale, color);
                }
            }
        }
    }
}

static void display_boot_screen(void)
{
    display_fill(DISPLAY_BACKGROUND);
    display_rectangle(0, 0, DISPLAY_WIDTH, 10, DISPLAY_ACCENT);
    display_rectangle(0, DISPLAY_HEIGHT - 10, DISPLAY_WIDTH, 10, DISPLAY_ACCENT);
    display_rectangle(24, 40, 232, 2, DISPLAY_DIM);
    display_text(43, 62, "MICRO OS", 5, DISPLAY_FOREGROUND);
    display_text(58, 132, "BOOTING", 3, DISPLAY_ACCENT);
    display_digit(93, 172, 1, 5, DISPLAY_WARNING);
    display_rectangle(126, 197, 9, 9, DISPLAY_WARNING);
    display_digit(146, 172, 0, 5, DISPLAY_WARNING);
    display_rectangle(179, 197, 9, 9, DISPLAY_WARNING);
    display_digit(199, 172, 0, 5, DISPLAY_WARNING);
}

static void display_draw_dashboard(uint32_t sequence, uint64_t uptime_ms)
{
    char ip_address[16];
    char mac_address[18];
    char cpu_text[12];

    network_service_get_ip(ip_address, sizeof(ip_address));
    network_service_get_mac(mac_address, sizeof(mac_address));
    snprintf(cpu_text, sizeof(cpu_text), "%lu", (unsigned long)(esp_clk_cpu_freq() / 1000000U));

    display_fill(DISPLAY_BACKGROUND);
    display_rectangle(0, 0, DISPLAY_WIDTH, 7, DISPLAY_ACCENT);
    display_compact_text(14, 20, "IP:", 3, DISPLAY_ACCENT);
    display_compact_text(58, 20, ip_address, 3, DISPLAY_FOREGROUND);
    display_rectangle(14, 48, 252, 1, DISPLAY_DIM);
    display_compact_text(14, 60, "MAC:", 2, DISPLAY_ACCENT);
    display_compact_text(56, 60, mac_address, 2, DISPLAY_FOREGROUND);
    display_rectangle(14, 82, 252, 1, DISPLAY_DIM);
    display_compact_text(14, 94, "CPU:", 3, DISPLAY_ACCENT);
    display_compact_text(74, 94, cpu_text, 3, DISPLAY_WARNING);
    display_compact_text(128, 94, "MHZ", 3, DISPLAY_WARNING);
    display_rectangle(14, 124, 252, 1, DISPLAY_DIM);
    display_compact_text(14, 138, "HB:", 3, DISPLAY_ACCENT);
    display_number(84, 132, sequence, 5, DISPLAY_FOREGROUND);
    display_compact_text(14, 184, "UP:", 3, DISPLAY_ACCENT);
    display_number(84, 178, (uint32_t)(uptime_ms / 1000U), 5, DISPLAY_WARNING);
    display_compact_text(220, 184, "S", 3, DISPLAY_WARNING);
    display_rectangle(0, DISPLAY_HEIGHT - 8, DISPLAY_WIDTH, 8, DISPLAY_ACCENT);
}

static void display_task(void *argument)
{
    micro_os_queue_t *events = argument;
    display_boot_screen();
    micro_os_sleep_ms(1500);
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
