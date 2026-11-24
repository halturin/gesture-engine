CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Isrc -Ireference

.PHONY: all test lut clean

all: test

# Regenerate the Apple LUT header from the captured reference curve.
lut: reference/apple_lut.h
reference/apple_lut.h: reference/apple_accel.csv tools/gen_config.py
	python3 tools/gen_config.py

test: test_engine test_vs_reference test_config test_mapping
	./test_engine && ./test_vs_reference && ./test_config && ./test_mapping

test_engine: test/test_engine.c src/engine.c src/*.h reference/apple_lut.h
	$(CC) $(CFLAGS) -o $@ test/test_engine.c src/engine.c -lm

test_vs_reference: test/test_vs_reference.c src/engine.c src/*.h reference/apple_lut.h
	$(CC) $(CFLAGS) -o $@ test/test_vs_reference.c src/engine.c -lm

test_config: test/test_config.c src/config_io.c src/*.h
	$(CC) $(CFLAGS) -o $@ test/test_config.c src/config_io.c

test_mapping: test/test_mapping.c src/mapping.c src/config_io.c src/*.h
	$(CC) $(CFLAGS) -o $@ test/test_mapping.c src/mapping.c src/config_io.c

clean:
	rm -f test_engine test_vs_reference test_config test_mapping
