#!/usr/bin/env bash
# The CI checks on this machine, without GitHub Actions minutes (docs/release.md):
# clang-format, a release build with warnings as errors, every test with a 1 MB stack, pluginval
# (strictness 10), Steinberg's VST3 validator and, where the compilers exist, the RealtimeSanitizer
# (Clang 20) and ASan/UBSan builds. Linux or macOS.
#
#   tools/ci/local-check.sh [--quick]    --quick: format, build and tests only
#
# PLUGINVAL=<path> picks the pluginval binary (otherwise `pluginval` from PATH).
set -uo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root" || exit 1
quick=0
if [[ "${1:-}" == "--quick" ]]; then
    quick=1
fi

failed=()
skipped=()
step() { printf '\n== %s\n' "$*"; }
check() { # <name> <command…>: runs the command and records a failure
    local name="$1"
    shift
    if ! "$@"; then
        failed+=("$name")
    fi
}

step "clang-format"
if command -v clang-format-18 > /dev/null; then
    check format bash -c "git ls-files 'src/*.cpp' 'src/*.h' 'tests/*.cpp' 'tests/*.h' 'tools/*.cpp' \
        | xargs clang-format-18 --style=file --dry-run --Werror"
else
    skipped+=("clang-format (clang-format-18 not found)")
fi

step "Release build, warnings as errors"
if cmake --preset release -DSSC_WARNINGS_AS_ERRORS=ON > /dev/null && cmake --build --preset release --parallel; then
    step "Tests (1 MB stack)"
    check tests bash -c "ulimit -s 1024 && ctest --preset release"
else
    failed+=("release build")
fi

plugin="build/release/src/SamSpectralControl_artefacts/Release/VST3/Sam Spectral Control.vst3"
if [[ $quick == 0 ]]; then
    step "pluginval (strictness 10)"
    pluginval="${PLUGINVAL:-$(command -v pluginval || true)}"
    if [[ -n "$pluginval" && -x "$pluginval" && -d "$plugin" ]]; then
        runner=()
        if [[ "$(uname)" == Linux && -z "${DISPLAY:-}" ]] && command -v xvfb-run > /dev/null; then
            runner=(xvfb-run -a)
        fi
        check pluginval ${runner[@]+"${runner[@]}"} "$pluginval" --strictness-level 10 --validate-in-process \
            --output-dir "build/pluginval-report" "$plugin"
    else
        skipped+=("pluginval (set PLUGINVAL or put pluginval in PATH)")
    fi

    step "VST3 validator (Steinberg, extensive)"
    if [[ -d "$plugin" ]]; then
        check "VST3 validator" bash tools/ci/vst3-validator.sh "$plugin" "$root/build/vst3sdk"
    else
        skipped+=("VST3 validator (no plug-in built)")
    fi

    step "RealtimeSanitizer (Clang 20)"
    if command -v clang++-20 > /dev/null; then
        if CC=clang-20 CXX=clang++-20 cmake --preset rtsan -DSSC_WARNINGS_AS_ERRORS=ON > /dev/null \
            && cmake --build --preset rtsan --parallel; then
            check RealtimeSanitizer bash -c "ulimit -s 1024 && ctest --preset rtsan"
        else
            failed+=("RealtimeSanitizer build")
        fi
    else
        skipped+=("RealtimeSanitizer (clang++-20 not found)")
    fi

    step "ASan/UBSan (DSP core)"
    if command -v clang++ > /dev/null; then
        if CC=clang CXX=clang++ cmake --preset sanitize -DSSC_WARNINGS_AS_ERRORS=ON > /dev/null \
            && cmake --build --preset sanitize --parallel; then
            check "ASan/UBSan" ctest --preset sanitize
        else
            failed+=("ASan/UBSan build")
        fi
    else
        skipped+=("ASan/UBSan (clang++ not found)")
    fi
fi

step "Summary"
for s in ${skipped[@]+"${skipped[@]}"}; do
    echo "skipped: $s"
done
if [[ ${#failed[@]} -gt 0 ]]; then
    for f in "${failed[@]}"; do
        echo "FAILED: $f"
    done
    exit 1
fi
echo "All checks passed."
