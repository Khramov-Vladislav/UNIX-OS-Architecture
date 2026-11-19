CC      ?= cc
CFLAGS  ?= -Wall -Wextra -O2

all: filemond

filemond: filemond.c
	$(CC) $(CFLAGS) -o $@ $<

run:
	./filemond ./filemond.conf

stop:
	pkill -TERM -x filemond

status:
	journalctl -t filemond -f

clean:
	rm -f filemond

.PHONY: clean
