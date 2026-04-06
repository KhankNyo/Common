
# path of the Common library
COMMON_LIB_PATH=../..

CC=gcc
CCF=-I"$(COMMON_LIB_PATH)" -I"." -I"./deps" -std=gnu99 -g -Wall -Wextra -D_DEBUG
DEPS_CCF=-I"$(COMMON_LIB_PATH)" -I"." -O2 -D_DEBUG -DCOMMON_UNITY_BUILD_WITH_PLATFORM_AND_RENDERER
LDF=-lvulkan -lm
EXE_NAME=bin/test

.PHONY:all clean

all: $(EXE_NAME)

$(EXE_NAME): Makefile bin bin/lib.o *.c *.h bin/texture.frag.spv bin/texture.vert.spv
	$(CC) $(CCF) -c UnityBuild.c -o $(EXE_NAME).o
	$(CC) $(EXE_NAME).o bin/lib.o -o $(EXE_NAME) $(LDF)

bin/lib.o:$(COMMON_LIB_PATH)/*.c $(COMMON_LIB_PATH)/*.h
	$(CC) $(DEPS_CCF) -c $(COMMON_LIB_PATH)/Common-UnityBuild.c -o $@

bin/texture.frag.spv: shaders/Texture.frag.glsl
	glslc -fshader-stage=frag shaders/Texture.frag.glsl -o bin/texture.frag.spv

bin/texture.vert.spv: shaders/Texture.vert.glsl
	glslc -fshader-stage=vert shaders/Texture.vert.glsl -o bin/texture.vert.spv

bin:
	mkdir bin

clean:
	rm -f bin/*.spv
	rm -f bin/lib.o
	rm -f $(EXE_NAME).o
	rm -f $(EXE_NAME)
	rmdir bin
