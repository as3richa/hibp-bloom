CFLAGS = -Iinclude -Wall -Wextra -Wpedantic -std=c99
LFLAGS = -lm -lcrypto

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

SOURCES = src/hibp-bloom.c
OBJECTS = $(addprefix obj/,$(notdir $(SOURCES:.c=.o)))
LIBRARY = bin/hibp-bloom.a

.PHONY: all lib clean test test-valgrind clean-test

all: lib

lib: $(LIBRARY)

$(LIBRARY): $(OBJECTS)
	ar rcs $@ $?

obj/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean: clean-test
	rm -f $(OBJECTS) $(TEST_BINARIES) $(LIBRARY)

# ========================================
# Tests
# ========================================

TEST_SOURCES=$(shell echo tst/src/test-*.c)
TEST_BINARIES=$(addprefix tst/bin/,$(notdir $(TEST_SOURCES:.c=)))

COMMON_SOURCE=tst/src/common.c
COMMON_OBJECT=tst/obj/common.o

SUPPRESSIONS=valgrind-suppressions.txt
SUPPRESSIONS_SOURCE=src/suppressions.c

test: $(TEST_BINARIES)
	script/run-tests.sh $(TEST_BINARIES)

test-valgrind: $(TEST_BINARIES) $(SUPPRESSIONS)
	VALGRIND=1 SUPPRESSIONS=$(SUPPRESSIONS) script/run-tests.sh $(TEST_BINARIES)

tst/bin/test-%: tst/src/test-%.c $(LIBRARY) $(COMMON_OBJECT)
	$(CC) $(CFLAGS) $(LFLAGS) -o $@ $^

$(COMMON_OBJECT): $(COMMON_SOURCE)
	$(CC) $(CFLAGS) -c -o $@ $<

$(SUPPRESSIONS): $(SUPPRESSIONS_SOURCE)
	script/gen-suppressions-wrapper.sh $(CC) $< > $@ || (rm $@ && false)

clean-test:
	rm -f $(TEST_BINARIES) $(COMMON_OBJECT) $(SUPPRESSIONS)
