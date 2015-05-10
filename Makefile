SEASTAR = ~/seastar

module: all

all: init main

init:
	mkdir -p gen

gen/stock.json.hh:
	${SEASTAR}/json/json2code.py -f stock.json --outdir gen

main: gen/stock.json.hh
	c++ `pkg-config --cflags --libs ${SEASTAR}/build/release/seastar.pc` main.cc
	
clean:
	rm -rf gen
.SECONDARY:
