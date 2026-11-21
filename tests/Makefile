# PC-side tests of the firmware's plain-C parts (see test_main.c):
#   make -C tests            build and run them (with AddressSanitizer and UBSan)
#   make -C tests golden     print the golden hashes (to pin a deliberate generator change)
#   make -C tests examples   the variant pages' example puzzles (docs/variant-examples.sdx)
#   make -C tests clean
CC      ?= cc
SRC      = ../src
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -std=gnu11 \
           -fsanitize=address,undefined -fno-omit-frame-pointer
CPPFLAGS = -I$(SRC) -DSTATS_CAGES_LEN_T=1024
GEN      = $(SRC)/gen_geometry.c $(SRC)/gen_solver.c $(SRC)/gen_grade.c $(SRC)/gen_make.c $(SRC)/gen_hints.c
OBJS     = test_main.c $(GEN) $(SRC)/puzzles_data.c $(SRC)/sdx.c

run: sudoku_tests
	./sudoku_tests

sudoku_tests: $(OBJS) $(SRC)/generator.h $(SRC)/gen_internal.h $(SRC)/puzzles.h $(SRC)/sdx.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(OBJS)

# the variant pages' example puzzles: docs/variant-examples.sdx, and their opening steps printed
examples: ../tools/variant_examples.c $(GEN) $(SRC)/sdx.c $(SRC)/generator.h $(SRC)/sdx.h
	$(CC) $(CPPFLAGS) -O2 -o variant_examples ../tools/variant_examples.c $(GEN) $(SRC)/sdx.c
	cd .. && tests/variant_examples docs/variant-examples.sdx

golden: sudoku_tests
	PRINT_GOLDEN=1 ./sudoku_tests determinism

clean:
	rm -f sudoku_tests variant_examples

.PHONY: run golden clean examples
