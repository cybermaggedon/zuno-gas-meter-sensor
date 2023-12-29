
#include "EEPROM.h"

// Interrupt driven by an exteral button on pin 18.
#define PULSE_PIN 18

// Comment this out to turn off debug output on serial board
//#define UART Serial

// Trying to use EM4 sleep mode, doesn't appear to work
#define SLEEP_MODE SLEEP_MODE_EM4

// EEPROM addresses
#define READING_ADDRESS (0x0)
#define INIT_COPY_ADDRESS (0x4)

// Configuration value IDs
enum{
    INITIAL_METER_READING=64,
    METER_REPORT_PERIOD,
    // Ignore repeated pulses in this time window (milliseconds)
    DEBOUNCE_TIME
};

#define USE_EEPROM 1

#ifdef USE_EEPROM

DWORD reading;
DWORD reading_delta;
boolean reading_valid = false;
DWORD debounce_time;

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

  init_value = zunoLoadCFGParam(INITIAL_METER_READING);
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
  debounce_time = zunoLoadCFGParam(DEBOUNCE_TIME);

}

#else

DWORD reading;
DWORD reading_delta;
boolean reading_valid = false;
DWORD debounce_time;

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
  reading = zunoLoadCFGParam(INITIAL_METER_READING);
  reading_delta = 0;
  reading_valid = true;
  debounce_time = zunoLoadCFGParam(DEBOUNCE_TIME);
}

#endif

// Device uses sleep mode (EM4)
ZUNO_SETUP_SLEEPING_MODE(ZUNO_SLEEPING_MODE_SLEEPING);

// Debug mode
ZUNO_SETUP_DEBUG_MODE(DEBUG_ON);

ZUNO_SETUP_CONFIGPARAMETERS(
        ZUNO_CONFIG_PARAMETER_INFO("Initial meter reading", "Specifies initial meter reading at time of install", 0, 4000000, 0),
        ZUNO_CONFIG_PARAMETER_INFO("Meter report period", "Specifies period in seconds between meter reports", 15, 3600, 60),
        ZUNO_CONFIG_PARAMETER_INFO("Pulse debounce time", "Specifies period in milliseconds in which to ignore multiple pulses", 0, 10000, 5000)
);

ZUNO_SETUP_CHANNELS(
  ZUNO_METER(
    ZUNO_METER_TYPE_GAS,
    METER_RESET_ENABLE,
    ZUNO_METER_WATER_SCALE_METERS3,
    METER_SIZE_FOUR_BYTES,
    METER_PRECISION_THREE_DECIMALS,
    get_reading,
    reset_reading
  )
);

ZUNO_SETUP_PRODUCT_ID(0x7A, 0xC2);

// Loop count, using this to check the device goes into EM4 i.e. memory is wiped
uint32_t loop_count = 0;

// Count number of returns from EM4 state when calling wake_reason
uint32_t em4_count = 0;

// Returns the reason for a wakeup as a string.  As a side-effect, WUT_EM4 and EXT_EM4 increment the em4_count so
// can check whether EM4 state is achieved.
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

boolean woken_by_timer() {
    byte wake_up_reason = zunoGetWakeReason();
    return (wake_up_reason == ZUNO_WAKEUP_REASON_WUT_EM4) || (wake_up_reason == ZUNO_WAKEUP_REASON_WUT_EM2);
}

uint32_t moment = 1 << 31;

void remember() {
  moment = millis();
}

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

  // Ignore multiple pulses in the de-bounce window.  This deals with extra pulses in the reed-switch open/close
  if (since() < debounce_time) return;

  remember();

  inc_reading(DWORD(1));

}

// Core calls this on reset, or wake from EM4 state
void setup() {

#ifdef UART
  UART.begin(115200);
  UART.println();

  UART.println("Gas meter pulse sensor");
  UART.println("======================");

  // Set UART to output debug data
  UART.print("setup: called, ");
  UART.println(wake_reason());

  UART.print("Initial reading config = ");
  UART.println(zunoLoadCFGParam(INITIAL_METER_READING));
  UART.print("Meter report period = ");
  UART.println(zunoLoadCFGParam(METER_REPORT_PERIOD));
  UART.print("Pulse debounce time = ");
  UART.println(zunoLoadCFGParam(DEBOUNCE_TIME));

#endif


  init_reading();


  // Not needed, according to ref, pin 18 is already set up as an EM4 wake
  zunoEM4EnablePinWakeup(PULSE_PIN);
  pinMode(PULSE_PIN, INPUT_PULLUP);
  attachInterrupt(PULSE_PIN, interrupt, FALLING);

  // Wake up in a couple of seeconds and provide an initial report
  zunoSetCustomWUPTimer(2);
  zunoLockSleep();
  zunoSendDeviceToSleep(SLEEP_MODE);

}

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

  // Count of interrupts, should also zero when device goes into EM4 state
  UART.print(" - em4_count = ");
  UART.print(em4_count);

  // Time since last interrupt in millis.
  UART.print(" - since = ");
  UART.print(since());

  // Count of wakeups from EM4 state.  Not sure what this value will show, since
  // going into EM4 state zeroes this value, will show 0 or 1?
  UART.print(" - em4 = ");
  UART.print(em4_count);

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
  get_reading();
#endif

  // Prevented from sleeping?
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

    // Sleep for a bit
    zunoSetCustomWUPTimer(zunoLoadCFGParam(METER_REPORT_PERIOD));

    // Send device to EM4 sleep state when it is ready to sleep
    zunoSendDeviceToSleep(SLEEP_MODE);

  }

  zunoSendDeviceToSleep(SLEEP_MODE);

  // Increment loop count
  loop_count++; // Increment variable

  // Wait 1 second before leaving loop
  delay(1000);

}

