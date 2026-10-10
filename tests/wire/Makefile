# tests/wire/Makefile -- shared follower primitives.
#
# No Rack link and no SDK needed. Lives::wireFresh and LapCounter were each
# split into dependency-free headers precisely so they could be covered here
# rather than only inside a running Rack.
#
#   make run    build + run the suites (ASan/UBSan)
#   make clean

REPO := $(dir $(lastword $(MAKEFILE_LIST)))../..
CXX  ?= c++
FLAGS := -std=c++11 -Wall -Wextra -I$(REPO)/src \
         -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1

wire_test: wire_test.cpp $(REPO)/src/lives/WireFresh.hpp
	$(CXX) $(FLAGS) -o $@ wire_test.cpp

lap_test: lap_test.cpp $(REPO)/src/lives/LapCounter.hpp
	$(CXX) $(FLAGS) -o $@ lap_test.cpp

run: wire_test lap_test
	./wire_test
	./lap_test

clean:
	rm -rf wire_test lap_test *.dSYM

.PHONY: run clean
