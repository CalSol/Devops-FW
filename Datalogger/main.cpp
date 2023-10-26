#include <cstdio>

#include <SDBlockDevice.h>
#include <FATFileSystem.h>

#define DEBUG_ENABLED
#include "debug.h"

#include "CanStruct.h"

#include "WDT.h"
#include "AnalogPeripherals.h"
#include "PCF2129.h"
#include "PCA9557.h"
#include "DataloggerFile.h"
#include "can_buffer_timestamp.h"
#include "RgbActivityLed.h"
#include "StatisticalCounter.h"
#include "Histogram.h"
#include "MovingAverage.h"
#include "DmaSerial.h"
#include "DigitalFilter.h"
#include "AnalogThresholdFilter.h"
#include "EInk.h"
#include "DefaultFonts.h"

#include "datalogger/datalogger.pb.h"
#include "RecordEncoding.h"

#include <locale>

/*
 * Local peripheral definitions
 */
const uint32_t CAN_FREQUENCY = 500000;
const uint32_t CAN_HEART_DATALOGGER = 0x049;  // heartbeat CAN ID
const uint32_t CAN_CORE_STATUS_DATALOGGER = 0x749;  // core status CAN ID

/*
 * Local peripheral definitions
 */
//
// System utilities
//
WDT Wdt(3 * 1000 * 1000);
Timer UsTimer;
LongTimer Timestamp(UsTimer);


//
// Comms interfaces
//
CAN Can(P1_8, P1_7, CAN_FREQUENCY);
CANTimestampedRxBuffer<128> CanBuffer(Can, Timestamp);

DigitalIn SdCd(P0_9);
DigitalFilter SdCdFilter(UsTimer, true, 250 * 1000, 25 * 1000);
SDBlockDevice Sd(P1_1, P0_10, P0_18, P0_7, 15000000);
FATFileSystem Fat("fs");
DataloggerProtoFile Datalogger(Fat);


//
// Sensors
//
SPI SpiAux(P0_2, P0_3, P0_1);
DigitalOut RtcCs(P0_29);
PCF2129 Rtc(SpiAux, RtcCs);

AnalogIn Adc12V(P0_6);
AnalogIn Adc5v(P0_5);
AnalogIn AdcSupercap(P0_4);
TempSensor AdcTempSensor;
BandgapReference AdcBandgap;

AnalogThresholdFilter MountDismountFilter(UsTimer, false, 3750, 3500, 25 * 1000, 250 * 1000);  // mV thresholds


//
// Timing constants
//
uint32_t kVoltageWritePeriod_us = 1000 * 1000;
TimerTicker VoltageSenseTicker(25 * 1000, UsTimer);
TimerTicker VoltageSaveTicker(kVoltageWritePeriod_us, UsTimer);
TimerTicker heartbeatTicker(1 * 1000 * 1000, UsTimer);
TimerTicker CanCheckTicker(1 * 1000 * 1000, UsTimer);
TimerTicker FileSyncTicker(5 * 60 * 1000 * 1000, UsTimer);
TimerTicker RemountTicker(250 * 1000, UsTimer);
TimerTicker UndismountTicker(10 * 1000 * 1000, UsTimer);

TimerTicker ChargerTicker(200 * 1000, UsTimer);
uint16_t swap16(uint16_t value)
{
    uint16_t result = 0;
    result |= (value & 0x00FF) << 8;
    result |= (value & 0xFF00) >> 8;
    return result;
}
#define CAN_CHARGER_CONTROL 0x1806E5F4
#define CAN_CHARGER_STATUS 0x18FF50E5
struct ChargerControlStruct {
  uint16_t voltage_be;
  uint16_t current_be;
  uint8_t control;
  uint8_t reserved1;
  uint8_t reserved2;
  uint8_t reserved3;
};
struct ChargerStatusStruct {
  uint16_t voltage_be;
  uint16_t current_be;
  uint8_t status;
  uint8_t reserved1;
  uint8_t reserved2;
  uint8_t reserved3;
};


TimerTicker EInkTicker(30 * 1000 * 1000, UsTimer);

//
// Debugging defs
//
DigitalIn EInkBusy(P0_30);
DigitalOut EInkReset(P0_0, 0);
DigitalOut EInkDc(P0_31, 1);
DigitalOut EInkCs(P1_0, 1);

//EInk152Graphics EInk(SpiAux, EInkCs, EInkDc, EInkReset, EInkBusy);

