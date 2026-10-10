# tests/expr/Makefile -- the EXPR compiler and runtime, headless.
#
# No Rack link and no SDK needed: src/expr/*.hpp are dependency-free so the
# language can be covered here, in every process order of the semantic
# corpus from the design (issue #10 section 13), rather than only by ear
# inside a running Rack.
#
#   make run    build + run under ASan/UBSan
#   make clean

REPO := $(dir $(lastword $(MAKEFILE_LIST)))../..
CXX  ?= c++
FLAGS := -std=c++11 -Wall -Wextra -I$(REPO)/src \
         -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1

HEADERS := $(REPO)/src/expr/ExprProgram.hpp $(REPO)/src/expr/ExprCompiler.hpp \
           $(REPO)/src/expr/ExprRuntime.hpp $(REPO)/src/expr/ExprCondition.hpp \
           $(REPO)/src/expr/ExprHistory.hpp $(REPO)/src/expr/ExprPreview.hpp \
           $(REPO)/src/expr/ExprDomain.hpp

expr_test: expr_test.cpp $(HEADERS)
	$(CXX) $(FLAGS) -o $@ expr_test.cpp

run: expr_test
	./expr_test

clean:
	rm -rf expr_test *.dSYM

.PHONY: run clean
