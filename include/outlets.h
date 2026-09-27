#pragma once

#include "config.h"
#include "events.h"

// Owns the relays. Every change, from any source or task, goes through request(), which
// applies the compressor minimum-off delay and records an event (PLAN.md R1, R4, R6).

enum class Want : uint8_t { kOff, kOn, kToggle };

struct OutletStatus {
  bool on;
  bool pending_on;        // waiting out the compressor delay
  uint32_t pending_left_s;
};

namespace Outlets {

// Drives relays low immediately, then applies each outlet's power-on behaviour.
void begin();

// Thread-safe. Returns the resulting (or pending) state.
bool request(int outlet, Want want, Source source);

// Releases compressor-delayed turn-ons. Called from the control task.
void tick();

OutletStatus status(int outlet);

// While locked (thermal trip), every turn-on request is refused and pending
// compressor-delayed turn-ons are dropped. Turning off still works.
void setLockout(bool locked);
bool lockedOut();

}  // namespace Outlets