DigitalIn SdSwitch(P0_11, PullUp);
DigitalFilter Sw1Filter(UsTimer, true, 250*1000);

DigitalIn CanSwitch(P1_4, PullUp);
DigitalFilter SwResetFilter(UsTimer, true, 1000*1000);

DigitalOut SystemLedR(P1_6), SystemLedG(P0_17), SystemLedB(P1_11);
RgbActivityDigitalOut MainStatusLed(UsTimer, SystemLedR, SystemLedG, SystemLedB, false);
DigitalOut CanLedR(P0_15), CanLedG(P0_16), CanLedB(P0_14);
RgbActivityDigitalOut CanStatusLed(UsTimer, CanLedR, CanLedG, CanLedB, false);
DigitalOut SdLedR(P1_13), SdLedG(P0_13), SdLedB(P1_2);
RgbActivityDigitalOut SdStatusLed(UsTimer, SdLedR, SdLedG, SdLedB, false);

DmaSerial<1024> swdConsole(P0_8, NC, 115200);  // TODO increase size when have more RAM

//
// Datalogger Constants and defs
//
enum SourceId {
  kUnknown = 0,
  kSystem,
  kMainLoop,

  kCan = 10,

  kRtc = 20,

  kVoltageBandgap = 30,
  kVoltage12v,
  kVoltage5v,
  kVoltageSupercap,

  kTemperatureChip = 40
};


void writeHeader(DataloggerProtoFile& datalogger) {
  DataloggerRecord rec = {
    0,
    0,
    0,
    DataloggerRecord_info_tag, {}
  };
  rec.payload.info = InfoString {
    "Datalogger Rv B, " __DATE__ " " __TIME__ " " COMPILERNAME
    };
  datalogger.write(rec);

  rec.which_payload = DataloggerRecord_sourceDef_tag;

  rec.sourceId = kSystem;
  rec.payload.sourceDef = SourceDef {
    SourceDef_SourceType_UNKNOWN,
    "System"
  };
  datalogger.write(rec);

  rec.sourceId = kMainLoop;
  rec.payload.sourceDef = SourceDef {
    SourceDef_SourceType_UNKNOWN,
    "Main loop, ms"
  };
  datalogger.write(rec);

  rec.sourceId = kCan;
  rec.payload.sourceDef = SourceDef {
    SourceDef_SourceType_CAN,
    "CAN"
  };
  datalogger.write(rec);

  rec.sourceId = kRtc;
  rec.payload.sourceDef = SourceDef {
    SourceDef_SourceType_TIME,
    "PCF2129 RTC"
  };
  datalogger.write(rec);

  rec.sourceId = kVoltageBandgap;
  rec.payload.sourceDef = SourceDef {
    SourceDef_SourceType_VOLTAGE,
    "Vref+, bandgap, mV"
  };
  datalogger.write(rec);

  rec.sourceId = kVoltage12v;
  rec.payload.sourceDef = SourceDef {
    SourceDef_SourceType_VOLTAGE,
    "12v, Vref+, mV"
  };
  datalogger.write(rec);

  rec.sourceId = kVoltage5v;
  rec.payload.sourceDef = SourceDef {
    SourceDef_SourceType_VOLTAGE,
    "5v, Vref+, mV"
  };
  datalogger.write(rec);

  rec.sourceId = kVoltageSupercap;
  rec.payload.sourceDef = SourceDef {
    SourceDef_SourceType_VOLTAGE,
    "Supercap, Vref+, mV"
  };
  datalogger.write(rec);

  rec.sourceId = kTemperatureChip;
  rec.payload.sourceDef = SourceDef {
    SourceDef_SourceType_TEMPERATURE,
    "LPC1549 temperature, milliC"
  };
  datalogger.write(rec);
}

enum DataloggerState {
  kInactive,
  kUnsafeEject,
  kBadCard,
  kActive,
  kUserDismount,
};

// Fixed lenghh itoa. Returns false if variable was too large to fit.
static bool itoaFixed(char* dst, uint32_t val, size_t len) {
  char* cur = dst + len - 1;
  while (val > 0) {
    if (cur < dst) {  // number too big to fit
      return false;
    }
    uint8_t digit = val % 10;
    val /= 10;
    *cur = '0' + digit;
    cur--;
  }

  while (cur >= dst) {
    *cur = '0';
    cur--;
  }
  return true;
}

