"""Generate `tests/vectors.h` from Almagest's Python `idcodec` package.

The C codec in `src/lib/shortid.c` is a port, and a port can drift. When it does,
the failure is nasty and quiet: a tag the API wrote reads as corrupt on the
Flipper, or — far worse — a corrupted code passes the check symbol on one side
and not the other, and a wrong ID gets typed into an inventory system. So the
expectations are not written by hand here; every case is run through the real
Python implementation and asserted before it is emitted.

Since ADR 0013 it also covers the *encoder*: Antlia can write a tag in bridge
mode, so `src/lib/ndef_encode.c` and `deviceagent/agent/ndef.py` are two
implementations that must emit **byte-identical** user memory for the same URI.
That is a nastier drift than the codec's, because both sides would still read
their own output happily — the tag would simply be one the other half of the
system writes differently, and nothing would notice until a phone and a Flipper
disagreed about a drawer.

Run from the `deviceagent` directory, whose venv has both `idcodec` (a path
dependency) and `agent`:

    make vectors

which is `cd ../deviceagent && uv run python ../antlia/tools/gen_vectors.py`.
"""

from __future__ import annotations

import random

from agent import ndef
from idcodec.shortid import ALPHABET, InvalidShortId, generate, validate
from idcodec.tagpayload import parse_ndef_url

# Fixed so the generated header is stable and a regeneration produces no diff
# unless the codec's behaviour actually changed.
SEED = 20260730
VALID_COUNT = 96


def c_string(text: str) -> str:
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def c_optional_string(text: str | None) -> str:
    return "NULL" if text is None else c_string(text)


def short_id_cases(valid: list[str], rng: random.Random) -> list[tuple[str, str | None]]:
    cases: list[tuple[str, str | None]] = []

    for code in valid:
        cases.append((code, code))
        cases.append((f"{code[:4]}-{code[4:]}", code))
        cases.append((code.lower(), code))

    # The O/I/L confusions are folded; U is not, because it is excluded from the
    # alphabet rather than merged into another symbol.
    for code in valid[:20]:
        swapped = code.replace("0", "O", 1).replace("1", "I", 1)
        if swapped != code:
            cases.append((swapped, code))

    # A cosmetic display prefix is tolerated, not parsed.
    for code in valid[:10]:
        cases.append((f"BIN {code[:4]}-{code[4:]}", code))

    # Whitespace used as the group separator means the whole string.
    for code in valid[:10]:
        cases.append((f"{code[:4]} {code[4:]}", code))

    # The check symbol itself is wrong.
    for code in valid[:30]:
        data, check = code[:7], code[7]
        cases.append((data + next(a for a in ALPHABET if a != check), None))

    # One data symbol is wrong — the first error class mod 37 must catch.
    for code in valid[:30]:
        position = rng.randrange(7)
        replacement = next(a for a in ALPHABET if a != code[position])
        cases.append((code[:position] + replacement + code[position + 1 :], None))

    # Two adjacent symbols are swapped — the second error class.
    for code in valid[:30]:
        for position in range(6):
            if code[position] != code[position + 1]:
                cases.append(
                    (
                        code[:position]
                        + code[position + 1]
                        + code[position]
                        + code[position + 2 :],
                        None,
                    )
                )
                break

    # Malformed shapes.
    cases.extend(
        (bad, None)
        for bad in ("", "   ", "4K7T", "4K7T-92M88", "4K7T-92MU", "4K7T-92M!", "ZZZZZZZZZ")
    )
    return cases


def uri_cases(valid: list[str]) -> list[str]:
    urls: list[str] = []
    for code in valid[:12]:
        grouped = f"{code[:4]}-{code[4:]}"
        # The host is deliberately varied: the rule is host-agnostic, because a
        # rename must not invalidate every tag already written.
        urls.append(f"https://almagest.lan/s/{grouped}")
        urls.append(f"https://almagest.lan/s/{code}")
        urls.append(f"http://10.0.0.5:8000/s/{grouped}")
        urls.append(f"https://almagest.lan/s/{grouped}/")
        urls.append(f"https://other-host.example/s/{code.lower()}")
    urls.extend(
        [
            "https://almagest.lan/",
            "https://almagest.lan/s/",
            "https://almagest.lan/s/NOTACODE",
            "https://almagest.lan/parts/4K7T-92M8",
            "https://almagest.lan/s/4K7T-92M8/extra",
            "tel:+15551234",
            "",
        ]
    )
    return urls


