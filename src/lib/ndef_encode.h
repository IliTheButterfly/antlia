// Building the one thing Almagest ever writes to a tag.
//
// The inverse of `ndef.h`, and deliberately just as narrow. Almagest writes
// `{base_url}/s/{short_id}` as a single Well Known URI record and nothing else,
// because **nothing mutable ever goes on a tag** — no count, no fill state. A
// tag is a foreign key, not a record. So this builds exactly that shape and
// refuses everything else, rather than being an NDEF writer with a policy on
// top that someone could later route around.
//
// **Antlia used to refuse to write at all**, and the reason was good: "a tag
// written by a device that cannot check the ID against the inventory is a tag
// that might be a duplicate." That objection is about a Flipper acting alone. In
// bridge mode (ADR 0013) the Flipper is a peripheral of a host that *is* talking
// to the inventory, and which minted the short ID being written. The objection
// does not apply, and the wedge mode it was written about still cannot write.
//
// The Python twin is `deviceagent/agent/ndef.py` — `encode_uri_record`,
// `wrap_tlv`, `pages_for_uri`. These two produce identical bytes for identical
// input, which is what `tests/vectors.h` exists to keep true.
//
// Standard library only, so it unit-tests on a host with plain gcc.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A Type 2 Tag page. Fixed by the tag, not a tuning knob.
#define NDEF_PAGE_SIZE 4

// User memory on an NTAG213, the tag PLAN.md specifies: pages 4-39, so 36 pages
// and 144 bytes. NTAG215/216 have more; a caller with a bigger tag says so
// rather than anything guessing on the tag's behalf.
#define NDEF_NTAG213_USER_PAGES 36

// The first user-memory page. Pages 0-2 hold the factory-locked UID, which is
// why a write interrupted halfway degrades a tag rather than destroying it: the
// tag still identifies itself perfectly and only its URI is gone.
#define NDEF_FIRST_USER_PAGE 4

// Room for the largest payload this can produce, rounded to a page.
#define NDEF_ENCODE_MAX 512

// Build the user-memory image for `uri`: a URI record, wrapped in an NDEF TLV,
// terminated, and zero-padded to a page boundary.
//
// Writes to `out` and reports the byte count in `written`. Returns false — and
// touches nothing — when the URI is empty, contains a control byte, or will not
// fit in `out`.
//
// Padded because a Type 2 Tag write is page-atomic: there is no way to write
// three bytes. The padding lands after the terminator TLV, so a reader stops
// before it.
bool ndef_build_uri_image(const char* uri, uint8_t* out, size_t out_size, size_t* written);

// The same image, expressed as the page count a writer must put down starting at
// `NDEF_FIRST_USER_PAGE`.
//
// Separate from the build so a caller can refuse an oversized payload **before
// writing a single page**. That ordering is the whole point: a Type 2 Tag write
// has no transaction, so running off the end of a tag leaves the earlier pages
// committed, and a half-written tag is worse than an unwritten one.
bool ndef_uri_page_count(const char* uri, size_t user_pages, size_t* pages);
