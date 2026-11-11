#pragma once

#include <stdint.h>

/* Accept a non-sensor ESP-NOW frame. Safe to call from the receive callback. */
void radioLinkAccept(const uint8_t *mac, const uint8_t *data, int len);

/* Remember the Heltec so status replies have a destination. */
void radioLinkNotePeer(const uint8_t *mac);

/* Apply a queued command on the main loop and send status. */
void radioLinkUpdate();
