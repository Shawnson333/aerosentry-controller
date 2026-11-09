#ifndef LVGL_V8_PORT_H
#define LVGL_V8_PORT_H

// Initialize the Type B display, GT911 touch, and LVGL 8.3.
bool lvgl_port_init();

// Run one LVGL timer-handler pass from the Arduino loop.
void lvgl_port_loop();

#endif // LVGL_V8_PORT_H