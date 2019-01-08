#include <EEPROM.h>
#include <SPI.h>
//#include <USBComposite.h>
#include "tuning_12tet.h"

// define this to initialize a fresh board..this will write the initial EEPROM data
//#define FIRST_TIME
//
// Configuration section
//
#define DAC_CS PA4
#define GATE_A PB4
#define GATE_B PB5
#define METRONOME PB12

#define NUM_DAC_PORTS 2
struct CalibrationData
{
  int gain[NUM_DAC_PORTS];  // fixed point * 10000
  int offset[NUM_DAC_PORTS];

  uint16 get(uint16 Address)
  {
    uint16 rval = EEPROM_OK;
    uint16* raw_data = (uint16*)this;
    for (uint16 off = sizeof(CalibrationData)/2; off; --off)
    {
      if ((rval = EEPROM.read(Address + off, &raw_data[off])) != EEPROM_OK)
        break;
    }
    return rval;
  }
  
  uint16 put(uint16 Address)
  {
    uint16 rval = FLASH_COMPLETE;
    const uint16* raw_data = (const uint16*)this;
    for (uint16 off = sizeof(CalibrationData)/2; off; --off)
    {
      rval = EEPROM.update(Address + off, raw_data[off]);
      if (rval != EEPROM_SAME_VALUE && rval != FLASH_COMPLETE)
        break;
    }
    return rval;
  }
};

#define EEPROM_CALIBRATION 0x0000  // The offset of the calibration data in the EEPROM
#define EEPROM_TUNING_SIZE = (sizeof(short) * 128)
#define EEPROM_TUNING(__bank) (EEPROM_CALIBRATION + ((__bank) * EEPROM_TUNING_SIZE))

#ifdef FIRST_TIME
void setup()
{
  pinMode(METRONOME, OUTPUT);
  digitalWrite(METRONOME, HIGH);
  CalibrationData calibration;
  for (int i = 0; i < NUM_DAC_PORTS; i++)
  {
    calibration.gain[i] = 10000;
    calibration.offset[i] = 0;
  }
  calibration.put(EEPROM_CALIBRATION);
}

void loop() {
  for (int i = 0; i < 5; i++)
  {
    digitalWrite(METRONOME, i%2 ? LOW : HIGH);
    delay(1000);
  }
}

#else

class TLV5618A
{
public:
  enum Port { PortA, PortB };
  
  TLV5618A(int cs_pin) : _pin(cs_pin) {}

  void init() {
    SPI.begin();
    SPI.setBitOrder(MSBFIRST);
    SPI.setDataMode(SPI_MODE0);
    SPI.setClockDivider(SPI_CLOCK_DIV16);
    pinMode(_pin, OUTPUT);
  }

  void set_output(Port port, unsigned short value) {
    unsigned short data;
    switch (_port) {
      case PortA:
        data = value | 0xc000;  // Port A fast settle
        break;
      case PortB:
        data = value | 0x4000;  // Port B fast settle
    }
    digitalWrite(_pin, LOW);
    SPI.transfer16(data);
    digitalWrite(_pin, HIGH);
    delayMicroseconds(10);  // make sure it completes before attempting to send another
  }

private:
  int _pin;
  Port _port;
};

class MIDI2CV ///: public USBMIDI
{
  short tuning[128];
  //
  // hardware cal procedure...gives approx 12 octave range..MIDI has 10 2/3 octave range..this leaves room for nearly 1 oct pitch bend
  //  - output 4095 and measure voltage...should be approx 6V
  //  - output 0 and mesure voltage...should be approx -6V

  // software cal:
  //  - output 0 and measure voltage Vmin
  //  - output 4095 and measure volage Vmax
  //  - Vspan = Vmax - Vmin
  //  - ideal span Vispan is 12 and ideal Vimin is -6
  //  - Voffset = Vmin - Vimin
  //  - offset = (4096 * Voffset) / Vspan
  //  calibration[0] = (Vispan / Vspan) * 10000
  //  calibration[1] = offset
  //  - DACactual = calibration[0] * DAC / 10000 + calibration[1]
  CalibrationData calibration;
  
  unsigned short get_dac_value(byte midi_note, unsigned int dac_channel)
  {
    int dac = tuning[midi_note];
    Serial1.print("\tCal="); Serial1.print(calibration.gain[dac_channel]); Serial1.print(","); Serial1.print(calibration.offset[dac_channel]);
    // apply calibration
    dac = calibration.gain[dac_channel] * dac / 10000 + calibration.offset[dac_channel];
    Serial1.print("\tResult="); Serial1.println(dac);
    dac = constrain(dac, 0, 4095);
    return dac;
  }
public:
  MIDI2CV(TLV5618A& dac) : _dac(dac), clockCount(0) {}

  void init()
  {
    // load calibration from the EEPROM
    calibration.get(EEPROM_CALIBRATION);

    // default tuning
    memcpy(tuning, tuning_default, sizeof(tuning));

    pinMode(GATE_A, OUTPUT);
    pinMode(GATE_B, OUTPUT);
    pinMode(METRONOME, OUTPUT);
    digitalWrite(METRONOME, HIGH);  // active low

    Serial1.println("Default tuning.");
    for (int i = 0; i < 128; i++) {
      Serial1.println(tuning[i]);
    }
  }
  
  virtual void handleNoteOff(unsigned int channel, unsigned int note, unsigned int velocity)
  {
    if (channel == 2) {
      digitalWrite(GATE_B, LOW);
    } else {
      digitalWrite(GATE_A, LOW);
    }
  }
  
  virtual void handleNoteOn(unsigned int channel, unsigned int note, unsigned int velocity)
  {
    // set the pitch and gate
    if (channel == 2) {
      _dac.set_output(TLV5618A::PortB, get_dac_value(note, 1));
      digitalWrite(GATE_B, HIGH);
    } else {
      _dac.set_output(TLV5618A::PortA, get_dac_value(note, 0));
      digitalWrite(GATE_A, HIGH);
    }
  }

  // Tick the clock and flash the LED every quarter note (MIDI uses 24 ppqn)
  // PB12 can be accessed externally as well, be careful how much current you sink
  virtual void handleSync()
  {
    this->clockCount++;

    if (this->clockCount % 24 == 0)
      digitalWrite(METRONOME, LOW);
    else if (this->clockCount % 24 == 2)
      digitalWrite(METRONOME, HIGH);
  }

private:
  TLV5618A& _dac;
  int clockCount;
};

TLV5618A dac(DAC_CS);
MIDI2CV midi(dac);
//USBCompositeSerial CompositeSerial;

void setup()
{
  Serial1.begin(9600);
//  USBComposite.setProductId(0x0030);

  // setup the main class
  midi.init();

  // setup the DAC
  dac.init();

  // Setup USB MIDI interface
//  midi.registerComponent();
//  CompositeSerial.registerComponent();
//  USBComposite.begin();
}

void loop() {
  for (int i = 0; i < 256; i++) {
    Serial1.println(i);
    midi.handleNoteOn(1, i, 0);
    delay(500);
    midi.handleNoteOff(1, i, 0);
    delay(500);
  }
//    midi.poll();
}
#endif
