# SPDX-License-Identifier: GPL-3.0-only
"""Puts the repository's tools/ on sys.path for a test that imports them. A test imports this module first, among its
other imports, so every import stays at the top of the file: `from tools_path import ROOT`, then `import ble_vm`."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
