#include <stdio.h>
#include <stdbool.h>
#include "nimble-nordic-uart.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/queue.h"

#include "ble_helpers.h"
#include "usb_helpers.h"

static const char *TAG = "BLE";
static bool ble_connected = false;

QueueHandle_t xBLETxQueue;
RingbufHandle_t ble_tx_buf_handle = NULL;

static void ble_to_usb_task(void *parameter) {
    char mbuf[CONFIG_NORDIC_UART_MAX_LINE_LENGTH + 1];

    for (;;) {
        size_t item_size;
        if (nordic_uart_rx_buf_handle) {
            uint8_t *item = (uint8_t *)xRingbufferReceive(nordic_uart_rx_buf_handle, &item_size, portMAX_DELAY);
        
            // Put item in transmit buffer
            if (item) {
                strcpy(mbuf, (char *)item);
                // Adding newline for now, since BLE terminal seems to leave it off.
                // TODO: Might need to revisit in the future, if app can be sure to send \n.
                // Will need special handling if/when uploading files, though.
                strcat(mbuf, "\n");
                usb_tx((uint8_t *)mbuf, item_size, 1000);
                vRingbufferReturnItem(nordic_uart_rx_buf_handle, (void *)item);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    vTaskDelete(NULL);
}

static void empty_tx_buffer() {
    size_t item_size;
    uint8_t *item = NULL;
    while(true) {
        item = (uint8_t *)xRingbufferReceive(ble_tx_buf_handle, &item_size, 0);
        if (item == NULL) { break; }
        vRingbufferReturnItem(ble_tx_buf_handle, (void *)item);
    }
}

static void _tx_queue_processing_task(void *pdParameters) {
    //char data[CONFIG_NORDIC_UART_MAX_LINE_LENGTH];
    size_t item_size;
    while(true) {
        if (ble_tx_buf_handle) {
            char *item = (char *)xRingbufferReceive(ble_tx_buf_handle, &item_size, portMAX_DELAY);
            if (item) {
                char mbuf[item_size+1];
                snprintf(mbuf, item_size+1, "%.*s", item_size+1, item);
                nordic_uart_send(mbuf);
                vRingbufferReturnItem(ble_tx_buf_handle, (void *)item);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    vTaskDelete(NULL);
}

void ble_event_callback(enum nordic_uart_callback_type callback_type) {
    static TaskHandle_t ble_to_usb_task_handle;
    static TaskHandle_t tx_queue_processing_task_handle;
    switch (callback_type) {
        case NORDIC_UART_CONNECTED:
            ESP_LOGI(TAG, "Bluetooth device connected.");
            ESP_LOGD(TAG, "Starting BLE -> USB Task");
            BaseType_t task_created = xTaskCreate(ble_to_usb_task, "ble_to_usb_task", 5000, NULL, 10, &ble_to_usb_task_handle);
            assert(task_created == pdTRUE);
            ESP_LOGD(TAG, "Starting BLE Transmit Queue Processing Task.");
            task_created = xTaskCreate(_tx_queue_processing_task, "tx_queue_processing_task", 5000, NULL, 10, &tx_queue_processing_task_handle);
            assert(task_created == pdTRUE);
            ble_connected = true;
            return;
        case NORDIC_UART_DISCONNECTED:
            ESP_LOGD(TAG, "Stopping BLE -> USB Task");
            vTaskDelete(ble_to_usb_task_handle);
            ESP_LOGD(TAG, "Stopping BLE Transmit Queue Processing Task.");
            vTaskDelete(tx_queue_processing_task_handle);
            empty_tx_buffer();
            ESP_LOGI(TAG, "Bluetooth device disconnected.");
            ble_connected = false;
            return;
        default:
            return;
    }
}

bool ble_is_connected() {
    return ble_connected;
}

void init_ble() {
    //esp_log_level_set(TAG, ESP_LOG_DEBUG);
    nordic_uart_start("Anima Link", ble_event_callback);

    ble_tx_buf_handle = xRingbufferCreate(67584, RINGBUF_TYPE_NOSPLIT);
    if (ble_tx_buf_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create transmit buffer.");
    }

    ESP_LOGI(TAG, "BLE initialized.");
}

// Write message to queue for transmitting to BLE device.
void ble_tx(char *data, size_t data_len) {
    ESP_LOGD(TAG, "To TX: %.*s", data_len, data);
    UBaseType_t res = xRingbufferSend(ble_tx_buf_handle, data, data_len, pdMS_TO_TICKS(1000));
    if (res != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send item to transmit buffer.");
    }
}