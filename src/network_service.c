#include "network_service.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "network_config.h"

static const char *const TAG = "network";
static bool s_connected;
static char s_ip_address[16] = "0.0.0.0";

static void network_event_handler(void *argument, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)argument;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_ERROR_CHECK(esp_wifi_connect());
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        strcpy(s_ip_address, "0.0.0.0");
        ESP_LOGW(TAG, "Wi-Fi disconnected; retrying");
        ESP_ERROR_CHECK(esp_wifi_connect());
        return;
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_ip_address, sizeof(s_ip_address));
        s_connected = true;
        ESP_LOGI(TAG, "Connected to %s; IP: %s", NETWORK_WIFI_SSID, s_ip_address);
    }
}

micro_os_status_t network_service_start(void)
{
    if (strcmp(NETWORK_WIFI_SSID, "YOUR_WIFI_SSID") == 0 ||
        strcmp(NETWORK_WIFI_PASSWORD, "YOUR_WIFI_PASSWORD") == 0) {
        ESP_LOGW(TAG, "Wi-Fi disabled: set NETWORK_WIFI_SSID and NETWORK_WIFI_PASSWORD in network_config.h");
        return MICRO_OS_OK;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &network_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &network_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {.capable = true, .required = false},
        },
    };
    strlcpy((char *)wifi_config.sta.ssid, NETWORK_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, NETWORK_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Connecting to configured Wi-Fi network");
    return MICRO_OS_OK;
}

bool network_service_is_connected(void)
{
    return s_connected;
}

void network_service_get_ip(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U) {
        return;
    }
    strlcpy(buffer, s_ip_address, buffer_size);
}