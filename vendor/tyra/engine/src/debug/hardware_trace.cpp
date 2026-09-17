// Modified by TyraX: startup-configured capture into bounded RAM, export later.
#include "debug/hardware_trace.hpp"
#include "file/file_utils.hpp"
#include <cstdio>
#include <cstdlib>

namespace Tyra { namespace HardwareTrace {
bool active = false;
struct Event { const char* label; u32 start, duration, frame, value; };
static Event* events = nullptr;
static const u32 capacity = 32768;
static u32 count = 0, dropped = 0, frame = 0, first = 120, frames = 4;
static u32 origin = 0, frameStart = 0;
static bool enabled = false, states = true;
u32 ticks() { u32 t; asm volatile("mfc0 %0, $9" : "=r"(t)); return t; }
void configure() {
  // One startup read; missing file means no allocation, clocks or later I/O.
  FILE* f = fopen(FileUtils::fromCwd("hardware-trace.cfg").c_str(), "r");
  if (!f) return;
  unsigned stateFlag = 1;
  const int n = fscanf(f, "%u %u %u", &first, &frames, &stateFlag);
  fclose(f);
  if (n != 3 || first > 1000000 || frames < 1 || frames > 32 || stateFlag > 1) return;
  events = static_cast<Event*>(malloc(sizeof(Event) * capacity));
  enabled = events != nullptr;
  states = stateFlag != 0;
}
void record(const char* label, u32 start, u32 end, u32 value) {
  if (!active) return;
  if (count == capacity) { ++dropped; return; }
  events[count++] = {label, start - origin, end - start, frame, value};
}
void state(const char* label) {
  if (!active || !states) return;
  const u32 t = ticks();
  // Read-only snapshots, NOT continuous utilization or execution durations.
  record(label, t, t, *(volatile u32*)0x10003c00); // VIF1_STAT
  record("GIF_STATE", t, t, *(volatile u32*)0x10003020);
}
void beginFrame() {
  if (!enabled) return;
  active = frame >= first && frame - first < frames;
  if (!active) return;
  frameStart = ticks();
  if (frame == first) origin = frameStart;
  state("VIF1_FRAME_START");
}
void endFrame() {
  if (!enabled) return;
  if (active) record("Frame", frameStart, ticks());
  active = false;
  ++frame;
  if (frame < first + frames) return;
  enabled = false;
  FILE* f = fopen(FileUtils::fromCwd("hardware-trace.csv").c_str(), "w");
  if (f) {
    fprintf(f, "label,start_ticks,duration_ticks,frame,value\n");
    for (u32 i = 0; i < count; ++i) {
      const Event& e = events[i];
      fprintf(f, "%s,%u,%u,%u,%u\n", e.label,e.start,e.duration,e.frame,e.value);
    }
    fprintf(f, "END,%u,%u,%u,%u\n", count,dropped,frames,first);
    fclose(f);
  }
  free(events); events = nullptr;
}
} }
