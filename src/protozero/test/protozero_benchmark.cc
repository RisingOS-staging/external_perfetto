/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// See /docs/design-docs/protozero.md for rationale and results.

#include <memory>
#include <vector>

#include <unistd.h>

#include <benchmark/benchmark.h>

#include "perfetto/base/compiler.h"
#include "perfetto/protozero/static_buffer.h"

// Autogenerated headers in out/*/gen/
#include "src/protozero/test/example_proto/library.pbzero.h"
#include "src/protozero/test/example_proto/test_messages.pb.h"
#include "src/protozero/test/example_proto/test_messages.pbzero.h"

// Generated by the protozero plugin.
namespace pbzero = protozero::test::protos::pbzero;

// Generated by the official protobuf compiler.
namespace pblite = protozero::test::protos;

namespace {

// This needs to be > the max size written by each iteration.
constexpr size_t kBufPerIteration = 512;

// Write cyclically on a 64 MB buffer set to simulate a realistic tracing
// scenario.
constexpr size_t kTotalWorkingSetSize = 64 * 1024 * 1024;
alignas(uint64_t) char g_out_buffer[kTotalWorkingSetSize];

char* g_cur = g_out_buffer;

uint64_t g_fake_input_simple[] = {0x12345678,
                                  0x90ABCDEF,
                                  0x11111111,
                                  0xFFFFFFFF,
                                  0x6666666666666666ULL,
                                  0x6666666666666666ULL,
                                  0x6666666666666666ULL,
                                  0x0066666666666666ULL};

// Speed-of-light serializer. Aa very simple C++ class that just appends data
// into a linear buffer making all sorts of favourable assumptions. It does not
// use any binary-stable encoding, it does not perform bound checking,
// all writes are 64-bit aligned, it doesn't deal with any thread-safety.
// The speed-of-light serializer serves as a reference for how fast a serializer
// could be if argument marshalling and bound checking were zero cost.
struct SOLMsg {
  template <typename T>
  void Append(T x) {
    // The reinterpret_cast is to give favorable alignment guarantees.
    // The memcpy will be elided by the compiler, which will emit just a
    // 64-bit aligned mov instruction.
    memcpy(reinterpret_cast<void*>(ptr_), &x, sizeof(x));
    ptr_ += sizeof(uint64_t);
  }

  void set_field_int32(int32_t x) { Append(x); }
  void set_field_uint32(uint32_t x) { Append(x); }
  void set_field_int64(int64_t x) { Append(x); }
  void set_field_uint64(uint64_t x) { Append(x); }
  void set_field_string(const char* str) { ptr_ = strcpy(ptr_, str); }

  SOLMsg* add_field_nested() { return new (this + 1) SOLMsg(); }

  alignas(uint64_t) char storage_[sizeof(g_fake_input_simple) + 8];
  char* ptr_ = &storage_[0];
};

template <typename T>
PERFETTO_ALWAYS_INLINE void FillMessage_Simple(T* msg) {
  benchmark::DoNotOptimize(g_fake_input_simple);
  msg->set_field_int32(static_cast<int32_t>(g_fake_input_simple[0]));
  msg->set_field_uint32(static_cast<uint32_t>(g_fake_input_simple[1]));
  msg->set_field_int64(static_cast<int64_t>(g_fake_input_simple[2]));
  msg->set_field_uint64(static_cast<uint64_t>(g_fake_input_simple[3]));
  msg->set_field_string(reinterpret_cast<const char*>(&g_fake_input_simple[4]));
}

template <typename T>
PERFETTO_ALWAYS_INLINE void FillMessage_Nested(T* msg, int depth = 0) {
  benchmark::DoNotOptimize(g_fake_input_simple);
  FillMessage_Simple(msg);
  if (depth < 3) {
    auto* child = msg->add_field_nested();
    FillMessage_Nested(child, depth + 1);
  }
}

PERFETTO_ALWAYS_INLINE void Clobber(benchmark::State& state) {
  uint64_t* buf = reinterpret_cast<uint64_t*>(g_cur);

  // Read-back the data written to have a realistic evaluation of the
  // speed-of-light scenario. This is to deal with architecture of modern CPUs.
  // If we write a bunch of memory bytes, never read-back from them, and then
  // just over-write them, the CPU can just throw away the whole stream of
  // instructions that produced them, if that's still in flight and tracked in
  // the out-of-order units.
  // The buf[i-1] ^= buf forces the CPU to consume the result of the writes.
  buf[0] = reinterpret_cast<uint64_t>(&state);
  for (size_t i = 1; i < kBufPerIteration / sizeof(uint64_t); i++)
    buf[i] ^= buf[i - 1];
  if (buf[(kBufPerIteration / sizeof(uint64_t)) - 1] == 42)
    PERFETTO_CHECK(false);
  benchmark::DoNotOptimize(buf);

  constexpr size_t kWrap = kTotalWorkingSetSize / kBufPerIteration;
  g_cur = &g_out_buffer[(state.iterations() % kWrap) * kBufPerIteration];
  benchmark::ClobberMemory();
}

}  // namespace

static void BM_Protozero_Simple_Libprotobuf(benchmark::State& state) {
  while (state.KeepRunning()) {
    {
      // The nested block is to account for RAII finalizers.
      pblite::EveryField msg;
      FillMessage_Simple(&msg);
      msg.SerializeToArray(g_cur, kBufPerIteration);
    }
    Clobber(state);
  }
}

static void BM_Protozero_Simple_Protozero(benchmark::State& state) {
  while (state.KeepRunning()) {
    {
      protozero::StaticBuffered<pbzero::EveryField> msg(g_cur,
                                                        kBufPerIteration);
      FillMessage_Simple(msg.get());
    }
    Clobber(state);
  }
}

static void BM_Protozero_Simple_SpeedOfLight(benchmark::State& state) {
  while (state.KeepRunning()) {
    SOLMsg* msg = new (g_cur) SOLMsg();
    FillMessage_Simple(msg);
    Clobber(state);
  }
}

static void BM_Protozero_Nested_Libprotobuf(benchmark::State& state) {
  while (state.KeepRunning()) {
    {
      pblite::EveryField msg;
      FillMessage_Nested(&msg);
      msg.SerializeToArray(g_cur, kBufPerIteration);
    }
    Clobber(state);
  }
}

static void BM_Protozero_Nested_Protozero(benchmark::State& state) {
  while (state.KeepRunning()) {
    {
      protozero::StaticBuffered<pbzero::EveryField> msg(g_cur,
                                                        kBufPerIteration);
      FillMessage_Nested(msg.get());
    }
    Clobber(state);
  }
}

static void BM_Protozero_Nested_SpeedOfLight(benchmark::State& state) {
  while (state.KeepRunning()) {
    SOLMsg* msg = new (g_cur) SOLMsg();
    FillMessage_Nested(msg);
    Clobber(state);
  }
}

BENCHMARK(BM_Protozero_Simple_Libprotobuf);
BENCHMARK(BM_Protozero_Simple_Protozero);
BENCHMARK(BM_Protozero_Simple_SpeedOfLight);

BENCHMARK(BM_Protozero_Nested_Libprotobuf);
BENCHMARK(BM_Protozero_Nested_Protozero);
BENCHMARK(BM_Protozero_Nested_SpeedOfLight);
