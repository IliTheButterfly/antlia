#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    // `4K7T-92M8` — what is printed on the label, so what a human comparing the
    // screen to the bin expects to see.
    AntliaOutputHyphenated,
    // `4K7T92M8` — the canonical stored form, for a field that rejects the
    // hyphen.
    AntliaOutputPlain,
    // The tag's URI verbatim, for an address bar. Taken from the tag rather than
    // rebuilt from a configured host, so there is no hostname to keep in sync
    // and no way for this app to type a URL the tag does not actually carry.
    AntliaOutputTagUrl,
} AntliaOutput;

typedef enum {
    AntliaTerminatorNone,
    AntliaTerminatorEnter,
    AntliaTerminatorTab,
} AntliaTerminator;

typedef struct {
    AntliaOutput output;
    AntliaTerminator terminator;
    // When false, a tag is read and shown but nothing is typed until OK is
    // pressed. The safe choice when the focused field is not yet known.
    bool auto_type;
    // Milliseconds between key events. A host that drops characters wants this
    // higher; 15 ms is reliable everywhere tried.
    uint32_t key_delay_ms;
} AntliaConfig;

void antlia_config_set_defaults(AntliaConfig* config);
void antlia_config_load(AntliaConfig* config);
void antlia_config_save(const AntliaConfig* config);

// Number of choices, and the label for each, for the settings list.
#define ANTLIA_OUTPUT_COUNT     3
#define ANTLIA_TERMINATOR_COUNT 3
#define ANTLIA_KEY_DELAY_COUNT  4

extern const char* const antlia_output_names[ANTLIA_OUTPUT_COUNT];
extern const char* const antlia_terminator_names[ANTLIA_TERMINATOR_COUNT];
extern const char* const antlia_key_delay_names[ANTLIA_KEY_DELAY_COUNT];
extern const uint32_t antlia_key_delay_values[ANTLIA_KEY_DELAY_COUNT];
