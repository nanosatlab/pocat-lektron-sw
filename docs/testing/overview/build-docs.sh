#!/bin/bash
# =============================================================================
# build-docs.sh: rebuild docker-and-test-guide.pdf from the current repository.
#
# Usage (from anywhere, Docker Desktop running):
#   bash docs/testing/overview/build-docs.sh               # tests + figures + PDF
#   bash docs/testing/overview/build-docs.sh --no-tests    # reuse last test results
#   bash docs/testing/overview/build-docs.sh --no-figures  # reuse exported figures
# From PowerShell:
#   & "C:\Program Files\Git\bin\bash.exe" docs/testing/overview/build-docs.sh
#
# Steps (each one is a throwaway container with the repo mounted):
#   1. pocat-tests + tools/collect-test-results.sh
#        -> generated/test-results.tex (+ test-run.log, build.log)
#   2. rlespinasse/drawio-export
#        figures/*.drawio -> figures/pdf/<name>.pdf (cropped)
#   3. dxjoke/tectonic-docker
#        docker-and-test-guide.tex -> docker-and-test-guide.pdf
#
# Needs the pocat-tests image (docker build -t pocat-tests -f Dockerfile.test .).
# The other two images are pulled on first use; Tectonic downloads LaTeX
# packages on its first run, so the first build needs internet.
# Outputs in generated/ and figures/pdf/ are ignored by git (see .gitignore here).
# =============================================================================
set -euo pipefail

# Pinned versions, so every PC renders the same figures.
DRAWIO_IMAGE="rlespinasse/drawio-export:v4.38.0"
TECTONIC_IMAGE="dxjoke/tectonic-docker:latest"

DOC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$DOC_DIR/../../.." && pwd)"

run_tests=1; run_figures=1
for a in "$@"; do
  case "$a" in
    --no-tests)   run_tests=0 ;;
    --no-figures) run_figures=0 ;;
    -h|--help)    sed -n '2,25p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "build-docs: unknown option $a (try --help)" >&2; exit 2 ;;
  esac
done

# Docker on Windows (Git Bash) wants Windows paths for -v and must not have
# its "/app"-style arguments rewritten by MSYS. On Linux/macOS pwd -W does
# not exist and the plain path is right.
host_path() { (cd "$1" && pwd -W 2>/dev/null) || (cd "$1" && pwd); }
# MSYS_NO_PATHCONV=1 only for docker: exported globally it would also stop
# Git Bash from translating the /c/... path given to git.exe below.
docker() { MSYS_NO_PATHCONV=1 command docker "$@"; }

docker info > /dev/null 2>&1 || { echo "build-docs: Docker is not running. Open Docker Desktop first." >&2; exit 1; }

# ---- 1. tests ---------------------------------------------------------------
if [ "$run_tests" = 1 ]; then
  echo "== 1/3 running the host tests"
  commit="$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)"
  dirty=no; [ -n "$(git -C "$REPO" status --porcelain 2>/dev/null)" ] && dirty=yes
  docker run --rm -e TR_COMMIT="$commit" -e TR_DIRTY="$dirty" \
    -v "$(host_path "$REPO"):/app" -w /app --entrypoint bash pocat-tests \
    docs/testing/overview/tools/collect-test-results.sh
else
  echo "== 1/3 skipped (--no-tests): using the existing generated/test-results.tex"
fi

# ---- 2. figures -------------------------------------------------------------
if [ "$run_figures" = 1 ]; then
  echo "== 2/3 exporting draw.io figures"
  rm -rf "$DOC_DIR/figures/pdf"
  docker run --rm -v "$(host_path "$DOC_DIR/figures"):/data" "$DRAWIO_IMAGE" \
    --format pdf --crop --remove-page-suffix --output pdf
else
  echo "== 2/3 skipped (--no-figures)"
fi

# ---- 3. PDF -----------------------------------------------------------------
echo "== 3/3 typesetting docker-and-test-guide.tex"
docker run --rm -v "$(host_path "$DOC_DIR"):/data" -w /data "$TECTONIC_IMAGE" \
  tectonic docker-and-test-guide.tex

echo "build-docs: done -> $DOC_DIR/docker-and-test-guide.pdf"