static void tmDateToStr(char* dst, const tm time) {
  itoaFixed(dst, time.tm_year + 1900, 4);
  itoaFixed(dst + 4, time.tm_mon + 1, 2);
  itoaFixed(dst + 6, time.tm_mday, 2);
  dst[8] = '\0';
}

static void tmMinToStr(char* dst, const tm time) {
  itoaFixed(dst, time.tm_hour, 2);
  itoaFixed(dst + 2, time.tm_min, 2);
  dst[4] = '\0';
}

bool mountSd(bool wasWdtReset, uint32_t sdInsertedTimestamp,
    SDBlockDevice& sd, FATFileSystem &fat, DataloggerProtoFile& datalogger) {
  tm time;
  uint32_t rtcTimestamp = Timestamp.read_ms();
  bool timeGood = Rtc.gettime(&time);

  debugInfo("RTC %s %04d-%02d-%02d %02d:%02d:%02d", timeGood ? "OK" : "Stopped",
      time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
      time.tm_hour, time.tm_min, time.tm_sec);

  char dirname[9], filename[9];
  tmDateToStr(dirname, time);
  tmMinToStr(filename, time);

  bool openSuccess = true;
  int sdInitResult = sd.init();
  if (sdInitResult) {
    debugInfo("SD init failed: %i", sdInitResult);
    sd.deinit();
    openSuccess = false;
  }
  if (openSuccess) {
    int fatMountResult = fat.mount(&sd);
    if (fatMountResult) {
      debugInfo("FAT mount failed: %i", fatMountResult);
      fat.unmount();
      sd.deinit();
      openSuccess = false;
    }
  }
  if (openSuccess) {
    bool newfileResult = datalogger.newFile(dirname, filename);
    if (!newfileResult) {
      debugInfo("New file failed: %i", newfileResult);
      openSuccess = false;
      fat.unmount();
      sd.deinit();
    }
  }

  uint32_t initTimestamp = Timestamp.read_ms();

  if (openSuccess) {
    writeHeader(datalogger);

    if (wasWdtReset) {
      datalogger.write(generateInfoRecord("WDT Reset", kSystem, 0));
    }

    datalogger.write(generateInfoRecord("SD inserted", kSystem, sdInsertedTimestamp));

    datalogger.write(timeToRecord(time, kRtc, rtcTimestamp));
    if (!timeGood) {
      datalogger.write(generateInfoRecord("RTC stopped", kRtc, rtcTimestamp));
    }

    datalogger.write(generateInfoRecord("FS mounted", kSystem, initTimestamp));

    datalogger.syncFile();

    return true;
  } else {
    return false;
  }
}

