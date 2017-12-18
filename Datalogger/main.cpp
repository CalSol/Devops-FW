#include <cstdio>

#include <can_id.h>
#include <can_struct.h>

#define DEBUG_ENABLED
#include "debug.h"

#include "WDT.h"
#include "AnalogPeripherals.h"
#include "PCF2129.h"
#include "PCA9557.h"
#include "SDFileSystem.h"
#include "DataloggerFile.h"
#include "can_buffer_timestamp.h"
#include "RgbActivityLed.h"
#include "StatisticalCounter.h"
#include "Histogram.h"
#include "MovingAverage.h"
#include "DmaSerial.h"

#include "datalogger.pb.h"
#include "RecordEncoding.h"

/*
 * Local peripheral definitions
 */
// System utilities
WDT wdt(5000000);  // 5s WDT
Timer usTimer;
LongTimer timestamp(usTimer);
DigitalIn BOD(P0_0);  // Brown-out detect

// Comms interfaces
CAN can(P0_7, P0_9);
CANTimestampedRxBuffer<128> canBuffer(can, timestamp);

SPI spi0(P0_24, P0_26, P0_25);
I2C i2c(P0_23, P0_22);

DigitalIn SD_CD(P0_15);
SDFileSystem sd(P0_13, P0_11, P0_12, P0_14, "sd");
DataloggerProtoFile datalogger(sd);

// Sensors
InterruptIn RTCINT(P0_18);  // RTC interrupt
DigitalOut RTC_CS(P0_17);
PCF2129 RTC(spi0, RTC_CS);

AnalogIn ADC12V(P0_1);
AnalogIn ADC3V3V(P0_10);
AnalogIn ADCSUPERCAPV(P0_2);
TempSensor AdcTempSensor;
BandgapReference AdcBandgap;

// User interface / debugging
DigitalIn SW1(P0_4, PullUp);

DigitalOut LEDR(P0_3);
DigitalOut LEDG(P0_5);
DigitalOut LEDB(P0_6);

uint8_t statusLedBits = 0xFF;
PCA9557 STATUSLEDS(i2c, 0x30);

RgbActivityBitvector MainStatusLed(usTimer, &statusLedBits,
    1 << 2, 1 << 3, 1 << 4, 0);
RgbActivityDigitalOut CanStatusLed(usTimer, LEDR, LEDG, LEDB, 1);
RgbActivityBitvector SdStatusLed(usTimer, &statusLedBits,
    1 << 5, 1 << 6, 1 << 7, 0);

// RawSerial uart(P0_28, P0_29);  // currently unused
DmaSerial<1024> swdConsole(P0_8, NC, 115200);

const uint32_t kStatusBlinkPeriod_us = 1000 * 1000;
const uint32_t kCanCheckPeriod_us = 1000 * 1000;
const uint32_t kVoltageSensePeriod_us = 100 * 1000;
const uint32_t kVoltageWritePeriod_us = 1000 * 1000;
const uint32_t kHeartbeatPeriod_us = 1000 * 1000;
const uint32_t kMpptStatusPeriod_us = 200 * 1000;
const uint32_t kFileSyncPeriod_us = 5000 * 1000;

const uint32_t kRemountPeriod_us = 250 * 1000;

const uint16_t kMountThreshold_mV = 3100;
const uint16_t kDismountThreshold_mV = 2850;

enum SourceId {
  kUnknown = 0,
  kSystem,
  kMainLoop,

  kCan = 10,

  kRtc = 20,

  kVoltageBandgap = 30,
  kVoltage12v,
  kVoltage3v3,
  kVoltageSupercap,

  kTemperatureChip = 40
};

#ifdef __GNUC__
  #define STRINGIFY(X) #X
  #define TOSTRING(x) STRINGIFY(x)
  #define COMPILERNAME "GNUC " TOSTRING(__GNUC__) "." TOSTRING(__GNUC_MINOR__) "." TOSTRING(__GNUC_PATCHLEVEL__)
#else
  #error "Unknown compiler, add definition above"
#endif

