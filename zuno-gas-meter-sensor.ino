
/*****************************************************************************

*************************************
* Gas meter sensor sketch for Z-Uno *
*************************************

Overview
========

This is code for a gas meter sensor for an old-style gas meter which support
taking readings through a magnetic pulse.  Gas meter readings are taken by
detecting the movement of a magnet on one of the rotating number dials using
a reed switch which is connected to a Z-Uno developer kit.

This has been tested with Z-Uno generation 2, not known if it works with
Z-Uno gen 1.

Hardware
--------

You need:

- A Z-Uno developer kit
- A reed switch.  I used a "BQLZR cylindrical plastic mounted reed proximity
  switch", no doubt others will do.
- Some way to power the Z-Uno, there are a lot of options, but to get
  started you can use a USB power supply.

The reed switch is connected between the ground pin (GND) and the pin
called "Pin 18".  These are not standard pin numbers, so verify with the
pin-out.  https://z-uno.z-wave.me/technical/pinout/

Programming
-----------

Software is is an Arduino sketch which can be loaded into the Arduino
IDE, and flashed to the Z-Uno through its USB port.

You need to follow the quickstart guide on the Z-Uno site to add Z-Uno
board support to the Arduino IDE.

I find Arduino 2.1.1 works with the Z-Uno support, Arduino 2.2.1 does not.

Z-Wave specification
====================

Identifier
----------

This is not an official Z-Wave accredited device.  I've used a random
user-defined product ID as described here:

  https://z-uno.z-wave.me/Reference/ZUNO_SETUP_PRODUCT_ID/

Manufacturer ID:     0x0115
Product Type:        0x0210
Product ID:          0x7ac2

Association groups
------------------

The root endpoint supports the 'lifeline' group, group 1.  The device
reports sensor information to this group, which should be configured
with the identity of the Z-Wave network controller to receive sensor
reports.  This is likely configured by default by the network controller.

Configuration parameters
------------------------

Configuration v4:

  Initial meter reading (64): Specifies initial meter reading at time of
      install (Default: 0)

  Meter report period (65): Specifies period in seconds between meter
      reports (Default: 60)

  Pulse debounce time (66): Specifies period in milliseconds in which to
      ignore multiple pulses (Default: 5000)

  Reading increment per pulse (67): Specifies the reading incremement to
      apply per pulse (Default: 1)

Command classes
---------------

- METER V6:
  - Gas consumption (Cubic meters)
- CONFIGURATION V4
- BATTERY V1
- WAKE UP V3

Design factors
==============

- Want to run low power and so selected EM4 sleep mode (which does not
  appear to work, I think it's in EM2 mode).
- Need to save reading values, hence these are written to EEPROM.
- EEPROM writes are slow, this cannot be done in the interrupt routine,
  or Z-Wave protocol routines.  Could trigger a watchdog reset.
- Pulse meters are read using a reed switch.  On opening and closing there
  are commonly multiple on/off events cause by some physical 'bounce' in
  the switch.  The de-bounce logic here turns these multiple triggers into
  a single pulse event.

Known issues
============

This has not been 'production' tested, it works for me and my meter.
No warranty.

The low-power mode (EM4) does not appear to work with this code, the reason
for that is not known.

There are periodic resets for an unknown cause.  I suspect this is a
watch-dog event and is linked to EEPROM writes.  The loop() code contains
a get_reading() call to get the reading updated and EEPROM value written
out.  I suspect a timing condition, possibly occasionally the get_reading()
invocation from the Z-Wave core can occur after an interrupt but before
the loop() code has reconciled the new meter pulse.  In practice this means
a reset at a safe time, and likely no information is lost.

Licence
=======

    Z-Uno Gas Meter Sensor - Z-Wave sensor for a pulse gas meter
    Copyright (C) 2023-2024  cybermaggedon

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

*****************************************************************************/

#include "EEPROM.h"

/****************************************************************************/
/* Constants etc.
/****************************************************************************/

// Interrupt driven by an exteral pulse on pin 18.
#define PULSE_PIN 18

// Comment this out to turn off debug output on serial board.
#define UART Serial

// Trying to use EM4 sleep mode, doesn't appear to work
#define SLEEP_MODE SLEEP_MODE_EM4

// EEPROM addresses
#define READING_ADDRESS (0x0)         // The last reading is stored here
#define INIT_COPY_ADDRESS (0x4)       // This is a copy of the initial reading
                                      // to help determine whether to
                                      // re-apply the setting.

