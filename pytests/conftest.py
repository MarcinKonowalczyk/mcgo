import pytest
from pathlib import Path

from pymemcache.client.base import Client

SERVER = ("127.0.0.1", 11211)

__file_dir__ = Path(__file__).resolve().parent
__project_root__ = __file_dir__.parent


@pytest.fixture
def client() -> Client:
    client = Client(SERVER, default_noreply=False, timeout=1.0)
    client.version() # Check if the server is running
    assert client.sock is not None
    return client

try:
    _client = Client(SERVER, default_noreply=False)
    _client.version()

except ConnectionRefusedError:
    # import subprocess

    # has_go = subprocess.run(["go", "version"]).returncode == 0
    # if not has_go:
    #     raise RuntimeError("Go is not installed")

    # filename = __project_root__ / "memcached.go"
    # if not filename.exists():
    #     raise FileNotFoundError(f"{filename} does not exist")

    raise ConnectionRefusedError(
        "Memcached server is not running. Please start the server and try again."
    )


# Add a newline before the first print statement
from typing import Any, Generator

@pytest.fixture(autouse=True)
def add_space_before_print() -> Generator[None, None, None]:
    import builtins

    _print = builtins.print

    def patched_print(*args: Any, **kwargs: Any) -> None:
        _print(end="\n")  # Add a newline
        _print(*args, **kwargs)
        builtins.print = _print

    builtins.print = patched_print
    try:
        yield
    finally:
        builtins.print = _print
