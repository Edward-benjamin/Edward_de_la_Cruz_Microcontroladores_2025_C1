#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include "esp_system.h"
#include "spi_flash_mmap.h"
#include <esp_http_server.h>
#include "esp_spiffs.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

// Configuración de Hardware
#define PWM_PIN          GPIO_NUM_32
#define PWM_CHANNEL      LEDC_CHANNEL_0
#define PWM_FREQ         5000       // 5 kHz
#define PWM_RESOLUTION   LEDC_TIMER_8_BIT

// Configuración WiFi
#define WIFI_SSID       "ESP32_555_PWM"
#define WIFI_PASS       "config1234"
#define WIFI_CHANNEL    6
#define MAX_CONNECTIONS 2

// Rutas y buffers
#define WEB_PATH        "/spiffs/web"
#define INDEX_HTML      "/spiffs/web/index.html"
char web_buffer[15000];

// Estructura de configuración
typedef struct {
    float frequency;        // Frecuencia actual (Hz)
    float duty_cycle;       // Ciclo de trabajo (%)
    float pulse_width;      // Ancho de pulso (s) - modo monoestable
    uint8_t operation_mode; // 0=inactivo, 1=monoestable, 2=astable
    bool pwm_enabled;       // Estado del PWM
    float r1_mono;          // Resistencia modo monoestable (Ω)
    float c1_mono;          // Capacitancia modo monoestable (F)
    float r1_astable;       // Resistencia R1 astable (Ω)
    float r2_astable;       // Resistencia R2 astable (Ω)
    float c1_astable;       // Capacitancia astable (F)
} pwm_config_t;

pwm_config_t pwm_cfg = {
    .frequency = 1.0,
    .duty_cycle = 50.0,
    .pulse_width = 0.1,
    .operation_mode = 0,
    .pwm_enabled = false,
    .r1_mono = 10000.0,
    .c1_mono = 10e-6,
    .r1_astable = 1000.0,
    .r2_astable = 1000.0,
    .c1_astable = 10e-6
};

static const char *TAG = "PWM_555_CONTROL";

/* ==================== PWM Functions ==================== */
void init_pwm() {
    // Configurar timer PWM
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = PWM_RESOLUTION,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_cfg);

    // Configurar canal PWM
    ledc_channel_config_t channel_cfg = {
        .gpio_num = PWM_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&channel_cfg);

    ESP_LOGI(TAG, "PWM inicializado en GPIO %d, Canal %d", PWM_PIN, PWM_CHANNEL);
}

void update_pwm_output() {
    if (!pwm_cfg.pwm_enabled) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL);
        return;
    }

    // Calcular duty cycle (0-255 para 8 bits)
    uint32_t duty = (uint32_t)((pwm_cfg.duty_cycle / 100.0) * ((1 << PWM_RESOLUTION) - 1));
    
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL);
    
    ESP_LOGD(TAG, "PWM actualizado: %.1f%% -> valor %d", pwm_cfg.duty_cycle, duty);
}

/* ==================== Web Server Handlers ==================== */
esp_err_t send_web_page(httpd_req_t *req) {
    // Leer archivo HTML
    FILE* file = fopen(INDEX_HTML, "r");
    if (!file) {
        ESP_LOGE(TAG, "Error al abrir archivo HTML");
        return ESP_FAIL;
    }

    size_t bytes_read = fread(web_buffer, 1, sizeof(web_buffer), file);
    fclose(file);

    // Reemplazar placeholders dinámicos
    char temp_buffer[50];
    
    // Frecuencia
    snprintf(temp_buffer, sizeof(temp_buffer), "%.2f", pwm_cfg.frequency);
    char *freq_pos = strstr(web_buffer, "%FREQ%");
    if (freq_pos) memcpy(freq_pos, temp_buffer, strlen(temp_buffer));
    
    // Duty Cycle
    snprintf(temp_buffer, sizeof(temp_buffer), "%.1f", pwm_cfg.duty_cycle);
    char *duty_pos = strstr(web_buffer, "%DUTY%");
    if (duty_pos) memcpy(duty_pos, temp_buffer, strlen(temp_buffer));
    
    // Estado
    const char *status_class = pwm_cfg.pwm_enabled ? "active" : "inactive";
    const char *status_text = pwm_cfg.pwm_enabled ? "ACTIVO" : "INACTIVO";
    
    char *status_pos = strstr(web_buffer, "%STATUS%");
    if (status_pos) memcpy(status_pos, status_class, strlen(status_class));
    
    status_pos = strstr(web_buffer, "%STATUS_TEXT%");
    if (status_pos) memcpy(status_pos, status_text, strlen(status_text));

    return httpd_resp_send(req, web_buffer, bytes_read);
}

