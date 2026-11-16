CC      ?= cc
CFLAGS  ?= -Wall -Wextra -O2

all: filemond

filemond: filemond.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f filemond

.PHONY: clean
