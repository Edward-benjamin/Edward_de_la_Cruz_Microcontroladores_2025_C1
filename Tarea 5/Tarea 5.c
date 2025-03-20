#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// Definición de pines
#define PIN_BOTON GPIO_NUM_2
#define PIN_LED   GPIO_NUM_12

// Variables globales
static uint64_t tiempoPresionado = 0;
static uint64_t tiempoInicio = 0;
static bool botonPresionado = false;
static TimerHandle_t timerLed = NULL;
TaskHandle_t tareaParpadeo = NULL;

// ISR para capturar la pulsación del botón
void IRAM_ATTR isr_boton(void *arg) {
    static BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (gpio_get_level(PIN_BOTON) == 0) { // Botón presionado
        tiempoInicio = esp_timer_get_time();
        gpio_set_level(PIN_LED, 1); // Enciende LED
        botonPresionado = true;
    } else { // Botón liberado
        if (botonPresionado) {
            botonPresionado = false;
            tiempoPresionado = (esp_timer_get_time() - tiempoInicio) / 1000; // Convertir a ms
            xTaskNotifyFromISR(tareaParpadeo, tiempoPresionado, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
        }
    }
}

// Función para hacer parpadear el LED
void tarea_parpadeo(void *pvParameters) {
    uint32_t duracionParpadeo = 0;

    while (1) {
        if (xTaskNotifyWait(0, 0, &duracionParpadeo, portMAX_DELAY) == pdTRUE) {
            printf("Parpadeo por %d ms\n", duracionParpadeo);
            uint32_t tiempoFinal = esp_timer_get_time() / 1000 + duracionParpadeo;

            while (esp_timer_get_time() / 1000 < tiempoFinal) {
                gpio_set_level(PIN_LED, 1);
                vTaskDelay(250 / portTICK_PERIOD_MS);
                gpio_set_level(PIN_LED, 0);
                vTaskDelay(250 / portTICK_PERIOD_MS);
            }
        }
    }
}

// Configuración de pines y botones con interrupciones
void configurarPines() {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << PIN_BOTON),
        .pull_up_en = 1
    };
    gpio_config(&io_conf);

    gpio_config_t led_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << PIN_LED)
    };
    gpio_config(&led_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_BOTON, isr_boton, NULL);
}

void app_main() {
    printf("Iniciando sistema\n");
    configurarPines();

    // Crear la tarea de parpadeo del LED
    xTaskCreate(tarea_parpadeo, "tarea_parpadeo", 2048, NULL, 2, &tareaParpadeo);
}
