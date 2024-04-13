from typing import Any, Generator
import pytest
from pathlib import Path

from pymemcache.client.base import Client

SERVER = ("127.0.0.1", 11211)

__file_dir__ = Path(__file__).resolve().parent
__project_root__ = __file_dir__.parent


def is_go_impl(client: Client) -> bool:
    return client.version().startswith(b"go")


@pytest.fixture
def client() -> Client:
    client = Client(SERVER, default_noreply=False, timeout=1.0)
    client.version()  # Check if the server is running
    assert client.sock is not None
    return client


try:
    _client = Client(SERVER, default_noreply=False)
    _client.version()  # Check if the server is running
    IS_GO_IMPLEMENTATION = is_go_impl(_client)

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


# Add a pytest mark to run some tests only for the Go implementation


def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line(
        "markers", "go_impl: Run the test only for the Go implementation"
    )
    config.addinivalue_line(
        "markers", "c_impl: Run the test only for the C implementation"
    )


def pytest_collection_modifyitems(
    config: pytest.Config, items: list[pytest.Item]
) -> None:
    for item in items:
        if "go_impl" in item.keywords and not IS_GO_IMPLEMENTATION:
            item.add_marker(
                pytest.mark.skip(reason="Test is only for the Go implementation")
            )
        elif "c_impl" in item.keywords and IS_GO_IMPLEMENTATION:
            item.add_marker(
                pytest.mark.skip(reason="Test is only for the C implementation")
            )


# Add a newline before the first print statement


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
