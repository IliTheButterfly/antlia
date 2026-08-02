#include "antlia_rpc.h"

#include <furi.h>
#include <gui/gui.h>
#include <nfc/nfc.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight_poller_sync.h>
#include <rpc/rpc_app.h>
#include <stdio.h>
#include <string.h>

#include "antlia_i.h"
#include "lib/ndef.h"
#include "lib/ndef_encode.h"
#include "lib/shortid.h"

#define TAG "AntliaRpc"

// The grammar's version, from `deviceagent/agent/flipper/antlia.py`. Announced
// in HELLO and checked by the bridge, so a stale `.fap` on a Flipper is an
// explicit refusal at attach rather than a reader that answers nothing.
#define ANTLIA_RPC_PROTOCOL 1

// One command line, plus room for a URL and a NUL.
#define LINE_MAX (NDEF_URI_MAX + 32)

// Commands queued from the RPC callback to the worker. Bounded: a host that
// pipelined faster than tags can be read would otherwise grow this without
// limit, and the honest answer to a full queue is to refuse, not to buffer.
#define QUEUE_DEPTH 4

// The first page of user memory. Pages 0-2 are the factory-locked UID.
#define USER_MEMORY_FIRST_PAGE 4

typedef struct {
    char text[LINE_MAX];
} AntliaRpcLine;

typedef struct {
    RpcAppSystem* rpc;
    FuriMessageQueue* commands;
    Gui* gui;
    ViewPort* view_port;
    volatile bool running;
    // Scratch for a reply. Owned by the struct rather than the worker's stack
    // because a URI can be 512 bytes and two of them do not fit comfortably in a
    // FAP's stack.
    char reply[LINE_MAX];
    char uri[NDEF_URI_MAX];
} AntliaRpc;

// ---------------------------------------------------------------------------
// The line protocol
// ---------------------------------------------------------------------------

static void antlia_rpc_send(AntliaRpc* app, const char* line) {
    size_t length = strlen(line);
    // One line in, one line out — the RPC layer already guarantees message
    // boundaries, so the newline is for a human reading a log, not for framing.
    FURI_LOG_D(TAG, "-> %s", line);
    rpc_system_app_exchange_data(app->rpc, (const uint8_t*)line, length);
}

static void antlia_rpc_sendf(AntliaRpc* app, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(app->reply, sizeof(app->reply), format, args);
    va_end(args);
    antlia_rpc_send(app, app->reply);
}

static void uid_to_text(const uint8_t* uid, size_t uid_size, char* out, size_t out_size) {
    static const char HEX[] = "0123456789ABCDEF";
    size_t written = 0;
    for(size_t i = 0; i < uid_size && written + 3 <= out_size; i++) {
        out[written++] = HEX[uid[i] >> 4];
        out[written++] = HEX[uid[i] & 0x0F];
    }
    out[written] = '\0';
}

// ---------------------------------------------------------------------------
// READ
// ---------------------------------------------------------------------------

static void antlia_rpc_read(AntliaRpc* app, Nfc* nfc, MfUltralightData* data) {
    if(mf_ultralight_poller_sync_read_card(nfc, data, NULL) != MfUltralightErrorNone) {
        // No tag, or one that would not answer. `NONE` rather than an error:
        // an empty field is a fact about the world, not a fault, and the bridge
        // draws the same distinction (`TagSource.poll` returning None).
        antlia_rpc_send(app, "NONE");
        return;
    }

    size_t uid_size = 0;
    const uint8_t* uid = mf_ultralight_get_uid(data, &uid_size);
    char uid_text[ANTLIA_UID_TEXT_SIZE];
    uid_to_text(uid, uid_size, uid_text, sizeof(uid_text));

    // A tag that answered but whose user memory did not read is exactly the
    // degraded state: the UID is in factory-locked pages, so the tag still
    // identifies itself perfectly and only its URI is missing. Reported as a
    // TAG with an absent URL rather than as a failure.
    if(data->pages_read <= USER_MEMORY_FIRST_PAGE) {
        antlia_rpc_sendf(app, "TAG %s -", uid_text[0] == '\0' ? "-" : uid_text);
        return;
    }

    const uint8_t* memory = (const uint8_t*)&data->page[USER_MEMORY_FIRST_PAGE];
    size_t memory_size =
        (size_t)(data->pages_read - USER_MEMORY_FIRST_PAGE) * MF_ULTRALIGHT_PAGE_SIZE;

    if(!ndef_find_uri(memory, memory_size, app->uri, sizeof(app->uri))) {
        // Blank, or carrying something that is not a URI record. Both are "this
        // tag has no URL", which is the normal state of a container before it is
        // provisioned and precisely what a walk offers to write.
        antlia_rpc_sendf(app, "TAG %s -", uid_text[0] == '\0' ? "-" : uid_text);
        return;
    }

    // The URI travels verbatim, **not** the short ID parsed out of it. The
    // bridge resolves NDEF-first with a UID fallback and needs to see both
    // carriers exactly as the tag holds them; parsing here would throw away the
    // host part, which is what lets the server report that the two disagree.
    antlia_rpc_sendf(app, "TAG %s %s", uid_text[0] == '\0' ? "-" : uid_text, app->uri);
}

