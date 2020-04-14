/**
 * This file is part of VW103.
 *
 *  VW103 is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Foobar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Foobar.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <zephyr.h>
#include <kernel.h>
#include <stddef.h>
#include <devicetree.h>
#include <drivers/uart.h>
#include <drivers/gpio.h>
#include <string.h>
#include "midi.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(VW103, LOG_LEVEL_DBG);

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