int main() {
  // disable the reset pin, to avoid accidental resets from EMI
  uint32_t* PINENABLE = (uint32_t*)0x400381C4;
  *PINENABLE = *PINENABLE | (1 << 21);

  bool wasWdtReset = Wdt.causedReset();

  swdConsole.baud(115200);

  debugInfo("\r\n\r\n\r\n");
  debugInfo("Datalogger 2");
  debugInfo("Built " __DATE__ " " __TIME__ " " COMPILERNAME);
  if (wasWdtReset) {
    debugWarn("WDT Reset");
  }

  // For manually resetting the RTC
//  while (SdSwitch);
//  Rtc.settime({30, 23, 14,  05, 07 - 1, 2022 - 1900});
 //           ss  mm  hh   dd  mm      yyyy


  tm time;
  bool timeGood = Rtc.gettime(&time);
  debugInfo("RTC %s %04d-%02d-%02d %02d:%02d:%02d", timeGood ? "OK" : "Stopped",
      time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
      time.tm_hour, time.tm_min, time.tm_sec);

  uint16_t numMountAttempts = 0;
  uint32_t sdInsertedTimestamp;

  StatisticalCounter<uint16_t, uint64_t> vrefpStats;
  StatisticalCounter<uint16_t, uint64_t> rail12vStats;
  StatisticalCounter<uint16_t, uint64_t> rail5vStats;
  StatisticalCounter<uint16_t, uint64_t> railSupercapStats;
  StatisticalCounter<int32_t, int64_t> tempStats;
  StatisticalCounter<uint32_t, uint64_t> loopStats;

  // Histogram buckets in us
  Histogram<8, int32_t, uint32_t> loopDistribution({33, 100, 333, 1000, 3333, 10000, 33333, 100000});

  DataloggerState state = kInactive;

  UsTimer.start();
  Wdt.enable();
//  EInk.init();

//  EInk.text(0, 0, "DATALOGGER", Font5x7, 255);

  char strBuf[128] = __DATE__ " " __TIME__;
  for (char* buildStrPtr = strBuf; *buildStrPtr != '\0'; buildStrPtr++) {
    *buildStrPtr = std::toupper(*buildStrPtr);
  }
//  EInk.text(0, 8, strBuf, Font3x5, 255);

  sprintf(strBuf, "ON %02d %02d %02d %02d:%02d:%02d  %s",
      (time.tm_year + 1900) % 100, time.tm_mon + 1, time.tm_mday,
      time.tm_hour, time.tm_min, time.tm_sec,
      timeGood ? "OK" : "STP");
//  EInk.text(0, 16, strBuf, Font5x7, 255);

//  EInk.update();


  while (true) {
    uint32_t loopStartTime = Timestamp.read_short_us();

    /** Feed the watchdog timer so the board doesn't reset */
    Wdt.feed();
    Timestamp.update();

    // Control reset switch in software to allow aggressive filtering
    if (SwResetFilter.update(CanSwitch) == DigitalFilter::kFalling) {
      // enable the reset pin to allow holding the system in reset
      *PINENABLE = *PINENABLE & ~(1 << 21);
      NVIC_SystemReset();
    }
    bool sdSwitchPressed = Sw1Filter.update(SdSwitch) == DigitalFilter::kFalling;
    SdCdFilter.update(SdCd);

    if (state == kInactive || state == kUnsafeEject) {
      if (!SdCdFilter.read()
          && MountDismountFilter.read()) {  // card inserted
        sdInsertedTimestamp = Timestamp.read_ms();

        if (mountSd(wasWdtReset, sdInsertedTimestamp, Sd, Fat, Datalogger)) {
          FileSyncTicker.reset();

          state = kActive;
          debugInfo("FSM -> kActive: successful mount");
          MainStatusLed.setIdle(RgbActivity::kOff);
          SdStatusLed.setIdle(RgbActivity::kGreen);
        } else {
          RemountTicker.reset();
          numMountAttempts = 0;

          state = kBadCard;
          debugInfo("FSM -> kBadCard: unsuccessful mount");
          MainStatusLed.setIdle(RgbActivity::kOff);
          SdStatusLed.setIdle(RgbActivity::kRed);
        }
      } else if (!MountDismountFilter.read()) {  // voltage bad
        MainStatusLed.setIdle(RgbActivity::kPurple);
      } else if (MountDismountFilter.read()) {  // voltage good, no SD card
        MainStatusLed.setIdle(RgbActivity::kOff);
      }
    } else if (state == kBadCard) {
      if (SdCdFilter.read() || !MountDismountFilter.read()) {  // disk ejected
        state = kInactive;
        debugInfo("FSM -> kInactive: ejected / undervoltage");
        MainStatusLed.setIdle(RgbActivity::kOff);
        SdStatusLed.setIdle(RgbActivity::kOff);
      } else if (RemountTicker.checkExpired()) {
        numMountAttempts++;

        if (mountSd(wasWdtReset, sdInsertedTimestamp, Sd, Fat, Datalogger)) {
          FileSyncTicker.reset();

          char remountInfoBuffer[128];
          sprintf(remountInfoBuffer, "%u unsuccessful mount attempts", numMountAttempts);
          Datalogger.write(generateInfoRecord(remountInfoBuffer, kSystem, Timestamp.read_ms()));

          state = kActive;
          debugInfo("FSM -> kActive: successful mount (after %u unsuccessful attempts)", numMountAttempts);
          MainStatusLed.setIdle(RgbActivity::kOff);
          SdStatusLed.setIdle(RgbActivity::kGreen);
        }
      }
    } else if (state == kActive) {
      if (SdCdFilter.read()) {  // unsafe dismount
        Datalogger.closeFile();

        state = kUnsafeEject;
        debugInfo("FSM -> kUnsafeEject: unsafe dismount");
        MainStatusLed.setIdle(RgbActivity::kOff);
        SdStatusLed.setIdle(RgbActivity::kRed);
      } else if (sdSwitchPressed) {  // user-requested dismount
        Datalogger.write(generateInfoRecord("User dismount", kSystem, Timestamp.read_ms()));
        Datalogger.closeFile();
        Fat.unmount();
        Sd.deinit();

        UndismountTicker.reset();

        state = kUserDismount;
        debugInfo("FSM -> kUserDismount: switch pressed");
        MainStatusLed.setIdle(RgbActivity::kBlue);
        SdStatusLed.setIdle(RgbActivity::kBlue);
      } else if (!MountDismountFilter.read()) {  // undervoltage dismount
        Datalogger.write(generateInfoRecord("Undervoltage dismount", kSystem, Timestamp.read_ms()));
        Datalogger.closeFile();
        Fat.unmount();
        Sd.deinit();

        state = kInactive;
        debugInfo("FSM -> kInactive: undervoltage");
        MainStatusLed.setIdle(RgbActivity::kPurple);
        SdStatusLed.setIdle(RgbActivity::kBlue);
      }
    } else if (state == kUserDismount) {
      if (SdCdFilter.read()) {  // disk ejected
        state = kInactive;
        debugInfo("FSM -> kInactive: ejected");
        MainStatusLed.setIdle(RgbActivity::kOff);
        SdStatusLed.setIdle(RgbActivity::kOff);
      } else if (UndismountTicker.checkExpired()) {
        state = kInactive;
        debugInfo("FSM -> kInactive: dismount timeout");
        MainStatusLed.setIdle(RgbActivity::kOff);
        SdStatusLed.setIdle(RgbActivity::kOff);
      }
    } else {  // fallback, should never happen!
      state = kInactive;
      debugWarn("FSM -> kInactive: fallback condition");
    }

    if (state == kUnsafeEject || state == kBadCard) {
      MainStatusLed.pulse(RgbActivity::kRed);
    }

    if (state == kActive && FileSyncTicker.checkExpired()) {
      Datalogger.syncFile();
      SdStatusLed.pulse(RgbActivity::kWhite);
    }

    if (VoltageSenseTicker.checkExpired()) {
      // Sample everything close together since the bandgap is used as a reference
      uint16_t bandgapSample = AdcBandgap.read_u16() >> 4;
      uint16_t rail12vSample = Adc12V.read_u16() >> 4;
      uint16_t rail5vSample = Adc5v.read_u16() >> 4;
      uint16_t railSupercapSample = AdcSupercap.read_u16() >> 4;
      uint16_t tempVoltageSample = AdcTempSensor.read_u16() >> 4;

      // Convert everything to mV
      uint16_t vrefpSample = 905 * 4095 / bandgapSample;

      // The precision 3v reference is more accurate than the internal bandgap,
      // so log measurements according to that.
      rail12vSample = (uint32_t)rail12vSample * vrefpSample * (47+15) / 15 / 4095;
      rail5vSample = (uint32_t)rail5vSample * vrefpSample * (10+15) / 15 / 4095;
      railSupercapSample = (uint32_t)railSupercapSample * vrefpSample * (10+15) / 15 / 4095;
      tempVoltageSample = (uint32_t)tempVoltageSample * vrefpSample / 4095;  // convert to mV
      int32_t tempSample = (577 - tempVoltageSample) * 1000 * 100 / 229;  // -2.29mV/C, 577.3mV @ 0C

      vrefpStats.addSample(vrefpSample);
      rail12vStats.addSample(rail12vSample);
      rail5vStats.addSample(rail5vSample);
      railSupercapStats.addSample(railSupercapSample);
      tempStats.addSample(tempSample);

      MountDismountFilter.update(railSupercapSample);
    }

//    if (EInkTicker.checkExpired()) {
//      EInk.rectFilled(0, 32, 152, 32, 0);
//
//      uint32_t timestampMs = Timestamp.read_ms();
//      sprintf(strBuf, "UP %02dH  %02dM  %02dS",
//          timestampMs / 1000 / 60 / 60, timestampMs / 1000 / 60 % 60, timestampMs / 1000 % 60);
//      EInk.text(0, 32, strBuf, Font5x7, 255);
//
//      sprintf(strBuf, "+V:  %d %03d    5V:  %d %03d",
//          vrefpStats.read().avg / 1000, vrefpStats.read().avg % 1000,
//          rail5vStats.read().avg / 1000, rail5vStats.read().avg % 1000);
//      EInk.text(0, 40, strBuf, Font5x7, 255);
//
//      EInk.update();
//    }

    if (VoltageSaveTicker.checkExpired()) {
      uint32_t thisTimestamp = Timestamp.read_ms();

      if (state == kActive) {
        Datalogger.write(generateStatsRecord<uint16_t, uint64_t>(
            vrefpStats, kVoltageBandgap, thisTimestamp, kVoltageWritePeriod_us / 1000));

        Datalogger.write(generateStatsRecord<uint16_t, uint64_t>(
            rail12vStats, kVoltage12v, thisTimestamp, kVoltageWritePeriod_us / 1000));
        Datalogger.write(generateStatsRecord<uint16_t, uint64_t>(
            rail5vStats, kVoltage5v, thisTimestamp, kVoltageWritePeriod_us / 1000));
        Datalogger.write(generateStatsRecord<uint16_t, uint64_t>(
            railSupercapStats, kVoltageSupercap, thisTimestamp, kVoltageWritePeriod_us / 1000));

        Datalogger.write(generateStatsRecord<int32_t, int64_t>(
            tempStats, kTemperatureChip, thisTimestamp, kVoltageWritePeriod_us / 1000));

        Datalogger.write(generateStatsRecord<uint32_t, uint64_t>(
            loopStats, kMainLoop, thisTimestamp, kVoltageWritePeriod_us / 1000));

        Datalogger.write(generateHistogramRecord<8>(
            loopDistribution, kMainLoop, thisTimestamp, kVoltageWritePeriod_us / 1000));

        SdStatusLed.pulse(RgbActivity::kYellow);
      }

      debugInfo("ADCs: Vrp=%5dmv,  12v=%5dmv,  Sv=%5dmv,  Vsc=%5dmv, T=%2ldmc",
          vrefpStats.read().avg, rail12vStats.read().avg, rail5vStats.read().avg, railSupercapStats.read().avg,
          tempStats.read().avg);

      rail12vStats.reset();
      rail5vStats.reset();
      railSupercapStats.reset();
      vrefpStats.reset();
      tempStats.reset();

      loopStats.reset();
      loopDistribution.reset();
    }

    if (heartbeatTicker.checkExpired()) {
      CanBuffer.write(makeMessage(CAN_HEART_DATALOGGER, Timestamp.read_short_us()));

      CoreStatus status;
      status.vref_bandgap = 905 * 4096 / (AdcBandgap.read_u16() >> 4);
      status.temperature = (AdcTempSensor.read_u16() >> 4) * status.vref_bandgap / 4095;  // convert to mV
      status.temperature = (577 - (uint32_t)status.temperature) * 100 * 100 / 229;
      CanBuffer.write(makeMessage(CAN_CORE_STATUS_DATALOGGER, status));

      CanStatusLed.pulse(RgbActivity::kCyan);
      if (state == kInactive) {
        MainStatusLed.pulse(RgbActivity::kRed);
      } else if (state == kActive) {
        MainStatusLed.pulse(RgbActivity::kGreen);
      }
    }

    if (CanCheckTicker.checkExpired()) {
      if (LPC_C_CAN0->CANCNTL & (1 << 0)) {
        LPC_C_CAN0->CANCNTL &= ~(1 << 0);
        CanStatusLed.pulse(RgbActivity::kRed);

        if (state == kActive) {
          Datalogger.write(generateInfoRecord("CAN Reset", kCan, Timestamp.read_ms()));
          SdStatusLed.pulse(RgbActivity::kCyan);
        }
      }
    }

    Timestamped_CANMessage msg;
    while (CanBuffer.read(msg)) {
      if (msg.isError) {
        CanStatusLed.pulse(RgbActivity::kRed);
      } else {
        CanStatusLed.pulse(RgbActivity::kGreen);
      }
      if (state == kActive) {
        Datalogger.write(canMessageToRecord(msg, kCan));
        SdStatusLed.pulse(RgbActivity::kYellow);
      }
    }


      if(ChargerTicker.checkExpired()) {
        ChargerControlStruct charger_control;

        charger_control.control = 0;
        charger_control.voltage_be = swap16(115*10 + 7);
        charger_control.current_be = swap16(10*10);
        CanBuffer.write(CANMessage(CAN_CHARGER_CONTROL, reinterpret_cast<const char*>(&charger_control), 8, CANData, CANExtended));

        MainStatusLed.pulse(RgbActivity::kCyan);
      }

    MainStatusLed.update();
    CanStatusLed.update();
    SdStatusLed.update();

    uint32_t loopTime = Timestamp.read_short_us() - loopStartTime;
    loopDistribution.addSample(loopTime);
    loopStats.addSample(loopTime);
  }
}

