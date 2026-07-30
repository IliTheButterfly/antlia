#include "ndef.h"

#include <ctype.h>
#include <string.h>

// NFCForum-TS-Type-2-Tag: the user memory is a TLV block sequence.
#define TLV_NULL       0x00
#define TLV_NDEF       0x03
#define TLV_TERMINATOR 0xFE

// NDEF record header bits.
#define NDEF_FLAG_SR  0x10 // short record: 1-byte payload length
#define NDEF_FLAG_IL  0x08 // ID length field present
#define NDEF_TNF_MASK 0x07
#define NDEF_TNF_WELL_KNOWN 0x01

// A path segment longer than this cannot be a short ID; bounding it keeps the
// copy below on the stack and off the heap.
#define CODE_MAX 64

// NFCForum-TS-RTD_URI abbreviation table. Index 0 means "no prefix, the payload
// is the whole URI". Only the entries that can plausibly appear on a tag this
// app reads are spelled out; the rest resolve to NULL and the record is refused
// rather than guessed at.
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

// Expand one URI record payload into `out`. `payload` starts at the URI
// identifier code byte.
static bool
    expand_uri(const uint8_t* payload, size_t payload_size, char* out, size_t out_size) {
    if(payload_size < 1) return false;

    uint8_t code = payload[0];
    if(code >= URI_PREFIX_COUNT) return false;
    const char* prefix = URI_PREFIXES[code];

    size_t prefix_length = strlen(prefix);
    size_t body_length = payload_size - 1;
    if(prefix_length + body_length + 1 > out_size) return false;

    memcpy(out, prefix, prefix_length);
    memcpy(out + prefix_length, payload + 1, body_length);
    out[prefix_length + body_length] = '\0';

    // A URI is text; a NUL or control byte inside one means the tag is not
    // carrying what it claims to.
    for(size_t i = 0; i < prefix_length + body_length; i++) {
        if((unsigned char)out[i] < 0x20 || (unsigned char)out[i] == 0x7F) return false;
    }
    return true;
}

// Walk the records of one NDEF message, stopping at the first URI record.
static bool find_uri_in_message(
    const uint8_t* message,
    size_t message_size,
    char* out,
    size_t out_size) {
    size_t offset = 0;
    while(offset < message_size) {
        uint8_t header = message[offset++];
        if(offset >= message_size) return false;

        size_t type_length = message[offset++];

        size_t payload_length = 0;
        if(header & NDEF_FLAG_SR) {
            if(offset >= message_size) return false;
            payload_length = message[offset++];
        } else {
            if(offset + 4 > message_size) return false;
            payload_length = ((size_t)message[offset] << 24) | ((size_t)message[offset + 1] << 16) |
                             ((size_t)message[offset + 2] << 8) | (size_t)message[offset + 3];
            offset += 4;
        }

        size_t id_length = 0;
        if(header & NDEF_FLAG_IL) {
            if(offset >= message_size) return false;
            id_length = message[offset++];
        }

        if(offset + type_length > message_size) return false;
        const uint8_t* type = message + offset;
        offset += type_length;

        offset += id_length;
        if(offset > message_size) return false;

        if(offset + payload_length > message_size) return false;
        const uint8_t* payload = message + offset;
        offset += payload_length;

        bool is_uri_record = (header & NDEF_TNF_MASK) == NDEF_TNF_WELL_KNOWN &&
                             type_length == 1 && type[0] == 'U';
        if(is_uri_record) {
            // A chunked record (CF set) would need reassembly. Almagest never
            // writes one, and guessing at a partial URI is worse than refusing.
            return expand_uri(payload, payload_length, out, out_size);
        }
    }
    return false;
}

bool ndef_find_uri(const uint8_t* data, size_t size, char* out, size_t out_size) {
    if(data == NULL || out == NULL || out_size == 0) return false;

    size_t offset = 0;
    while(offset < size) {
        uint8_t tag = data[offset++];

        if(tag == TLV_NULL) continue;
        if(tag == TLV_TERMINATOR) return false;

        if(offset >= size) return false;
        size_t length = data[offset++];
        if(length == 0xFF) {
            // Three-byte length form: 0xFF then a 16-bit big-endian length.
            if(offset + 2 > size) return false;
            length = ((size_t)data[offset] << 8) | (size_t)data[offset + 1];
            offset += 2;
        }

        if(offset + length > size) return false;

        if(tag == TLV_NDEF) {
            if(find_uri_in_message(data + offset, length, out, out_size)) return true;
        }
        offset += length;
    }
    return false;
}

bool ndef_short_id_from_uri(const char* uri, char out[SHORTID_SIZE]) {
    if(uri == NULL) return false;

    // Trim, mirroring the Python side's `url.strip()`.
    const char* begin = uri;
    while(*begin != '\0' && isspace((unsigned char)*begin))
        begin++;
    size_t length = strlen(begin);
    while(length > 0 && isspace((unsigned char)begin[length - 1]))
        length--;

    // The rule is `/s/{code}` with an optional single trailing slash, anchored
    // at the end of the URI.
    if(length > 0 && begin[length - 1] == '/') length--;
    if(length == 0) return false;

    // Walk back to the slash that opens the final path segment.
    size_t code_start = length;
    while(code_start > 0 && begin[code_start - 1] != '/')
        code_start--;
    if(code_start == 0) return false;

    size_t code_length = length - code_start;
    if(code_length == 0 || code_length >= CODE_MAX) return false;

    // The segment before it must be exactly `/s`.
    size_t prefix_length = code_start - 1; // drop the slash we stopped on
    if(prefix_length < 2) return false;
    if(begin[prefix_length - 1] != 's' || begin[prefix_length - 2] != '/') return false;

    // `[^/?#]+` — a query or fragment means the URI does not end in the code.
    for(size_t i = 0; i < code_length; i++) {
        char c = begin[code_start + i];
        if(c == '?' || c == '#' || c == '/') return false;
    }

    char code[CODE_MAX];
    memcpy(code, begin + code_start, code_length);
    code[code_length] = '\0';

    return shortid_validate(code, out) == ShortIdOk;
}

bool ndef_short_id_from_memory(const uint8_t* data, size_t size, char out[SHORTID_SIZE]) {
    char uri[NDEF_URI_MAX];
    if(!ndef_find_uri(data, size, uri, sizeof(uri))) return false;
    return ndef_short_id_from_uri(uri, out);
}
