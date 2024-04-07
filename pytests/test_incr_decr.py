import pytest
from pymemcache.client.base import Client

from conftest import is_go_impl


def test_incr_decr(client: Client) -> None:
    """Test incrementing and decrementing a key."""
    client.set("hello", 1)
    client.incr("hello", 17)
    result = client.get("hello")
    assert result == b"18"

    client.decr("hello", 5)
    result = client.get("hello")
    assert result == b"13"


def test_decr_unsigned(client: Client) -> None:
    """Test decrementing a key below zero. The value should be set to 0."""

    client.set("hello", 1)
    result = client.decr("hello", 1)
    assert result == 0
    assert client.get("hello") == b"0"

    result = client.decr("hello", 4)
    if is_go_impl(client):
        # Go implementation allows signed integers
        assert result == -4
    else:
        assert result == 0
        assert client.get("hello") == b"0"


def test_incr_decr_not_found_c(client: Client) -> None:
    """Test incrementing and decrementing a key which never existed."""
    result = client.incr("non_existent_key", 5)
    assert result is None
    value = client.get("non_existent_key")
    assert value is None

    result = client.decr("non_existent_key", 5)
    assert result is None
    value = client.get("non_existent_key")
    assert value is None


@pytest.mark.skip(reason="Not working")
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


@pytest.mark.c_impl
def test_incr_decr_non_numeric_c(client: Client) -> None:
    """Test behaviour on incrementing and decrementing a non-numeric key. The key should be treated as 0."""
    client.set("hello", "abcd")
    result = client.incr("hello", 2)
    assert result == 2
    result = client.get("hello")
    assert result == b"2   "

    # DECR on non-numeric decrements
    client.set("hello", "abcd")
    result = client.decr("hello", 2)
    assert result == 0
    result = client.get("hello")
    assert result == b"0   "


@pytest.mark.go_impl
def test_incr_decr_non_numeric_go(client: Client) -> None:
    """Test behaviour on incrementing and decrementing a non-numeric key. The key should be treated as 0."""
    client.set("hello", "abcd")
    result = client.incr("hello", 2)
    assert result == None
    result = client.get("hello")
    assert result == b"abcd"
