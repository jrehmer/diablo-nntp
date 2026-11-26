CC ?= cc
PYTHON ?= python3
CFLAGS ?= -O2 -g
LDLIBS ?= -lsqlite3

.PHONY: all check clean

all: ovshadow

ovshadow: shadow.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -Wall -Wextra $(LDFLAGS) -o $@ shadow.c $(LDLIBS)

check: ovshadow
	$(PYTHON) shadow_test.py check ./ovshadow

clean:
	rm -f ovshadow
