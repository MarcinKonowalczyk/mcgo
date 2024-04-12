import pytest
from pymemcache.client.base import Client
from pymemcache.exceptions import MemcacheClientError


def test_incr_decr(client: Client) -> None:
    """Test incrementing and decrementing a key."""
    client.set("hello", 1)
    client.incr("hello", 17)
    result = client.get("hello")
    assert result == b"18"

    client.decr("hello", 5)
    result = client.get("hello")
    assert result == b"13"


def test_decr_below_zero(client: Client) -> None:
    client.set("hello", 0)
    assert client.get("hello") == b"0"
    result = client.decr("hello", 1)
    assert result == -1
    assert client.get("hello") == b"-1"


def test_number_format(client: Client) -> None:
    """Test decrementing a key below zero. The value should be set to 0."""

    for value in (1, "1", "+1"):
        client.set("hello", value)
        assert client.get("hello") == str(value).encode()
        result = client.incr("hello", 1)
        assert result == 2
        assert client.get("hello") == b"2"

    for value in (0, "0", "+0", "-0"):
        client.set("hello", value)
        assert client.get("hello") == str(value).encode()
        result = client.incr("hello", 1)
        assert result == 1
        assert client.get("hello") == b"1"

    for value in (-1, "-1"):
        client.set("hello", value)
        assert client.get("hello") == str(value).encode()
        result = client.incr("hello", 1)
        assert result == 0
        assert client.get("hello") == b"0"


def test_incr_decr_not_found(client: Client) -> None:
    result = client.incr("non_existent_key", 5)
    assert result is None
    value = client.get("non_existent_key")
    assert value is None

    result = client.decr("non_existent_key", 5)
    assert result is None
    value = client.get("non_existent_key")
    assert value is None


def test_incr_decr_noreply(client: Client) -> None:
    """Test incrementing and decrementing with noreply=True."""
    client.set("hello", 1)
    result = client.incr("hello", 2, noreply=True)
    assert result is None
    result = client.get("hello")
    assert result == b"3"

    result = client.decr("hello", 1, noreply=True)
    assert result is None
    result = client.get("hello")
    assert result == b"2"


def test_incr_decr_non_numeric(client: Client) -> None:
    """Test behaviour on incrementing and decrementing a non-numeric key. The key should be treated as 0."""
    client.set("hello", "world")
    with pytest.raises(MemcacheClientError):
        client.incr("hello", 2)

    assert client.get("hello") == b"world"

    with pytest.raises(MemcacheClientError):
        client.decr("hello", 2)

    assert client.get("hello") == b"world"


def test_incr_behaviour(client: Client) -> None:
    # We still parse
    client.set("hello", 0)
    result = client.decr("hello", 2)
    assert result == -2
