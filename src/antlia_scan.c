// The scan view and its polling worker.
//
// There is no "press the button to read" step: the worker polls continuously
// while the view is open, so the gesture is a tap and nothing else. Almagest's
// bench station arrived at the same shape for the same reason (see its ADR 0003 —
// continuous polling is the trigger), and a tap that needs a keypress first is a
// tap the user stops making.

#include <furi.h>
#include <gui/elements.h>
#include <gui/view.h>
#include <input/input.h>
#include <nfc/nfc.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight_poller_sync.h>

#include "antlia_i.h"

#define TAG "AntliaScan"

// How long to wait between reads. Short enough that a tap feels immediate, long
// enough that the field is not on constantly.
#define POLL_INTERVAL_MS 120

// The first page of user memory on every Type 2 tag: pages 0-3 are the UID,
// internal bytes, lock bytes and capability container.
#define USER_MEMORY_FIRST_PAGE 4

static void uid_to_text(const uint8_t* uid, size_t uid_size, char* out, size_t out_size) {
    static const char HEX[] = "0123456789ABCDEF";
    size_t written = 0;
    for(size_t i = 0; i < uid_size && written + 3 <= out_size; i++) {
        out[written++] = HEX[uid[i] >> 4];
        out[written++] = HEX[uid[i] & 0x0F];
    }
    out[written] = '\0';
}

// The text this scan should type, given the configured output form.
static const char* output_text(const AntliaScanModel* model, AntliaOutput output) {
    switch(output) {
    case AntliaOutputPlain:
        return model->short_id;
    case AntliaOutputTagUrl:
        return model->uri;
    case AntliaOutputHyphenated:
    default:
        return model->display;
    }
}

static void antlia_scan_draw_callback(Canvas* canvas, void* context) {
    const AntliaScanModel* model = context;
    canvas_clear(canvas);

    switch(model->state) {
    case AntliaScanWaiting:
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 18, AlignCenter, AlignTop, "Hold tag to reader");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas,
            64,
            36,
            AlignCenter,
            AlignTop,
            model->hid_ready ? "Keyboard ready" : "Waiting for USB host");
        break;

    case AntliaScanTyped:
    case AntliaScanHolding:
    case AntliaScanNoHost:
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 14, AlignCenter, AlignTop, model->display);
        canvas_set_font(canvas, FontSecondary);
        if(model->state == AntliaScanTyped) {
            canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignTop, "Typed");
        } else if(model->state == AntliaScanNoHost) {
            canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignTop, "No USB host");
            canvas_draw_str_aligned(canvas, 64, 41, AlignCenter, AlignTop, "Plug into a computer");
        } else {
            elements_button_center(canvas, "Type");
        }
        break;

    case AntliaScanUnknownTag:
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignTop, "Not an Almagest tag");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignTop, model->reason);
        if(model->uid[0] != '\0') {
            // The UID is the one thing still usable: it is what a UID-fallback
            // binding is keyed on, so showing it lets the tag be bound in the
            // PWA instead of leaving the user with nothing.
            canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignTop, "UID");
            canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignTop, model->uid);
        }
        break;
    }
}

static bool antlia_scan_input_callback(InputEvent* event, void* context) {
    Antlia* app = context;
    if(event->type != InputTypeShort || event->key != InputKeyOk) return false;

    bool handled = false;
    with_view_model(
        app->scan_view,
        AntliaScanModel * model,
        {
            // OK only means anything when a tag is being held for confirmation.
            if(model->state == AntliaScanHolding) handled = true;
        },
        false);

    if(handled) app->type_requested = true;
    return handled;
}

// Type the text for the current model and record the outcome in it. Runs on the
// worker thread: typing blocks for as long as the string takes, and the GUI
// thread must not be the one waiting.
static void type_current(Antlia* app) {
    char text[NDEF_URI_MAX];
    with_view_model(
        app->scan_view,
        AntliaScanModel * model,
        { strlcpy(text, output_text(model, app->config.output), sizeof(text)); },
        false);

    bool typed =
        antlia_hid_type(&app->hid, text, app->config.terminator, app->config.key_delay_ms);

    with_view_model(
        app->scan_view,
        AntliaScanModel * model,
        {
            model->state = typed ? AntliaScanTyped : AntliaScanNoHost;
            model->hid_ready = antlia_hid_ready(&app->hid);
        },
        true);

    notification_message(app->notifications, typed ? &sequence_success : &sequence_error);
}

static void set_unknown_tag(Antlia* app, const char* reason, const uint8_t* uid, size_t uid_size) {
    with_view_model(
        app->scan_view,
        AntliaScanModel * model,
        {
            model->state = AntliaScanUnknownTag;
            model->reason = reason;
            model->short_id[0] = '\0';
            model->display[0] = '\0';
            model->uri[0] = '\0';
            if(uid != NULL) {
                uid_to_text(uid, uid_size, model->uid, sizeof(model->uid));
            } else {
                model->uid[0] = '\0';
            }
        },
        true);
    notification_message(app->notifications, &sequence_error);
}

