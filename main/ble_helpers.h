#ifndef BLE_HELPERS_H_
#define BLE_HELPERS_H_

#include <stdbool.h>

bool ble_is_connected(void);
void init_ble(void);
void ble_tx(char *data, size_t data_len);

#endif