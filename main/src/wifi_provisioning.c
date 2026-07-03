
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <string.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "inc/wifi_provisioning.h"
#include "inc/wifi_web.h"
#include "inc/dns_server.h"
#include "inc/wifi_storage.h"


#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define WIFI_MAX_RETRIES   5

#define PROV_AP_SSID       "SETD_Provisioning"
#define PROV_AP_PASSWORD   ""
#define PROV_AP_CHANNEL    1
#define PROV_AP_MAX_CONN   4

static EventGroupHandle_t s_wifi_event_group;

static dns_server_handle_t s_dns_handle = NULL;

static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;

static const char *TAG = "[wifi_prov]";

static wifi_prov_state_t s_state = WIFI_PROV_STATE_IDLE;
static int s_retry_count = 0;

static char current_ssid[33] = {0};
static char current_password[65] = {0};

static void wifi_set_state(wifi_prov_state_t state)
{
    s_state = state;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA started");
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;

        ESP_LOGW(TAG, "STA disconnected, reason=%d", disc->reason);

        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        if (s_state == WIFI_PROV_STATE_CONNECTING &&
            s_retry_count < WIFI_MAX_RETRIES) {
            s_retry_count++;

            ESP_LOGW(TAG, "Retrying WiFi connection %d/%d", s_retry_count, WIFI_MAX_RETRIES);

            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "WiFi connection failed");
            memset(current_ssid, 0, sizeof(current_ssid));
            memset(current_password, 0, sizeof(current_password));
            wifi_set_state(WIFI_PROV_STATE_CONNECT_FAILED);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "Provisioning AP started");
        wifi_set_state(WIFI_PROV_STATE_AP_ACTIVE);
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Client connected to AP, AID=%d", event->aid);
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Client disconnected from AP, AID=%d", event->aid);
    }

    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        s_retry_count = 0;
        wifi_set_state(WIFI_PROV_STATE_CONNECTED);

        wifi_storage_save_credentials(current_ssid, current_password);

        
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_prov_init(void)
{
    esp_err_t err;

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    if (s_sta_netif == NULL || s_ap_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create netifs");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        wifi_event_handler,
        NULL
    ));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        wifi_event_handler,
        NULL
    ));

    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_set_state(WIFI_PROV_STATE_IDLE);

    ESP_LOGI(TAG, "WiFi provisioning initialized");

    return ESP_OK;
}

esp_err_t wifi_prov_start_ap(void)
{
    wifi_config_t ap_config = {
        .ap = {
            .ssid = PROV_AP_SSID,
            .ssid_len = strlen(PROV_AP_SSID),
            .channel = PROV_AP_CHANNEL,
            .password = PROV_AP_PASSWORD,
            .max_connection = PROV_AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_LOGI(TAG, "Starting provisioning AP: %s", PROV_AP_SSID);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    wifi_set_state(WIFI_PROV_STATE_AP_ACTIVE);

    // Start the DNS server that will redirect all queries to the softAP IP
    dns_server_config_t dns_config = DNS_SERVER_CONFIG_SINGLE("*" /* all A queries */, "WIFI_AP_DEF" /* softAP netif ID */);
    s_dns_handle = start_dns_server(&dns_config);

    return ESP_OK;
}

esp_err_t wifi_prov_connect(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t sta_config = {0};

    snprintf((char *)sta_config.sta.ssid, sizeof(sta_config.sta.ssid), "%s", ssid);

    if (password != NULL) {
        snprintf((char *)sta_config.sta.password, sizeof(sta_config.sta.password), "%s", password);
    }

    s_retry_count = 0;
    wifi_set_state(WIFI_PROV_STATE_CONNECTING);

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    current_ssid[0] = '\0';
    current_password[0] = '\0';
    strncpy(current_ssid, ssid, sizeof(current_ssid) - 1);
    if (password != NULL) {
        strncpy(current_password, password, sizeof(current_password) - 1);
    }

    return esp_wifi_connect();
}

wifi_prov_state_t wifi_prov_get_state(void)
{
    return s_state;
}

esp_err_t wifi_prov_scan(wifi_ap_record_t *records, uint16_t *count)
{
    wifi_scan_config_t scan_config = {
        .show_hidden = false,
    };

    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    return esp_wifi_scan_get_ap_records(count, records);
}

bool wifi_prov_is_connected(void)
{
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

void wifi_prov_wait_until_connected(void)
{
    xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);

    vTaskDelay(pdMS_TO_TICKS(3000));

    wifi_web_stop();
    if(s_dns_handle != NULL) {
        stop_dns_server(s_dns_handle);
        s_dns_handle = NULL;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
}

bool wifi_prov_wait_for_connection_timeout(TickType_t timeout)
{
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        timeout
    );

    return (bits & WIFI_CONNECTED_BIT) != 0;
}