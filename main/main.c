#include <stdio.h>

#include "sdkconfig.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ble_helpers.h"
#include "log_helpers.h"
#include "usb_helpers.h"

static const char *TAG = "Anima Link";  // logging tag

// ----- Main Func -----
void app_main(void)
{   
    init_logging();
    init_usb();
    init_ble();
    ESP_LOGI(TAG, "Anima Link Started!");
}