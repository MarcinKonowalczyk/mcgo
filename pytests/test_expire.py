import pytest
from pymemcache.client.base import Client

import time


@pytest.mark.slow
def test_expire_on_get(client: Client) -> None:
    client.set("hello", "world", expire=1)
    assert client.get("hello") == b"world"
    time.sleep(2)
    assert client.get("hello") is None


@pytest.mark.slow
def test_expire_on_delete(client: Client) -> None:
    client.set("hello", "world", expire=1)
    assert client.get("hello") == b"world"
    time.sleep(2)
    # Delete should return False if the key is not found
    assert client.delete("hello") is False
