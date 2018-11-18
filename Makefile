CFLAGS = -Iinclude -Wall -Wextra -Wpedantic -std=c99
LFLAGS = -lm -lcrypto

# ========================================
# Build config
# ========================================

ifeq ($(BUILD), debug)
CFLAGS += -O0 -g
else ifeq ($(BUILD), debug-valgrind)
CFLAGS += -O0 -g
else ifeq ($(BUILD), debug-asan)
CFLAGS += -O3 -fsanitize=address -fno-omit-frame-pointer -g
else ifeq ($(BUILD), release)
CFLAGS += -O3 -DNDEBUG
else ifndef BUILD
CFLAGS += -O3 -DNDEBUG
else
$(error Bad BUILD: $(BUILD))
endif

# No implicit rules, please
.SUFFIXES:

BINARY_SOURCES = $(shell echo src/bin/*.c)
BINARY_OBJECTS = $(addprefix obj/bin/, $(notdir $(BINARY_SOURCES:.c=.o)))
BINARIES=$(addprefix bin/, $(notdir $(BINARY_SOURCES:.c=)))

LIBRARY_SOURCES = $(shell echo src/*.c)
LIBRARY_OBJECTS = $(addprefix obj/, $(notdir $(LIBRARY_SOURCES:.c=.o)))
LIBRARY=lib/hibp-bloom.a

TEST_SOURCES=$(shell echo tst/src/test-*.c)
TEST_OBJECTS=$(addprefix tst/obj/, $(notdir $(TEST_SOURCES:.c=.o)))
TEST_BINARIES=$(addprefix tst/bin/, $(notdir $(TEST_SOURCES:.c=)))

TEST_UTIL_SOURCE=tst/src/util.c
TEST_UTIL_OBJECT=$(addprefix tst/obj/, $(notdir $(TEST_UTIL_SOURCE:.c=.o)))

VG_SUPPRESSIONS_SOURCE=src/misc/suppressions.c
VG_SUPPRESSIONS_LIST=valgrind-suppressions.txt

ALL_OBJECTS = $(BINARY_OBJECTS) $(LIBRARY_OBJECTS)  $(TEST_OBJECTS)  $(TEST_UTIL_OBJECT) 
ALL_BUILT_FILES = $(ALL_OBJECTS) $(BINARIES) $(LIBRARY) $(TEST_BINARIES) $(VG_SUPPRESSIONS_LIST)

.PHONY: all bin lib test test-valgrind

# Prevent make from nuking our object files between builds
.SECONDARY:

all: bin lib

# ========================================
# Binaries
# ========================================

bin: $(BINARIES)

bin/%: obj/bin/%.o $(LIBRARY)
	$(CC) $(CFLAGS) $(LFLAGS) -o $@ $^

obj/bin/%.o: src/bin/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ========================================
# Static library
# ========================================

lib: $(LIBRARY)

$(LIBRARY): $(LIBRARY_OBJECTS)
	ar rcs $@ $?

obj/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ========================================
# Tests
# ========================================

test: $(TEST_BINARIES)
	script/run-tests.sh $(TEST_BINARIES)

test-valgrind: $(TEST_BINARIES) $(VG_SUPPRESSIONS_LIST)
	VALGRIND=1 SUPPRESSIONS=$(VG_SUPPRESSIONS_LIST) script/run-tests.sh $(TEST_BINARIES)

tst/bin/%: tst/obj/%.o $(LIBRARY) $(TEST_UTIL_OBJECT)
	$(CC) $(CFLAGS) $(LFLAGS) -o $@ $^

tst/obj/%.o: tst/src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(VG_SUPPRESSIONS_LIST): $(VG_SUPPRESSIONS_SOURCE)
	script/gen-suppressions-wrapper.sh $(CC) $< > $@ || (rm $@ && false)

# ========================================
# Clean
# ========================================

clean:
	rm -f $(ALL_BUILT_FILES)
