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
#include <drivers/uart.h>
#include <kernel.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/__assert.h>
#include "midi.h"

#include <logging/log.h>
    LOG_MODULE_REGISTER(MIDI, LOG_LEVEL_DBG);

enum midi_status_t midi_parse_status_byte(uint8_t byte, int *cnt, uint8_t *chan)
{
    enum midi_status_t status = MIDI_NONE;
    __ASSERT(cnt != NULL, "Unexpected NULL");
    
    *cnt = 0;
    if ((byte & 0x80) == 0)
        return status;
    
    status = (enum midi_status_t)(byte >> 4);
    if (chan != NULL)
        *chan = byte & 0xf;
    switch (status)
    {
    case MIDI_PROGRAM_CHANGE:
    case MIDI_CHANNEL_PRESSURE:
        *cnt = 1;
        break;
    case MIDI_NOTE_OFF:
    case MIDI_NOTE_ON:
    case MIDI_POLY_KEY_PRESSURE:
    case MIDI_CC:
    case MIDI_PITCH_BEND:
        *cnt = 2;
        break;
    case MIDI_SYSTEM_MESSAGE:
        break;
    default:
        return MIDI_NONE;
    }
    return status;
}

enum midi_system_t midi_parse_system_byte(uint8_t byte, int* cnt, int* sysex_state)
{
    enum midi_system_t sys;
    __ASSERT(cnt != NULL, "Unexpected NULL");
    __ASSERT((byte >> 4) == MIDI_SYSTEM_MESSAGE, "Invalid system message byte");

    *cnt = 0;
    sys = (enum midi_system_t)byte;
    switch (sys)
    {
    case MIDI_TIME_CODE:
    case MIDI_SONG_SELECT:
        *cnt = 1;
        break;
    case MIDI_SONG_POSITION:
        *cnt = 2;
        break;
    case MIDI_SYSEX:
        if (sysex_state != NULL)
            *sysex_state = 1;
        *cnt = -1;
        break;
    case MIDI_END_SYSEX:
        if (sysex_state != NULL)
            *sysex_state = 0;
        break;
    case MIDI_TUNE_REQUEST:
    case MIDI_TIMING_CLOCK:
    case MIDI_SEQUENCE_START:
    case MIDI_SEQUENCE_CONTINUE:
    case MIDI_SEQUENCE_STOP:
    case MIDI_ACTIVE_SENSING:
    case MIDI_RESET:
    default:
        break;
    };
    return sys;
}

#define MIDI_QUEUE_DEPTH 10
K_MSGQ_DEFINE(midi_msgq, 4, MIDI_QUEUE_DEPTH, 4);

static void midi_read_from_uart(struct device *midi_in)
{
    /* read the data and queue it for processing */
    if (uart_irq_rx_ready(midi_in))
    {
        static struct
        {
            uint8_t buf[4]; /* work item size, message queues need a power of 2 size */
            size_t offset;
            int remaining;
            int sysex_state;
            uint8_t sysex_data[MAX_SYSEX_DATA];
            size_t sysex_offset;
        } data = {{0}, 0, 0, 0, {0}, 0};

        size_t read;
        read = uart_fifo_read(midi_in, data.buf + data.offset, 4 - data.offset);
        __ASSERT(read > 0, "Invalid UART read");

        /* first check for a system realtime and process immediately */
        if (IS_SYSTEM_REALTIME(data.buf[data.offset]))
        {
            while (k_msgq_put(&midi_msgq, data.buf+data.offset, K_NO_WAIT) != 0)
            {
                /* message queue is full, purge old data */
                k_msgq_purge(&midi_msgq);
            }
            if (read > 1)
            {
                memcpy(data.buf + data.offset, data.buf + data.offset + 1, read - 1);
            }
            return;  /* done processing, move to next read */
        }

        /* handle an open sysex */
        if (data.sysex_state != 0)
        {
            for (size_t off = data.offset; read > 0; --read)
            {
                if (midi_parse_status_byte(data.buf[off], &data.remaining, NULL) == MIDI_SYSTEM_MESSAGE)
                {
                    midi_parse_system_byte(data.buf[off], &data.remaining, &data.sysex_state);
                    if (data.sysex_state == 0) /* got the end, let's reset */
                    {
                        data.offset = 0;
                        data.remaining = 0;
                        /* TODO: this is in the ISR and it shouldn't be...need something more robust
                         * just ignore it for now */
                        /*midi_sysex_end(data.sysex_data, data.sysex_offset);*/
                        break;
                    }
                }
                data.sysex_data[data.sysex_offset++] = data.buf[off];
            }
            return; /* done processing the sysex */
        }

        /* new midi message */
        if (data.offset == 0)
        {
            if (midi_parse_status_byte(data.buf[0], &data.remaining, NULL) == MIDI_SYSTEM_MESSAGE)
            {
                midi_parse_system_byte(data.buf[0], &data.remaining, &data.sysex_state);
                if (data.sysex_state != 0)
                {
                    memcpy(data.sysex_data, data.buf+1, read-1);
                    data.sysex_offset = read-1;
                    read = 0;
                }
            }
            data.offset += read;
        }
        else
        {
            data.offset += read;
            data.remaining -= read;
        }
        if (data.remaining <= 0)
        {
            /* submit the block */
            while (k_msgq_put(&midi_msgq, data.buf, K_NO_WAIT) != 0)
            {
                /* message queue is full, purge old data */
                k_msgq_purge(&midi_msgq);
            }

            /* reset the block */
            data.offset = 0;
            data.remaining = 0;
        }
    }
}

