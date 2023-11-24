from pymemcache.client.base import Client

client = Client('127.0.0.1:11211', default_noreply=False)
client.set('some_key', 'some_value')
result = client.get('some_key')

import random
import string
import time

random_string = lambda n: ''.join(random.choices(string.ascii_uppercase + string.digits, k=n))
N = 10_000

data = {random_string(10): random_string(10).encode() for _ in range(N)}

time_start = time.time()
for key, value in data.items():
    client.set(key, value)
time_end = time.time()
write_time = time_end - time_start
writes_per_second = N / write_time
print(f"Set {N} items in {time_end - time_start} seconds ({writes_per_second/1000:.0f} kwrites)")

time_start = time.time()
for key, value in data.items():
    got_value = client.get(key)
time_end = time.time()
read_time = time_end - time_start
reads_per_second = N / read_time
print(f"Get {N} items in {time_end - time_start} seconds ({reads_per_second/1000:.0f} kreads)")

stats = client.stats()
print("stats")
for key, value in stats.items():
    print(f"{key.decode()}: {value}")

print(client.version())

# test incr/decr
client.set("hello", 1)
client.incr("hello", 17)
assert client.get("hello") == b"18"
client.decr("hello", 5)
assert client.get("hello") == b"13"
client.delete("hello")
assert client.get("hello") is None

