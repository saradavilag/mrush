CC      := gcc
CFLAGS  := -Wall -Wextra -Wpedantic -O2 -Iinclude
LDFLAGS := -pthread

SRC := src/main.c src/managers.c src/pow.c src/logger.c
OBJ := $(SRC:.c=.o)
BIN := miner

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)
	rm -f /tmp/mrush_miners.txt /tmp/mrush_target.txt /tmp/mrush_votes.txt

.PHONY: all clean