static void midi_process_data(void* p1, void* p2, void* p3)
{
    LOG_DBG("MIDI process data");
    for (;;)
    {
        enum midi_status_t msg;
        enum midi_system_t sys;
        size_t cnt;
        uint8_t chan;
        uint8_t buf[4];
        k_msgq_get(&midi_msgq, buf, K_FOREVER);
        if ((msg = midi_parse_status_byte(buf[0], &cnt, &chan)) == MIDI_SYSTEM_MESSAGE)
        {
            int sysex_state = 0;
            sys = midi_parse_system_byte(buf[0], &cnt, &sysex_state);
        }

        switch (msg)
        {
        case MIDI_NOTE_OFF:
            midi_note_off(chan, buf[1], buf[2]);
            break;
        case MIDI_NOTE_ON:
            midi_note_on(chan, buf[1], buf[2]);
            break;
        case MIDI_POLY_KEY_PRESSURE:
            midi_poly_key_pressure(chan, buf[1], buf[2]);
            break;
        case MIDI_CC:
            midi_cc(chan, buf[1], buf[2]);
            break;
        case MIDI_PROGRAM_CHANGE:
            midi_program_change(chan, buf[1]);
            break;
        case MIDI_CHANNEL_PRESSURE:
            midi_channel_pressure(chan, buf[1]);
            break;
        case MIDI_PITCH_BEND:
        {
            uint16_t value = (buf[1] & 0x7f) | ((buf[2] & 0x7f) << 7);
            midi_pitch_bend(chan, value);
            break;
        }
        case MIDI_SYSTEM_MESSAGE:
            switch (sys)
            {
            case MIDI_SYSEX:
                midi_sysex_start();
                break;
            case MIDI_TIME_CODE:
            {
                uint8_t mtype = (buf[1] >> 4) & 0x7;
                uint8_t values = buf[1] & 0xf;
                midi_time_code(mtype, values);
                break;
            }
            case MIDI_SONG_POSITION:
            {
                uint16_t position = (buf[1] & 0x7f) | ((buf[2] & 0x7f) << 7);
                midi_song_position(position);
                break;
            }
            case MIDI_SONG_SELECT:
                midi_song_select(buf[1]);
                break;
            case MIDI_TUNE_REQUEST:
                midi_tune_request();
                break;
            case MIDI_END_SYSEX:
                break;  /* TODO: implement this */
            case MIDI_TIMING_CLOCK:
                midi_timing_clock();
                break;
            case MIDI_SEQUENCE_START:
                midi_sequence_start();
                break;
            case MIDI_SEQUENCE_CONTINUE:
                midi_sequence_continue();
                break;
            case MIDI_SEQUENCE_STOP:
                midi_sequence_stop();
                break;
            case MIDI_ACTIVE_SENSING:
                midi_active_sensing();
                break;
            case MIDI_RESET:
                midi_reset();
                break;
            default:
                break;
            }
            break;
        default:
            break; /* invalid message, just ignore it */
        }
    }
}

K_THREAD_DEFINE(midi_tid, 512, midi_process_data, NULL, NULL, NULL, 5, 0, K_NO_WAIT);

void midi_init(struct device *midi_in)
{
    __ASSERT_NO_MSG(midi_in);

    LOG_DBG("midi_init");

    uart_irq_callback_set(midi_in, midi_read_from_uart);
}

void midi_start(struct device *midi_in)
{
    LOG_DBG("midi_start");
    uart_irq_rx_enable(midi_in);
}

__weak void midi_note_off(uint8_t chan, uint8_t key, uint8_t velocity)
{
    LOG_DBG("NOTE OFF\t%d, %d, %d", chan, key, velocity);
}

__weak void midi_note_on(uint8_t chan, uint8_t key, uint8_t velocity)
{
    LOG_DBG("NOTE ON\t%d, %d, %d", chan, key, velocity);
}

__weak void midi_poly_key_pressure(uint8_t chan, uint8_t key, uint8_t velocity)
{
    LOG_DBG("Poly Key Pres\t%d, %d, %d", chan, key, velocity);
}

__weak void midi_cc(uint8_t chan, uint8_t controller, uint8_t value)
{
    LOG_DBG("CC\t\t%d, %d, %d", chan, controller, value);
}

__weak void midi_program_change(uint8_t chan, uint8_t program)
{
    LOG_DBG("Prog Chg\t%d, %d", chan, program);
}

__weak void midi_channel_pressure(uint8_t chan, uint8_t pressure)
{
    LOG_DBG("Chan Pres\t%d, %d", chan, pressure);
}

__weak void midi_pitch_bend(uint8_t chan, uint16_t value)
{
    LOG_DBG("Pitch Bend\t%d, %d", chan, value);
}

__weak void midi_sysex_start()
{
    LOG_DBG("SYSEX start");
}

__weak void midi_sysex_end(uint8_t* data, size_t cnt)
{
    LOG_DBG("SYSEX end");
}

__weak void midi_time_code(uint8_t mtype, uint8_t values)
{
    LOG_DBG("Time code %d %d", mtype, values);
}

__weak void midi_song_position(uint16_t position)
{
    LOG_DBG("Song position %d", position);
}

__weak void midi_song_select(uint8_t song)
{
    LOG_DBG("Song select %d", song);
}

__weak void midi_tune_request()
{
    LOG_DBG("Tune rqst");
}

__weak void midi_timing_clock()
{
    LOG_DBG("Timing clock");
}

__weak void midi_sequence_start()
{
    LOG_DBG("Seq start");
}

__weak void midi_sequence_continue()
{
    LOG_DBG("Seq continue");
}

__weak void midi_sequence_stop()
{
    LOG_DBG("Seq stop");
}

__weak void midi_active_sensing()
{
    LOG_DBG("Active sense");
}

__weak void midi_reset()
{
    LOG_DBG("Reset");
}