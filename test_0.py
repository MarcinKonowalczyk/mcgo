from pymemcache.client.base import Client

client = Client("127.0.0.1:11211", default_noreply=False)
client.set("some_key", "some_value")
# result = client.get("some_key")