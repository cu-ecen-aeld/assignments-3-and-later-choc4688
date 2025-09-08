#Reference: Typed 'makefile add support for cross compilation' into Google, received the AI response below and modified to fit my use case:
#-----------------------------------------------------------------
#     CROSS_COMPILE = arm-linux-gnueabihf-
#     CC = $(CROSS_COMPILE)gcc
#     CXX = $(CROSS_COMPILE)g++
#     AS = $(CROSS_COMPILE)as
#     LD = $(CROSS_COMPILE)ld
#
#     SYSROOT = /path/to/your/sysroot
#     CFLAGS += --sysroot=$(SYSROOT)
#     CXXFLAGS += --sysroot=$(SYSROOT)
#     LDFLAGS += --sysroot=$(SYSROOT)
#
# 	TARGET_INCLUDE = $(SYSROOT)/usr/include
#     TARGET_LIB = $(SYSROOT)/usr/lib
#
#     CFLAGS += -I$(TARGET_INCLUDE)
#     CXXFLAGS += -I$(TARGET_INCLUDE)
#     LDFLAGS += -L$(TARGET_LIB)
#
# 	ifeq ($(CROSS_BUILD), yes)
#     # Cross-compilation settings
#     CROSS_COMPILE = arm-linux-gnueabihf-
#     CC = $(CROSS_COMPILE)gcc
#     # ... other cross-compilation settings
#     else
#     # Native compilation settings
#     CC = gcc
#     # ... other native settings
#     endif

#-----------------------------------------------------------------

CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld

#Output from 'aarch64-none-linux-gnu-gcc -print-sysroot'
SYSROOT = /home/chaseo/Documents/ARM/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/bin/../aarch64-none-linux-gnu/libc
CFLAGS += --sysroot=$(SYSROOT)
CXXFLAGS += --sysroot=$(SYSROOT)
LDFLAGS += --sysroot=$(SYSROOT)

TARGET_INCLUDE = $(SYSROOT)/usr/include
TARGET_LIB = $(SYSROOT)/usr/lib

CFLAGS += -I$(TARGET_INCLUDE)
CXXFLAGS += -I$(TARGET_INCLUDE)
LDFLAGS += -L$(TARGET_LIB)

CC = $(CROSS_COMPILE)gcc

#------------------------------------------------------------------

#First target is the default target
#Runs when called if writer.c is newer than the generated writer file
writer: writer.o
	$(CC) writer.o -o writer

writer.o: writer.c
	$(CC) -c writer.c -o writer.o

#Alternatively, the 2 above can be combined into like:
#writer: writer.c
#cc writer.c -o writer

clean: 
	rm -f writer writer.o