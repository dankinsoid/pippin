# @ai-generated(solo)
# The parser fuzzers of design §3 «Корректность реализации» item 5 (docs/notes/fuzzing.md, "Parser fuzzing").
# Included by the Makefile. CljCore is compiled here, not by SwiftPM, which has no flag for coverage instrumentation.

.PHONY: fuzz-parsers fuzz-parsers-build

FUZZP = $(BUILD_ROOT)/fuzz-parsers
FUZZP_CC = xcrun clang
FUZZP_CXX = xcrun clang++
FUZZP_TARGETS = reader regex number format
# Seconds per target, a nightly budget; a check run passes about 30 min in all.
FUZZ_PARSERS_TIME ?= reader=1200 regex=1200 number=600 format=600

# Xcode's clang takes -fsanitize=fuzzer-no-link but ships no libFuzzer runtime, so it is built from this release.
LIBFUZZER_LLVM = 21.1.8
LIBFUZZER_SHA256 = dd54ae21aee1780fac59445b51ebff601ad016b31ac3a7de3b21126fd3ccb229
LIBFUZZER_URL = https://github.com/llvm/llvm-project/releases/download/llvmorg-$(LIBFUZZER_LLVM)/compiler-rt-$(LIBFUZZER_LLVM).src.tar.xz
FUZZP_LIBFUZZER = $(FUZZP)/libfuzzer/libFuzzer.a

FUZZP_SAN = -fsanitize=address,undefined -fno-sanitize-recover=undefined
FUZZP_CFLAGS = -std=c17 -O1 -g -fno-omit-frame-pointer -ffp-contract=off -DCLJ_DEBUG=1 \
	-Wall -Wextra -Wpedantic -Werror $(FUZZP_SAN) -fsanitize=fuzzer-no-link \
	-ISources/CljCore -ISources/CljCore/include -MMD -MP
FUZZP_CORE_OBJ = $(patsubst Sources/CljCore/%.c,$(FUZZP)/core/%.o,$(wildcard Sources/CljCore/*.c Sources/CljCore/boot/*.c))
FUZZP_BINS = $(FUZZP_TARGETS:%=$(FUZZP)/bin/fuzz-%)

fuzz-parsers-build: $(FUZZP_BINS)
.SECONDARY: $(FUZZP_CORE_OBJ) $(FUZZP_TARGETS:%=$(FUZZP)/harness/%.o) $(FUZZP)/harness/fuzz.o

# Opt-in, for nights or by hand: in neither gates nor gates-full. A finding fails the target after every one has run.
fuzz-parsers: fuzz-parsers-build
	sh fuzz/parsers/run.sh $(FUZZP) $(FUZZ_PARSERS_TIME)

$(FUZZP)/core/%.o: Sources/CljCore/%.c
	@mkdir -p $(@D)
	$(FUZZP_CC) $(FUZZP_CFLAGS) -c $< -o $@

$(FUZZP)/harness/%.o: fuzz/parsers/%.c
	@mkdir -p $(@D)
	$(FUZZP_CC) $(FUZZP_CFLAGS) -c $< -o $@

$(FUZZP)/bin/fuzz-%: $(FUZZP)/harness/%.o $(FUZZP)/harness/fuzz.o $(FUZZP_CORE_OBJ) $(FUZZP_LIBFUZZER)
	@mkdir -p $(@D)
	$(FUZZP_CXX) $(FUZZP_SAN) $^ -framework CoreFoundation -framework Foundation -lobjc -o $@

$(FUZZP_LIBFUZZER):
	sh fuzz/parsers/libfuzzer.sh $(LIBFUZZER_URL) $(LIBFUZZER_SHA256) $(@D) "$(FUZZP_CXX)"

-include $(FUZZP_CORE_OBJ:.o=.d) $(FUZZP_TARGETS:%=$(FUZZP)/harness/%.d) $(FUZZP)/harness/fuzz.d
