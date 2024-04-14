import pytest
from pymemcache.client.base import Client

import time


@pytest.mark.slow
def test_expire(client: Client) -> None:
    client.set("hello", "world", expire=1)
    assert client.get("hello") == b"world"
    time.sleep(2)
    assert client.get("hello") is None
