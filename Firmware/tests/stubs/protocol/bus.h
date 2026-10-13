#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint16_t id; uint8_t d[8]; } BusFrame;
extern BusFrame g_bus[256]; extern int g_bus_n;       /* frames transmitted */
extern BusFrame g_rx[256];  extern int g_rx_n, g_rx_i; /* frames to receive  */
#ifdef __cplusplus
}
#endif
