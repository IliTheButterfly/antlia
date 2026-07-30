#include "antlia_config.h"

#include <furi.h>
#include <saved_struct.h>
#include <storage/storage.h>

#define CONFIG_PATH    APP_DATA_PATH("antlia.settings")
#define CONFIG_VERSION 1
#define CONFIG_MAGIC   0x41 // 'A'

const char* const antlia_output_names[ANTLIA_OUTPUT_COUNT] = {
    "4K7T-92M8",
    "4K7T92M8",
    "Tag URL",
};

const char* const antlia_terminator_names[ANTLIA_TERMINATOR_COUNT] = {
    "None",
    "Enter",
    "Tab",
};

const char* const antlia_key_delay_names[ANTLIA_KEY_DELAY_COUNT] = {
    "5 ms",
    "15 ms",
    "30 ms",
    "60 ms",
};

const uint32_t antlia_key_delay_values[ANTLIA_KEY_DELAY_COUNT] = {5, 15, 30, 60};

void antlia_config_set_defaults(AntliaConfig* config) {
    config->output = AntliaOutputHyphenated;
    config->terminator = AntliaTerminatorEnter;
    config->auto_type = true;
    config->key_delay_ms = 15;
}

// A settings file from a future version, or a corrupt one, falls back to the
// defaults rather than refusing to start: this app is a convenience, and a
// broken preferences file must never be the reason a bin cannot be scanned.
void antlia_config_load(AntliaConfig* config) {
    antlia_config_set_defaults(config);

    AntliaConfig loaded;
    if(saved_struct_load(
           CONFIG_PATH, &loaded, sizeof(AntliaConfig), CONFIG_MAGIC, CONFIG_VERSION)) {
        // Clamp rather than trust: the file is on removable storage.
        if(loaded.output < ANTLIA_OUTPUT_COUNT) config->output = loaded.output;
        if(loaded.terminator < ANTLIA_TERMINATOR_COUNT) config->terminator = loaded.terminator;
        config->auto_type = loaded.auto_type;
        for(size_t i = 0; i < ANTLIA_KEY_DELAY_COUNT; i++) {
            if(antlia_key_delay_values[i] == loaded.key_delay_ms) {
                config->key_delay_ms = loaded.key_delay_ms;
                break;
            }
        }
    }
}

void antlia_config_save(const AntliaConfig* config) {
    // The app data directory does not exist until something creates it, and
    // saved_struct_save will not create it for us.
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, APP_DATA_PATH(""));
    furi_record_close(RECORD_STORAGE);

    if(!saved_struct_save(
           CONFIG_PATH, config, sizeof(AntliaConfig), CONFIG_MAGIC, CONFIG_VERSION)) {
        FURI_LOG_W("Antlia", "could not save settings");
    }
}
