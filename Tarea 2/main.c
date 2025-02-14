#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// Definición de pines
#define BOTON_ABRIR GPIO_NUM_2
#define BOTON_CERRAR GPIO_NUM_3
#define BOTON_PARO GPIO_NUM_4
#define SENSOR_OBSTACULO GPIO_NUM_5
#define LED_ESTADO GPIO_NUM_12
#define LED_FALLA GPIO_NUM_13

// Definición de estados
typedef enum {
    ESPERA,
    ABRIENDO,
    ABIERTA,
    CERRANDO,
    CERRADA,
    DETENIDA,
    ERROR
} EstadoPuerta;

// Variables globales
EstadoPuerta estadoActual = ESPERA;
bool obstaculoDetectado = false;
bool errorSistema = false;
uint64_t tiempoInicio = 0;
const uint32_t tiempoMovimiento = 30000;
const uint32_t tiempoEspera = 2000;

// Prototipos de funciones
void configurarHardware();
void gestionarEstado();
void manejarError();
void parpadearIndicadorError();

void app_main() {
    printf("Iniciando sistema de control de puerta\n");
    configurarHardware();

    while (1) {
        gestionNarEstado();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void configurarHardware() {
    gpio_config_t configuracion;
    configuracion.intr_type = GPIO_INTR_DISABLE;
    configuracion.mode = GPIO_MODE_INPUT;
    configuracion.pin_bit_mask = (1ULL << BOTON_ABRIR) | (1ULL << BOTON_CERRAR) | (1ULL << BOTON_PARO) | (1ULL << SENSOR_OBSTACULO);
    configuracion.pull_up_en = 1;
    gpio_config(&configuracion);

    configuracion.mode = GPIO_MODE_OUTPUT;
    configuracion.pin_bit_mask = (1ULL << LED_ESTADO) | (1ULL << LED_FALLA);
    gpio_config(&configuracion);
}

void gestionarEstado() {
    if (!gpio_get_level(BOTON_ABRIR)) {
        printf("Apertura iniciada\n");
        estadoActual = ABRIENDO;
        tiempoInicio = esp_timer_get_time();
    }
    if (!gpio_get_level(BOTON_CERRAR)) {
        printf("Cierre iniciado\n");
        estadoActual = CERRANDO;
        tiempoInicio = esp_timer_get_time();
    }
    if (!gpio_get_level(BOTON_PARO)) {
        printf("Operación detenida\n");
        estadoActual = DETENIDA;
    }
    if (!gpio_get_level(SENSOR_OBSTACULO)) {
        printf("Obstáculo detectado\n");
        obstaculoDetectado = true;
    } else {
        obstaculoDetectado = false;
    }

    switch (estadoActual) {
        case ESPERA:
            gpio_set_level(LED_ESTADO, 0);
            break;
        case ABRIENDO:
            gpio_set_level(LED_ESTADO, 1);
            if ((esp_timer_get_time() - tiempoInicio) / 1000 >= tiempoMovimiento) {
                estadoActual = ABIERTA;
            }
            break;
        case ABIERTA:
            vTaskDelay(pdMS_TO_TICKS(tiempoEspera));
            estadoActual = CERRANDO;
            break;
        case CERRANDO:
            gpio_set_level(LED_ESTADO, 0);
            if (obstaculoDetectado) {
                printf("¡Obstáculo! Reabriendo\n");
                estadoActual = ABRIENDO;
                tiempoInicio = esp_timer_get_time();
            } else if ((esp_timer_get_time() - tiempoInicio) / 1000 >= tiempoMovimiento) {
                estadoActual = CERRADA;
            }
            break;
        case CERRADA:
            estadoActual = ESPERA;
            break;
        case DETENIDA:
            if (!obstaculoDetectado) estadoActual = ESPERA;
            break;
        case ERROR:
            manejarError();
            break;
    }
}

void manejarError() {
    printf("¡Error en el sistema!\n");
    parpadearIndicadorError();
    estadoActual = ESPERA;
}

void parpadearIndicadorError() {
    for (int i = 0; i < 5; i++) {
        gpio_set_level(LED_FALLA, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(LED_FALLA, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
