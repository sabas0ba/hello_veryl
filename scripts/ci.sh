#!/usr/bin/env bash
# CI entry point: build and test the Veryl project.
set -euo pipefail

cd "$(dirname "$0")/.."

veryl build
veryl test
