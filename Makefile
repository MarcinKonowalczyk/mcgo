all: memcached

# $(CC)  -I. -L. -o memcached memcached.c slabs.c items.c /usr/local/opt/judy/lib/libJudy.a /usr/local/opt/libevent/lib/libevent.a
memcached: memcached.c slabs.c items.c memcached.h
	$(CC)  -I. -L. -o memcached memcached.c slabs.c items.c -levent -lJudy -Wall -Wextra -pedantic -std=c99

# $(CC) -g  -I. -L. -o memcached-debug memcached.c slabs.c items.c /usr/local/opt/judy/lib/libJudy.a /usr/local/opt/libevent/lib/libevent.a
debug: memcached.c slabs.c items.c memcached.h
	$(CC) -g  -I. -L. -o memcached-debug memcached.c slabs.c items.c -levent -lJudy -Wall -Wextra -pedantic -std=c99

clean:
	rm -rf memcached memcached-debug
	rm -rf memcached-debug.dSYM
