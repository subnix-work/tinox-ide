#!/usr/bin/env python3
"""Reads the accessibility tree of the running IDE over AT-SPI, the way a screen reader does.

Usage: atspi-tree.py <application name> <expected role:name> ...
Prints every (role, name) found below the application and fails when an expected pair is missing.
"""
import sys
import time

import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi  # noqa: E402


def walk(node, out, depth=0):
    if depth > 40:
        return
    try:
        out.add((node.get_role_name(), node.get_name()))
        for i in range(node.get_child_count()):
            child = node.get_child_at_index(i)
            if child is not None:
                walk(child, out, depth + 1)
    except Exception:
        pass


def main():
    app_name, expected = sys.argv[1], [e.split(":", 1) for e in sys.argv[2:]]
    desktop = Atspi.get_desktop(0)
    app = None
    for _ in range(100):
        for i in range(desktop.get_child_count()):
            child = desktop.get_child_at_index(i)
            if child is not None and child.get_name() == app_name:
                app = child
        if app:
            break
        time.sleep(0.2)
    if app is None:
        print(f"application '{app_name}' is not on the accessibility bus", file=sys.stderr)
        return 2
    found = set()
    walk(app, found)
    for role, name in sorted(found):
        print(f"{role}: {name}")
    missing = [f"{role}:{name}" for role, name in expected if (role, name) not in found]
    if missing:
        print("missing from the accessibility tree: " + ", ".join(missing), file=sys.stderr)
        return 1
    return 0


sys.exit(main())
