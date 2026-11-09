#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

// Waveshare ESP32-S3 4.3-inch Type B hardware (ST7262 + GT911).
bool displayHardwareBegin();
void displayWritePixels(int x1, int y1, int x2, int y2, const void *colorData);
bool touchHardwareRead(uint16_t &x, uint16_t &y, bool &pressed);

#endif // DISPLAY_H