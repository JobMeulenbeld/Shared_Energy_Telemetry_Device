#ifndef ENERGYBOXX_API_H
#define ENERGYBOXX_API_H

#include "esp_err.h"

typedef struct
{
    float community_power_import_kw;
    float community_power_export_kw;
    float community_power_result_kw;

    float community_export_price_eur;
    float community_import_price_eur;

    float community_shared_import_price_eur;
    float community_shared_export_price_eur;
} energyboxx_data_t;

esp_err_t energyboxx_api_fetch_token(void);
esp_err_t energyboxx_api_get_test(void);
const char *energyboxx_api_get_token(void);

#endif // ENERGYBOXX_API_H