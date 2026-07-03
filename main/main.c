#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "nvs_flash.h"

#include "inc/wifi_manager.h"
#include "inc/energyboxx_api.h"

static const char *TAG = "main";
energyboxx_data_t data;

static void energyboxx_task(void *pvParameters)
{
    while(true)
    {
        //Check if WiFi is connected

        ESP_LOGI(TAG, "free heap: %lu", esp_get_free_heap_size());
        ESP_LOGI(TAG, "min free heap: %lu", esp_get_minimum_free_heap_size());
        ESP_LOGI(TAG, "largest block: %u", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        esp_err_t err = energyboxx_api_fetch_token();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to fetch token: %s", esp_err_to_name(err));
            vTaskDelete(NULL); //TODO Don't delete the task, but retry with some backoff strategy
            return;
        }

        err = energyboxx_api_get_test(&data);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to perform API call: %s", esp_err_to_name(err));
            ESP_LOGW(TAG, "Renewing token in 10 seconds...");
            vTaskDelay(pdMS_TO_TICKS(10 * 1000)); // Wait for 10 seconds before retrying
            energyboxx_api_set_renew_token(true);
            continue;
        }

        energyboxx_data_print(&data);
        ESP_LOGI(TAG, "stack watermark: %u", uxTaskGetStackHighWaterMark(NULL));
        vTaskDelay(pdMS_TO_TICKS(90 * 1000)); // Wait for 90 seconds.
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_ERROR_CHECK(wifi_manager_init());

    wifi_manager_wait_connected();

    esp_err_t err = energyboxx_api_fetch_token();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to fetch token: %s", esp_err_to_name(err));
        //vTaskDelete(NULL); //TODO Don't delete the task, but retry with some backoff strategy
        return;
    }

    // xTaskCreate(
    //     energyboxx_task,
    //     "energyboxx_task",
    //     16384,
    //     NULL,
    //     5,
    //     NULL
    // );
}