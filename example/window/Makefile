
COMMON_LIB_PATH=../..

CC=gcc
CCF=-I"$(COMMON_LIB_PATH)" -I"." -std=gnu99 -g -Wall -Wextra 
DEPS_CCF=-I"$(COMMON_LIB_PATH)" -I"." -O2
LDF=-lvulkan -lm

.PHONY:all clean

all: bin/test

bin/test: Makefile bin bin/deps.o $(COMMON_LIB_PATH)/*.c $(COMMON_LIB_PATH)/*.h *.c *.h
	$(CC) $(CCF) -c UnityBuild.c -o bin/test.o
	$(CC) bin/test.o bin/deps.o -o bin/test $(LDF)

bin/deps.o: $(COMMON_LIB_PATH)/Common-Glfw-UnityBuild.c
	# NOTE: glfw's build process is separate because it defines vulkan structures differently from vulkan.h
	$(CC) $(DEPS_CCF) -c $(COMMON_LIB_PATH)/Common-Glfw-UnityBuild.c -o bin/deps.o

bin:
	mkdir bin

clean:
	rm -f bin/deps.o
	rm -f bin/test.o
	rm -f bin/test
	rmdir bin
