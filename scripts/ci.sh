#!/usr/bin/env bash
# CI entry point: format check, semantic check, build and test the Veryl project.
set -euo pipefail

cd "$(dirname "$0")/.."

veryl fmt --check
veryl check
veryl build
veryl test
