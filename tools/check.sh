#!/usr/bin/env bash
# The verification gate: release + asan builds with zero warnings, every selftest, clang-format check.
# Docs-sync and header checks only warn. Usage: tools/check.sh [--fast]   (--fast skips the asan build)
#
# Windows: run from a shell where the MSVC environment is loaded (vcvars64.bat) or set BW_VCVARS to its path.
set -u
cd "$(dirname "$0")/.."

FAIL=0
fail() { echo "CHECK FAILED: $*"; FAIL=1; }
warn() { echo "advisory: $*"; }

run_in_env() {
    if [ -n "${BW_VCVARS:-}" ]; then
        cmd //c "call \"$BW_VCVARS\" >nul && $*"
    else
        "$@"
    fi
}

build_preset() {
    local preset=$1 log
    log=$(mktemp)
    echo "== build $preset"
    run_in_env cmake --preset "$preset" -DBW_WERROR=ON >"$log" 2>&1 || { tail -20 "$log"; fail "configure $preset"; rm -f "$log"; return 1; }
    run_in_env cmake --build "build/$preset" >"$log" 2>&1 || { grep -E "error|warning" "$log" | head -30; fail "build $preset (warnings are errors)"; rm -f "$log"; return 1; }
    if grep -qiE "warning( [A-Z]*[0-9]+)?:" "$log"; then
        grep -iE "warning( [A-Z]*[0-9]+)?:" "$log" | head -20
        fail "warnings in $preset build"
    fi
    rm -f "$log"
    return 0
}

server_bin() {
    local d=build/$1
    [ -x "$d/server.exe" ] && echo "$d/server.exe" || echo "$d/server"
}

run_selftests() {
    local preset=$1 bin
    bin=$(server_bin "$preset")
    echo "== selftests ($preset)"
    "$bin" --selftest all || fail "selftests in $preset"
}

presets="release asan"
[ "${1:-}" = "--fast" ] && presets="release"
for p in $presets; do
    build_preset "$p" && run_selftests "$p"
done

echo "== clang-format"
if command -v clang-format >/dev/null 2>&1; then
    files=$(git ls-files 'src/*.cpp' 'src/*.hpp')
    # shellcheck disable=SC2086
    clang-format --dry-run --Werror $files >/dev/null 2>&1 || fail "clang-format: run clang-format -i on src files"
else
    warn "clang-format not installed: format check skipped"
fi

echo "== advisory docs checks"
for hook in $(grep -rhoE 'getenv\("BW_[A-Z_]+"\)' src | sed -E 's/getenv\("(.*)"\)/\1/' | sort -u); do
    grep -q "$hook" ARCHITECTURE.md || warn "env hook $hook is not documented in ARCHITECTURE.md"
done
bin=$(server_bin release)
if [ -x "$bin" ]; then
    for name in $("$bin" --selftest list | cut -f1); do
        grep -q "$name" ARCHITECTURE.md || warn "selftest $name is not documented in ARCHITECTURE.md"
    done
fi
for f in $(git ls-files 'src/*.cpp' 'src/*.hpp'); do
    head -3 "$f" | grep -q "@file" || warn "$f has no Doxygen header"
done

[ $FAIL -eq 0 ] && echo "CHECK OK" || { echo "CHECK FAILED"; exit 1; }
