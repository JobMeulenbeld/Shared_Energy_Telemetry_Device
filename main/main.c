#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "nvs_flash.h"

#include "inc/wifi_manager.h"
#include "inc/energyboxx_api.h"

static const char *TAG = "main";

static void energyboxx_task(void *pvParameters)
{
    wifi_manager_wait_connected();

    while(true)
    {
        esp_err_t err = energyboxx_api_fetch_token();

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to fetch token: %s", esp_err_to_name(err));
            vTaskDelete(NULL); //TODO Don't delete the task, but retry with some backoff strategy
            return;
        }

        esp_err_t test_err = energyboxx_api_get_test();

        if (test_err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to perform test API call: %s", esp_err_to_name(test_err));
            vTaskDelete(NULL);
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(90 * 1000)); // Wait for 90 seconds.
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_ERROR_CHECK(wifi_manager_init());

    xTaskCreate(
        energyboxx_task,
        "energyboxx_task",
        8192,
        NULL,
        5,
        NULL
    );
}