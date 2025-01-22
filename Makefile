CC = clang

SRC_DIR = src
BIN_DIR = bin

SRV_SRC = $(SRC_DIR)/srv.c
CLI_SRC = $(SRC_DIR)/cli.c

SRV_BIN = $(BIN_DIR)/server
CLI_BIN = $(BIN_DIR)/client

.PHONY: all clean

all: $(SRV_BIN) $(CLI_BIN)

$(SRV_BIN): $(SRV_SRC)
	$(CC) -o $@ $<

$(CLI_BIN): $(CLI_SRC)
	$(CC) -o $@ $<

clean:
	rm -rf $(BIN_DIR)/*

