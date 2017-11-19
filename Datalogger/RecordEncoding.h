#ifndef _RECORD_ENCODING_H
#define _RECORD_ENCODING_H

#include <cstdint>
#include <ctime>

#include "StatisticalCounter.h"
#include "Histogram.h"
#include "MovingAverage.h"
#include "can_buffer_timestamp.h"

#include "datalogger.pb.h"

DataloggerRecord timeToRecord(tm time, uint8_t sourceId, uint32_t timestampMs);
DataloggerRecord canMessageToRecord(Timestamped_CANMessage msg, uint8_t sourceId);
DataloggerRecord generateInfoRecord(const char* info, uint8_t sourceId, uint32_t timestampMs);

template<typename T, typename V>
DataloggerRecord generateStatsRecord(
    const StatisticalCounter<T, V>& counter,
    uint8_t sourceId, uint32_t timestampMs, uint32_t periodMs) {
  typename StatisticalCounter<T, V>::StatisticalResult stats = counter.read();

  DataloggerRecord rec = {
    timestampMs,
    periodMs,
    sourceId,
    DataloggerRecord_sensorReading_tag, {}
  };
  if (stats.numSamples == 0) {
    rec.payload.sensorReading = StatisticalAggregate {
      0,
      0,
      0,
      0,
      0
    };
  } else {
    rec.payload.sensorReading = StatisticalAggregate {
      stats.numSamples,
      (int32_t)stats.min,  // prevent signedness narrowing warnings
      (int32_t)stats.max,
      (int32_t)stats.avg,
      (uint32_t)stats.stdev
    };
  }

  return rec;
}

template <size_t NumDividers>
DataloggerRecord generateHistogramRecord(
    Histogram<NumDividers, int32_t, uint32_t>& histogram,
    uint8_t sourceId, uint32_t timestampMs, uint32_t periodMs) {
  const int32_t* dividers;
  const uint32_t* counts;

  size_t count = histogram.read(&dividers, &counts);

  DataloggerRecord rec = {
    timestampMs,
    periodMs,
    sourceId,
    DataloggerRecord_sensorDistribution_tag, {}
  };

  IntHistogram& histogramRec = rec.payload.sensorDistribution;
  static_assert(NumDividers <= sizeof(histogramRec.buckets) / sizeof(histogramRec.buckets[0]),
      "Insufficient buckets in proto message");

  histogramRec = IntHistogram {
    (pb_size_t)(count - 1), {},  // buckets
    (pb_size_t)(count), {}  // counts
  };
  for (uint8_t i=0; i<count-1; i++) {
    histogramRec.buckets[i] = dividers[i];
    histogramRec.counts[i] = counts[i];
  }
  histogramRec.counts[count - 1] = counts[count-1];

  return rec;
}


#endif
