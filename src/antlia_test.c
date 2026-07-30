// The keyboard self-test.
//
// Types every symbol a short ID can contain, plus the cosmetic hyphen, so two
// things can be checked before a real bin is scanned: that the focused field is
// the one you meant, and that the host's keyboard layout agrees with the
// Flipper's US-only ASCII map. If the letters arrive but the digits or the hyphen
// do not, that is a layout problem and the plain output form avoids it.

#include <furi.h>
#include <gui/view.h>
#include <input/input.h>

#include "antlia_i.h"

// The reduced Crockford alphabet — no I, L, O or U — and the separator.
#define ANTLIA_TEST_STRING "0123456789-ABCDEFGHJKMNPQRSTVWXYZ"

typedef enum {
    TestStateTyping,
    TestStateDone,
    TestStateFailed,
} TestState;

typedef struct {
    TestState state;
} AntliaTestModel;

static void antlia_test_draw_callback(Canvas* canvas, void* context) {
    const AntliaTestModel* model = context;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    switch(model->state) {
    case TestStateTyping:
        canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignTop, "Typing test text");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignTop, "into the focused field");
        break;
    case TestStateDone:
        canvas_draw_str_aligned(canvas, 64, 14, AlignCenter, AlignTop, "Sent");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignTop, "0123456789-ABC...XYZ");
        canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignTop, "Check what arrived");
        break;
    case TestStateFailed:
        canvas_draw_str_aligned(canvas, 64, 18, AlignCenter, AlignTop, "No USB host");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignTop, "Plug into a computer");
        break;
    }
}

static int32_t antlia_test_worker(void* context) {
    Antlia* app = context;

    bool typed = antlia_hid_type(
        &app->hid, ANTLIA_TEST_STRING, app->config.terminator, app->config.key_delay_ms);

    // Release immediately rather than on view exit. The self-test is one shot, and
    // holding the HID interface would keep the serial interface torn down — so the
    // CLI and qFlipper would stay unreachable until someone pressed Back on the
    // device. A test you can only recover from by hand is a bad test.
    antlia_hid_release(&app->hid);

    with_view_model(
        app->test_view,
        AntliaTestModel * model,
        { model->state = typed ? TestStateDone : TestStateFailed; },
        true);

    notification_message(app->notifications, typed ? &sequence_success : &sequence_error);
    return 0;
}

static void antlia_test_enter_callback(void* context) {
    Antlia* app = context;

    with_view_model(
        app->test_view, AntliaTestModel * model, { model->state = TestStateTyping; }, true);

    antlia_hid_claim(&app->hid);
    // On its own thread: typing blocks for as long as the string takes, and a
    // frozen screen during it would look like a crash.
    app->test_worker = furi_thread_alloc_ex("AntliaTest", 1024, antlia_test_worker, app);
    furi_thread_start(app->test_worker);
}

static void antlia_test_exit_callback(void* context) {
    Antlia* app = context;

    if(app->test_worker != NULL) {
        furi_thread_join(app->test_worker);
        furi_thread_free(app->test_worker);
        app->test_worker = NULL;
    }
    antlia_hid_release(&app->hid);
}

View* antlia_test_view_alloc(Antlia* app) {
    View* view = view_alloc();
    view_allocate_model(view, ViewModelTypeLocking, sizeof(AntliaTestModel));
    view_set_context(view, app);
    view_set_draw_callback(view, antlia_test_draw_callback);
    view_set_enter_callback(view, antlia_test_enter_callback);
    view_set_exit_callback(view, antlia_test_exit_callback);
    return view;
}

void antlia_test_view_free(View* view) {
    view_free(view);
}
