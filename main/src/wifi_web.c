#include "esp_http_server.h"
#include "esp_log.h"

#include "inc/wifi_web.h"
#include "inc/wifi_provisioning.h"
#include "inc/dns_server.h"

static const char *TAG = "wifi_web";

static httpd_handle_t s_server = NULL;

static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>SETD WiFi Setup</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;margin:0;background:#f4f6f8;color:#111;}"
        ".card{max-width:420px;margin:40px auto;padding:24px;background:white;border-radius:16px;"
        "box-shadow:0 8px 30px rgba(0,0,0,.12);}"
        "h1{margin-top:0;font-size:28px;}"
        "label{display:block;margin-top:18px;font-weight:bold;}"
        "select,input,button{width:100%;box-sizing:border-box;font-size:16px;padding:12px;margin-top:8px;"
        "border-radius:10px;border:1px solid #ccc;}"
        "button{background:#111;color:white;border:none;margin-top:24px;font-weight:bold;}"
        "button:disabled{background:#999;}"
        "#status{margin-top:18px;font-weight:bold;}"
        "</style>"
        "</head>"
        "<body>"
        "<div class='card'>"
        "<h1>SETD WiFi Setup</h1>"
        "<p>Choose the WiFi network this device should connect to.</p>"

        "<label for='ssid'>Network</label>"
        "<select id='ssid'>"
        "<option>Scanning...</option>"
        "</select>"
        "<button type='button' onclick='scan()'>Refresh networks</button>"

        "<label for='password'>Password</label>"
        "<input id='password' type='password' placeholder='WiFi password'>"

        "<button id='connectBtn' onclick='connectWifi()'>Connect</button>"
        "<div id='status'>Status: Ready</div>"
        "</div>"

        "<script>"
        "async function scan(){"
        " const ssid=document.getElementById('ssid');"
        " ssid.innerHTML='<option>Scanning...</option>';"
        " try{"
        "  const r=await fetch('/scan');"
        "  const networks=await r.json();"
        "  ssid.innerHTML='';"
        "  networks.forEach(n=>{"
        "   const o=document.createElement('option');"
        "   o.value=n.ssid;"
        "   o.textContent=n.ssid+' ('+n.rssi+' dBm)';"
        "   ssid.appendChild(o);"
        "  });"
        " }catch(e){ssid.innerHTML='<option>Scan failed</option>';}"
        "}"
        ""
        "async function connectWifi(){"
        " const btn=document.getElementById('connectBtn');"
        " const status=document.getElementById('status');"
        " btn.disabled=true;"
        " status.textContent='Status: Connecting...';"
        " const ssid = document.getElementById('ssid').value;"
        " const password = document.getElementById('password').value;"
        " await fetch('/connect', {"
        "     method: 'POST',"
        "     headers: {'Content-Type': 'application/x-www-form-urlencoded'},"
        "     body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}`"
        " });"
        " pollStatus();"
        "}"
        ""
        "async function pollStatus(){"
        " const btn=document.getElementById('connectBtn');"
        " const status=document.getElementById('status');"
        " const timer=setInterval(async()=>{"
        "  const r=await fetch('/status');"
        "  const s=await r.json();"
        "  status.textContent='Status: '+s.state;"
        "  if(s.state==='connected'){clearInterval(timer);status.textContent='Connected successfully!';}"
        "  if(s.state==='failed'){clearInterval(timer);status.textContent='Could not connect. Check password.';btn.disabled=false;}"
        " },1000);"
        "}"
        "scan();"
        "</script>"
        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static const char *state_to_string(wifi_prov_state_t state)
{
    switch (state) {
        case WIFI_PROV_STATE_IDLE: return "idle";
        case WIFI_PROV_STATE_AP_ACTIVE: return "ready";
        case WIFI_PROV_STATE_CONNECTING: return "connecting";
        case WIFI_PROV_STATE_CONNECTED: return "connected";
        case WIFI_PROV_STATE_CONNECT_FAILED: return "failed";
        default: return "unknown";
    }
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    char json[64];

    snprintf(json, sizeof(json),"{\"state\":\"%s\"}", state_to_string(wifi_prov_get_state()));

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t connect_post_handler(httpd_req_t *req)
{
    char body[160] = {0};

    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }

    body[received] = '\0';

    char ssid[33] = {0};
    char password[65] = {0};

    if (httpd_query_key_value(body, "ssid", ssid, sizeof(ssid)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid");
        return ESP_FAIL;
    }

    httpd_query_key_value(body, "password", password, sizeof(password));

    esp_err_t err = wifi_prov_connect(ssid, password);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Connect failed");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    wifi_ap_record_t aps[20];
    uint16_t count = 20;

    esp_err_t err = wifi_prov_scan(aps, &count);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan failed");
        return err;
    }

    httpd_resp_set_type(req, "application/json");

    httpd_resp_sendstr_chunk(req, "[");

    for (int i = 0; i < count; i++) {
        char item[128];

        snprintf(item, sizeof(item),
                 "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                 i == 0 ? "" : ",",
                 (char *)aps[i].ssid,
                 aps[i].rssi);

        httpd_resp_sendstr_chunk(req, item);
    }

    httpd_resp_sendstr_chunk(req, "]");
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

// HTTP Error (404) Handler - Redirects all requests to the root page
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    // Set status
    httpd_resp_set_status(req, "303 See Other");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to root");
    return ESP_OK;
}


esp_err_t wifi_web_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    ESP_LOGI(TAG, "Starting HTTP server");

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t connect_uri = {
        .uri = "/connect",
        .method = HTTP_POST,
        .handler = connect_post_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t scan_uri = {
        .uri = "/scan",
        .method = HTTP_GET,
        .handler = scan_get_handler,
    };

    err = httpd_register_uri_handler(s_server, &favicon_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register favicon URI handler: %s", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(s_server, &root_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register root URI handler: %s", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, http_404_error_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register 404 error handler: %s", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(s_server, &status_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register status URI handler: %s", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(s_server, &connect_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register connect URI handler: %s", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(s_server, &scan_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register scan URI handler: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t wifi_web_stop(void)
{
    if (s_server == NULL) {
        return ESP_OK;
    }

    esp_err_t err = httpd_stop(s_server);
    s_server = NULL;
    return err;
}