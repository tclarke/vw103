#include <zephyr.h>
#include <kernel.h>
#include <stddef.h>
#include <devicetree.h>
#include <drivers/uart.h>
#include <drivers/gpio.h>
#include <string.h>
#include "midi.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(VW103);

#define MIDI_DEV "UART_3"
#define LED0 DT_ALIAS_LED0_GPIOS_CONTROLLER
#define LED0_PIN DT_ALIAS_LED0_GPIOS_PIN
#define FLAGS 0

void main(void)
{
    struct device* led_dev = device_get_binding(LED0);
    __ASSERT_NO_MSG(led_dev);
    gpio_pin_configure(led_dev, LED0_PIN, GPIO_OUTPUT_ACTIVE | FLAGS);
    gpio_pin_set(led_dev, LED0_PIN, 1);
    struct device *midi_in = device_get_binding(MIDI_DEV);
    __ASSERT_NO_MSG(midi_in);
    midi_init(midi_in);
    midi_start(midi_in);
}