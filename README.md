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
- [ ] reset stats, and then use it in tests
- [ ] LRU eviction
- [ ] distributed... ?

- [ ] tests in other python libraries
- [ ] go tests
- [ ] memory limit

# Done's

- [x] stats
- [x] full(er) noreply support
    - [x] SET
    - [x] INCR/DECR
    - [x] DELETE
- [x] multiple clients working ok
    