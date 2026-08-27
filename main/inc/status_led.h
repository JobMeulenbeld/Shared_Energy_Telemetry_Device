#ifndef LED_RING_H
#define LED_RING_H

#include "esp_err.h"

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "led_strip.h"


#define LED_RING_NUM_LEDS 8

#define STATUS_LED_WIFI_GPIO  GPIO_NUM_44 // D7, blue
#define STATUS_LED_POWER_GPIO GPIO_NUM_7  // D8, red
#define STATUS_LED_DATA_GPIO  GPIO_NUM_8  // D9, blue

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_rgb_t;

typedef struct {
    led_strip_handle_t strip;
    led_rgb_t pixels[LED_RING_NUM_LEDS];
    uint8_t brightness;
} led_ring_t;

esp_err_t led_ring_init(led_ring_t *ring, int gpio_num);
esp_err_t led_ring_set_brightness(led_ring_t *ring, float brightness_percent);
esp_err_t led_ring_set_pixel(led_ring_t *ring, uint8_t index, led_rgb_t color);
esp_err_t led_ring_set_all(led_ring_t *ring, led_rgb_t color);
esp_err_t led_ring_clear(led_ring_t *ring);
esp_err_t led_ring_show(led_ring_t *ring);
esp_err_t led_ring_set_fill(
    led_ring_t *ring,
    float percentage,
    led_rgb_t color);
esp_err_t led_ring_start_loop_async(
    led_ring_t *ring,
    led_rgb_t color,
    uint32_t delay_ms,
    int loop_count
);

esp_err_t status_leds_init(void);
esp_err_t status_led_set_wifi(bool on);
esp_err_t status_led_set_power(bool on);
esp_err_t status_led_set_data(bool on);
#endif
