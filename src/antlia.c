// Antlia — entry point, view wiring, settings and about.

#include "antlia_i.h"
#include "antlia_rpc.h"

#include <dolphin/dolphin.h>

typedef enum {
    MenuIndexScan,
    MenuIndexTest,
    MenuIndexSettings,
    MenuIndexAbout,
} MenuIndex;

static void menu_callback(void* context, uint32_t index) {
    Antlia* app = context;
    switch(index) {
    case MenuIndexScan:
        view_dispatcher_switch_to_view(app->view_dispatcher, AntliaViewScan);
        break;
    case MenuIndexTest:
        view_dispatcher_switch_to_view(app->view_dispatcher, AntliaViewTest);
        break;
    case MenuIndexSettings:
        view_dispatcher_switch_to_view(app->view_dispatcher, AntliaViewSettings);
        break;
    case MenuIndexAbout:
        view_dispatcher_switch_to_view(app->view_dispatcher, AntliaViewAbout);
        break;
    default:
        break;
    }
}

static uint32_t navigate_to_menu(void* context) {
    UNUSED(context);
    return AntliaViewMenu;
}

static uint32_t navigate_to_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

// --- settings ---------------------------------------------------------------

static void output_changed(VariableItem* item) {
    Antlia* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->config.output = (AntliaOutput)index;
    variable_item_set_current_value_text(item, antlia_output_names[index]);
}

static void terminator_changed(VariableItem* item) {
    Antlia* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->config.terminator = (AntliaTerminator)index;
    variable_item_set_current_value_text(item, antlia_terminator_names[index]);
}

static void auto_type_changed(VariableItem* item) {
    Antlia* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->config.auto_type = index == 1;
    variable_item_set_current_value_text(item, index == 1 ? "On" : "Off");
}

static void key_delay_changed(VariableItem* item) {
    Antlia* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->config.key_delay_ms = antlia_key_delay_values[index];
    variable_item_set_current_value_text(item, antlia_key_delay_names[index]);
}

void antlia_settings_build(Antlia* app) {
    VariableItemList* list = app->settings_list;
    VariableItem* item;

    item = variable_item_list_add(list, "Types", ANTLIA_OUTPUT_COUNT, output_changed, app);
    variable_item_set_current_value_index(item, (uint8_t)app->config.output);
    variable_item_set_current_value_text(item, antlia_output_names[app->config.output]);

    item = variable_item_list_add(
        list, "Ends with", ANTLIA_TERMINATOR_COUNT, terminator_changed, app);
    variable_item_set_current_value_index(item, (uint8_t)app->config.terminator);
    variable_item_set_current_value_text(item, antlia_terminator_names[app->config.terminator]);

    item = variable_item_list_add(list, "Auto type", 2, auto_type_changed, app);
    variable_item_set_current_value_index(item, app->config.auto_type ? 1 : 0);
    variable_item_set_current_value_text(item, app->config.auto_type ? "On" : "Off");

    uint8_t delay_index = 1;
    for(uint8_t i = 0; i < ANTLIA_KEY_DELAY_COUNT; i++) {
        if(antlia_key_delay_values[i] == app->config.key_delay_ms) delay_index = i;
    }
    item =
        variable_item_list_add(list, "Key delay", ANTLIA_KEY_DELAY_COUNT, key_delay_changed, app);
    variable_item_set_current_value_index(item, delay_index);
    variable_item_set_current_value_text(item, antlia_key_delay_names[delay_index]);
}

// --- lifecycle --------------------------------------------------------------