esp_err_t handle_monostable_config(httpd_req_t *req) {
    char buffer[150];
    int ret = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (ret <= 0) return ESP_FAIL;
    buffer[ret] = '\0';

    // Parsear parámetros
    float r1 = pwm_cfg.r1_mono;
    float c1 = pwm_cfg.c1_mono;
    sscanf(buffer, "r1-mono=%f&c1-mono=%f", &r1, &c1);

    // Validar y actualizar configuración
    if (r1 > 0 && c1 > 0) {
        pwm_cfg.r1_mono = r1;
        pwm_cfg.c1_mono = c1 * 1e-6; // Convertir µF a F
        pwm_cfg.pulse_width = 1.1 * r1 * pwm_cfg.c1_mono;
        pwm_cfg.operation_mode = 1;
        pwm_cfg.pwm_enabled = true;
        
        ESP_LOGI(TAG, "Config monoestable: R1=%.0fΩ, C1=%.2fµF -> T=%.3fs", 
                r1, c1, pwm_cfg.pulse_width);
    }

    update_pwm_output();
    return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

esp_err_t handle_astable_config(httpd_req_t *req) {
    char buffer[150];
    int ret = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (ret <= 0) return ESP_FAIL;
    buffer[ret] = '\0';

    // Parsear parámetros
    float r1 = pwm_cfg.r1_astable;
    float r2 = pwm_cfg.r2_astable;
    float c1 = pwm_cfg.c1_astable;
    sscanf(buffer, "r1-astable=%f&r2-astable=%f&c1-astable=%f", &r1, &r2, &c1);

    // Validar y actualizar configuración
    if (r1 > 0 && r2 > 0 && c1 > 0) {
        pwm_cfg.r1_astable = r1;
        pwm_cfg.r2_astable = r2;
        pwm_cfg.c1_astable = c1 * 1e-6; // Convertir µF a F
        
        // Calcular frecuencia y duty cycle
        pwm_cfg.frequency = 1.44 / ((r1 + 2 * r2) * pwm_cfg.c1_astable);
        pwm_cfg.duty_cycle = (r1 + r2) / (r1 + 2 * r2) * 100.0;
        pwm_cfg.operation_mode = 2;
        pwm_cfg.pwm_enabled = true;
        
        ESP_LOGI(TAG, "Config astable: R1=%.0fΩ, R2=%.0fΩ, C1=%.2fµF -> f=%.2fHz, DC=%.1f%%", 
                r1, r2, c1, pwm_cfg.frequency, pwm_cfg.duty_cycle);
    }

    update_pwm_output();
    return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

esp_err_t handle_pwm_update(httpd_req_t *req) {
    char buffer[50];
    int ret = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (ret <= 0) return ESP_FAIL;
    buffer[ret] = '\0';

    // Parsear duty cycle
    float duty;
    sscanf(buffer, "duty=%f", &duty);

    // Validar y actualizar
    if (duty >= 1.0 && duty <= 100.0) {
        pwm_cfg.duty_cycle = duty;
        pwm_cfg.pwm_enabled = true;
        ESP_LOGI(TAG, "PWM actualizado: Duty=%.1f%%", duty);
    } else {
        pwm_cfg.pwm_enabled = false;
        ESP_LOGW(TAG, "PWM desactivado (duty fuera de rango)");
    }

    update_pwm_output();
    return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

void register_uri_handlers(httpd_handle_t server) {
    httpd_uri_t index = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = send_web_page,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &index);

    httpd_uri_t mono = {
        .uri = "/mono",
        .method = HTTP_POST,
        .handler = handle_monostable_config,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &mono);

    httpd_uri_t astable = {
        .uri = "/astable",
        .method = HTTP_POST,
        .handler = handle_astable_config,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &astable);

    httpd_uri_t pwm = {
        .uri = "/updatePWM",
        .method = HTTP_POST,
        .handler = handle_pwm_update,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &pwm);
}

/* ==================== WiFi Setup ==================== */
void wifi_event_handler(void* arg, esp_event_base_t event_base,
                       int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Dispositivo conectado: "MACSTR, MAC2STR(event->mac));
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Dispositivo desconectado: "MACSTR, MAC2STR(event->mac));
    }
}

void init_wifi_ap() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &wifi_event_handler,
                                                      NULL,
                                                      NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .password = WIFI_PASS,
            .max_connection = MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = true,
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP WiFi iniciado. SSID: %s, Canal: %d", WIFI_SSID, WIFI_CHANNEL);
}

void init_spiffs() {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = WEB_PATH,
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));

    size_t total = 0, used = 0;
    esp_err_t ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al obtener información de SPIFFS");
    } else {
        ESP_LOGI(TAG, "SPIFFS montado. Tamaño: %d, Usado: %d", total, used);
    }
}

void app_main() {
    // Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Inicializar sistemas
    init_spiffs();
    init_wifi_ap();
    init_pwm();

    // Configurar servidor web
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        register_uri_handlers(server);
        ESP_LOGI(TAG, "Servidor HTTP iniciado");
    }

    // Configuración inicial PWM
    pwm_cfg.operation_mode = 2; // Modo astable por defecto
    pwm_cfg.pwm_enabled = true;
    update_pwm_output();

    ESP_LOGI(TAG, "Sistema listo. PWM en GPIO %d", PWM_PIN);
}