void writeHeader(DataloggerProtoFile& datalogger) {
  DataloggerRecord rec = {
    0,
    0,
    0,
    DataloggerRecord_info_tag, {}
  };
  rec.payload.info = InfoString {
    "Datalogger Rv B, " GITVERSION ", " __DATE__ " " __TIME__ " " COMPILERNAME
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

  rec.sourceId = kVoltage3v3;
  rec.payload.sourceDef = SourceDef {
    SourceDef_SourceType_VOLTAGE,
    "3.3v, Vref+, mV"
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
    SDFileSystem& sd, DataloggerProtoFile& datalogger) {
  tm time;
  uint32_t rtcTimestamp = timestamp.read_ms();
  bool timeGood = RTC.gettime(&time);

  debugInfo("RTC %s %04d-%02d-%02d %02d:%02d:%02d", timeGood ? "OK" : "Stopped",
      time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
      time.tm_hour, time.tm_min, time.tm_sec);

  char dirname[9], filename[9];
  tmDateToStr(dirname, time);
  tmMinToStr(filename, time);

  sd.set_transfer_sck(15000000);
  bool openSuccess = !sd.disk_initialize() && datalogger.newFile(dirname, filename);
  uint32_t initTimestamp = timestamp.read_ms();

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

    return true;
  } else {
    return false;
  }
}

int main() {
  bool wasWdtReset = wdt.causedReset();

  swdConsole.baud(115200);

  debugInfo("\r\n\r\n\r\n");
  debugInfo("Datalogger Rv B, " GITVERSION);
  debugInfo("Built " __DATE__ " " __TIME__ " " COMPILERNAME);
  if (wasWdtReset) {
    debugWarn("WDT Reset");
  }

  // For manually resetting the RTC
//  while (SW1);
//  RTC.settime({00, 33, 07,  02, 12 - 1, 2017 - 1900});
//  //           ss  mm  hh   dd  mm      yyyy

  tm time;
  bool timeGood = RTC.gettime(&time);
  debugInfo("RTC %s %04d-%02d-%02d %02d:%02d:%02d", timeGood ? "OK" : "Stopped",
      time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
      time.tm_hour, time.tm_min, time.tm_sec);

  can.frequency(CAN_FREQUENCY);

  TimerTicker statusIndicatorTicker(kHeartbeatPeriod_us, usTimer);
  TimerTicker voltageSenseTicker(kVoltageSensePeriod_us, usTimer);
  TimerTicker voltageSaveTicker(kVoltageWritePeriod_us, usTimer);
  TimerTicker heartbeatTicker(kHeartbeatPeriod_us, usTimer);
  TimerTicker mpptStatusTicker(kMpptStatusPeriod_us, usTimer);
  TimerTicker canCheckTicker(kCanCheckPeriod_us, usTimer);
  TimerTicker fileSyncTicker(kFileSyncPeriod_us, usTimer);

  TimerTicker remountTicker(kRemountPeriod_us, usTimer);
  uint32_t numMountAttempts = 0;
  uint32_t sdInsertedTimestamp;

  StatisticalCounter<uint16_t, uint64_t> vrefpStats;
  StatisticalCounter<uint16_t, uint64_t> rail12vStats;
  StatisticalCounter<uint16_t, uint64_t> rail3v3Stats;
  StatisticalCounter<uint16_t, uint64_t> railSupercapStats;
  StatisticalCounter<int32_t, int64_t> tempStats;
  StatisticalCounter<uint32_t, uint64_t> loopStats;

  // Histogram buckets in us
  Histogram<8, int32_t, uint32_t> loopDistribution({33, 100, 333, 1000, 3333, 10000, 33333, 100000});

  MovingAverage<uint16_t, uint32_t, 8> rail3v3Avg;

  DataloggerState state = kInactive;

  STATUSLEDS.setDirection(0x00);

  usTimer.start();
  wdt.enable();

  while (true) {
    uint32_t loopStartTime = timestamp.read_short_us();

    /** Feed the watchdog timer so the board doesn't reset */
    wdt.feed();
    timestamp.update();

    if (state == kInactive || state == kUnsafeEject) {
      if (!SD_CD
          && rail3v3Avg.read() > kMountThreshold_mV) {  // card inserted
        sdInsertedTimestamp = timestamp.read_ms();

        if (mountSd(wasWdtReset, sdInsertedTimestamp, sd, datalogger)) {
          fileSyncTicker.reset();

          state = kActive;
          debugInfo("FSM -> kActive: successful mount");
          MainStatusLed.setIdle(RgbActivity::kOff);
          SdStatusLed.setIdle(RgbActivity::kGreen);
        } else {
          remountTicker.reset();
          numMountAttempts = 0;

          state = kBadCard;
          debugInfo("FSM -> kBadCard: unsuccessful mount");
          MainStatusLed.setIdle(RgbActivity::kOff);
          SdStatusLed.setIdle(RgbActivity::kRed);
        }
      } else if (rail3v3Avg.read() <= kMountThreshold_mV) {
        MainStatusLed.setIdle(RgbActivity::kPurple);
      } else if (rail3v3Avg.read() > kMountThreshold_mV) {
        MainStatusLed.setIdle(RgbActivity::kOff);
      }
    } else if (state == kBadCard) {
      if (SD_CD || rail3v3Avg.read() <= kMountThreshold_mV) {  // disk ejected
        state = kInactive;
        debugInfo("FSM -> kInactive: ejected / undervoltage");
        MainStatusLed.setIdle(RgbActivity::kOff);
        SdStatusLed.setIdle(RgbActivity::kOff);
      } else if (remountTicker.checkExpired()) {
        numMountAttempts++;

        if (mountSd(wasWdtReset, sdInsertedTimestamp, sd, datalogger)) {
          fileSyncTicker.reset();

          char remountInfoBuffer[128];
          sprintf(remountInfoBuffer, "%u unsuccessful mount attempts", numMountAttempts);
          datalogger.write(generateInfoRecord(remountInfoBuffer, kSystem, timestamp.read_ms()));

          state = kActive;
          debugInfo("FSM -> kActive: successful mount (after %u unsuccessful attempts)", numMountAttempts);
          MainStatusLed.setIdle(RgbActivity::kOff);
          SdStatusLed.setIdle(RgbActivity::kGreen);
        }
      }
    } else if (state == kActive) {
      if (SD_CD) {  // unsafe dismount
        state = kUnsafeEject;
        debugInfo("FSM -> kUnsafeEject: unsafe dismount");
        MainStatusLed.setIdle(RgbActivity::kOff);
        SdStatusLed.setIdle(RgbActivity::kRed);
      } else if (!SW1) {  // user-requested dismount
        datalogger.write(generateInfoRecord("User dismount", kSystem, timestamp.read_ms()));
        datalogger.closeFile();

        state = kUserDismount;
        debugInfo("FSM -> kUserDismount: switch pressed");
        MainStatusLed.setIdle(RgbActivity::kBlue);
        SdStatusLed.setIdle(RgbActivity::kBlue);
      } else if (rail3v3Avg.read() < kDismountThreshold_mV) {  // undervoltage dismount
        datalogger.write(generateInfoRecord("Undervoltage dismount", kSystem, timestamp.read_ms()));
        datalogger.closeFile();

        state = kInactive;
        debugInfo("FSM -> kInactive: undervoltage");
        MainStatusLed.setIdle(RgbActivity::kPurple);
        SdStatusLed.setIdle(RgbActivity::kBlue);
      }
    } else if (state == kUserDismount) {
      if (SD_CD) {  // disk ejected
        state = kInactive;
        debugInfo("FSM -> kInactive: ejected");
        MainStatusLed.setIdle(RgbActivity::kOff);
        SdStatusLed.setIdle(RgbActivity::kOff);
      }
    } else {  // fallback, should never happen!
      state = kInactive;
      debugWarn("FSM -> kInactive: fallback condition");
    }

    if (statusIndicatorTicker.checkExpired()) {
      if (state == kInactive) {
        MainStatusLed.pulse(RgbActivity::kRed);
      } else if (state == kActive) {
        MainStatusLed.pulse(RgbActivity::kGreen);
      }
    }
    if (state == kUnsafeEject || state == kBadCard) {
      MainStatusLed.pulse(RgbActivity::kRed);
    }

    if (state == kActive && fileSyncTicker.checkExpired()) {
      datalogger.syncFile();
      SdStatusLed.pulse(RgbActivity::kWhite);
    }

    if (voltageSenseTicker.checkExpired()) {
      // Sample everything close together since the bandgap is used as a reference
      uint16_t bandgapSample = AdcBandgap.read_u16() >> 4;
      uint16_t rail12vSample = ADC12V.read_u16() >> 4;
      uint16_t rail3v3Sample = ADC3V3V.read_u16() >> 4;
      uint16_t railSupercapSample = ADCSUPERCAPV.read_u16() >> 4;
      uint16_t tempVoltageSample = AdcTempSensor.read_u16() >> 4;

      // Convert everything to mV
      uint16_t vrefpSample = 905 * 4095 / bandgapSample;

      // The 3.3 rail will be used to control dismount, so must be accurate even
      // as Vref+ dips and is referenced to the internal 0.9v bandgap
      uint16_t rail3v3BandgapSample = (uint32_t)rail3v3Sample * vrefpSample * (1+1) / 1 / 4095;
      rail3v3Avg.update(rail3v3BandgapSample);

      // The precision 3v reference is more accurate than the internal bandgap,
      // so log measurements according to that.
      rail12vSample = (uint32_t)rail12vSample * 3000 * (15+82) / 15 / 4095;
      rail3v3Sample = (uint32_t)rail3v3Sample * 3000 * (1+1) / 1 / 4095;
      railSupercapSample = (uint32_t)railSupercapSample * 3000 * (1+1) / 1 / 4095;
      tempVoltageSample = (uint32_t)tempVoltageSample * 3000 / 4095;  // convert to mV
      int32_t tempSample = (577 - tempVoltageSample) * 1000 * 100 / 229;  // -2.29mV/C, 577.3mV @ 0C

      vrefpStats.addSample(vrefpSample);
      rail12vStats.addSample(rail12vSample);
      rail3v3Stats.addSample(rail3v3Sample);
      railSupercapStats.addSample(railSupercapSample);
      tempStats.addSample(tempSample);
    }

    if (voltageSaveTicker.checkExpired()) {
      uint32_t thisTimestamp = timestamp.read_ms();

      if (state == kActive) {
        datalogger.write(generateStatsRecord<uint16_t, uint64_t>(
            vrefpStats, kVoltageBandgap, thisTimestamp, kVoltageWritePeriod_us / 1000));

        datalogger.write(generateStatsRecord<uint16_t, uint64_t>(
            rail12vStats, kVoltage12v, thisTimestamp, kVoltageWritePeriod_us / 1000));
        datalogger.write(generateStatsRecord<uint16_t, uint64_t>(
            rail3v3Stats, kVoltage3v3, thisTimestamp, kVoltageWritePeriod_us / 1000));
        datalogger.write(generateStatsRecord<uint16_t, uint64_t>(
            railSupercapStats, kVoltageSupercap, thisTimestamp, kVoltageWritePeriod_us / 1000));

        datalogger.write(generateStatsRecord<int32_t, int64_t>(
            tempStats, kTemperatureChip, thisTimestamp, kVoltageWritePeriod_us / 1000));

        datalogger.write(generateStatsRecord<uint32_t, uint64_t>(
            loopStats, kMainLoop, thisTimestamp, kVoltageWritePeriod_us / 1000));

        datalogger.write(generateHistogramRecord<8>(
            loopDistribution, kMainLoop, thisTimestamp, kVoltageWritePeriod_us / 1000));

        SdStatusLed.pulse(RgbActivity::kYellow);
      }

      rail12vStats.reset();
      rail3v3Stats.reset();
      railSupercapStats.reset();
      vrefpStats.reset();
      tempStats.reset();

      loopStats.reset();
      loopDistribution.reset();
    }

    if (heartbeatTicker.checkExpired()) {
      canBuffer.write(makeMessage(CAN_HEART_DATALOGGER, timestamp.read_short_us()));
    }
    if (mpptStatusTicker.checkExpired()) {
      CANMessage msg(CAN_FRONT_RIGHT_MPPT_STATUS, NULL, 0, CANRemote);
      canBuffer.write(msg);
      msg.id = CAN_FRONT_LEFT_MPPT_STATUS;
      canBuffer.write(msg);
      msg.id = CAN_BACK_RIGHT_MPPT_STATUS;
      canBuffer.write(msg);
      msg.id = CAN_BACK_LEFT_MPPT_STATUS;
      canBuffer.write(msg);
    }

    if (canCheckTicker.checkExpired()) {
      if (LPC_C_CAN0->CANCNTL & (1 << 0)) {
        LPC_C_CAN0->CANCNTL &= ~(1 << 0);
        CanStatusLed.pulse(RgbActivity::kBlue);

        if (state == kActive) {
          datalogger.write(generateInfoRecord("CAN Reset", kCan, timestamp.read_ms()));
          SdStatusLed.pulse(RgbActivity::kYellow);
        }
      }
    }

    Timestamped_CANMessage msg;
    while (canBuffer.read(msg)) {
      if (msg.isError) {
        CanStatusLed.pulse(RgbActivity::kRed);
      } else {
        CanStatusLed.pulse(RgbActivity::kGreen);
      }
      if (state == kActive) {
        datalogger.write(canMessageToRecord(msg, kCan));
        SdStatusLed.pulse(RgbActivity::kYellow);
      }
    }


    bool i2cLedsUpdated = false;
    i2cLedsUpdated |= MainStatusLed.update();
    i2cLedsUpdated |= CanStatusLed.update();
    i2cLedsUpdated |= SdStatusLed.update();
    if (i2cLedsUpdated) {
      STATUSLEDS.writeOutputs(statusLedBits);
    }

    uint32_t loopTime = timestamp.read_short_us() - loopStartTime;
    loopDistribution.addSample(loopTime);
    loopStats.addSample(loopTime);
  }
}

