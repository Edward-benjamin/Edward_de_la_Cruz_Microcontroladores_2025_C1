#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_PIN 2

void led_task(void *pvParameter);

void app_main(void)
{
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    
    xTaskCreate(&led_task, "LED Task", 2048, NULL, 5, NULL);
}

void led_task(void *pvParameter)
{
    uint8_t estado = 0;
    while (1)
    {
        estado = !estado;
        gpio_set_level(LED_PIN, estado);
        printf("LED Status: %u\n", estado);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
