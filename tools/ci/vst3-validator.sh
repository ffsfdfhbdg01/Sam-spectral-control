#!/usr/bin/env bash
# Builds Steinberg's VST3 validator from the VST3 SDK (MIT) and runs its extensive tests on a
# plug-in (TST-2). Used by the CI and release workflows on Linux, macOS and Windows (Git Bash).
#
#   tools/ci/vst3-validator.sh <plug-in.vst3> [work dir]
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <plug-in.vst3> [work dir]" >&2
    exit 2
fi

plugin="$1"
work="${2:-${RUNNER_TEMP:-/tmp}/vst3sdk}"
tag="${VST3_SDK_TAG:-v3.8.1_build_84}"

if [[ ! -d "$work/vst3sdk" ]]; then
    git clone --depth 1 --branch "$tag" https://github.com/steinbergmedia/vst3sdk "$work/vst3sdk"
    git -C "$work/vst3sdk" submodule update --init --depth 1 base cmake pluginterfaces public.sdk
fi

# Only the validator of the hosting samples: the editor and audio hosts need GTK on Linux.
echo 'add_subdirectory(validator)' > "$work/vst3sdk/public.sdk/samples/vst-hosting/CMakeLists.txt"

cmake -S "$work/vst3sdk" -B "$work/build" -DCMAKE_BUILD_TYPE=Release -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF \
    -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=ON -DSMTG_RUN_VST_VALIDATOR=OFF
cmake --build "$work/build" --config Release --target validator --parallel 4

validator="$(find "$work/build/bin" -type f \( -name validator -o -name validator.exe \) | head -n 1)"
if [[ -z "$validator" ]]; then
    echo "validator was not built" >&2
    exit 1
fi
"$validator" -e "$plugin"
