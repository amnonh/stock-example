SEASTAR ?= ~/seastar
TARGET = stock
module: all

all: init ${TARGET}

init:
	mkdir -p gen

gen/stock.json.hh:
	${SEASTAR}/json/json2code.py -f stock.json --outdir gen

${TARGET}: gen/stock.json.hh
	c++ `pkg-config --cflags --libs ${SEASTAR}/build/release/seastar.pc` main.cc -o ${TARGET}
	
clean:
	rm -rf gen
	rm -f ${TARGET}
.SECONDARY:

.PHONY: all clean
