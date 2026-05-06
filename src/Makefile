CC      := gcc
CFLAGS  := -Wall -Wextra -Wpedantic -O2
LDFLAGS := -pthread -lrt

# Objetos para el programa Minero
MINER_OBJ   := main.o miner.o pow.o logger.o

# Objetos para el programa Monitor/Comprobador
MONITOR_OBJ := monitor.o pow.o

# Ejecutables a generar
BIN_MINER   := miner
BIN_MONITOR := monitor

all: $(BIN_MINER) $(BIN_MONITOR)

$(BIN_MINER): $(MINER_OBJ)
	$(CC) $(CFLAGS) $(MINER_OBJ) -o $@ $(LDFLAGS)

$(BIN_MONITOR): $(MONITOR_OBJ)
	$(CC) $(CFLAGS) $(MONITOR_OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(BIN_MINER) $(BIN_MONITOR)
	rm -rf log/

.PHONY: all clean