// Configuration value IDs
enum{
    INITIAL_METER_READING=64,   // Initial reading value only used at install
    METER_REPORT_PERIOD,        // Period (s) to send meter reports
    DEBOUNCE_TIME,              // Period in which to ignore multiple pulses
    PULSE_INCREMENT             // Amount to increase the reading for each
                                // pulse.
};

// This allows EEPROM storage to be turned off during development.  I do
// this because EEPROM writes aren't an infinite resource.
#define USE_EEPROM 1

// When enabled, outputs sleep/wake messages and turns on the LED when
// awake.
#define SLEEP_WAKE_DEBUG_HANDLERS 1

/****************************************************************************/
/* Configuration values
/****************************************************************************/

DWORD initial_meter_reading;
DWORD meter_report_period;
DWORD debounce_time;
DWORD pulse_increment;
boolean reading_valid = false;

void config_parameter_changed(uint8_t param, uint32_t value) {

    if (param == INITIAL_METER_READING) {
        initial_meter_reading = value;
        reading_valid = false;         // This triggers a reset of the meter
#ifdef UART
        UART.print("Initial reading config = ");
        UART.println(initial_meter_reading);
#endif
    }

    if (param == meter_report_period) {
        meter_report_period = value;
#ifdef UART
        UART.print("Meter report period = ");
        UART.println(meter_report_period);
#endif
    }

    if (param == DEBOUNCE_TIME) {
        debounce_time = value;
#ifdef UART
        UART.print("Pulse debounce time = ");
        UART.println(debounce_time);
#endif
    }

    if (param == PULSE_INCREMENT) {
        pulse_increment = value;
#ifdef UART
        UART.print("Pulse increment = ");
        UART.println(pulse_increment);
#endif
    }

}

ZUNO_SETUP_CFGPARAMETER_HANDLER(config_parameter_changed);

/****************************************************************************/
/* State implementation: EEPROM version
/****************************************************************************/

#ifdef USE_EEPROM

DWORD reading;
DWORD reading_delta;

DWORD get_reading() {

  if (!reading_valid)
    init_reading();

  if (reading_delta) {
    reading += reading_delta;
    reading_delta = 0;
    EEPROM.put(READING_ADDRESS, &reading, sizeof(reading));
  }

  return reading;
}

void inc_reading(DWORD value) {
  reading_delta += value;
}

void reset_reading() {
  reading = 0;
  reading_delta = 0;
  EEPROM.put(READING_ADDRESS, &reading, sizeof(reading));
}

void init_reading() {

  DWORD init_value;
  DWORD init_copy;

  init_value = initial_meter_reading;
  EEPROM.get(INIT_COPY_ADDRESS, &init_copy, sizeof(init_copy));

  if (init_value != init_copy) { 
#ifdef UART
    UART.println("Resetting EEPROM values");
#endif
    EEPROM.put(READING_ADDRESS, &init_value, sizeof(init_value));
    EEPROM.put(INIT_COPY_ADDRESS, &init_value, sizeof(init_value));

  }

  EEPROM.get(READING_ADDRESS, &reading, sizeof(reading));
#ifdef UART
  UART.print("Loading previous reading = ");
  UART.println(reading);
#endif

  reading_delta = 0;
  reading_valid = true;

}

#else

/****************************************************************************/
/* State implementation: In-memory, doesn't use EEPROM
/****************************************************************************/

DWORD reading;
DWORD reading_delta;
DWORD debounce_time;
DWORD pulse_increment;

DWORD get_reading() {

  if (!reading_valid)
    init_reading();

  if (reading_delta) {
    reading += reading_delta;
    reading_delta = 0;
  }

  return reading;
}

void inc_reading(DWORD value) {
  reading_delta += value;
}

void reset_reading() {
  reading = 0;
  reading_delta = 0;
}

void init_reading() {
  reading = initial_meter_reading;
  reading_delta = 0;
  reading_valid = true;
}

#endif

/****************************************************************************/
/* Z-Wave definitions
/****************************************************************************/

// Enable advanced options
//ZUNO_ENABLE(WITH_CC_WAKEUP WITH_CC_BATTERY LOGGING_DBG LOGGING_UART=Serial SKETCH_FLAGS=16);

// Device uses sleep mode (EM4)
ZUNO_SETUP_SLEEPING_MODE(ZUNO_SLEEPING_MODE_SLEEPING);

// Debug mode
//ZUNO_SETUP_DEBUG_MODE(DEBUG_ON);

