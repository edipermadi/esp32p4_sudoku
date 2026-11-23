# PC-side tests of the firmware's plain-C parts (see test_main.c):
#   make -C tests            build and run them (with AddressSanitizer and UBSan)
#   make -C tests golden     print the golden hashes (to pin a deliberate generator change)
#   make -C tests examples   the variant pages' example puzzles (docs/variant-examples.sdx)
#   make -C tests canon      the built-in puzzles' canonical hashes (src/puzzles_canon.c)
#   make -C tests clean
CC      ?= cc
SRC      = ../src
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -std=gnu11 \
           -fsanitize=address,undefined -fno-omit-frame-pointer
CPPFLAGS = -I$(SRC) -DSTATS_CAGES_LEN_T=1024
GEN      = $(SRC)/gen_geometry.c $(SRC)/gen_texts.c $(SRC)/gen_solver.c $(SRC)/gen_grade.c $(SRC)/gen_make.c $(SRC)/gen_regions.c $(SRC)/gen_lines.c $(SRC)/gen_hints.c
CORES    = $(SRC)/canon_core.c $(SRC)/search_core.c $(SRC)/learn_core.c $(SRC)/game_core.c $(SRC)/hint_core.c $(SRC)/share_core.c $(SRC)/daily_core.c $(SRC)/import_parse.c
OBJS     = test_main.c $(GEN) $(SRC)/puzzles_data.c $(SRC)/puzzles_canon.c $(SRC)/sdx.c $(CORES)

run: sudoku_tests
	./sudoku_tests

sudoku_tests: $(OBJS) $(SRC)/generator.h $(SRC)/gen_internal.h $(SRC)/puzzles.h $(SRC)/sdx.h $(CORES:.c=.h)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(OBJS)

# the variant pages' example puzzles: docs/variant-examples.sdx, and their opening steps printed
examples: ../tools/variant_examples.c $(GEN) $(SRC)/sdx.c $(SRC)/generator.h $(SRC)/sdx.h
	$(CC) $(CPPFLAGS) -O2 -o variant_examples ../tools/variant_examples.c $(GEN) $(SRC)/sdx.c
	cd .. && tests/variant_examples docs/variant-examples.sdx

# the built-in puzzles' canonical hashes, worked out once: src/puzzles_canon.c
canon: ../tools/gen_canon.c $(GEN) $(SRC)/canon_core.c $(SRC)/puzzles_data.c $(SRC)/canon_core.h $(SRC)/puzzles.h
	$(CC) $(CPPFLAGS) -O2 -o gen_canon ../tools/gen_canon.c $(GEN) $(SRC)/canon_core.c $(SRC)/puzzles_data.c
	cd .. && tests/gen_canon src/puzzles_canon.c

golden: sudoku_tests
	PRINT_GOLDEN=1 ./sudoku_tests determinism

clean:
	rm -f sudoku_tests variant_examples gen_canon

.PHONY: run golden clean examples
