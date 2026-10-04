#pragma once
typedef int gpio_num_t;
typedef int esp_err_t;
inline esp_err_t gpio_hold_dis(gpio_num_t) { return 0; }
inline esp_err_t gpio_hold_en(gpio_num_t) { return 0; }
inline esp_err_t gpio_deep_sleep_hold_dis() { return 0; }
inline esp_err_t gpio_deep_sleep_hold_en() { return 0; }
inline esp_err_t gpio_reset_pin(gpio_num_t) { return 0; }
