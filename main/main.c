#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WIFI_SSID "VaultESP"
#define WIFI_PASS "12345678"

#define LOGIN_USER "Dhaanes"
#define LOGIN_PASS "1234"

#define MAX_ENTRIES 20

static const char *TAG = "vault";

/* ================= STORAGE ================= */

typedef struct {
    char website[40];
    char username[40];
    char password[40];
} vault_entry;

vault_entry vault[MAX_ENTRIES];
int entry_count = 0;

/* ================= LOGIN PAGE ================= */

const char *login_page =
"<html><head><style>"
"body{font-family:sans-serif;background:#eef2f7;text-align:center;}"
".box{margin-top:120px;background:white;padding:30px;border-radius:12px;"
"box-shadow:0 4px 12px rgba(0,0,0,0.1);display:inline-block;}"
"input{margin:10px;padding:10px;width:220px;border-radius:6px;border:1px solid #ccc;}"
"button{padding:10px 20px;background:#4CAF50;color:white;border:none;border-radius:6px;cursor:pointer;}"
"</style></head><body>"

"<div class='box'>"
"<h2>Private Vault Login</h2>"
"<form action='/login' method='post'>"
"<input name='user' placeholder='Username'><br>"
"<input name='pass' type='password' placeholder='Password'><br>"
"<button type='submit'>Login</button>"
"</form>"
"</div>"

"</body></html>";

/* ================= ENTRY + DASHBOARD PAGE ================= */

const char *dashboard_page =
"<html><head><style>"
"body{font-family:sans-serif;background:#f4f7fb;padding:20px;}"
".card{background:white;padding:20px;border-radius:12px;"
"box-shadow:0 4px 10px rgba(0,0,0,0.1);margin-bottom:20px;}"
"input{margin:5px;padding:10px;border-radius:6px;border:1px solid #ccc;width:30%;}"
"button{padding:10px 20px;background:#007BFF;color:white;border:none;border-radius:6px;cursor:pointer;}"
".entry{padding:10px;border-bottom:1px solid #eee;}"
"</style></head><body>"

"<div class='card'>"
"<h2>Add Entry</h2>"
"<form action='/save' method='post'>"
"<input name='website' placeholder='Website'>"
"<input name='username' placeholder='Email'>"
"<input name='password' placeholder='Password'>"
"<br><br>"
"<button type='submit'>Save</button>"
"</form>"
"</div>"

"<div class='card'>"
"<h2>Dashboard</h2>"
"<div id='data'></div>"
"</div>"

"<script>"
"function load(){"
"fetch('/vault').then(r=>r.json()).then(d=>{"
"let html='';"
"d.forEach(e=>{"
"html += `<div class='entry'><b>${e.website}</b><br>${e.username}<br>${e.password}</div>`;"
"});"
"document.getElementById('data').innerHTML = html;"
"});"
"}"
"load();"
"</script>"

"</body></html>";

/* ================= HANDLERS ================= */

esp_err_t login_get(httpd_req_t *req) {
    httpd_resp_send(req, login_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t login_post(httpd_req_t *req) {
    char buf[100];
    int len = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (len <= 0) return ESP_FAIL;
    buf[len] = 0;

    if (strstr(buf, "user=Dhaanes") && strstr(buf, "pass=1234")) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/dashboard");
        httpd_resp_send(req, NULL, 0);
    } else {
        httpd_resp_send(req, "Wrong Login", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

esp_err_t dashboard_handler(httpd_req_t *req) {
    httpd_resp_send(req, dashboard_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t save_handler(httpd_req_t *req) {
    char buf[150];
    int len = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (len <= 0) return ESP_FAIL;
    buf[len] = 0;

    if (entry_count < MAX_ENTRIES) {
        sscanf(buf,
               "website=%39[^&]&username=%39[^&]&password=%39s",
               vault[entry_count].website,
               vault[entry_count].username,
               vault[entry_count].password);
        entry_count++;
    }

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/dashboard");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t vault_handler(httpd_req_t *req) {
    char buffer[1500];
    buffer[0] = '\0';

    strcat(buffer, "[");

    for (int i = 0; i < entry_count; i++) {

        size_t remaining = sizeof(buffer) - strlen(buffer) - 1;

        strncat(buffer, "{\"website\":\"", remaining);

        remaining = sizeof(buffer) - strlen(buffer) - 1;
        strncat(buffer, vault[i].website, remaining);

        remaining = sizeof(buffer) - strlen(buffer) - 1;
        strncat(buffer, "\",\"username\":\"", remaining);

        remaining = sizeof(buffer) - strlen(buffer) - 1;
        strncat(buffer, vault[i].username, remaining);

        remaining = sizeof(buffer) - strlen(buffer) - 1;
        strncat(buffer, "\",\"password\":\"", remaining);

        remaining = sizeof(buffer) - strlen(buffer) - 1;
        strncat(buffer, vault[i].password, remaining);

        remaining = sizeof(buffer) - strlen(buffer) - 1;
        strncat(buffer, "\"}", remaining);

        if (i < entry_count - 1) {
            remaining = sizeof(buffer) - strlen(buffer) - 1;
            strncat(buffer, ",", remaining);
        }
    }

    strcat(buffer, "]");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ================= SERVER ================= */

void start_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    httpd_start(&server, &config);

    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = login_get };
    httpd_register_uri_handler(server, &root);

    httpd_uri_t login = { .uri = "/login", .method = HTTP_POST, .handler = login_post };
    httpd_register_uri_handler(server, &login);

    httpd_uri_t dash = { .uri = "/dashboard", .method = HTTP_GET, .handler = dashboard_handler };
    httpd_register_uri_handler(server, &dash);

    httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = save_handler };
    httpd_register_uri_handler(server, &save);

    httpd_uri_t vault_uri = { .uri = "/vault", .method = HTTP_GET, .handler = vault_handler };
    httpd_register_uri_handler(server, &vault_uri);
}

/* ================= WIFI AP ================= */

void wifi_init_softap(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .password = WIFI_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "WiFi AP started");
}

/* ================= MAIN ================= */

void app_main(void) {
    nvs_flash_init();

    wifi_init_softap();

    vTaskDelay(pdMS_TO_TICKS(2000)); // FIX crash

    start_server();

    ESP_LOGI(TAG, "Server started");
}