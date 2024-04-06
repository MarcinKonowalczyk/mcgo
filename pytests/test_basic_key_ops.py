from pymemcache.client.base import Client

def test_set_get_delete(client: Client) -> None:
    # Set a key
    client.set("some_key", "some_value")
    result = client.get("some_key")
    assert result == b"some_value"
    
    # Delete the key
    client.delete("some_key")
    result = client.get("some_key")
    assert result is None

def test_key_not_found(client: Client) -> None:
    # Test getting a key which never existed
    result = client.get("non_existent_key")
    assert result is None
