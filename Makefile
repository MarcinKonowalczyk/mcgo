# For Mac:
# brew install libevent judy
# Bash / Zsh:
#   export CPATH=/opt/homebrew/include
#   export LIBRARY_PATH=/opt/homebrew/lib
#
# Fish:
#   set -x CPATH /opt/homebrew/include
#   set -x LIBRARY_PATH /opt/homebrew/lib

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
