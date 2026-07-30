#include "shortid.h"

#include <ctype.h>
#include <string.h>

// `I`, `L`, `O` and `U` are absent: the first three are confusable with `1` and
// `0` on a label read at arm's length, and dropping `U` keeps the generator from
// producing an obscenity.
static const char SHORTID_ALPHABET[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
#define SHORTID_BASE 32u
// 37 is the smallest prime above the alphabet size, and primality is what makes
// the check exhaustive rather than probabilistic for the two mistakes humans
// actually make: one wrong symbol, and one adjacent transposition.
#define SHORTID_CHECK_MODULUS 37u

// -1 for symbols outside the alphabet.
static int symbol_value(char symbol) {
    const char* found = memchr(SHORTID_ALPHABET, symbol, SHORTID_BASE);
    return found == NULL ? -1 : (int)(found - SHORTID_ALPHABET);
}

static bool is_strippable(char c) {
    // The Python side strips [\s\-_.] — cosmetic separators only.
    return isspace((unsigned char)c) || c == '-' || c == '_' || c == '.';
}

// Upper-case, drop cosmetic separators, fold Crockford's canonical confusions.
// `U` is deliberately *not* folded: it is excluded from the alphabet rather
// than merged into another symbol, so a `U` is an error and not a `V`.
// Returns the number of symbols written, never more than `out_size - 1`.
static size_t squash(const char* text, size_t length, char* out, size_t out_size) {
    size_t written = 0;
    for(size_t i = 0; i < length && written + 1 < out_size; i++) {
        char c = text[i];
        if(is_strippable(c)) continue;
        c = (char)toupper((unsigned char)c);
        if(c == 'O')
            c = '0';
        else if(c == 'I' || c == 'L')
            c = '1';
        out[written++] = c;
    }
    out[written] = '\0';
    return written;
}

unsigned shortid_check_value(const char* data_symbols) {
    unsigned total = 0;
    for(size_t i = 0; i < SHORTID_DATA_SYMBOLS; i++) {
        int value = symbol_value(data_symbols[i]);
        if(value < 0) return SHORTID_CHECK_MODULUS; // never a valid residue
        // Reducing every step is the same residue as reducing once at the end,
        // and it keeps the accumulator far away from overflow.
        total = (total * SHORTID_BASE + (unsigned)value) % SHORTID_CHECK_MODULUS;
    }
    return total;
}

// Enough room to squash a generously long line of input; anything longer than
// this cannot be a short ID anyway and fails the length check either way.
#define SQUASH_BUFFER 64

ShortIdError shortid_validate(const char* raw, char out[SHORTID_SIZE]) {
    if(raw == NULL) return ShortIdErrEmpty;

    // Python's str.strip(): leading and trailing whitespace.
    const char* begin = raw;
    while(*begin != '\0' && isspace((unsigned char)*begin))
        begin++;
    const char* end = begin + strlen(begin);
    while(end > begin && isspace((unsigned char)end[-1]))
        end--;
    if(end == begin) return ShortIdErrEmpty;

    // A display prefix is dropped by keeping the final whitespace-separated
    // token — but only when that token is itself a full-length code. Otherwise
    // the whitespace was being used as the group separator (`4K7T 92M8`) and the
    // whole string is meant. Deterministic either way; nothing is guessed.
    const char* last_token = end;
    while(last_token > begin && !isspace((unsigned char)last_token[-1]))
        last_token--;
    if(last_token > begin) {
        char token_buffer[SQUASH_BUFFER];
        size_t token_length =
            squash(last_token, (size_t)(end - last_token), token_buffer, sizeof(token_buffer));
        if(token_length == SHORTID_TOTAL_SYMBOLS) begin = last_token;
    }

    char text[SQUASH_BUFFER];
    size_t length = squash(begin, (size_t)(end - begin), text, sizeof(text));
    if(length != SHORTID_TOTAL_SYMBOLS) return ShortIdErrLength;

    for(size_t i = 0; i < SHORTID_TOTAL_SYMBOLS; i++) {
        if(symbol_value(text[i]) < 0) return ShortIdErrAlphabet;
    }

    unsigned expected = shortid_check_value(text);
    if(expected >= SHORTID_BASE || SHORTID_ALPHABET[expected] != text[SHORTID_DATA_SYMBOLS]) {
        return ShortIdErrCheck;
    }

    memcpy(out, text, SHORTID_TOTAL_SYMBOLS);
    out[SHORTID_TOTAL_SYMBOLS] = '\0';
    return ShortIdOk;
}

bool shortid_is_valid(const char* raw) {
    char canonical[SHORTID_SIZE];
    return shortid_validate(raw, canonical) == ShortIdOk;
}

void shortid_format_display(const char canonical[SHORTID_SIZE], char out[SHORTID_DISPLAY_SIZE]) {
    memcpy(out, canonical, 4);
    out[4] = '-';
    memcpy(out + 5, canonical + 4, 4);
    out[SHORTID_DISPLAY_SIZE - 1] = '\0';
}

const char* shortid_error_text(ShortIdError error) {
    switch(error) {
    case ShortIdOk:
        return "ok";
    case ShortIdErrEmpty:
        return "empty";
    case ShortIdErrLength:
        return "wrong length";
    case ShortIdErrAlphabet:
        return "bad symbol";
    case ShortIdErrCheck:
        return "check failed";
    default:
        return "invalid";
    }
}
