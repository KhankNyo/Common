
COMMON_LIB_PATH="../../"
SHADERC_LIB_PATH="../../"

CC=gcc
CCF=-I$(COMMON_LIB_PATH) -I"." -Wall -Wextra -std=c2x -g
LDF=-lvulkan -lm

all: bin/test

bin/test: bin *.c *.h
	# NOTE: glfw's build process is separate because it defines vulkan structures differently from vulkan.h
	$(CC) $(CCF) UnityBuild.c $(COMMON_LIB_PATH)/Common-Glfw-UnityBuild.c -o bin/test $(LDF)

bin:
	mkdir bin

clean:
	rm bin/test
	rmdir bin
