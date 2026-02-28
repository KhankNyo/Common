
CC=gcc
CCF=-std=c2x -Wall -Wextra -Wno-missing-braces -O2 -msse4.2 -I".."
LDF=-lm

.PHONY: all clean
	
all: bin bin/test-la bin/test-slice

bin:
	mkdir bin

bin/test-la:bin LinearAlgebra.c Makefile
	$(CC) $(CCF) LinearAlgebra.c -o bin/test-la $(LDF)

bin/test-slice:bin Slice.c Makefile
	$(CC) $(CCF) Slice.c -o bin/test-slice $(LDF)

test:bin/test-la bin/test-slice
	bin/test-la
	bin/test-slice

clean:
	rm -f bin/test-la
	rm -f bin/test-slice
	rmdir bin

