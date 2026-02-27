
CC=gcc
CCF=-Wall -Wextra -Wno-missing-braces -O2 -msse4.2 -I".."
LDF=-lm

.PHONY: all clean
	
all: bin bin/test-la

bin:
	mkdir bin

bin/test-la: test/LinearAlgebra.c Makefile
	$(CC) $(CCF) test/LinearAlgebra.c -o bin/test-la $(LDF)

clean:
	rm bin/test-la
	rmdir bin

