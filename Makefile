CFLAGS=-Wall -Wextra -std=c99 -pedantic
LFLAGS=-lm -lcrypto

SOURCES=$(shell echo src/*.c)
OBJECTS=$(addprefix obj/,$(notdir $(SOURCES:.c=.o)))

.PHONY: all release clean

release: CFLAGS += -O3 -DNDEBUG
release: all

all: $(OBJECTS)

obj/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm $(OBJECTS)
