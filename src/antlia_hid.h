// Typing a short ID into the connected computer as a USB keyboard.
//
// **USB, not Bluetooth.** BLE HID is unreachable from an external app on this
// firmware: `ble_profile_hid` and `ble_profile_hid_kb_press` are both absent from
// the SDK's exported symbol table (`targets/f7/api_symbols.csv` marks them `-`),
// so only apps compiled into the firmware — the stock Remote app — can use it.
// See README.md; if a future firmware exports them, this is the one file that
// changes.
//
// Claiming the USB HID interface replaces the serial interface, so the Flipper's
// CLI and qFlipper are unavailable while a scan session is open. That is why the
// claim is scoped to the scan view rather than the whole app.

#pragma once

// stdint first: furi_hal_usb.h pulls in libusb_stm32 headers that use the
// fixed-width types without including them themselves.
#include <stdbool.h>
#include <stdint.h>

#include <furi_hal_usb.h>

#include "antlia_config.h"

typedef struct {
    // Whatever the interface was before we took it, restored on release.
    FuriHalUsbInterface* previous;
    bool claimed;
} AntliaHid;

void antlia_hid_init(AntliaHid* hid);

// Claim the USB HID interface. Idempotent.
void antlia_hid_claim(AntliaHid* hid);

// Give the interface back to whoever had it. Idempotent.
void antlia_hid_release(AntliaHid* hid);

// True once the host has enumerated the keyboard and is accepting reports.
// Typing before this is true silently goes nowhere, which is why the scan view
// shows it rather than assuming it.
bool antlia_hid_ready(const AntliaHid* hid);

// Type `text` followed by `terminator`. Returns false if the interface is not
// claimed, the host is not ready, or `text` contains a character with no US
// keycode.
bool antlia_hid_type(
    const AntliaHid* hid,
    const char* text,
    AntliaTerminator terminator,
    uint32_t key_delay_ms);
