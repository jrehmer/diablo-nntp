CC ?= cc
PYTHON ?= python3
CFLAGS ?= -O2 -g
LDLIBS ?= -lsqlite3

.PHONY: all check check-writer clean

all: ovshadow

ovshadow: shadow.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -Wall -Wextra $(LDFLAGS) -o $@ shadow.c $(LDLIBS)

ovwriter: writer.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -Wall -Wextra $(LDFLAGS) -o $@ writer.c $(LDLIBS)

check: ovshadow
	$(PYTHON) shadow_test.py check ./ovshadow

check-writer: ovwriter ovshadow
	$(PYTHON) writer_test.py check ./ovwriter ./ovshadow

clean:
	rm -f ovshadow ovwriter
