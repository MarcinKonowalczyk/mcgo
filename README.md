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

- [ ] stats
- [ ] expire items
- [ ] LRU eviction
- [ ] distributed... ?

# Done's

- [x] full(er) noreply support
    - [x] SET
    - [x] INCR/DECR
    - [x] DELETE
    