from pymemcache.client.base import PooledClient

import random
import string
import threading
import time


def random_string(n: int) -> str:
    return "".join(random.choices(string.ascii_uppercase + string.digits, k=n))


client = PooledClient(("127.0.0.1", 11211), default_noreply=False)
print(f"Version: {client.version().decode()}")

N_WRITES = 10_000
N_CLIENTS = 10


def writer():
    data = {random_string(10): random_string(10).encode() for _ in range(N_WRITES)}
    for key, value in data.items():
        client.set(key, value)


def stats_checker(stop_notification=threading.Event()):
    while not stop_notification.is_set():
        stats = client.stats()
        print("stats at ", time.time())
        for key, value in stats.items():
            print(f" {key.decode()}: {value}")
        curr_connections = stats[b"curr_connections"]
        if curr_connections != N_CLIENTS + 1:
            print(
                f"Expected one connection per writer {N_CLIENTS +1}, got {curr_connections}"
            )
        time.sleep(0.1)


stop_notification = threading.Event()

threads = [threading.Thread(target=writer) for _ in range(N_CLIENTS)]
threads.append(threading.Thread(target=stats_checker, args=(stop_notification,)))

for thread in threads:
    thread.start()

try:
    for thread in threads[:-1]:
        thread.join()
finally:
    stop_notification.set()

client.quit()