// ---------------------------------------------------------------------------
// WRITE
// ---------------------------------------------------------------------------

static void
    antlia_rpc_write(AntliaRpc* app, Nfc* nfc, MfUltralightData* data, const char* url, bool overwrite) {
    if(url == NULL || url[0] == '\0') {
        antlia_rpc_send(app, "ERR unsupported WRITE needs a url");
        return;
    }

    if(mf_ultralight_poller_sync_read_card(nfc, data, NULL) != MfUltralightErrorNone) {
        antlia_rpc_send(app, "ERR no_tag nothing in the field");
        return;
    }

    // Refusals in cheapest-first order, and every one of them happens **before a
    // single page is written**. A Type 2 Tag write has no transaction: running
    // off the end of a tag, or changing our mind halfway, leaves the earlier
    // pages committed. A half-written tag is worse than an unwritten one.
    size_t total_pages = data->pages_read > USER_MEMORY_FIRST_PAGE ?
                             (size_t)(data->pages_read - USER_MEMORY_FIRST_PAGE) :
                             0;
    size_t needed = 0;
    if(!ndef_uri_page_count(url, total_pages, &needed)) {
        antlia_rpc_send(app, "ERR too_long the payload does not fit on this tag");
        return;
    }

    if(!overwrite && total_pages > 0) {
        const uint8_t* memory = (const uint8_t*)&data->page[USER_MEMORY_FIRST_PAGE];
        size_t memory_size = total_pages * MF_ULTRALIGHT_PAGE_SIZE;
        if(ndef_find_uri(memory, memory_size, app->uri, sizeof(app->uri))) {
            // A tag carrying a URI is very likely bound to another container,
            // and the two mistakes are not symmetrical: refusing costs a toggle,
            // overwriting costs a drawer that answers to someone else's short id.
            antlia_rpc_send(app, "ERR not_blank the tag already carries a URL");
            return;
        }
    }

    uint8_t image[NDEF_ENCODE_MAX];
    size_t written = 0;
    if(!ndef_build_uri_image(url, image, sizeof(image), &written)) {
        antlia_rpc_send(app, "ERR unsupported that URL cannot be encoded");
        return;
    }

    for(size_t index = 0; index < needed; index++) {
        MfUltralightPage page;
        memcpy(page.data, image + index * NDEF_PAGE_SIZE, MF_ULTRALIGHT_PAGE_SIZE);
        uint16_t number = (uint16_t)(USER_MEMORY_FIRST_PAGE + index);
        if(mf_ultralight_poller_sync_write_page(nfc, number, &page) != MfUltralightErrorNone) {
            // The tag is now in the degraded state, and saying so matters: the
            // host must not report this as "nothing happened".
            antlia_rpc_sendf(
                app, "ERR read_back_failed the write stopped at page %u", (unsigned)number);
            return;
        }
    }

    // **Read back through this same reader, always.** A write that reports its
    // own success is exactly what ADR 0012 refuses, and the device holding the
    // tag is the only witness there is. This is why the reply carries a URL
    // rather than a boolean.
    if(mf_ultralight_poller_sync_read_card(nfc, data, NULL) != MfUltralightErrorNone ||
       data->pages_read <= USER_MEMORY_FIRST_PAGE) {
        antlia_rpc_send(app, "WROTE -");
        return;
    }

    const uint8_t* memory = (const uint8_t*)&data->page[USER_MEMORY_FIRST_PAGE];
    size_t memory_size =
        (size_t)(data->pages_read - USER_MEMORY_FIRST_PAGE) * MF_ULTRALIGHT_PAGE_SIZE;
    if(!ndef_find_uri(memory, memory_size, app->uri, sizeof(app->uri))) {
        antlia_rpc_send(app, "WROTE -");
        return;
    }
    antlia_rpc_sendf(app, "WROTE %s", app->uri);
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

static void
    antlia_rpc_dispatch(AntliaRpc* app, Nfc* nfc, MfUltralightData* data, char* line) {
    FURI_LOG_D(TAG, "<- %s", line);

    if(strcmp(line, "PING") == 0) {
        antlia_rpc_send(app, "PONG");
        return;
    }
    if(strcmp(line, "READ") == 0) {
        antlia_rpc_read(app, nfc, data);
        return;
    }

    // `WRITE!` before `WRITE`, or the prefix match would swallow it. The bang
    // rather than a trailing flag so the destructive form is unmistakable in a
    // log, and so a parser that only knew `WRITE` could never be talked into
    // overwriting by a token it ignored — which is the failure this ordering
    // would otherwise create.
    if(strncmp(line, "WRITE! ", 7) == 0) {
        antlia_rpc_write(app, nfc, data, line + 7, true);
        return;
    }
    if(strncmp(line, "WRITE ", 6) == 0) {
        antlia_rpc_write(app, nfc, data, line + 6, false);
        return;
    }

    antlia_rpc_send(app, "ERR unsupported unknown verb");
}

// ---------------------------------------------------------------------------
// RPC plumbing
// ---------------------------------------------------------------------------

static void antlia_rpc_callback(const RpcAppSystemEvent* event, void* context) {
    AntliaRpc* app = context;

    switch(event->type) {
    case RpcAppEventTypeSessionClose:
        // The context is invalid after this. Stop before anything can use it.
        app->running = false;
        break;

    case RpcAppEventTypeAppExit:
        app->running = false;
        rpc_system_app_confirm(app->rpc, true);
        break;

    case RpcAppEventTypeDataExchange: {
        AntliaRpcLine line = {0};
        size_t size = event->data.bytes.size;
        if(size >= sizeof(line.text)) size = sizeof(line.text) - 1;
        memcpy(line.text, event->data.bytes.ptr, size);
        // Trim the trailing newline the bridge sends; the queue carries whole
        // lines and nothing below wants to think about it again.
        for(size_t i = 0; i < size; i++) {
            if(line.text[i] == '\r' || line.text[i] == '\n') {
                line.text[i] = '\0';
                break;
            }
        }
        // Confirmed regardless of whether the queue accepted it: the confirmation
        // is about *delivery*, and the host times out without one. A full queue
        // is answered by the worker refusing, not by silence.
        bool queued = furi_message_queue_put(app->commands, &line, 0) == FuriStatusOk;
        rpc_system_app_confirm(app->rpc, true);
        if(!queued) {
            FURI_LOG_W(TAG, "command queue full, dropped a line");
        }
        break;
    }

    default:
        // Button presses and file loads are wedge-mode concepts with no meaning
        // here. Confirmed rather than ignored, or the host waits for a timeout.
        rpc_system_app_confirm(app->rpc, false);
        break;
    }
}

static void antlia_rpc_draw(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 12, AlignCenter, AlignTop, "Antlia");
    canvas_set_font(canvas, FontSecondary);
    // A person watching the Flipper must be able to tell it is being driven
    // rather than frozen — an app that shows nothing while a host talks to it is
    // indistinguishable from one that has hung.
    canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignTop, "Bridge mode");
    canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignTop, "Almagest is driving");
    canvas_draw_str_aligned(canvas, 64, 54, AlignCenter, AlignTop, "Hold a tag to the back");
}

