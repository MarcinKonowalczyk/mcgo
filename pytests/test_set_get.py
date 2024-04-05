from pymemcache.client.base import Client, PooledClient

def test_set_get(client: Client):
    client.set("some_key", "some_value")
    result = client.get("some_key")
    assert result == b"some_value"

