# Configuration file for the Sphinx documentation builder.
# https://www.sphinx-doc.org/en/master/usage/configuration.html

import subprocess
import os

# -- Project information -------------------------------------------------------
project = "xE310 Modem Library"
copyright = "2024, zimbora"
author = "zimbora"
release = "1.0"

# -- General configuration -----------------------------------------------------
extensions = [
    "breathe",
    "sphinx.ext.autodoc",
    "sphinx.ext.viewcode",
]

templates_path = ["_templates"]
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store", "doxygen"]

# -- HTML output ---------------------------------------------------------------
html_theme = "sphinx_rtd_theme"
html_static_path = []
html_title = "xE310 Modem Library Documentation"
html_short_title = "xE310 Docs"

# -- Breathe configuration -----------------------------------------------------
breathe_projects = {
    "xE310ModemLibrary": os.path.join(os.path.dirname(__file__), "doxygen", "xml"),
}
breathe_default_project = "xE310ModemLibrary"
breathe_default_members = ("members", "undoc-members")
