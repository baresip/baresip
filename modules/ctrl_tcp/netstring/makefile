CFLAGS = -O2 -Wall
LDFLAGS= -lm

.PHONY: clean

all: testsuite

testsuite: testsuite.c netstring.o

clean:
	rm -f *.o testsuite
