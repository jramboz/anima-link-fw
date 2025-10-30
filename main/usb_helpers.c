#include <stdbool.h>
#include "usb_helpers.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "usb/cdc_acm_host.h"
#include "usb/usb_host.h"

#include "ble_helpers.h"

static const char *TAG = "USB";   // logging tag
cdc_acm_dev_hdl_t cdc_dev = NULL;       // CDC device handle
bool _usb_connected = false;

/**
 * @brief USB data received callback
 *
 * @param[in] data     Pointer to received data
 * @param[in] data_len Length of received data in bytes
 * @param[in] arg      Argument we passed to the device open function
 * @return
 *   true:  We have processed the received data
 *   false: We expect more data
 */
static bool handle_usb_rx(const uint8_t *data, size_t data_len, void *arg)
{
    ESP_LOGD(TAG, "Received USB data:\n%.*s", data_len, data);
    // char mbuf[CONFIG_NORDIC_UART_MAX_LINE_LENGTH];
    // int start_i = 0;
    // int data_remaining = (int) data_len;
    // int tx_size = 0;
    // do {
    //     if (data_remaining <= CONFIG_NORDIC_UART_MAX_LINE_LENGTH) {
    //         tx_size = data_remaining;
    //     } else {
    //         tx_size = CONFIG_NORDIC_UART_MAX_LINE_LENGTH;
    //     }

    //     //ESP_LOGI(TAG, "tx_size: %d", tx_size);
    //     sprintf(mbuf, "%.*s", tx_size, data + start_i);
    //     //ESP_LOGI(TAG, "mbuf: %s", mbuf);
    //     ble_tx(mbuf);
    //     data_remaining -= tx_size;
    // } while (data_remaining > 0);
    ble_tx((char *)data, data_len);
    return true;
}

/**
 * @brief Device event callback
 *
 * Apart from handling device disconnection it doesn't do anything useful
 *
 * @param[in] event    Device event type and data
 * @param[in] user_ctx Argument we passed to the device open function
 */
static void handle_usb_event(const cdc_acm_host_dev_event_data_t *event, void *user_ctx)
{
    switch (event->type) {
    case CDC_ACM_HOST_ERROR:
        ESP_LOGE(TAG, "CDC-ACM error has occurred, err_no = %d", event->data.error);
        break;
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
        ESP_LOGI(TAG, "Anima disconnected");
        cdc_acm_host_close(cdc_dev);
        break;
    case CDC_ACM_HOST_SERIAL_STATE:
        ESP_LOGI(TAG, "Serial state notif 0x%04X", event->data.serial_state.val);
        break;
    case CDC_ACM_HOST_NETWORK_CONNECTION:
    default: 
        ESP_LOGW(TAG, "Unsupported CDC event: %i", event->type);
        break;
    }
}

void wait_for_anima_connect(void *args) {
    esp_err_t err = 1; 
    const cdc_acm_host_device_config_t dev_config = {
        .connection_timeout_ms = 1000,
        .out_buffer_size = 1024,
        .in_buffer_size = 32768,        // need large buffer to hold config.ini. TODO: figure out how big this actually needs to be.
        .event_cb = handle_usb_event,
        .data_cb = handle_usb_rx,
        .user_arg = NULL,
    };
    
    while(true) {
        if (!_usb_connected) {
            while (err != ESP_OK) {
                err = cdc_acm_host_open(CDC_HOST_ANY_PID, CDC_HOST_ANY_VID, 0, &dev_config, &cdc_dev);
                vTaskDelay(pdMS_TO_TICKS(500));
            }

            // Set up line coding
            cdc_acm_line_coding_t line_coding = {
                .dwDTERate = ANIMA_USB_BAUDRATE,
                .bCharFormat = ANIMA_USB_STOP_BITS,
                .bParityType = ANIMA_USB_PARITY,
                .bDataBits = ANIMA_USB_DATA_BITS
            };
            cdc_acm_host_line_coding_set(cdc_dev, &line_coding);
            cdc_acm_host_set_control_line_state(cdc_dev, true, false);
            ESP_LOGI(TAG, "Anima connected.\n");
            _usb_connected = true;
            //usb_tx((uint8_t *)"P=FF00FF00\n", 11, 1000);
            break; // End task. Will start a new one on disconnect.
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    vTaskDelete(NULL);
}

/**
 * @brief USB Host library handling task
 *
 * @param arg Unused
 */
static void usb_lib_task(void *arg)
{
    while (1) {
        // Start handling system events
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGD(TAG, "No clients. Freeing devices.\n");
            ESP_ERROR_CHECK(usb_host_device_free_all());
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGD(TAG, "All devices freed.\n");
            // Continue handling USB events to allow device reconnection
            _usb_connected = false;
            BaseType_t task_created = xTaskCreate(wait_for_anima_connect, "wait_for_anima_connect", 4096, NULL, 10, NULL);
            assert(task_created == pdTRUE);
        }
    }
}

bool usb_is_connected() {
    return _usb_connected;
}

void init_usb() {
    //esp_log_level_set(TAG, ESP_LOG_DEBUG);
    // Install USB Host driver. Should only be called once in entire application
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };
    usb_host_install(&host_config);

    // Create a task that will handle USB library events
    BaseType_t task_created = xTaskCreate(usb_lib_task, "usb_lib_task", 4096, NULL, 20, NULL);
    assert(task_created == pdTRUE);

    // Create a task to handle anima connection
    task_created = xTaskCreate(wait_for_anima_connect, "wait_for_anima_connect", 4096, NULL, 10, NULL);
    assert(task_created == pdTRUE);

    // Install CDC-ACM driver
    cdc_acm_host_install(NULL);

    ESP_LOGI(TAG, "USB Initialized.");
}

esp_err_t usb_tx(uint8_t *data, size_t data_len, uint32_t timeout_ms) {
    return cdc_acm_host_data_tx_blocking(cdc_dev, data, data_len, timeout_ms);
}