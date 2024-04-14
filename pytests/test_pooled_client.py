import pytest
from pymemcache.client.base import PooledClient

import random
import string
import threading
from conftest import SERVER


def random_string(n: int) -> str:
    return "".join(random.choices(string.ascii_uppercase + string.digits, k=n))


def test_pooled_client() -> None:
    client = PooledClient(SERVER, default_noreply=False)

    N_WRITES = 100
    N_CLIENTS = 100

    def writer():
        data = {random_string(10): random_string(10).encode() for _ in range(N_WRITES)}
        for key, value in data.items():
            client.set(key, value)

    threads = [threading.Thread(target=writer) for _ in range(N_CLIENTS)]

    for thread in threads:
        thread.start()

    for thread in threads:
        thread.join()

    _stats = client.stats()
