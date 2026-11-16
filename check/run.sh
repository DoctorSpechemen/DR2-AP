#!/bin/sh
# Compiles the plugin's Archipelago logic (Ap.inc) against a pretend game and runs it. Needs g++.
set -e
cd "$(dirname "$0")/unit"
g++ -std=c++20 -w -o harness harness.cpp
./harness | tail -n 20
