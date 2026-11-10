#!/usr/bin/env python3
"""joycode.py: the JOYPOLIS code at SELECT A COURSE (MEDIUM highlighted): d-pad down, up, then Y (Switch: top button X),
all within 90 fields, so held ~0.1 s each instead of sb.py's click timing."""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sb import cmd
for b in ("DDOWN", "DUP", "X"):
    cmd("press " + b); time.sleep(0.1); cmd("release " + b); time.sleep(0.12)
