import pytest

from pymemcache.client.base import Client

SERVER = ("127.0.0.1", 11211)

@pytest.fixture
def client() -> Client:
    return Client(SERVER, default_noreply=False)