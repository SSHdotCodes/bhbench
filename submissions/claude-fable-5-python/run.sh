#!/bin/sh
cd "$(dirname "$0")" && exec .venv/bin/python blackhole.py "$@"