static Antlia* antlia_alloc(void) {
    Antlia* app = malloc(sizeof(Antlia));
    memset(app, 0, sizeof(Antlia));

    antlia_config_load(&app->config);
    antlia_hid_init(&app->hid);

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->menu = submenu_alloc();
    submenu_add_item(app->menu, "Scan a tag", MenuIndexScan, menu_callback, app);
    submenu_add_item(app->menu, "Test keyboard", MenuIndexTest, menu_callback, app);
    submenu_add_item(app->menu, "Settings", MenuIndexSettings, menu_callback, app);
    submenu_add_item(app->menu, "About", MenuIndexAbout, menu_callback, app);
    view_set_previous_callback(submenu_get_view(app->menu), navigate_to_exit);
    view_dispatcher_add_view(app->view_dispatcher, AntliaViewMenu, submenu_get_view(app->menu));

    app->scan_view = antlia_scan_view_alloc(app);
    view_set_previous_callback(app->scan_view, navigate_to_menu);
    view_dispatcher_add_view(app->view_dispatcher, AntliaViewScan, app->scan_view);

    app->test_view = antlia_test_view_alloc(app);
    view_set_previous_callback(app->test_view, navigate_to_menu);
    view_dispatcher_add_view(app->view_dispatcher, AntliaViewTest, app->test_view);

    app->settings_list = variable_item_list_alloc();
    antlia_settings_build(app);
    view_set_previous_callback(variable_item_list_get_view(app->settings_list), navigate_to_menu);
    view_dispatcher_add_view(
        app->view_dispatcher, AntliaViewSettings, variable_item_list_get_view(app->settings_list));

    app->about = widget_alloc();
    widget_add_text_scroll_element(
        app->about,
        0,
        0,
        128,
        64,
        "\e#Antlia\e#\n"
        "Reads an Almagest container tag\n"
        "and types its short ID into the\n"
        "computer this Flipper is plugged\n"
        "into, as a USB keyboard.\n"
        "\n"
        "USB only. Bluetooth HID is not\n"
        "reachable from an app outside the\n"
        "firmware on this build.\n"
        "\n"
        "Keystrokes assume a US keyboard\n"
        "layout on the host.\n"
        "\n"
        "The check symbol is verified\n"
        "before anything is typed, so a\n"
        "misread tag types nothing.\n");
    view_set_previous_callback(widget_get_view(app->about), navigate_to_menu);
    view_dispatcher_add_view(app->view_dispatcher, AntliaViewAbout, widget_get_view(app->about));

    return app;
}

static void antlia_free(Antlia* app) {
    antlia_config_save(&app->config);

    view_dispatcher_remove_view(app->view_dispatcher, AntliaViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, AntliaViewScan);
    view_dispatcher_remove_view(app->view_dispatcher, AntliaViewTest);
    view_dispatcher_remove_view(app->view_dispatcher, AntliaViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, AntliaViewAbout);

    submenu_free(app->menu);
    antlia_scan_view_free(app->scan_view);
    antlia_test_view_free(app->test_view);
    variable_item_list_free(app->settings_list);
    widget_free(app->about);

    view_dispatcher_free(app->view_dispatcher);

    // Belt and braces: the scan view releases HID on exit, but a crash-free exit
    // from any other path must not leave the host with a keyboard and no serial.
    antlia_hid_release(&app->hid);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t antlia_app(void* p) {
    // **Bridge mode returns before anything else is allocated**, and that is the
    // point. The wedge's whole apparatus — the view dispatcher, the scan view,
    // and above all `AntliaHid` — is never constructed under RPC, so there is no
    // path by which a host-driven session could claim USB HID and sever the CDC
    // interface it is being driven over. Making the two modes disjoint at the
    // entry point is cheaper to keep true than a flag checked in five places.
    //
    // See `antlia_rpc.h` and ADR 0013.
    const char* args = p;
    if(antlia_rpc_wanted(args)) {
        return antlia_rpc_run(args);
    }

    Antlia* app = antlia_alloc();

    dolphin_deed(DolphinDeedNfcRead);
    view_dispatcher_switch_to_view(app->view_dispatcher, AntliaViewMenu);
    view_dispatcher_run(app->view_dispatcher);

    antlia_free(app);
    return 0;
}
