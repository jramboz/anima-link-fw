#ifndef USB_HELPERS_H_
#define USB_HELPERS_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Serial settings
#define ANIMA_USB_BAUDRATE     (115200)
#define ANIMA_USB_STOP_BITS    (0)      // 0: 1 stopbit, 1: 1.5 stopbits, 2: 2 stopbits
#define ANIMA_USB_PARITY       (0)      // 0: None, 1: Odd, 2: Even, 3: Mark, 4: Space
#define ANIMA_USB_DATA_BITS    (8)

bool usb_is_connected(void);
void init_usb(void);
esp_err_t usb_tx(uint8_t *data, size_t data_len, uint32_t timeout_ms);

#endif