import pytest
from pymemcache.client.base import Client

import random
import string
import time


def random_string(n: int) -> str:
    return "".join(random.choices(string.ascii_uppercase + string.digits, k=n))


client = Client(("127.0.0.1", 11211), default_noreply=False)

N = 10_000
data = {random_string(10): random_string(10).encode() for _ in range(N)}

time_start = time.time()
for key, value in data.items():
    client.set(key, value)
time_end = time.time()
write_time = time_end - time_start
writes_per_second = N / write_time
print(
    f"Set {N} items in {time_end - time_start} seconds ({writes_per_second/1000:.0f} kwrites)"
)

time_start = time.time()
for i, (key, value) in enumerate(data.items()):
    _ = client.get(key)
time_end = time.time()
read_time = time_end - time_start
reads_per_second = N / read_time
print(
    f"Get {N} items in {time_end - time_start} seconds ({reads_per_second/1000:.0f} kreads)"
)


# stats = client.stats()
# print("stats")
# for key, value in stats.items():
#     print(f"{key.decode()}: {value!r}")

# print(client.version())

# # test incr/decr
# client.set("hello", 1)
# client.incr("hello", 17)
# assert client.get("hello") == b"18"
# client.decr("hello", 5)
# assert client.get("hello") == b"13"
# client.delete("hello")
# assert client.get("hello") is None

# client.quit()
