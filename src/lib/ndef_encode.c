#include "ndef_encode.h"

#include <string.h>

#define TLV_NDEF       0x03
#define TLV_TERMINATOR 0xFE

// MB | ME | SR, TNF = Well Known. One record, first and last, short payload
// length. Identical to `agent/ndef.py`'s `header = 0xD1`.
#define NDEF_RECORD_HEADER 0xD1
#define NDEF_TYPE_URI      'U'

// The longest payload a short record can carry. Beyond this the record would
// need a 4-byte length and a different header, which Almagest never writes and
// this never produces — a URI that long is a bug upstream, not a bigger record.
#define NDEF_SHORT_RECORD_MAX 0xFE

// Same table as `ndef.c`'s, in the same order, because the two are two halves of
// one codec: a prefix this encoder abbreviates with must be one that decoder
// expands. `https://` is the only one Almagest ever uses (ADR 0001 fixes the
// base URL as `https://almagest.lan`), and the rest are here so a round-trip
// test has something to round-trip.
static const char* const URI_PREFIXES[] = {
    "", // 0x00 no prefix
    "http://www.", // 0x01
    "https://www.", // 0x02
    "http://", // 0x03
    "https://", // 0x04
    "tel:", // 0x05
    "mailto:", // 0x06
};
#define URI_PREFIX_COUNT (sizeof(URI_PREFIXES) / sizeof(URI_PREFIXES[0]))

// The abbreviation code for `uri`, and how many characters it swallows.
//
// **Longest match wins**, which matters: `https://www.x` starts with both
// `https://` (0x04) and `https://www.` (0x02), and picking the shorter one
// would produce a record that decodes correctly but does not match what the
// Python encoder wrote for the same input. Byte-identical output is the
// property `tests/vectors.h` is protecting.
static uint8_t abbreviate(const char* uri, size_t* prefix_length) {
    uint8_t best_code = 0;
    size_t best_length = 0;
    for(size_t code = 1; code < URI_PREFIX_COUNT; code++) {
        size_t length = strlen(URI_PREFIXES[code]);
        if(length > best_length && strncmp(uri, URI_PREFIXES[code], length) == 0) {
            best_code = (uint8_t)code;
            best_length = length;
        }
    }
    *prefix_length = best_length;
    return best_code;
}

// A URI is text. A control byte in one means the caller is about to write
// something that is not a URI onto a tag, and the tag would then read back as
// garbage that this app's own decoder refuses — so refuse it here, where the
// error still has a cause attached.
static bool is_printable(const char* uri, size_t length) {
    for(size_t i = 0; i < length; i++) {
        unsigned char byte = (unsigned char)uri[i];
        if(byte < 0x20 || byte == 0x7F) return false;
    }
    return true;
}

// The unpadded image size for `uri`, or 0 if it cannot be built.
static size_t image_size(const char* uri) {
    if(uri == NULL) return 0;

    size_t uri_length = strlen(uri);
    if(uri_length == 0) return 0;
    if(!is_printable(uri, uri_length)) return 0;

    size_t prefix_length = 0;
    (void)abbreviate(uri, &prefix_length);

    // identifier code + the unabbreviated tail
    size_t payload_length = 1 + (uri_length - prefix_length);
    if(payload_length > NDEF_SHORT_RECORD_MAX) return 0;

    // header + type length + payload length + 'U' + payload
    size_t record_length = 4 + payload_length;

    // A message of 255 bytes or more needs the three-byte TLV length form.
    // Unreachable while the record is capped at a short payload above, and
    // refused rather than silently emitting a form the decoder does not read.
    if(record_length >= 0xFF) return 0;

    // TLV tag + TLV length + record + terminator
    return 2 + record_length + 1;
}

bool ndef_build_uri_image(const char* uri, uint8_t* out, size_t out_size, size_t* written) {
    if(out == NULL || written == NULL) return false;

    size_t body = image_size(uri);
    if(body == 0) return false;

    size_t padding = (NDEF_PAGE_SIZE - (body % NDEF_PAGE_SIZE)) % NDEF_PAGE_SIZE;
    size_t total = body + padding;
    if(total > out_size) return false;

    size_t prefix_length = 0;
    uint8_t code = abbreviate(uri, &prefix_length);
    size_t payload_length = 1 + (strlen(uri) - prefix_length);
    size_t record_length = 4 + payload_length;

    size_t at = 0;
    out[at++] = TLV_NDEF;
    out[at++] = (uint8_t)record_length;
    out[at++] = NDEF_RECORD_HEADER;
    out[at++] = 1; // type length
    out[at++] = (uint8_t)payload_length;
    out[at++] = NDEF_TYPE_URI;
    out[at++] = code;
    memcpy(out + at, uri + prefix_length, payload_length - 1);
    at += payload_length - 1;
    out[at++] = TLV_TERMINATOR;

    memset(out + at, 0x00, padding);
    *written = total;
    return true;
}

bool ndef_uri_page_count(const char* uri, size_t user_pages, size_t* pages) {
    if(pages == NULL) return false;

    size_t body = image_size(uri);
    if(body == 0) return false;

    size_t padding = (NDEF_PAGE_SIZE - (body % NDEF_PAGE_SIZE)) % NDEF_PAGE_SIZE;
    size_t needed = (body + padding) / NDEF_PAGE_SIZE;
    if(needed > user_pages) return false;

    *pages = needed;
    return true;
}
