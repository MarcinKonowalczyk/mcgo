package main

import (
	"testing"

	"github.com/bradfitz/gomemcache/memcache"
)

const SERVER = "localhost:11211"

func TestVersion(t *testing.T) {
	mc := memcache.New("localhost:11211")
	mc.Ping()
}
