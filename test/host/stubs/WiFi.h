#pragma once

#include <Arduino.h>

#define WIFI_STA 1

class HostWiFi {
public:
    void mode(int) {}
    void disconnect() {}
};
extern HostWiFi WiFi;
