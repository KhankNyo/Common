
# path of the Common library
COMMON_LIB_PATH=../..

CC=gcc
CCF=-I"$(COMMON_LIB_PATH)" -I"." -std=gnu99 -g -Wall -Wextra 
DEPS_CCF=-I"$(COMMON_LIB_PATH)" -I"." -O2
LDF=-lvulkan -lm
EXE_NAME=bin/test

.PHONY:all clean

all: $(EXE_NAME)

$(EXE_NAME): Makefile bin $(COMMON_LIB_PATH)/*.c $(COMMON_LIB_PATH)/*.h *.c *.h
	$(CC) $(CCF) UnityBuild.c -o $(EXE_NAME) $(LDF)

bin:
	mkdir bin

clean:
	rm -f $(EXE_NAME)
	rmdir bin
