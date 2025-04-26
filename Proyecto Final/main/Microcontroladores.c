#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define TAG "APP"
#define WIFI_SSID "your_SSID"
#define WIFI_PASS "your_PASSWORD"

const char html_index[] =
"<!DOCTYPE html><html lang=\"es\"><head><meta charset=\"UTF-8\">"
"<title>Control de Señal - GPIO</title><style>"
"body{font-family:Arial,sans-serif;padding:30px;background:#f4f4f4}"
"h1{text-align:center}button{padding:10px 20px;margin:10px;font-size:16px}"
".form-container{display:none;margin-top:20px;padding:20px;background:#fff;"
"border-radius:10px;box-shadow:0 0 10px rgba(0,0,0,0.1)}input,select{margin:5px}"
"</style></head><body><h1>Simulación de Señal GPIO</h1>"
"<button onclick=\"toggleForm('astable')\">Modo Astable</button>"
"<button onclick=\"toggleForm('pwm')\">Modo PWM</button>"
"<div id=\"astable-form\" class=\"form-container\"><h2>Modo Astable</h2>"
"<form id=\"astableForm\"><label>R1 (ohm):<input type=\"number\" name=\"r1\" required></label><br>"
"<label>R2 (ohm):<input type=\"number\" name=\"r2\" required></label><br>"
"<label>C1 (faradios):<input type=\"number\" name=\"c1\" required></label><br>"
"<label>GPIO de salida:<select name=\"gpio\">"
"<option value=\"0\">GPIO0</option><option value=\"2\">GPIO2</option>"
"</select></label><br><button type=\"submit\">Enviar al ESP32</button></form>"
"<p id=\"astable-result\"></p></div>"
"<div id=\"pwm-form\" class=\"form-container\"><h2>Modo PWM</h2>"
"<form id=\"pwmForm\"><label>Frecuencia deseada (Hz):<input type=\"number\" name=\"freq\" required></label><br>"
"<label>GPIO de salida:<select name=\"gpio\">"
"<option value=\"0\">GPIO0</option><option value=\"2\">GPIO2</option>"
"</select></label><br><button type=\"submit\">Enviar al ESP32</button></form>"
"<p id=\"pwm-result\"></p></div>"
"<script>"
"function toggleForm(m){document.getElementById('astable-form').style.display=m==='astable'?'block':'none';"
"document.getElementById('pwm-form').style.display=m==='pwm'?'block':'none'}"
"document.getElementById('astableForm').addEventListener('submit',function(e){"
"e.preventDefault();const f=new FormData(this);fetch('/submit',{method:'POST',body:new URLSearchParams(f)})"
".then(r=>r.text()).then(d=>{document.getElementById('astable-result').innerText='Respuesta: '+d;})"
".catch(e=>console.error('Error:',e));});"
"document.getElementById('pwmForm').addEventListener('submit',function(e){"
"e.preventDefault();const f=new FormData(this);fetch('/pwm',{method:'POST',body:new URLSearchParams(f)})"
".then(r=>r.text()).then(d=>{document.getElementById('pwm-result').innerText='Respuesta: '+d;})"
".catch(e=>console.error('Error:',e));});"
"</script></body></html>";

float calcular_frecuencia(float r1, float r2, float c1) {
    return 1.44f / ((r1 + 2 * r2) * c1);
}

void signal_task(void *param) {
    int gpio = (int)(intptr_t)param;
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);

    float r1 = 1000, r2 = 1000, c1 = 0.000001;
    float freq = calcular_frecuencia(r1, r2, c1);
    int delay_ms = (int)(500.0 / freq);

    while (1) {
        gpio_set_level(gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        gpio_set_level(gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t submit_post_handler(httpd_req_t *req) {
    char content[100];
    httpd_req_recv(req, content, sizeof(content));

    float r1 = atof(strstr(content, "r1=") + 3);
    float r2 = atof(strstr(content, "r2=") + 3);
    float c1 = atof(strstr(content, "c1=") + 3);
    int gpio = atoi(strstr(content, "gpio=") + 5);

    float freq = calcular_frecuencia(r1, r2, c1);
    float periodo_ms = 1000.0 / freq;

    xTaskCreatePinnedToCore(signal_task, "signal_task", 2048, (void*)(intptr_t)gpio, 1, NULL, 1);

    char resp[100];
    snprintf(resp, sizeof(resp), "Frecuencia: %.2f Hz, Periodo: %.2f ms en GPIO %d", freq, periodo_ms, gpio);
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

esp_err_t pwm_post_handler(httpd_req_t *req) {
    char content[100];
    httpd_req_recv(req, content, sizeof(content));

    float freq = atof(strstr(content, "freq=") + 5);
    int gpio = atoi(strstr(content, "gpio=") + 5);

    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = (uint32_t)freq,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .gpio_num = gpio,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 512,
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel);

    char resp[100];
    snprintf(resp, sizeof(resp), "PWM aplicado: %.2f Hz en GPIO %d", freq, gpio);
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_index, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &(wifi_config_t){
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    }));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_start(&server, &config);

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler
    };
    httpd_register_uri_handler(server, &root_uri);

    httpd_uri_t submit_uri = {
        .uri = "/submit",
        .method = HTTP_POST,
        .handler = submit_post_handler
    };
    httpd_register_uri_handler(server, &submit_uri);

    httpd_uri_t pwm_uri = {
        .uri = "/pwm",
        .method = HTTP_POST,
        .handler = pwm_post_handler
    };
    httpd_register_uri_handler(server, &pwm_uri);
}
