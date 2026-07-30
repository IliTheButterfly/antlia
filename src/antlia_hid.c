#include "antlia_hid.h"

#include <furi.h>
#include <furi_hal_usb_hid.h>

#define TAG "AntliaHid"

// The host needs a moment to enumerate the new interface after the switch.
// Polled rather than slept through, so a fast host is not made to wait.
#define ENUMERATION_TIMEOUT_MS 2000
#define ENUMERATION_POLL_MS    50

void antlia_hid_init(AntliaHid* hid) {
    hid->previous = NULL;
    hid->claimed = false;
}

void antlia_hid_claim(AntliaHid* hid) {
    if(hid->claimed) return;

    hid->previous = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    if(furi_hal_usb_set_config(&usb_hid, NULL)) {
        hid->claimed = true;
    } else {
        FURI_LOG_E(TAG, "could not claim the USB HID interface");
        hid->previous = NULL;
    }
}

void antlia_hid_release(AntliaHid* hid) {
    if(!hid->claimed) return;

    // Let go of any key still held, or the host sees a stuck modifier after the
    // app exits.
    furi_hal_hid_kb_release_all();
    furi_hal_usb_set_config(hid->previous, NULL);
    hid->previous = NULL;
    hid->claimed = false;
}

bool antlia_hid_ready(const AntliaHid* hid) {
    return hid->claimed && furi_hal_hid_is_connected();
}

static uint16_t keycode_for(char character) {
    // `hid_asciimap` is US-layout ASCII → keycode, modifiers included in the
    // high bits. Non-ASCII cannot be typed, and a short ID never contains any.
    if((unsigned char)character >= 128) return HID_KEYBOARD_NONE;
    return hid_asciimap[(unsigned char)character];
}

static bool type_keycode(uint16_t keycode, uint32_t key_delay_ms) {
    if(keycode == HID_KEYBOARD_NONE) return false;
    if(!furi_hal_hid_kb_press(keycode)) return false;
    furi_delay_ms(key_delay_ms);
    bool released = furi_hal_hid_kb_release(keycode);
    furi_delay_ms(key_delay_ms);
    return released;
}

bool antlia_hid_type(
    const AntliaHid* hid,
    const char* text,
    AntliaTerminator terminator,
    uint32_t key_delay_ms) {
    furi_assert(text);
    if(!hid->claimed) return false;

    // Wait out enumeration rather than dropping the first scan after the
    // interface switch — which is otherwise the common case, since the switch
    // happens as the scan view opens.
    uint32_t waited = 0;
    while(!furi_hal_hid_is_connected() && waited < ENUMERATION_TIMEOUT_MS) {
        furi_delay_ms(ENUMERATION_POLL_MS);
        waited += ENUMERATION_POLL_MS;
    }
    if(!furi_hal_hid_is_connected()) {
        FURI_LOG_W(TAG, "host is not accepting keystrokes");
        return false;
    }

    for(const char* cursor = text; *cursor != '\0'; cursor++) {
        uint16_t keycode = keycode_for(*cursor);
        if(keycode == HID_KEYBOARD_NONE) {
            FURI_LOG_E(TAG, "no keycode for 0x%02X", (unsigned)(unsigned char)*cursor);
            furi_hal_hid_kb_release_all();
            return false;
        }
        if(!type_keycode(keycode, key_delay_ms)) {
            furi_hal_hid_kb_release_all();
            return false;
        }
    }

    switch(terminator) {
    case AntliaTerminatorEnter:
        return type_keycode(HID_KEYBOARD_RETURN, key_delay_ms);
    case AntliaTerminatorTab:
        return type_keycode(HID_KEYBOARD_TAB, key_delay_ms);
    case AntliaTerminatorNone:
    default:
        return true;
    }
}
