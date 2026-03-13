
COMMON_LIB_PATH=../..

CC=gcc
CCF=-I"$(COMMON_LIB_PATH)" -I"." -std=c2x -g -Wall -Wextra 
DEPS_CCF=-I"$(COMMON_LIB_PATH)" -I"." -O2
LDF=-lvulkan -lm

all: bin/test

bin/test: bin bin/deps.o *.c *.h
	$(CC) $(CCF) -c UnityBuild.c -o bin/test.o
	$(CC) bin/test.o bin/deps.o -o bin/test $(LDF)

bin/deps.o: bin $(COMMON_LIB_PATH)/Common-Glfw-UnityBuild.c
	# NOTE: glfw's build process is separate because it defines vulkan structures differently from vulkan.h
	$(CC) $(DEPS_CCF) -c $(COMMON_LIB_PATH)/Common-Glfw-UnityBuild.c -o bin/deps.o

bin:
	mkdir bin

clean:
	rm -f bin/deps.o
	rm -f bin/test.o
	rm -f bin/test
	rmdir bin
