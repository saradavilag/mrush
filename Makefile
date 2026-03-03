CC      := gcc
CFLAGS  := -Wall -Wextra -Wpedantic -O2 -Iinclude
LDFLAGS := -pthread

# Agregamos main.c
SRC := src/main.c src/miner.c src/logger.c src/pow.c
OBJ := $(SRC:.c=.o)
BIN := miner

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)
	rm -f log/*.log
	rmdir log 2>/dev/null || true

.PHONY: all clean