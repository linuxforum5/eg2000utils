# Simple makefile for utils

CC=gcc
SRC=src
BIN=bin
INSTALL_DIR=~/.local/bin

all: cas2tap cmd2tap tap2wav 

cmd2tap: $(SRC)/cmd2tap.c
	$(CC) -o $(BIN)/cmd2tap $(SRC)/cmd2tap.c

cas2tap: $(SRC)/cas2tap.c
	$(CC) -o $(BIN)/cas2tap $(SRC)/cas2tap.c

tap2wav: $(SRC)/tap2wav.c
	$(CC) -o $(BIN)/tap2wav $(SRC)/tap2wav.c

clean:
	rm -f $(BIN)/* *~ $(SRC)/*~ 

install:
	cp $(BIN)/* $(INSTALL_DIR)/
