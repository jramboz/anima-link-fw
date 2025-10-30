#include <stdio.h>
#include <string.h>
#include "log_helpers.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ble_helpers.h"
#include "nimble-nordic-uart.h"

//#define LOG_TO_STDOUT

static const char *TAG = "LOG";

QueueHandle_t xLogQueue;

static int log_to_queue(const char* fmt, va_list args) {
    char msg[CONFIG_NORDIC_UART_MAX_LINE_LENGTH];
    vsnprintf(msg, sizeof(msg), fmt, args);

    BaseType_t ret = xQueueSend(xLogQueue, msg, 0);
    //printf("Queue items: %u\n", uxQueueMessagesWaiting(xLogQueue));
    while (ret != pdTRUE) {
        // Queue full. Remove oldest message and try again.
        char buf[CONFIG_NORDIC_UART_MAX_LINE_LENGTH];
        xQueueReceive(xLogQueue, buf, 0);
        ret = xQueueSend(xLogQueue, msg, 0);
    }

    #ifdef LOG_TO_STDOUT
    return vprintf(fmt, args);
    #else
    return strlen(msg);
    #endif
}

static void _log_queue_processing_task(void *pdParameters) {
    char msg[CONFIG_NORDIC_UART_MAX_LINE_LENGTH];
    static const char pre[] = "# ";
    while (true) {
        if(ble_is_connected()) {
            if (xQueueReceive(xLogQueue, &msg, portMAX_DELAY) == pdTRUE) {
                char mbuf[CONFIG_NORDIC_UART_MAX_LINE_LENGTH];
                // Prepend "# " to the beginning of a line. BUT the logger submits a separate message
                // for each piece of the line (e.g., timestamp, message, end). So check to see if it
                // starts with a log level code first.
                if (
                    strncmp(msg, "I ", 2) == 0 ||
                    strncmp(msg, "W ", 2) == 0 ||
                    strncmp(msg, "E ", 2) == 0 ||
                    strncmp(msg, "D ", 2) == 0 ||
                    strncmp(msg, "V ", 2) == 0
                ) {
                    snprintf(mbuf, CONFIG_NORDIC_UART_MAX_LINE_LENGTH, "%.3s%.252s", pre, msg);
                } else {
                    strcpy(mbuf, msg);
                }
                nordic_uart_send(mbuf);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    vTaskDelete(NULL);
}

void init_logging() {
    // Prepare logging
    xLogQueue = xQueueCreate(QUEUE_LENGTH, CONFIG_NORDIC_UART_MAX_LINE_LENGTH);
    if (xLogQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create log queue.");
    }
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("NimBLE", ESP_LOG_NONE);
    esp_log_set_vprintf(log_to_queue);
    BaseType_t task_created = xTaskCreate(_log_queue_processing_task, "log_queue_processing_task", 5000, NULL, 20, NULL);
    assert(task_created == pdTRUE);
    ESP_LOGI(TAG, "Logging initialized.");
}