#: URIs whose encoded image both implementations must agree on, byte for byte.
#: `https://almagest.lan/...` is the real one (ADR 0001); the rest exercise the
#: abbreviation table, including the longest-match case that a naive encoder gets
#: wrong — `https://www.` and `https://` both match, and picking the shorter
#: produces a record that still *decodes* correctly and is not the same bytes.
def encode_cases(valid: list[str]) -> list[str]:
    cases = [f"https://almagest.lan/s/{code}" for code in valid[:12]]
    cases += [
        f"https://www.almagest.lan/s/{valid[0]}",
        f"http://almagest.lan/s/{valid[1]}",
        f"http://www.almagest.lan/s/{valid[2]}",
        # No abbreviation applies, so the identifier code is 0x00 and the whole
        # URI travels verbatim.
        f"almagest://s/{valid[3]}",
        # A long host, to push the image over one page boundary and then another.
        f"https://almagest.internal.example.invalid/s/{valid[4]}",
    ]
    return cases


def c_bytes(payload: bytes) -> str:
    return "{" + ", ".join(f"0x{byte:02X}" for byte in payload) + "}"


def main() -> None:
    rng = random.Random(SEED)
    valid = [generate(rng.getrandbits) for _ in range(VALID_COUNT)]

    lines = [
        "// GENERATED by tools/gen_vectors.py from the Python `idcodec` package.",
        "// Do not edit by hand — run `make vectors`.",
        "//",
        "// These vectors are the contract between the two implementations. The C codec",
        "// must agree with the Python one symbol for symbol, or a tag the API wrote is",
        "// unreadable by the Flipper.",
        "#pragma once",
        "",
        "typedef struct {",
        "    const char* raw;",
        "    const char* canonical; // NULL when the input must be rejected",
        "} ShortIdVector;",
        "",
        "static const ShortIdVector shortid_vectors[] = {",
    ]

    seen: set[str] = set()
    for raw, expected in short_id_cases(valid, rng):
        if raw in seen:
            continue
        seen.add(raw)
        try:
            actual: str | None = validate(raw)
        except InvalidShortId:
            actual = None
        assert actual == expected, f"vector disagrees with idcodec: {raw!r} {expected!r} {actual!r}"
        lines.append(f"    {{{c_string(raw)}, {c_optional_string(expected)}}},")

    lines += [
        "};",
        "",
        "typedef struct {",
        "    const char* url;",
        "    const char* short_id; // NULL when the URL carries no usable short id",
        "} NdefUrlVector;",
        "",
        "static const NdefUrlVector ndef_url_vectors[] = {",
    ]

    for url in uri_cases(valid):
        lines.append(f"    {{{c_string(url)}, {c_optional_string(parse_ndef_url(url))}}},")

    lines += [
        "};",
        "",
        "// The encoder's half: the exact user-memory image `agent/ndef.py` produces,",
        "// padded to a page boundary. `src/lib/ndef_encode.c` must match byte for byte.",
        "typedef struct {",
        "    const char* uri;",
        "    unsigned char image[128];",
        "    unsigned size;",
        "    unsigned pages;",
        "} NdefEncodeVector;",
        "",
        "static const NdefEncodeVector ndef_encode_vectors[] = {",
    ]

    for uri in encode_cases(valid):
        pages = ndef.pages_for_uri(uri)
        image = b"".join(pages)
        assert len(image) % ndef.PAGE_SIZE == 0, uri
        # Round-tripped through the real parser before it is emitted, so a
        # vector can never bless an image that Almagest itself cannot read.
        assert ndef.parse_uri_record(ndef.collect_ndef_bytes(_pager(pages))) == uri, uri
        assert len(image) <= 128, f"{uri} needs a bigger vector buffer"
        lines.append(
            f"    {{{c_string(uri)}, {c_bytes(image)}, {len(image)}, {len(pages)}}},"
        )

    # No trailing blank line: clang-format strips one, and `ufbt format` runs over
    # this directory — so emitting one would make the generated file differ from a
    # freshly generated one the moment anybody formatted the tree.
    lines.append("};")
    print("\n".join(lines))


def _pager(pages: list[bytes]):
    """`collect_ndef_bytes`'s `read_page`, over the pages just built."""

    def read_page(page: int) -> bytes | None:
        index = page - ndef.FIRST_USER_PAGE
        if index < 0 or index >= len(pages):
            return None
        return pages[index]

    return read_page


if __name__ == "__main__":
    main()
