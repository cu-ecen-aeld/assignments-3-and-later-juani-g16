# Compiler
CC ?= $(CROSS_COMPILE)gcc

# Target name
TARGET ?= aesdsocket

#Flags
CFLAGS ?= -g -Wall -Werror
LDFLAGS ?= -pthread -lrt

# Source files
SRC = aesdsocket.c

# Compile the program
all: 
	$(CC) $(CFLAGS) $^ -o $(TARGET) $(SRC) $(LDFLAGS)

default:
	$(CC) $(CFLAGS) $^ -o $(TARGET) $(SRC) $(LDFLAGS)

clean :
	rm $(TARGET)