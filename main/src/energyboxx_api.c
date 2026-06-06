#include <stdio.h>
#include <inttypes.h>

#include "energyboxx_api.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

static const char *TAG = "energyboxx_api";

#define ENERGYBOXX_DATA_URL "https://energyboxx.grexx.today/api/v1/form/1:10173:112860/1:10310:6276618"

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI(TAG, "Received %d bytes", evt->data_len);
        printf("%.*s", evt->data_len, (char *)evt->data);
        break;

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP request finished");
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP disconnected");
        break;

    default:
        break;
    }

    return ESP_OK;
}

esp_err_t energyboxx_api_get_test(void)
{
    esp_http_client_config_t config = {
        .url = ENERGYBOXX_DATA_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .event_handler = http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (client == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Accept", "application/json");

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Status = %d",
                 esp_http_client_get_status_code(client));

        ESP_LOGI(TAG, "Content length = %" PRId64,
                 esp_http_client_get_content_length(client));
    }
    else
    {
        ESP_LOGE(TAG, "GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    return err;
}