CC      := gcc
CFLAGS  := -Wall -Wextra -Wpedantic -O2 -Iinclude
LDFLAGS := -pthread

SRC := src/main.c src/miner.c src/logger.c pow.c
OBJ := $(SRC:.c=.o)
BIN := mrush

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean