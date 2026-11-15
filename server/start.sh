#!/bin/sh
# Runs the bridge without Docker. Creates a Python environment on first run.
cd "$(dirname "$0")" || exit 1
[ -d .venv ] || { python3 -m venv .venv && .venv/bin/pip install -q -r requirements.txt; }
exec .venv/bin/python server.py
