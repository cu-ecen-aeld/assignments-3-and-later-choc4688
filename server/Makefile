
CC ?= $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld

#Output from 'aarch64-none-linux-gnu-gcc -print-sysroot'
SYSROOT := $(aarch64-none-linux-gnu-gcc -print-sysroot)
CFLAGS ?= --sysroot=$(SYSROOT)
CXXFLAGS += --sysroot=$(SYSROOT)
LDFLAGS ?= --sysroot=$(SYSROOT)

TARGET_INCLUDE = $(SYSROOT)/usr/include
TARGET_LIB = $(SYSROOT)/usr/lib

CFLAGS += -I$(TARGET_INCLUDE)
CXXFLAGS += -I$(TARGET_INCLUDE)
LDFLAGS += -L$(TARGET_LIB)

TARGET ?= aesdsocket


#-g for valgrind output to show specific line numbers
all: aesdsocket.c
	$(CC) $(CFLAGS) $^ -o aesdsocket $(INCLUDES) $(LDFLAGS)

clean: 
	rm -f aesdsocket aesdsocket.o