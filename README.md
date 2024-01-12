
# Gas meter sensor sketch for Z-Uno

## Overview

This is code for a gas meter sensor for an old-style gas meter which support
taking readings through a magnetic pulse.  Gas meter readings are taken by
detecting the movement of a magnet on one of the rotating number dials using
a reed switch which is connected to a Z-Uno developer kit.

This has been tested with Z-Uno generation 2, not known if it works with
Z-Uno gen 1.

### Hardware

You need:

- A Z-Uno developer kit
- A reed switch.  I used a "BQLZR cylindrical plastic mounted reed proximity
  switch", no doubt others will do.
- Some way to power the Z-Uno, there are a lot of options, but to get
  started you can use a USB power supply.  I found a power supply from
  Treedix which supplies 3.3V from a 3.7V (18650) battery, should keep the
  device going a good long while.

The reed switch is connected between the ground pin (GND) and the pin
called "Pin 11".  These are not standard pin numbers, so verify with the
pin-out.  https://z-uno.z-wave.me/technical/pinout/

                     (-)                (+)
                        ,--------------.
                 ,------| power supply |------.
                 |      `--------------'      |
                 |                            |
                 |      ,-------------.       |
         +-------+------| Z-UNO gen 2 |-------'
         |          GND `-------------' 3.3V
         |                  11 |
         |                     |
         |                     |
         |                     |
         |   ,-------------.   |
         `---| reed switch |---'
             `-------------'

### Programming

Software is is an Arduino sketch which can be loaded into the Arduino
IDE, and flashed to the Z-Uno through its USB port.

You need to follow the quickstart guide on the Z-Uno site to add Z-Uno
board support to the Arduino IDE.

I find Arduino 2.1.1 works with the Z-Uno support, Arduino 2.2.1 does not.

## Z-Wave specification

### Identifier

This is not an official Z-Wave accredited device.  I've used a random
user-defined product ID as described here:

  https://z-uno.z-wave.me/Reference/ZUNO_SETUP_PRODUCT_ID/

Manufacturer ID:     0x0115
Product Type:        0x0210
Product ID:          0x7ac2

### Association groups

The root endpoint supports the 'lifeline' group, group 1.  The device
reports sensor information to this group, which should be configured
with the identity of the Z-Wave network controller to receive sensor
reports.  This is likely configured by default by the network controller.

### Configuration parameters

- Configuration v4:
  - Initial meter reading (64): Specifies initial meter reading at time of
    install (Default: 0)

  - Meter report period (65): Specifies period in seconds between meter
    reports (Default: 60)

  - Pulse debounce time (66): Specifies period in milliseconds in which to
    ignore multiple pulses (Default: 5000)

  - Reading increment per pulse (67): Specifies the reading incremement to
    apply per pulse (Default: 1)

### Command classes

- METER V6:
  - Gas consumption (Cubic meters)
- CONFIGURATION V4
- BATTERY V1
- WAKE UP V3

## Design factors

- Want to run low power and so selected EM4 sleep mode (which does not
  appear to work, I think it's in EM2 mode).
- Need to save reading values, hence these are written to EEPROM.
- EEPROM writes are slow, this cannot be done in the interrupt routine,
  or Z-Wave protocol routines.  Could trigger a watchdog reset.
- Pulse meters are read using a reed switch.  On opening and closing there
  are commonly multiple on/off events cause by some physical 'bounce' in
  the switch.  The de-bounce logic here turns these multiple triggers into
  a single pulse event.

## Known issues

This has not been 'production' tested, it works for me and my meter.
No warranty.

The low-power mode (EM4) does not appear to work with this code, the reason
for that is not known.

The Z-Uno reference says that the device automatically sends and receives
wake-up requests to the controller.  In practice I have found this not to be
the case, so have implemented a slightly 'hacky' invocation of wake-ups.
The code examines a place in the EEPROM where the Z-Uno core is known to
store its the wakeup interval parameter and then uses that to decide
when wake-ups are sent.  If you change the wakeup configuration interval using
your Z-Wave controller software, after that value has been received by the
device you need to hit the reset button on the Z-Uno to have it restart
and load the new value from EEPROM.

## Licence

    zuno-gas-meter-sensor - Z-Wave sensor for a pulse gas meter using Z-Uno
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
