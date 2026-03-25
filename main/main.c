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
#include <ctype.h>

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
"body{font-family:'Segoe UI';background:#eef2ff;text-align:center;margin:0;}"
".box{margin-top:100px;background:white;padding:40px;border-radius:16px;"
"box-shadow:0 10px 30px rgba(0,0,0,0.1);display:inline-block;}"
"input{margin:10px;padding:10px;width:220px;border-radius:8px;border:1px solid #ccc;}"
"button{padding:10px 20px;background:#4f46e5;color:white;border:none;border-radius:8px;cursor:pointer;}"
"</style></head><body>"

"<div class='box'>"
"<h2>VaultESP</h2>"
"<form action='/login' method='post'>"
"<input name='user' placeholder='Username'><br>"
"<input name='pass' type='password' placeholder='Password'><br>"
"<button type='submit'>Login</button>"
"</form>"
"</div>"

"</body></html>";

/* ================= DASHBOARD ================= */

const char *dashboard_page =
"<html><head><style>"
"body{font-family:'Segoe UI';background:#f4f7ff;padding:20px;}"
".card{background:white;padding:20px;border-radius:16px;"
"box-shadow:0 8px 25px rgba(0,0,0,0.1);margin-bottom:20px;}"
"input{padding:10px;margin:5px;border-radius:8px;border:1px solid #ccc;width:30%;}"
"button{padding:10px;border:none;border-radius:8px;background:#4f46e5;color:white;cursor:pointer;}"
".entry{padding:10px;border-bottom:1px solid #eee;}"
"</style></head><body>"

"<h2>Vault Dashboard</h2>"

"<div class='card'>"
"<form action='/save' method='post'>"
"<input name='website' placeholder='Website'>"
"<input name='username' placeholder='Email'>"
"<input name='password' placeholder='Password'>"
"<br><br><button type='submit'>Save</button>"
"</form>"
"</div>"

"<div class='card'>"
"<h3>Saved</h3>"
"<div id='data'></div>"
"</div>"

"<script>"
"function toggle(id){"
"let e=document.getElementById(id);"
"e.textContent=e.textContent==='******'?e.dataset.real:'******';"
"}"

"function load(){"
"fetch('/vault').then(r=>r.json()).then(d=>{"
"let h='';"
"d.forEach((e,i)=>{"
"h+=`<div class='entry'>"
"<b>${e.website}</b><br>${e.username}<br>"
"<span id='p${i}' data-real='${e.password}'>******</span><br>"
"<button onclick='toggle(\"p${i}\")'>Show</button>"
"</div>`;"
"});"
"document.getElementById('data').innerHTML=h;"
"});"
"}"
"load();"
"</script>"

"</body></html>";

/* ================= HELPERS ================= */

void url_decode(char *src, char *dest){
    char a,b;
    while(*src){
        if((*src=='%')&&((a=src[1])&&(b=src[2]))&&(isxdigit(a)&&isxdigit(b))){
            if(a>='a')a-=32;
            if(a>='A')a=a-'A'+10; else a-='0';
            if(b>='a')b-=32;
            if(b>='A')b=b-'A'+10; else b-='0';
            *dest++=16*a+b;
            src+=3;
        } else if(*src=='+'){*dest++=' ';src++;}
        else {*dest++=*src++;}
    }
    *dest=0;
}

/* ================= HANDLERS ================= */

esp_err_t login_get(httpd_req_t *req){
    httpd_resp_send(req, login_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t login_post(httpd_req_t *req){
    char buf[100];
    int len=httpd_req_recv(req,buf,sizeof(buf)-1);
    buf[len]=0;

    if(strstr(buf,"user=Dhaanes") && strstr(buf,"pass=1234")){
        httpd_resp_set_status(req,"302 Found");
        httpd_resp_set_hdr(req,"Location","/dashboard");
        httpd_resp_send(req,NULL,0);
    } else {
        httpd_resp_send(req,"Wrong Login",HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

esp_err_t dashboard_handler(httpd_req_t *req){
    httpd_resp_send(req,dashboard_page,HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t save_handler(httpd_req_t *req){
    char buf[200];
    int len=httpd_req_recv(req,buf,sizeof(buf)-1);
    buf[len]=0;

    if(entry_count<MAX_ENTRIES){
        char w[40],u[40],p[40];
        sscanf(buf,"website=%39[^&]&username=%39[^&]&password=%39s",w,u,p);

        url_decode(w,vault[entry_count].website);
        url_decode(u,vault[entry_count].username);
        url_decode(p,vault[entry_count].password);

        entry_count++;
    }

    httpd_resp_set_status(req,"302 Found");
    httpd_resp_set_hdr(req,"Location","/dashboard");
    httpd_resp_send(req,NULL,0);
    return ESP_OK;
}

/* ✅ FIXED JSON BUILDER (NO WARNINGS) */
esp_err_t vault_handler(httpd_req_t *req){
    char buffer[2048];
    buffer[0]=0;

    strcat(buffer,"[");

    for(int i=0;i<entry_count;i++){
        char temp[256];  // ✅ increased size

        snprintf(temp,sizeof(temp),
        "{\"website\":\"%s\",\"username\":\"%s\",\"password\":\"%s\"}%s",
        vault[i].website,
        vault[i].username,
        vault[i].password,
        (i<entry_count-1)?",":"");

        strncat(buffer,temp,sizeof(buffer)-strlen(buffer)-1);
    }

    strcat(buffer,"]");

    httpd_resp_set_type(req,"application/json");
    httpd_resp_send(req,buffer,HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ================= SERVER ================= */

void start_server(void){
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server=NULL;

    httpd_start(&server,&config);

    httpd_uri_t root={.uri="/",.method=HTTP_GET,.handler=login_get};
    httpd_register_uri_handler(server,&root);

    httpd_uri_t login={.uri="/login",.method=HTTP_POST,.handler=login_post};
    httpd_register_uri_handler(server,&login);

    httpd_uri_t dash={.uri="/dashboard",.method=HTTP_GET,.handler=dashboard_handler};
    httpd_register_uri_handler(server,&dash);

    httpd_uri_t save={.uri="/save",.method=HTTP_POST,.handler=save_handler};
    httpd_register_uri_handler(server,&save);

    httpd_uri_t vault_uri={.uri="/vault",.method=HTTP_GET,.handler=vault_handler};
    httpd_register_uri_handler(server,&vault_uri);
}

/* ================= WIFI ================= */

void wifi_init_softap(void){
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config={
        .ap={
            .ssid=WIFI_SSID,
            .ssid_len=strlen(WIFI_SSID),
            .password=WIFI_PASS,
            .max_connection=4,
            .authmode=WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP,&wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG,"WiFi AP started");
}

/* ================= MAIN ================= */

void app_main(void){
    nvs_flash_init();
    wifi_init_softap();
    vTaskDelay(pdMS_TO_TICKS(2000));
    start_server();
}