CC      ?= gcc
CFLAGS  ?= -std=gnu99 -Wall -Wextra -O2
SRC     := $(wildcard src/*.c)
BIN     := exam_system

all: $(BIN)

$(BIN): $(SRC) src/common.h
	$(CC) $(CFLAGS) -o $@ $(SRC)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)

# wipes every saved record (the data/ folder is re-created with sample data on next start)
reset:
	rm -rf data

.PHONY: all run clean reset
