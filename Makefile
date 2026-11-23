# Memory Hierarchy Simulator - build file
CC      ?= gcc
CFLAGS  ?= -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -O2 -g
LDFLAGS ?=
INCLUDES = -Iinclude

TARGET = memsim
SRCS   = src/cache.c src/hierarchy.c src/trace.c src/main.c
OBJS   = $(SRCS:.c=.o)

.PHONY: all debug clean run demo test help

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

debug: CFLAGS = -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -O0 -g3
debug: clean $(TARGET)

run: $(TARGET)
	./$(TARGET)

demo: $(TARGET)
	./$(TARGET) --demo

test: $(TARGET)
	./$(TARGET) -l1 8K,32,4,lru -g ws -p 40 -n 50000 -v | head -30
	./$(TARGET) -l1 16K,64,1 -l2 256K,64,8 -g col -p 512 -n 100000

clean:
	rm -f $(OBJS) $(TARGET) examples/hello

help:
	@echo "make          - build memsim"
	@echo "make debug    - build with no optimisation and full debug info"
	@echo "make run      - build and run the demo"
	@echo "make test     - quick smoke tests"
	@echo "make clean    - remove build artefacts"