static int32_t antlia_scan_worker(void* context) {
    Antlia* app = context;
    Nfc* nfc = nfc_alloc();
    MfUltralightData* data = mf_ultralight_alloc();

    // The last ID typed while the current tag has stayed on the reader. Cleared
    // when the tag leaves, so re-tapping the same tag deliberately types again
    // while holding it still does not.
    char last_handled[SHORTID_SIZE] = {0};

    while(app->worker_running) {
        if(app->type_requested) {
            app->type_requested = false;
            type_current(app);
            continue;
        }

        MfUltralightError error = mf_ultralight_poller_sync_read_card(nfc, data, NULL);

        if(error != MfUltralightErrorNone) {
            // No tag, or an unreadable one. The last result stays on screen —
            // there is nothing useful to replace it with, and blanking it the
            // instant the tag lifts makes the ID unreadable to a human.
            last_handled[0] = '\0';
            bool ready = antlia_hid_ready(&app->hid);
            with_view_model(
                app->scan_view, AntliaScanModel * model, { model->hid_ready = ready; }, true);
            furi_delay_ms(POLL_INTERVAL_MS);
            continue;
        }

        size_t uid_size = 0;
        const uint8_t* uid = mf_ultralight_get_uid(data, &uid_size);

        if(data->pages_read <= USER_MEMORY_FIRST_PAGE) {
            set_unknown_tag(app, "no user memory read", uid, uid_size);
            furi_delay_ms(POLL_INTERVAL_MS);
            continue;
        }

        // `page` is an array of 4-byte pages, so the user memory is already
        // contiguous — no copy needed to hand it to the parser.
        const uint8_t* memory = (const uint8_t*)&data->page[USER_MEMORY_FIRST_PAGE];
        size_t memory_size =
            (size_t)(data->pages_read - USER_MEMORY_FIRST_PAGE) * MF_ULTRALIGHT_PAGE_SIZE;

        char uri[NDEF_URI_MAX];
        if(!ndef_find_uri(memory, memory_size, uri, sizeof(uri))) {
            set_unknown_tag(app, "no NDEF URI record", uid, uid_size);
            furi_delay_ms(POLL_INTERVAL_MS);
            continue;
        }

        char short_id[SHORTID_SIZE];
        if(!ndef_short_id_from_uri(uri, short_id)) {
            // The URI is well formed but is not an Almagest `/s/{short_id}`, or
            // its check symbol does not match. Either way it must not be typed:
            // a wrong-but-plausible ID is worse than no ID.
            set_unknown_tag(app, "URL carries no valid ID", uid, uid_size);
            furi_delay_ms(POLL_INTERVAL_MS);
            continue;
        }

        if(strcmp(short_id, last_handled) == 0) {
            // Same tag, still sitting there. Already dealt with.
            furi_delay_ms(POLL_INTERVAL_MS);
            continue;
        }
        strlcpy(last_handled, short_id, sizeof(last_handled));

        with_view_model(
            app->scan_view,
            AntliaScanModel * model,
            {
                strlcpy(model->short_id, short_id, sizeof(model->short_id));
                shortid_format_display(model->short_id, model->display);
                strlcpy(model->uri, uri, sizeof(model->uri));
                uid_to_text(uid, uid_size, model->uid, sizeof(model->uid));
                model->reason = "";
                model->state = AntliaScanHolding;
                model->hid_ready = antlia_hid_ready(&app->hid);
            },
            true);

        if(app->config.auto_type) {
            type_current(app);
        } else {
            notification_message(app->notifications, &sequence_blink_blue_100);
        }

        furi_delay_ms(POLL_INTERVAL_MS);
    }

    mf_ultralight_free(data);
    nfc_free(nfc);
    return 0;
}

static void antlia_scan_enter_callback(void* context) {
    Antlia* app = context;

    with_view_model(
        app->scan_view,
        AntliaScanModel * model,
        {
            model->state = AntliaScanWaiting;
            model->short_id[0] = '\0';
            model->display[0] = '\0';
            model->uri[0] = '\0';
            model->uid[0] = '\0';
            model->reason = "";
            model->hid_ready = false;
        },
        true);

    // Claiming HID replaces the serial interface, so this is scoped to the scan
    // session rather than the whole app — the CLI keeps working in the menus.
    antlia_hid_claim(&app->hid);

    app->type_requested = false;
    app->worker_running = true;
    app->worker = furi_thread_alloc_ex("AntliaWorker", 2048, antlia_scan_worker, app);
    furi_thread_start(app->worker);
}

static void antlia_scan_exit_callback(void* context) {
    Antlia* app = context;

    app->worker_running = false;
    if(app->worker != NULL) {
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
        app->worker = NULL;
    }

    antlia_hid_release(&app->hid);
}

View* antlia_scan_view_alloc(Antlia* app) {
    View* view = view_alloc();
    view_allocate_model(view, ViewModelTypeLocking, sizeof(AntliaScanModel));
    view_set_context(view, app);
    view_set_draw_callback(view, antlia_scan_draw_callback);
    view_set_input_callback(view, antlia_scan_input_callback);
    view_set_enter_callback(view, antlia_scan_enter_callback);
    view_set_exit_callback(view, antlia_scan_exit_callback);
    view_set_previous_callback(view, NULL);
    return view;
}

void antlia_scan_view_free(View* view) {
    view_free(view);
}
