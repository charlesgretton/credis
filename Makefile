CC=g++
CFLAGS = -std=c++11 -g -O2 -Wall
LDFLAGS =
# CPPFLAGS = -DPRINTDEBUG

# build shared lib under OS X or Linux
OS = $(shell uname -s)
ifeq ($(OS),Darwin)
	SHAREDLIB_LINK_OPTIONS=-dynamiclib -Wl,-install_name -Wl,
else
	SHAREDLIB_LINK_OPTIONS=-shared -Wl,-soname,
endif

# targets to build with 'make all'
TARGETS = credis-test libcredis.a libcredis.so

all: $(TARGETS)

credis-test: credis-test.o libcredis.a
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) -o $@ $^

libcredis.a: credis.o
	$(AR) -cvq $@ $^

libcredis.so: credis.o
	$(CC) $(SHAREDLIB_LINK_OPTIONS)$@ -o $@ $^

credis.o: credis.cc credis.hh Makefile
	$(CC) -c -fPIC $(CFLAGS) $(CPPFLAGS) -o $@ credis.cc

install:
	@echo "Installing credis library"
	cp -f libcredis.so /usr/lib
	cp -f libcredis.a /usr/include
	cp -f credis.h /usr/include
clean:
	rm -f *.o *~ $(TARGETS)
