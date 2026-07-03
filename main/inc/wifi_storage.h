#ifndef WIFI_STORAGE_H
#define WIFI_STORAGE_H

#include "esp_err.h"
#include <stdbool.h>

esp_err_t wifi_storage_init(void);

esp_err_t wifi_storage_save_credentials(const char *ssid, const char *password);

esp_err_t wifi_storage_load_credentials(
    char *ssid,
    size_t ssid_len,
    char *password,
    size_t password_len
);

esp_err_t wifi_storage_clear_credentials(void);

#endif // WIFI_STORAGE_H