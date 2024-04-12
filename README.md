# mcgo

Hobbyist attempt at reimplementing parts of memcached in Go.


Run in verbose mode:

```sh
go run . -v
```

Test:
```sh
cd gotests && go test && cd -
pytest ./pytests
```

# ToDo's

- [ ] expire items
- [ ] LRU eviction
- [ ] stats
- [ ] distributed... ?
- [ ] full noreply support
    - [ ] GET
    - [ ] SET
    - [ ] INCR/DECR
    - [ ] DELETE
    