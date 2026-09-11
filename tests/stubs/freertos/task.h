#pragma once
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
void vTaskDelay(TickType_t ticks);
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg, unsigned prio, TaskHandle_t *out);
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg, unsigned prio, TaskHandle_t *out, int core);
