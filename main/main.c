#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "inc/wifi_storage.h"
#include "inc/wifi_provisioning.h"
#include "inc/wifi_web.h"
#include "inc/energyboxx_api.h"

static const char *TAG = "main";
energyboxx_data_t data;

#define RESET_WIFI_GPIO GPIO_NUM_17
#define RESET_HOLD_MS   3000

static bool reset_button_held_on_boot(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << RESET_WIFI_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&io_conf);

    if (gpio_get_level(RESET_WIFI_GPIO) != 0) {
        return false;
    }

    int elapsed = 0;

    while (elapsed < RESET_HOLD_MS) {
        if (gpio_get_level(RESET_WIFI_GPIO) != 0) {
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
        elapsed += 100;
    }

    return true;
}

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
    ESP_ERROR_CHECK(wifi_storage_init());

    if (reset_button_held_on_boot()) {
        ESP_LOGW(TAG, "WiFi reset button held, clearing credentials");
        ESP_ERROR_CHECK(wifi_storage_clear_credentials());
    }


    ESP_ERROR_CHECK(wifi_prov_init());

    char ssid[33] = {0};
    char password[65] = {0};

    bool started_provisioning = false;

    if (wifi_storage_load_credentials(ssid, sizeof(ssid), password, sizeof(password)) == ESP_OK) {
        ESP_ERROR_CHECK(wifi_prov_connect(ssid, password));

        if (!wifi_prov_wait_for_connection_timeout(pdMS_TO_TICKS(30000))) {
            ESP_LOGW(TAG, "Saved WiFi failed, starting provisioning");
            ESP_ERROR_CHECK(wifi_prov_start_ap());
            ESP_ERROR_CHECK(wifi_web_start());
            started_provisioning = true;
        }
    } else {
        ESP_ERROR_CHECK(wifi_prov_start_ap());
        ESP_ERROR_CHECK(wifi_web_start());
        started_provisioning = true;
    }

    if (started_provisioning) {
        wifi_prov_wait_until_connected();
    }

    // ESP_ERROR_CHECK(wifi_manager_init());

    // wifi_manager_wait_connected();

    // esp_err_t err = energyboxx_api_fetch_token();
    // if (err != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "Failed to fetch token: %s", esp_err_to_name(err));
    //     //vTaskDelete(NULL); //TODO Don't delete the task, but retry with some backoff strategy
    //     return;
    // }

    xTaskCreate(
        energyboxx_task,
        "energyboxx_task",
        16384,
        NULL,
        5,
        NULL
    );
}