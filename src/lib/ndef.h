// Enough NDEF to find one URI record, and the Almagest rule for turning that
// URI into a short ID.
//
// Deliberately not a general NDEF library. Almagest writes exactly one thing to
// a tag — `{base_url}/s/{short_id}` as a Well Known URI record — because
// **nothing mutable ever goes on a tag**: no count, no fill state. A tag is a
// foreign key, not a record. So the only question worth asking a tag is "which
// short ID is this", and everything here exists to answer it and refuse
// anything else.
//
// Standard library only, so it unit-tests on a host with plain gcc.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shortid.h"

// NTAG213 holds 144 bytes of user memory; the URL is far shorter, but accept a
// full NTAG216 payload so a bigger tag is not silently truncated.
#define NDEF_URI_MAX 512

// Scan `data` (the tag's user memory, starting at the first byte of page 4) for
// the first NDEF URI record and write the fully expanded URI to `out`.
// Returns false when there is no well-formed URI record.
bool ndef_find_uri(const uint8_t* data, size_t size, char* out, size_t out_size);

// The short ID carried by an Almagest URI, check symbol verified.
//
// Matched **host-agnostically**: the host may legitimately change (a rename, a
// reverse proxy, a new lab hostname) while every tag already written keeps the
// old one, and the meaning of the payload was never the host — it is the short
// ID. Refusing a tag because it names the previous hostname would be refusing a
// tag that is perfectly correct.
bool ndef_short_id_from_uri(const char* uri, char out[SHORTID_SIZE]);

// The two steps together: tag memory in, canonical short ID out.
bool ndef_short_id_from_memory(const uint8_t* data, size_t size, char out[SHORTID_SIZE]);
