CC = gcc
CFLAGS = -Wall -Wextra -pthread
LDLIBS = -lncurses -lm
SRCS = surv.c checker.c
TARGET = surv

.PHONY: all surv surv-run clean

all: $(TARGET)

surv: $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDLIBS)

surv-run: surv
	@echo "Starting $(TARGET) (demo mode) - press Ctrl-C to stop"
	./$(TARGET) -d

clean:
	rm -f $(TARGET) *.o
