# Antlia

A Flipper Zero app that reads an [Almagest](https://github.com/IliTheButterfly/almagest)
container tag and types the short ID it carries into the computer the Flipper is
plugged into, as a USB keyboard.

Almagest is a self-hosted electronic-component inventory system. Its bins and
drawers carry NFC tags, and every tag holds one thing: the URL
`https://<host>/s/{short_id}`. A phone reads that with Web NFC; the bench station
reads it with a PN532. A laptop has no NFC reader at all — Antlia is the reader,
and the keyboard is the wire.

```
    tag  ──NFC──▶  Flipper  ──USB HID──▶  4K7T-92M8⏎  into the focused field
```

Nothing about this is Flipper-specific from the application's point of view: it
is a **keyboard wedge**, indistinguishable from a $25 USB barcode scanner or from
someone typing the ID by hand. That is the entire design. It means Antlia needs
no driver, no pairing, no companion app and no network access, and it means
Almagest needs no Antlia-specific code path.

## What it does

- Polls continuously while the scan view is open. The gesture is a tap and
  nothing else — no button press to start a read.
- Decodes the tag's NDEF URI record and extracts the short ID.
- **Verifies the mod-37 check symbol before typing anything.** A misread or
  partially corrupted tag types nothing at all. A wrong-but-plausible ID in an
  inventory system is worse than no ID.
- Types `4K7T-92M8` (or the plain form, or the tag's URL verbatim), optionally
  followed by Enter or Tab.
- Shows the tag UID when a tag carries no usable ID, because the UID is what a
  UID-fallback binding is keyed on — so an unprovisioned tag leaves you with
  something to bind rather than nothing.

Re-tapping the same tag types it again. Leaving it sitting on the reader does
not.

## Install

Grab `antlia.fap` from the
[latest release](https://github.com/IliTheButterfly/antlia/releases) and copy it
to `/ext/apps/NFC/` on the Flipper's SD card, or build it yourself:

```bash
make sdk       # fetch the SDK (Momentum channel — see below)
make build     # build antlia.fap
make launch    # build, upload and run on a connected Flipper
make test      # host-side unit tests, no hardware needed
```

### Which firmware

Built and tested against **Momentum `mntm-012`** (API 87.1). The `make sdk`
target points `ufbt` at Momentum's release channel rather than the official one,
because a FAP built against a mismatched SDK loads and then dies on a missing
symbol. If you run official firmware, drop the `--index-url` from the `sdk`
target; the app uses no Momentum-specific API.

## Limitations worth knowing before you file a bug

**USB only. Bluetooth HID is not possible from an app outside the firmware.**
This is not a design choice — it is the SDK's exported symbol table. In
`targets/f7/api_symbols.csv`:

```
Variable,-,ble_profile_hid,const FuriHalBleProfileTemplate*,
Function,-,ble_profile_hid_kb_press,_Bool,"FuriHalBleProfileBase*, uint16_t"
```

The `-` means not exported to external applications. `bt_profile_start` *is*
exported, but it needs the profile template that is not, and the key-press
functions are not either — so only apps compiled into the firmware (the stock
Remote app) can be a Bluetooth keyboard. `src/antlia_hid.c` is the one file that
would change if a future firmware exports them.

In practice USB is also the better carrier here: no pairing, no dropouts, and
it is already plugged in when you are sitting at the laptop that needed the
reader in the first place.

**Keystrokes assume a US keyboard layout on the host.** The Flipper's
`hid_asciimap` is US-only, so on another layout the digits and the hyphen may
arrive as something else. Short IDs are `0-9` and `A-Z` from a reduced alphabet
plus one cosmetic hyphen, so the exposure is small — but if the hyphen lands
wrong, switch **Types** to `4K7T92M8`, which contains nothing but digits and
letters.

**Only NTAG / MIFARE Ultralight tags are read.** That is what Almagest specifies
(NTAG213), and it is what NDEF is native to. A MIFARE Classic tag reads as *no tag
at all* rather than as a rejected one — worth knowing if you are holding one and
wondering why nothing happens.

Also: claiming the USB HID interface replaces the serial one, so the Flipper's
CLI and qFlipper cannot talk to it *while the scan view is open*. Backing out to
the menu restores serial. The claim is deliberately scoped to the scan session
for exactly this reason.

## Settings

| Setting | Choices | Notes |
|---|---|---|
| Types | `4K7T-92M8`, `4K7T92M8`, `Tag URL` | The URL form is taken **verbatim from the tag**, not rebuilt from a configured hostname — so there is no host to keep in sync and no way to type a URL the tag does not carry. |
| Ends with | None, Enter, Tab | Enter is what a keyboard-wedge input field expects. |
| Auto type | On, Off | Off reads and displays the tag but waits for OK. The safe choice when you are not sure what has focus. |
| Key delay | 5, 15, 30, 60 ms | Raise it if the host drops characters. |

## How it is built

Two of the four source modules are **standard-library-only C with no `furi`
includes at all**:

- `src/lib/shortid.c` — Crockford base32, 7 data symbols + a mod-37 check symbol.
- `src/lib/ndef.c` — enough NDEF to find one URI record, plus the `/s/{code}` rule.

That is what makes `make test` possible: 1100+ assertions run on the host with
plain `gcc`, no Flipper attached, on every push. It matters because this is a
**second implementation** of a codec Almagest already has in Python, and a port
can drift. So the expectations are not hand-written — `tools/gen_vectors.py`
generates `tests/vectors.h` by running every case through the real Python
`idcodec` package and asserting the result before emitting it. Regenerate with
`make vectors` from a checkout that has Almagest's `idcodec/` next door.

The tests also assert the two properties the check symbol exists for, rather than
trusting them: exhaustively, that **no single-symbol change** and **no adjacent
transposition** of the data symbols leaves the check value unchanged. Those are
precisely the two mistakes humans make copying a code by eye.

## Two modes, and only one of them touches HID

**Wedge mode** is everything described above: you open the app, it claims USB
HID, and it types.

**Bridge mode** is entered only when Almagest's device bridge launches the app
over the Flipper's RPC with the argument `RPC` (see Almagest's ADR 0014). The
host asks for reads and writes over a small text protocol and gets both carriers
back — the UID *and* the URI exactly as the tag holds them, rather than the short
ID the wedge types.

The two are disjoint at the entry point: `antlia_app` returns into
`antlia_rpc_run` before the view dispatcher or `AntliaHid` is ever constructed.

**Bridge mode must never claim USB HID**, and this is not a preference. The HID
claim *replaces* the CDC interface — it is already why the wedge scopes its claim
to the scan view, and already why claiming it strands the Flipper's CLI until
someone presses a button. Under RPC the CDC interface **is the session giving
the orders**, so claiming HID would sever the connection mid-command. It costs
nothing to forgo, because HID exists to reach a computer that has no other
channel, and in bridge mode there is one.

### Writing tags

Wedge mode still cannot write, and the original reasoning stands: *a tag written
by a device that cannot check the ID against the inventory is a tag that might be
a duplicate.* That objection is about a Flipper acting alone.

In bridge mode the Flipper is a peripheral of a host that **is** talking to the
inventory and which minted the short ID being written, so it does not apply.
`src/lib/ndef_encode.c` builds the payload and `mf_ultralight_poller_sync_write_page`
puts it down, refusing — before a single page is written — a tag that is not
blank, a payload that will not fit, or a URI that is not one. Every write is
read back through the same reader before it is reported, because a write that
attests to its own success is exactly what Almagest's ADR 0012 refuses.

`tests/vectors.h` now carries the encoder's half too: the exact user-memory image
Almagest's Python encoder produces for the same URI. The two must match **byte
for byte**, which is a nastier kind of drift than the codec's — both sides would
read their own output happily, and nothing would notice until a phone and a
Flipper disagreed about a drawer.

### Not yet run against a Flipper

Bridge mode compiles clean against Momentum `mntm-012` / API 87.1 with `-Werror`,
and the encoder is tested on a host. Nothing has executed on a device. The first
thing to check if it does not start: the app receives its `RpcAppSystem*`
formatted into the launch argument (`RPC <pointer>`), because `rpc_app.h` exports
no way to fetch it and the loader passes only a string — that convention is the
one part of this that could not be verified against the SDK headers.

## Name

Almagest names its repositories after constellations, most of them from
Lacaille's 1756 southern catalogue, whose constellations are all scientific
instruments. **Antlia** is the air pump — the instrument whose whole job is
moving the contents of one vessel into another, which is precisely and only what
this app does.

## Licence

MIT. See [LICENSE](LICENSE).