// Configuration parameter definitions
ZUNO_SETUP_CONFIGPARAMETERS(
        ZUNO_CONFIG_PARAMETER_INFO(
            "Initial meter reading",
            "Specifies initial meter reading at time of install",
            0, 100000000, 0
        ),
        ZUNO_CONFIG_PARAMETER_INFO(
            "Meter report period",
            "Specifies period in seconds between meter reports",
            15, 3600, 60
        ),
        ZUNO_CONFIG_PARAMETER_INFO(
            "Pulse debounce time",
            "Specifies period in milliseconds for ignored pulses",
            0, 30000, 5000
        ),
        ZUNO_CONFIG_PARAMETER_INFO(
            "Reading increment per pulse",
            "Specifies the reading incremement to apply per pulse",
            1, 1000000, 1
        )
);

// Channels
ZUNO_SETUP_CHANNELS(
    ZUNO_METER(
        ZUNO_METER_TYPE_GAS,               // Gas meter
        METER_RESET_ENABLE,                // Provides meter reset function
        ZUNO_METER_WATER_SCALE_METERS3,    // Unit is cubic-meters
        METER_SIZE_FOUR_BYTES,             // 4-bytes precision
        METER_PRECISION_THREE_DECIMALS,    // 3 decimal places
        get_reading,                       // Reading 'get' function
        reset_reading                      // Reading 'reset' function
    )
);

// Product ID set here.  I made up this number at random.
ZUNO_SETUP_PRODUCT_ID(0x7A, 0xC2);

/****************************************************************************/
/* Wake-up & interrupt support
/****************************************************************************/

// Loop count, using this to check the device goes into EM4 i.e.
// memory is wiped
uint32_t loop_count = 0;

// Count number of returns from EM4 state when calling wake_reason.  Using
// this to diagnose why it isn't going into EM4 state.
uint32_t em4_count = 0;

// Returns the reason for a wakeup as a string.  As a side-effect, WUT_EM4
// and EXT_EM4 increment the em4_count so can check whether EM4 state is
// achieved.
String wake_reason() {
  byte wakeUpReason = zunoGetWakeReason();
  switch (wakeUpReason) {
    case ZUNO_WAKEUP_REASON_POR: return "power-on-reset";
    case ZUNO_WAKEUP_REASON_PIN: return "reset-button";
    case ZUNO_WAKEUP_REASON_EXT_EM4:
      em4_count++;
      return "ext-interrupt-em4";
    case ZUNO_WAKEUP_REASON_EXT_EM2: return "ext-interrupt-em2";
    case ZUNO_WAKEUP_REASON_WUT_EM4:
      em4_count++;
      return "wakeup-timer-em4";
    case ZUNO_WAKEUP_REASON_WUT_EM2: return "wakeup-timer-em2";
    case ZUNO_WAKEUP_REASON_RADIO_EM2: return "flirs-packet-em2";
    case ZUNO_WAKEUP_REASON_WATCH_DOG: return "watchdog";
    case ZUNO_WAKEUP_REASON_SOFTRESET: return "soft-reset";
    default: return "not-defined";
  }
}

// Returns true if the device was last woken by a wake-timer.
boolean woken_by_timer() {

    // Wake reason
    byte wake_up_reason = zunoGetWakeReason();

    // Wake-up from EM4 and EM2 states.
    return
        (wake_up_reason == ZUNO_WAKEUP_REASON_WUT_EM4) ||
        (wake_up_reason == ZUNO_WAKEUP_REASON_WUT_EM2);
}

// This stores the last time the pulse was received
uint32_t moment = 1 << 31;

// When a pulse is received, store the time
void remember() {
  moment = millis();
}

// Work out time since last pulse, allowing for the time to wrap.
uint32_t since() {
  uint32_t now = millis();
  uint32_t then = moment;
  uint32_t diff = now - then;
  if (diff > 65536) diff -= 65536;
  return diff;
}

// Interrupt handler for button on pin 18, increments the interrupt count.
// No de-bounce on the button, so click can result in multiple increemnts
void interrupt() {

  // Ignore multiple pulses in the de-bounce window.  This deals with extra
  // pulses in the reed-switch open/close
  if (since() < debounce_time) return;

  // Pulse is accepted, remember pulse time
  remember();

  // Increase the reading.  This increases a delta value, don't want any
  // slow EEPROM type stuff happening in this interrupt function.
  inc_reading(pulse_increment);

}

/****************************************************************************/
/* Core lifecycle
/****************************************************************************/

