CC ?= gcc
CFLAGS = -Wall -Wextra -Werror -I../../main/alarm

all: test_alarm_sync
	./test_alarm_sync

test_alarm_sync: ../../main/alarm/alarm_sync.c test_alarm_sync.c ../../main/alarm/alarm_sync.h
	$(CC) $(CFLAGS) ../../main/alarm/alarm_sync.c test_alarm_sync.c -o test_alarm_sync

clean:
	rm -f test_alarm_sync

.PHONY: all clean
