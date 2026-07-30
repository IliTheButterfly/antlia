// Antlia — shared app state.
//
// Antlia reads an Almagest container tag and types the short ID it carries into
// whatever computer the Flipper is plugged into, as a USB keyboard. That makes a
// laptop — which has no NFC reader — able to identify a bin, using the same
// payload the phone PWA and the bench station already read.

#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/view_dispatcher.h>
#include <notification/notification_messages.h>

#include "antlia_config.h"
#include "antlia_hid.h"
#include "lib/ndef.h"
#include "lib/shortid.h"

#define ANTLIA_TAG "Antlia"

// Hex UID, colon-free, plus a NUL. A 7-byte NTAG UID is 14 characters; the
// spare room covers a 10-byte UID without truncating one.
#define ANTLIA_UID_TEXT_SIZE 24

typedef enum {
    AntliaViewMenu,
    AntliaViewScan,
    AntliaViewTest,
    AntliaViewSettings,
    AntliaViewAbout,
} AntliaViewId;

typedef enum {
    // Nothing on the reader.
    AntliaScanWaiting,
    // A tag was read and its short ID typed.
    AntliaScanTyped,
    // A tag was read; auto-type is off, so it is waiting for OK.
    AntliaScanHolding,
    // A tag was read but carries no usable Almagest short ID.
    AntliaScanUnknownTag,
    // A tag was read and parsed, but the host is not accepting keystrokes.
    AntliaScanNoHost,
} AntliaScanState;

typedef struct {
    AntliaScanState state;
    // Canonical 8 symbols, and the `4K7T-92M8` rendering of them.
    char short_id[SHORTID_SIZE];
    char display[SHORTID_DISPLAY_SIZE];
    // The URI exactly as the tag carries it, for `AntliaOutputTagUrl`.
    char uri[NDEF_URI_MAX];
    // Shown when a tag is unreadable: the UID is the one thing still usable,
    // because it is what a UID-fallback binding in the PWA is keyed on.
    char uid[ANTLIA_UID_TEXT_SIZE];
    // Why the tag was refused, when it was.
    const char* reason;
    bool hid_ready;
} AntliaScanModel;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    NotificationApp* notifications;

    Submenu* menu;
    View* scan_view;
    View* test_view;
    VariableItemList* settings_list;
    Widget* about;

    AntliaConfig config;
    AntliaHid hid;

    FuriThread* worker;
    FuriThread* test_worker;
    // Cleared to stop the worker; it is the only writer of the scan model.
    volatile bool worker_running;
    // Set by the OK key, consumed by the worker. Typing blocks for as long as
    // the string takes, so the GUI thread must not be the one doing it.
    volatile bool type_requested;
} Antlia;

// Scan view lifecycle. The view owns the worker: entering it claims USB HID and
// starts polling, leaving it releases both.
View* antlia_scan_view_alloc(Antlia* app);
void antlia_scan_view_free(View* view);

// The keyboard self-test view: types every symbol a short ID can contain, so the
// focused field and the host's layout can be checked before scanning a real bin.
View* antlia_test_view_alloc(Antlia* app);
void antlia_test_view_free(View* view);

void antlia_settings_build(Antlia* app);
