#pragma once
#include "freertos/FreeRTOS.h"
#include <stddef.h>
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned length, size_t item_size);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait);
