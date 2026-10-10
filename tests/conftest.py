import pytest

from panelkit.config import load_footprints, load_layout, load_theme
from panelkit.text import FontBook


@pytest.fixture(scope="session")
def footprints():
    return load_footprints()


@pytest.fixture(scope="session")
def layout():
    return load_layout()


@pytest.fixture(scope="session")
def theme():
    return load_theme()


@pytest.fixture(scope="session")
def fontbook(theme):
    return FontBook(theme)
