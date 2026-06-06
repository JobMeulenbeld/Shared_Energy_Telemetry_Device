#include <stdio.h>
#include <inttypes.h>

#include "secrets.h"

#include "energyboxx_api.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#include "cJSON.h"

#define ENERGYBOXX_TOKEN_URL "https://energyboxx.grexx.today/oauth/access_token"

#define ENERGYBOXX_DATA_URL "https://energyboxx.grexx.today/api/v1/form/1:10173:112860/1:10310:6276618"

#define TOKEN_REFRESH_MARGIN_SECONDS (5 * 60)

static char access_token[2048] = {0};
static int expires_in_seconds = 0;
static int64_t token_acquired_us = 0;

static const char *TAG = "energyboxx_api";

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
    if (evt->user_data != NULL)
    {
        strncat((char *)evt->user_data, (char *)evt->data, evt->data_len);
    }
    else
    {
        printf("%.*s", evt->data_len, (char *)evt->data);
    }
    printf("\n");
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

esp_err_t energyboxx_api_fetch_token(void)
{
    int64_t elapsed_seconds = (esp_timer_get_time() - token_acquired_us) / 1000000;

    if (expires_in_seconds > 0 && elapsed_seconds < expires_in_seconds - TOKEN_REFRESH_MARGIN_SECONDS)
    {
        ESP_LOGI(TAG, "Token still valid for %d more seconds, skipping fetch", expires_in_seconds - (int)elapsed_seconds);
        return ESP_OK;
    }
    
    const char *post_data =
        "grant_type=client_credentials"
        "&client_id=" CLIENT_ID
        "&client_secret=" CLIENT_SECRET;

    char response_buffer[4096] = {0};

    esp_http_client_config_t config = {
        .url = "https://energyboxx.grexx.today/oauth/access_token",
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .user_data = response_buffer,
        .event_handler = http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (client == NULL)
    {
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Token status = %d", esp_http_client_get_status_code(client));
        ESP_LOGI(TAG, "Token response: %s", response_buffer);
        
        cJSON *root = cJSON_Parse(response_buffer);

        if (root == NULL)
        {
            ESP_LOGE(TAG, "Failed to parse JSON response");
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }

        cJSON *token_json = cJSON_GetObjectItem(root, "access_token");
        cJSON *expires_json = cJSON_GetObjectItem(root, "expires_in");

        if (!cJSON_IsString(token_json))
        {
            ESP_LOGE(TAG, "access_token missing");
            cJSON_Delete(root);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }

        if (!cJSON_IsNumber(expires_json))
        {
            ESP_LOGE(TAG, "expires_in missing");
            cJSON_Delete(root);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }

        strncpy(
            access_token,
            token_json->valuestring,
            sizeof(access_token) - 1
        );

        access_token[sizeof(access_token) - 1] = '\0';

        token_acquired_us = esp_timer_get_time();
        expires_in_seconds = expires_json->valueint;

        ESP_LOGI(TAG, "Stored token (%d chars)", strlen(access_token));

        ESP_LOGI(TAG, "Token expires in %d seconds", expires_in_seconds);

        cJSON_Delete(root);
    }
    else
    {
        ESP_LOGE(TAG, "Token request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

const char *energyboxx_api_get_token(void)
{
    return access_token;
}

esp_err_t energyboxx_api_get_test(void)
{
    esp_http_client_config_t config = {
        .url = ENERGYBOXX_DATA_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .event_handler = http_event_handler,
    };

    if (strlen(access_token) == 0)
    {
        ESP_LOGE(TAG, "No access token available");
        return ESP_FAIL;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (client == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    char auth_header[sizeof(access_token) + 8];

    snprintf(auth_header, sizeof(auth_header), "Bearer %s", access_token);

    esp_http_client_set_header(client, "Authorization", auth_header);

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