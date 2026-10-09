# SPDX-License-Identifier: GPL-3.0-only
"""Puts tools/builder/ (and tools/, through tools_path) on sys.path for a test of the firmware builder. A test imports
this module first, among its other imports, so every import stays at the top of the file: `from builder_path import ROOT`,
then `import configure as C`."""
import sys

from tools_path import ROOT, TOOLS

BUILDER = TOOLS / "builder"
if str(BUILDER) not in sys.path:
    sys.path.insert(0, str(BUILDER))

__all__ = ["BUILDER", "ROOT", "TOOLS"]
