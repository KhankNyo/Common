
COMMON_LIB_PATH=../..

CC=gcc
CCF=-I"$(COMMON_LIB_PATH)" -I"." -I"./deps" -D_DEBUG -std=gnu99 -g -Wall -Wextra 
DEPS_CCF=-I"$(COMMON_LIB_PATH)" -I"." -O2
LDF=-lvulkan -lm

.PHONY:all clean

all: bin/test

bin/test: Makefile bin bin/deps.o $(COMMON_LIB_PATH)/*.c $(COMMON_LIB_PATH)/*.h *.c *.h bin/texture.vert.spv bin/texture.frag.spv
	$(CC) $(CCF) -c UnityBuild.c -o bin/test.o
	$(CC) bin/test.o bin/deps.o -o bin/test $(LDF)

bin/deps.o: $(COMMON_LIB_PATH)/Common-Glfw-UnityBuild.c
	# NOTE: glfw's build process is separate because it defines vulkan structures differently from vulkan.h
	$(CC) $(DEPS_CCF) -c $(COMMON_LIB_PATH)/Common-Glfw-UnityBuild.c -o bin/deps.o

bin/texture.frag.spv: shaders/Texture.frag.glsl
	glslc -fshader-stage=frag shaders/Texture.frag.glsl -o bin/texture.frag.spv

bin/texture.vert.spv: shaders/Texture.vert.glsl
	glslc -fshader-stage=vert shaders/Texture.vert.glsl -o bin/texture.vert.spv

bin:
	mkdir bin

clean:
	rm -f bin/*.spv
	rm -f bin/deps.o
	rm -f bin/test.o
	rm -f bin/test
	rmdir bin
