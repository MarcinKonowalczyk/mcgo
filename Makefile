CC = gcc
CFLAGS = -Wall -Wextra -Wshadow -pedantic -std=c99


all: memcached


memcached: memcached.c slabs.c items.c memcached.h
	$(CC)  -I. -L. -o memcached memcached.c slabs.c items.c -levent -lJudy $(CFLAGS)


debug: memcached.c slabs.c items.c memcached.h
	$(CC) -g  -I. -L. -o memcached-debug memcached.c slabs.c items.c -levent -lJudy $(CFLAGS)

clean:
	rm -rf memcached memcached-debug
	rm -rf memcached-debug.dSYM
