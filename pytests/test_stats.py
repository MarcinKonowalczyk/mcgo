from pymemcache.client.base import Client


def test_stats(client: Client) -> None:
    stats = client.stats()
    print(stats)

    # for i in range(10):
    #     client.set(f"key_{i}", f"value_{i}")

    # stats = client.stats()
    # print(stats)
