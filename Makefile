# Antlia — host tests and device builds.
#
# `make test` needs nothing but a C compiler: the codec and the NDEF parser are
# standard-library-only on purpose, so a codec divergence is caught in CI rather
# than on a bench with a tag in hand.
#
# The device targets need `ufbt`, and this firmware is Momentum, not official —
# `make sdk` points ufbt at Momentum's channel. Building against the wrong SDK
# produces a FAP that loads and then fails on a missing symbol.

UFBT ?= ufbt
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O1 -g
SDK_INDEX ?= https://up.momentum-fw.dev/firmware/directory.json
SDK_CHANNEL ?= release

BUILD := build
SOURCES := src/lib/shortid.c src/lib/ndef.c src/lib/ndef_encode.c tests/test_lib.c

.PHONY: all test clean sdk build launch install cli lint format check

all: test

## test: compile and run the host-side unit tests
test: $(BUILD)/test_lib
	$(BUILD)/test_lib

$(BUILD)/test_lib: $(SOURCES) src/lib/shortid.h src/lib/ndef.h src/lib/ndef_encode.h tests/vectors.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

## sdk: fetch the Momentum SDK this firmware actually runs
sdk:
	$(UFBT) update --index-url=$(SDK_INDEX) --channel=$(SDK_CHANNEL)

## build: build the FAP against the Momentum SDK
build:
	$(UFBT)

## launch: build, upload and start the app on a connected Flipper
launch:
	$(UFBT) launch

## install: build and copy the FAP to the Flipper without launching it
install:
	$(UFBT) install

## cli: open the Flipper CLI
cli:
	$(UFBT) cli

## check: everything CI runs
check: test build

## format: apply the Flipper firmware clang-format style
format:
	$(UFBT) format

## lint: check formatting without changing anything
lint:
	$(UFBT) lint

## vectors: regenerate tests/vectors.h from Almagest's Python idcodec + agent.ndef
vectors:
	cd ../deviceagent && uv run python ../antlia/tools/gen_vectors.py > ../antlia/tests/vectors.h

clean:
	rm -rf $(BUILD)
	-$(UFBT) -c
