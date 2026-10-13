#!/usr/bin/env python3
"""Compatibility shim.

The canonical observer lives in docs/hw/observe_state.py, next to the other hardware
scripts. This file used to be a second, diverging copy of it; it now just runs the
canonical one so older references keep working. Edit docs/hw/observe_state.py instead.
"""
import runpy
from pathlib import Path

runpy.run_path(str(Path(__file__).resolve().with_name('hw') / 'observe_state.py'),
               run_name='__main__')
