CC = gcc
CFLAGS = -Wall -Wextra -I../firmware/include
LDFLAGS = -lm

.PHONY: all clean test

all: test_pid

test_pid: test_pid.c ../firmware/src/pid_control.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test: test_pid
	@echo "=== Running PID Tests ==="
	./test_pid
	@echo ""
	@echo "=== Running Redfish Tests ==="
	python3 test_redfish.py || echo "(Redfish tests require running servers)"

clean:
	rm -f test_pid
