
# path of the Common library
COMMON_LIB_PATH=../..

CC=tcc
CCF=-I"$(COMMON_LIB_PATH)" -I"." -D_DEBUG -std=gnu99 -g -Wall -Wextra 
DEPS_CCF=-I"$(COMMON_LIB_PATH)" -I"." -D_DEBUG -Wall -g
LDF=-lvulkan -lm -g
EXE_NAME=bin/test

.PHONY:all clean

all: $(EXE_NAME)

$(EXE_NAME): Makefile bin bin/deps.o *.c *.h
	$(CC) $(CCF) -c UnityBuild.c -o bin/main.o
	$(CC) bin/main.o bin/deps.o -o $(EXE_NAME) $(LDF)

bin/deps.o: Makefile $(COMMON_LIB_PATH)/*.c $(COMMON_LIB_PATH)/*.h
	$(CC) $(DEPS_CCF) -DCOMMON_UNITY_BUILD_WITH_PLATFORM_AND_RENDERER -c $(COMMON_LIB_PATH)/Common-UnityBuild.c -o bin/deps.o

bin:
	mkdir bin

clean:
	rm -f bin/deps.o
	rm -f bin/main.o
	rm -f $(EXE_NAME)
	rmdir bin
