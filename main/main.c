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

    while (true)
    {
        ESP_LOGI(TAG, "Fetching Energyboxx data...");

        esp_err_t err = energyboxx_api_get_test();

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Energyboxx request failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(120000));
    }
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