bool antlia_rpc_wanted(const char* args) {
    if(args == NULL) return false;
    return strncmp(args, "RPC", 3) == 0;
}

int32_t antlia_rpc_run(const char* args) {
    // See `antlia_rpc.h`: the loader only passes a string, and `rpc_app.h`
    // exports no way to fetch the context, so the pointer arrives formatted into
    // the argument. **Unverified against a real device.**
    uint32_t context = 0;
    if(sscanf(args, "RPC %lx", (unsigned long*)&context) != 1 || context == 0) {
        FURI_LOG_E(TAG, "bridge mode asked for, but no RPC context in %s", args);
        return 1;
    }

    AntliaRpc* app = malloc(sizeof(AntliaRpc));
    memset(app, 0, sizeof(AntliaRpc));
    app->rpc = (RpcAppSystem*)context;
    app->commands = furi_message_queue_alloc(QUEUE_DEPTH, sizeof(AntliaRpcLine));
    app->running = true;

    app->gui = furi_record_open(RECORD_GUI);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, antlia_rpc_draw, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    rpc_system_app_set_callback(app->rpc, antlia_rpc_callback, app);
    rpc_system_app_send_started(app->rpc);

    Nfc* nfc = nfc_alloc();
    MfUltralightData* data = mf_ultralight_alloc();

    // Announced unprompted, immediately after `send_started`. The bridge waits
    // for this rather than treating the launch acknowledgement as readiness — an
    // app that has been loaded is not yet an app that is listening.
    //
    // `rw` because this build has `ndef_build_uri_image` and a write path. A
    // build without one answers `r`, and the bridge hides the write affordance
    // rather than discovering the gap at the moment a user needs it.
    snprintf(app->reply, sizeof(app->reply), "HELLO %d rw", ANTLIA_RPC_PROTOCOL);
    antlia_rpc_send(app, app->reply);

    while(app->running) {
        AntliaRpcLine line;
        // A timeout rather than an infinite wait, so `running` going false from
        // the callback thread is noticed promptly on a session close.
        if(furi_message_queue_get(app->commands, &line, 250) != FuriStatusOk) continue;
        antlia_rpc_dispatch(app, nfc, data, line.text);
    }

    mf_ultralight_free(data);
    nfc_free(nfc);

    rpc_system_app_send_exited(app->rpc);
    rpc_system_app_set_callback(app->rpc, NULL, NULL);

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);

    furi_message_queue_free(app->commands);
    free(app);
    return 0;
}
