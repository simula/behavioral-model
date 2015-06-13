/* Copyright 2013-present Barefoot Networks, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Antonin Bas (antonin@barefootnetworks.com)
 *
 */

#ifndef _BM_METERS_H_
#define _BM_METERS_H_

#include <vector>
#include <mutex>
#include <memory>

#include "named_p4object.h"
#include "packet.h"

/* I initially implemented this with template values: meter type and rate
   count. I thought it would potentially speed up operations. However, it meant
   I also had to use a virtual interface (e.g. to store in p4 objects / use in
   action primitives). After some separate benchmarking, I decided against using
   templates (not really worth it). The only time I observed a real speed up was
   without using virtual functions (which is not really possible here anyway)
   and for a certain rate count (for which the compiler was doing loop
   unrolling, probably was a more cache friendly value too). I think I can live
   with the extra overhead of having a vector (vs an array) and having to test
   for the meter type.
   Maybe I will change this later, but meters are not used that much so for now
   I am going for simplicity */

class Meter
{
public:
  typedef unsigned int color_t;
  typedef size_t rate_idx_t;
  struct rate_config_t {
    double info_rate;
    size_t burst_size;

    static rate_config_t make(double info_rate, size_t burst_size) {
      return {info_rate, burst_size};
    }
  };
  typedef std::chrono::steady_clock clock;

public:
  enum class MeterType {
    BYTES,
    PACKETS
  };

  enum MeterErrorCode {
    SUCCESS = 0,
    INVALID_INDEX,
    BAD_RATES_LIST,
    INVALID_INFO_RATE_VALUE,
    INVALID_BURST_SIZE_VALUE,
    ERROR
  };

public:
  Meter(MeterType type, size_t rate_count)
    : type(type), rates(rate_count) { }

  // the rate configs must be sorted from smaller rate to higher rate
  // in the 2 rate meter case: {CIR, PIR}
  MeterErrorCode set_rates(const std::initializer_list<rate_config_t> &configs);

  MeterErrorCode reset_rates();

  color_t execute(const Packet &pkt);

private:
  typedef std::unique_lock<std::mutex> UniqueLock;
  UniqueLock unique_lock() const { return UniqueLock(*m_mutex); }
  void unlock(UniqueLock &lock) const { lock.unlock(); }

private:
  struct MeterRate {
    bool valid{}; // TODO: get rid of this?
    double info_rate{}; // in bytes / packets per microsecond
    size_t burst_size{};
    size_t tokens{};
    uint64_t tokens_last{};
    color_t color{};
  };

private:
  MeterType type;
  // I decided to take the easy route and wrap the m_mutex into a
  // unique_ptr. Mutexes are not movable and that would be a problem with the
  // MeterArray implementation. I don't think this will incur a performance hit
  std::unique_ptr<std::mutex> m_mutex{new std::mutex()};
  // mutable std::mutex m_mutex;
  std::vector<MeterRate> rates;
  bool configured{false};
};

typedef p4object_id_t meter_array_id;

class MeterArray {
public:
  typedef Meter::MeterErrorCode MeterErrorCode;
  typedef Meter::color_t color_t;
  typedef Meter::MeterType MeterType;

  typedef vector<Meter>::iterator iterator;
  typedef vector<Meter>::const_iterator const_iterator;

public:
  MeterArray(MeterType type, size_t rate_count, size_t size) {
    meters.reserve(size);
    for(size_t i = 0; i < size; i++)
      meters.emplace_back(type, rate_count);
  }

  color_t execute_meter(const Packet &pkt, size_t idx) {
    return meters[idx].execute(pkt);
  }

  Meter &get_meter(size_t idx) {
    return meters[idx];
  }

  const Meter &get_meter(size_t idx) const {
    return meters[idx];
  }

  Meter &operator[](size_t idx) {
    assert(idx < size());
    return meters[idx];
  }

  const Meter &operator[](size_t idx) const {
    assert(idx < size());
    return meters[idx];
  }

  // iterators
  iterator begin() { return meters.begin(); }

  const_iterator begin() const { return meters.begin(); }

  iterator end() { return meters.end(); }

  const_iterator end() const { return meters.end(); }

  size_t size() const { return meters.size(); }

private:
  std::vector<Meter> meters{};
};

#endif
