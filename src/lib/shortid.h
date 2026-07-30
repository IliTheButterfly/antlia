// Crockford base32 short IDs with a mod-37 check symbol.
//
// A faithful C port of the Python `idcodec.shortid` module in the Almagest
// repository. The two must agree symbol for symbol: this app reads a tag the
// API wrote, so a disagreement means a perfectly good tag reads as corrupt (or,
// worse, a corrupt one reads as good and types a wrong ID into an inventory
// system). `tests/vectors.h` is generated from the Python implementation and is
// the contract between them.
//
// Standard library only, no `furi` includes — that is what lets the whole codec
// be unit-tested on a host with plain gcc instead of only on the device.

#pragma once

#include <stdbool.h>
#include <stddef.h>

#define SHORTID_DATA_SYMBOLS  7
#define SHORTID_TOTAL_SYMBOLS 8

// Canonical form is 8 bare symbols plus a NUL.
#define SHORTID_SIZE (SHORTID_TOTAL_SYMBOLS + 1)
// Display form is `4K7T-92M8` — 8 symbols, one hyphen, a NUL.
#define SHORTID_DISPLAY_SIZE (SHORTID_TOTAL_SYMBOLS + 2)

typedef enum {
    ShortIdOk = 0,
    ShortIdErrEmpty,
    ShortIdErrLength,
    ShortIdErrAlphabet,
    ShortIdErrCheck,
} ShortIdError;

// Normalise `raw` and verify its check symbol, writing the canonical 8 symbols
// to `out`. Tolerates the cosmetic hyphen, surrounding whitespace, lower case,
// the O/I/L confusions, and a leading display prefix (`BIN 4K7T-92M8`).
// `out` is left untouched on failure.
ShortIdError shortid_validate(const char* raw, char out[SHORTID_SIZE]);

// True when `raw` is a valid short ID. Convenience over shortid_validate.
bool shortid_is_valid(const char* raw);

// The mod-37 residue of `data_symbols` (which must be SHORTID_DATA_SYMBOLS
// symbols from the alphabet). Exposed for the tests.
unsigned shortid_check_value(const char* data_symbols);

// Render canonical 8 symbols as `4K7T-92M8`.
void shortid_format_display(const char canonical[SHORTID_SIZE], char out[SHORTID_DISPLAY_SIZE]);

// A short human-readable reason, for the on-screen error state.
const char* shortid_error_text(ShortIdError error);
