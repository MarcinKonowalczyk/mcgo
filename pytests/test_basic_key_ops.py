import pytest
from pymemcache.client.base import Client


def test_version(client: Client) -> None:
    result = client.version()
    assert result is not None


def test_quit(client: Client) -> None:
    assert client.sock is not None
    client.quit()
    assert client.sock is None


def test_set_get_delete(client: Client) -> None:
    # Set a key
    client.set("some_key", "some_value")
    result = client.get("some_key")
    assert result == b"some_value"

    # Delete the key
    deleted = client.delete("some_key")
    assert deleted is True
    result = client.get("some_key")
    assert result is None

    deleted = client.delete("some_key")
    assert deleted is False


def test_delete_non_existent_key(client: Client) -> None:
    client.delete("not_existent_key")
    deleted = client.delete("not_existent_key")
    assert deleted is False


def test_key_not_found(client: Client) -> None:
    # Test getting a key which never existed
    result = client.get("non_existent_key")
    assert result is None


def test_set_noreply(client: Client) -> None:
    client.set("hello", "world", noreply=True)
    result = client.get("hello")
    assert result == b"world"
