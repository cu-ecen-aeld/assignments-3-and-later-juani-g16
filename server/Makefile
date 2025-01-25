# Compiler
CC=gcc

# Target name
TARGET = aesdsocket

#Flags
CFLAGS ?= -g -Wall -Werror
LDFLAGS ?= -pthread -lrt -lm

# Source files
SRC = aesdsocket.c

# Compile the program
all:
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

default:
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean :
	rm $(TARGET)