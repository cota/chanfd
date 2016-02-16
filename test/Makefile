CFLAGS := -g3 -ggdb -O2 -Wall -I..
LDFLAGS := -L..

LDLIBS := -lpthread

CHANFD_LIB := ../libchanfd.a

TESTS := test
ifdef PLAN9
TESTS += test9
endif

all: $(TESTS)

run.o: run.c compat.h

test: run.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(CHANFD_LIB)

run9.o: run.c compat.h
	9c $(CFLAGS) -I$(PLAN9)/include -D__PLAN9__ $< -o $@
test9: run9.o
	9l $(CFLAGS) -o $@ $(LDFLAGS) -L$(PLAN9)/lib $^ $(LDLIBS) -lthread

clean:
	$(RM) *.o $(TESTS)
