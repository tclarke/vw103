#ifndef MIDI_H__
#define MIDI_H__

enum midi_status_t {
    MIDI_NONE               =0x0,
    MIDI_NOTE_OFF           =0x8,
    MIDI_NOTE_ON            =0x9,
    MIDI_POLY_KEY_PRESSURE  =0xa,
    MIDI_CC                 =0xb,
    MIDI_PROGRAM_CHANGE     =0xc,
    MIDI_CHANNEL_PRESSURE   =0xd,
    MIDI_PITCH_BEND         =0xe,
    MIDI_SYSTEM_MESSAGE     =0xf
};

#define IS_SYSTEM_REALTIME(b__) ((b__) & 0xf8)

enum midi_system_t
{
    /* system common messages */
    MIDI_TIME_CODE          =0xf1,
    MIDI_SONG_POSITION      =0xf2,
    MIDI_SONG_SELECT        =0xf3,
    MIDI_TUNE_REQUEST       =0xf6,
    /* system exclusive messages */
    MIDI_SYSEX              =0xf0,
    MIDI_END_SYSEX          =0xf7,
    /* system realtime messages */
    MIDI_TIMING_CLOCK       =0xf8,
    MIDI_SEQUENCE_START     =0xfa,
    MIDI_SEQUENCE_CONTINUE  =0xfb,
    MIDI_SEQUENCE_STOP      =0xfc,
    MIDI_ACTIVE_SENSING     =0xfe,
    MIDI_RESET              =0xff,

    MIDI_SYSTEM_NONE        =0
};

#define MAX_SYSEX_DATA 128

extern enum midi_status_t midi_parse_status_byte(uint8_t byte, int *cnt, uint8_t *chan);
extern enum midi_system_t midi_parse_system_byte(uint8_t byte, int *cnt, int *sysex_state);
extern void midi_init(struct device *midi_in);
extern void midi_start(struct device *midi_in);

extern void midi_note_off(uint8_t chan, uint8_t key, uint8_t velocity);
extern void midi_note_on(uint8_t chan, uint8_t key, uint8_t velocity);
extern void midi_poly_key_pressure(uint8_t chan, uint8_t key, uint8_t velocity);
extern void midi_cc(uint8_t chan, uint8_t controller, uint8_t value);
extern void midi_program_change(uint8_t chan, uint8_t program);
extern void midi_channel_pressure(uint8_t chan, uint8_t pressure);
extern void midi_pitch_bend(uint8_t chan, uint16_t value);
extern void midi_sysex_start();
extern void midi_sysex_end(uint8_t *data, size_t cnt);
extern void midi_time_code(uint8_t mtype, uint8_t values);
extern void midi_song_position(uint16_t position);
extern void midi_song_select(uint8_t song);
extern void midi_tune_request();
extern void midi_timing_clock();
extern void midi_sequence_start();
extern void midi_sequence_continue();
extern void midi_sequence_stop();
extern void midi_active_sensing();
extern void midi_reset();

#endif