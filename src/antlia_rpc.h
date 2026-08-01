// Bridge mode: Antlia driven by a host over the Flipper's RPC.
//
// Antlia has two disjoint modes and this is the second one.
//
// **Wedge mode** — everything else in this app — claims USB HID and types a
// short ID into whatever computer the Flipper is plugged into. It exists because
// a laptop has no NFC reader and no other channel to give one.
//
// **Bridge mode** is entered only when the app is launched over RPC with the
// argument `RPC`, by Almagest's device bridge (`deviceagent`). The host asks for
// reads and writes over a text line protocol and gets both carriers back.
//
// **Bridge mode must never claim USB HID**, and that is not a preference. The
// HID claim *replaces* the CDC interface — it is already why the wedge's claim
// is scoped to the scan view, and already why claiming it strands the Flipper's
// CLI until someone presses a button. Under RPC the CDC interface **is the
// session giving the orders**, so claiming HID would sever the connection
// mid-command. It is a self-inflicted disconnect, not a trade-off.
//
// It costs nothing, because HID is redundant here. The wedge exists to get a
// short ID into a computer that has no other channel; in bridge mode there is a
// channel, and it is strictly better — both carriers instead of a short id, and
// a write instead of nothing.
//
// The grammar is defined in one place, `deviceagent/agent/flipper/antlia.py`,
// and this file is a transcription of it. See ADR 0013.

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Whether the launch argument asks for bridge mode.
//
// The host sends `app_start_request{args: "RPC"}`; the firmware's RPC service
// rewrites that into `RPC <pointer>` before handing it to the loader, because
// the app needs the `RpcAppSystem*` and the loader only passes a string.
//
// **This convention is the one thing here that could not be checked against the
// SDK headers** — `rpc_app.h` exports no way to fetch the context, so it must
// arrive through the argument, and the format is what the firmware's own
// RPC-driven apps parse. If bridge mode never starts on a real device, verify
// this first: log the raw `args` string and compare.
bool antlia_rpc_wanted(const char* args);

// Run bridge mode to completion. Returns the app's exit code.
//
// Blocks until the host closes the session or asks the app to exit. Draws a
// minimal screen — a person watching the Flipper should be able to tell it is
// being driven rather than frozen.
int32_t antlia_rpc_run(const char* args);
