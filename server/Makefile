# Compiler
CC=gcc

# Target name
TARGET = aesdsocket

#Flags
CFLAGS = -g -Wall -Werror

# Source files
SRC = aesdsocket.c

# Compile the program
all:
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

default:
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean :
	rm $(TARGET)