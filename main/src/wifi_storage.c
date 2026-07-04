#include "inc/wifi_storage.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "[wifi_storage]";

#define WIFI_NAMESPACE "wifi_creds"
#define KEY_SSID       "ssid"
#define KEY_PASSWORD   "password"

esp_err_t wifi_storage_init(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    return err;
}

esp_err_t wifi_storage_save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t handle;

    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, KEY_PASSWORD, password ? password : "");
    }

    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    ESP_LOGI(TAG, "Saved WiFi credentials");

    return err;
}

esp_err_t wifi_storage_load_credentials(
    char *ssid,
    size_t ssid_len,
    char *password,
    size_t password_len)
{
    nvs_handle_t handle;

    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_str(handle, KEY_SSID, ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_get_str(handle, KEY_PASSWORD, password, &password_len);

    nvs_close(handle);

    return err;
}

esp_err_t wifi_storage_clear_credentials(void)
{
    nvs_handle_t handle;

    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    nvs_erase_key(handle, KEY_SSID);
    nvs_erase_key(handle, KEY_PASSWORD);

    err = nvs_commit(handle);

    nvs_close(handle);

    ESP_LOGI(TAG, "Cleared WiFi credentials");

    return err;
}