#ifdef SLEEP_WAKE_DEBUG_HANDLERS
void wake_handler(){
    UART.println("*** WAKE!");
    digitalWrite(LED_BUILTIN, 1);
}

void sleep_handler(){
    UART.println("*** SLEEP!");
    digitalWrite(LED_BUILTIN, 0);
    zunoEM4EnablePinWakeup(PULSE_PIN);
}
#endif

// Core calls this on reset
void setup() {

    initial_meter_reading = zunoLoadCFGParam(INITIAL_METER_READING);
    meter_report_period = zunoLoadCFGParam(METER_REPORT_PERIOD);
    debounce_time = zunoLoadCFGParam(DEBOUNCE_TIME);
    pulse_increment = zunoLoadCFGParam(PULSE_INCREMENT);

    // Debug stuff if debug is enabled
#ifdef UART
    UART.begin(115200);
    UART.println();

    UART.println("Gas meter pulse sensor");
    UART.println("======================");

    // Set UART to output debug data
    UART.print("setup: called, ");
    UART.println(wake_reason());

    UART.print("Initial reading config = ");
    UART.println(initial_meter_reading);
    UART.print("Meter report period = ");
    UART.println(meter_report_period);
    UART.print("Pulse debounce time = ");
    UART.println(debounce_time);
    UART.print("Pulse increment = ");
    UART.println(pulse_increment);
#endif

    // Initialise the reading
    init_reading();

    // Configure pin 18 for an interrupt
    zunoEM4EnablePinWakeup(PULSE_PIN);     // Not needed, according to ref,
                                           // pin 18 is already set up as an
                                           // EM4 wake
    pinMode(PULSE_PIN, INPUT_PULLUP);      // Pin 18 pull-up mode
    attachInterrupt(PULSE_PIN, interrupt, FALLING); // Falling interrupt
    
    // Wake up in a couple of seeconds and provide an initial report
    zunoSetCustomWUPTimer(2);
//    zunoLockSleep();                       // Probably not needed
    zunoSendDeviceToSleep(SLEEP_MODE);     // Also probably not needed

#ifdef SLEEP_WAKE_DEBUG_HANDLERS
    // Calls to wake/sleep handlers
    zunoAttachSysHandler(ZUNO_HANDLER_SLEEP, 0, (void*) &sleep_handler);
    zunoAttachSysHandler(ZUNO_HANDLER_WUP, 0, (void*) &wake_handler);
#endif

}

// Core calls this repeatedly while awake
void loop() {

#ifdef UART
    // Some debug output

    // Milliseconds since boot
    UART.print("uptime = ");
    UART.print(millis());

    // Loop counter.  Should zero when device goes into EM4 state
    UART.print(" - loop count = ");
    UART.print(loop_count);

    // Reading
    UART.print(" - reading = ");
    UART.print(get_reading());

    // Count of EM4 wake-ups, should also zero when device goes into EM4 state
    UART.print(" - em4_count = ");
    UART.print(em4_count);

    // Time since last interrupt in millis.
    UART.print(" - since = ");
    UART.print(since());

    // Dump out last wake reason
    UART.print(" - ");
    UART.print(wake_reason());

    if (zunoInNetwork())
        UART.print(" - in-network");
    else
        UART.print(" - not-in-network");

    // Outputs sleep_locked if sleep locked
    if (zunoIsSleepLocked())
        UART.println(" - sleep_locked");
    else    
        UART.println();

#else

    // Behind the scenes, this reconciles the 'delta' value into the
    // reading value and then writes to EEPROM as a side-effect.  This is
    // a good place to do this because we don't want the reconciliation/
    // slow EEPROM write to happen in an interrupt routine or Z-Wave
    // protocol function.
    get_reading();

#endif

    // Woken by a timer, and also prevented from sleeping?  Good place
    // to do a Z-Wave sensor report.
    if(woken_by_timer() && zunoIsSleepLocked()) {

        // Send channel reports
        if (zunoInNetwork()) {
#ifdef UART
            UART.println("Sending report");
#endif
            zunoSendReport(1);
        } else {
#ifdef UART
            UART.println("Not reporting, not in network");
#endif
        }

        // Set sleep for next meter report
        zunoSetCustomWUPTimer(meter_report_period);

        // Send device to EM4 sleep state when it is ready to sleep
        zunoSendDeviceToSleep(SLEEP_MODE);

    }

    zunoSendDeviceToSleep(SLEEP_MODE);

    // Increment loop count
    loop_count++; // Increment variable

    // Wait 1 second before leaving loop
    delay(1000);

}

