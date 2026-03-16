
CC=gcc
CCF=-std=c2x -Wall -Wextra -Wno-missing-braces -D_DEBUG -O2 -msse4.2 -g -I".."
LDF=-lm

.PHONY: all clean
	
all: bin/test-la bin/test-slice bin/test-fl

bin:
	mkdir bin

bin/test-la:bin LinearAlgebra.c Makefile
	$(CC) $(CCF) LinearAlgebra.c -o bin/test-la $(LDF)

bin/test-slice:bin Slice.c Makefile
	$(CC) $(CCF) Slice.c -o bin/test-slice $(LDF)

bin/test-fl:bin FreeList.c Makefile
	$(CC) $(CCF) FreeList.c -o bin/test-fl $(LDF)

test:bin/test-la bin/test-slice bin/test-fl
	bin/test-la
	bin/test-slice
	bin/test-fl

clean:
	rm -f bin/test-la
	rm -f bin/test-fl
	rm -f bin/test-slice
	rmdir bin

