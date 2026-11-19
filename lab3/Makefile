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
	tail -f /tmp/filemond.log

clean:
	rm -f filemond

.PHONY: clean
