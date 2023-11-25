from pymemcache.client.base import PooledClient

client = PooledClient("127.0.0.1:11211", default_noreply=False)
client.set("some_key", "some_value")
result = client.get("some_key")

# spawn a bunch of writers
import random
import string
import time

random_string = lambda n: "".join(
    random.choices(string.ascii_uppercase + string.digits, k=n)
)

N_WRITERS = 10

import threading


def writer():
    N = 10_000
    data = {random_string(10): random_string(10).encode() for _ in range(N)}
    for key, value in data.items():
        client.set(key, value)


def stats_checker(stop_notification=threading.Event()):
    while not stop_notification.is_set():
        stats = client.stats()
        print("stats at ", time.time())
        for key, value in stats.items():
            print(f" {key.decode()}: {value}")
        if stats[b"curr_connections"] != N_WRITERS + 1:
            print("Expected one connection per writer + 1 for the stats thread")
        time.sleep(1)


stop_notification = threading.Event()

threads = [threading.Thread(target=writer) for _ in range(10)]
threads.append(threading.Thread(target=stats_checker, args=(stop_notification,)))

for thread in threads:
    thread.start()

try:
    for thread in threads[:-1]:
        thread.join()
finally:
    stop_notification.set()

client.quit()