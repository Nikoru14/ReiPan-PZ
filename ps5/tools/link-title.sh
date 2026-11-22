#!/usr/bin/env bash
# ReiPan for the PlayStation 5 - link the title's ELF with RADV.
#
#   link-title.sh OUTPUT WORK_DIR PS5_VULKAN_DIR SDK OBJECT...
#
# CMake runs this in place of the linker for the ReiPan executable
# (cmake/ps5.cmake sets CMAKE_CXX_LINK_EXECUTABLE). The link is PS5_Vulkan's
# (https://github.com/mihawk-99/PS5_Vulkan): its RADV release archive, linked
# whole, and its recipe, tools/radv-link.sh (the SDK's platform layer, the
# heap and thread wraps, the libc names bound to ps5_*). The ELF it writes to
# OUTPUT is then converted, signed and staged by cmake/ps5.cmake.
#
# Adapted from PS5_Dusklight's ps5/tools/link-title.sh
# (griches/PS5_Dusklight, commit 72d6885, MIT).
#
# SPDX-License-Identifier: MIT

set -euo pipefail
[[ -n ${LINK_TRACE:-} ]] && set -x

out=$1 work=$2 vulkan=$3 sdk=$4
shift 4
ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

# CMake's link line, as it would go to the compiler driver: keep the objects
# and archives. The libraries named with -l are the SDK's stubs, which the
# link below takes from the SDK as needed; zlib is in the RADV archive already
# (Mesa's own, linked whole), so a second copy would define every name twice.
objects=()
for argument in "$@"; do
    case $argument in
    -Xlinker | --start-group | --end-group | -Wl,* | -l* | -pthread | -fPIC) ;;
    */libz.a | */libzlibstatic.a | */libzlib.a) ;;
    *) objects+=("$argument") ;;
    esac
done

archive=${RADV_ARCHIVE:-$vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a}
for file in "$archive" "$sdk/bin/prospero-lld" "$vulkan/tools/radv-link.sh" \
        "$ps5/native/ps5-pie.ld" "$ps5/native/app-symbols.map"; do
    [[ -e $file ]] || { echo "link-title.sh: missing $file" >&2; exit 2; }
done
mkdir -p "$work/obj" "$work/stubs" "$work/clang-resource"

# The compiler the SDK's wrapper runs (LLVM_CONFIG picks it: LLVM 22)
llvm_bindir=$("$sdk/bin/prospero-llvm-config" --bindir)
compiler="$llvm_bindir/clang"
cc() { "$sdk/bin/prospero-clang" "$@"; }

# RADV calls AGC, which the SDK has no stubs for: these name its imports.
stub() {
    local library=$1 source=$2
    cc -std=c11 -O2 -fPIC -c "$vulkan/$source" -o "$work/obj/${library}_stub.o"
    "$sdk/bin/prospero-lld" --shared -soname "${library}.prx" \
        -o "$work/stubs/${library}.so" "$work/obj/${library}_stub.o"
}
stub libSceAgc vendor/ps5/sdk/stubs/agc_canary_link_stub.c
stub libSceAgcDriver vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c

# The recipe finds Clang's builtins (__emutls_get_address) under the resource
# directory's lib/linux; LLVM's own release builds keep them under the target
# triple. Point the recipe at a directory that has them where it looks.
resource=$("$compiler" --print-resource-dir)
builtins="$resource/lib/linux/libclang_rt.builtins-x86_64.a"
if [[ ! -f $builtins ]]; then
    builtins="$resource/lib/x86_64-unknown-linux-gnu/libclang_rt.builtins.a"
    [[ -f $builtins ]] || { echo "link-title.sh: no Clang builtins under $resource" >&2; exit 2; }
fi
mkdir -p "$work/clang-resource/lib/linux"
ln -sf "$builtins" "$work/clang-resource/lib/linux/libclang_rt.builtins-x86_64.a"
printf '#!/bin/sh\necho %q\n' "$work/clang-resource" > "$work/clang-resource/print"
chmod +x "$work/clang-resource/print"

# shellcheck source=/dev/null
source "$vulkan/tools/radv-link.sh"
PS5_CLANG="$work/clang-resource/print" radv_link_recipe "$vulkan" "$sdk" "$archive" || exit 2
# ReiPan's own layout: .got opens the RELRO segment, without which the
# console refuses the title (CE-107750-0), and it defines the eh_frame markers.
radv_linker_script=(-T "$ps5/native/ps5-pie.ld")

# libc names that no module a title loads exports, which SDL and the libraries
# call: ps5/platform/shims.c answers each as ps5dl_<name>, kept local like the
# recipe's own bound names.
shims=(isatty getresuid getresgid dl_iterate_phdr fchown fork vfork setsid pathconf getnameinfo gai_strerror lstat)
{ printf '{\n    local:\n'; printf '        %s;\n' "${shims[@]}"; printf '};\n'; } > "$work/shims-local.map"
for name in "${shims[@]}"; do
    radv_link_flags+=("--defsym=$name=ps5dl_$name")
done
radv_link_flags+=(--version-script "$work/shims-local.map")

"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$ps5/native/app-symbols.map" --exclude-libs=ALL \
    -e _start -o "$out.new" \
    --start-group "${objects[@]}" --end-group \
    "$work/stubs/libSceAgc.so" "$work/stubs/libSceAgcDriver.so" \
    "${radv_link_inputs[@]}" \
    --as-needed "$sdk"/target/lib/*.so

# A title loads neither libkernel_sys's exports nor libScePosixForWebKit's: an
# import only their stubs define links, but is null at run time, so its first
# call jumps to address 0. Refuse that here rather than on the console.
null_imports=$(comm -23 \
    <("$sdk/bin/llvm-nm" -D --undefined-only "$out.new" |
        awk '$1 == "U" { sub(/@.*/, "", $2); print $2 }' | sort -u) \
    <(for library in "$sdk"/target/lib/*.so "$work/stubs/libSceAgc.so" "$work/stubs/libSceAgcDriver.so"; do
        case ${library##*/} in libkernel_sys.so | libScePosixForWebKit.so) continue ;; esac
        "$sdk/bin/llvm-nm" -D --defined-only "$library" 2>/dev/null | awk '{ print $NF }'
    done | sort -u))
if [[ -n $null_imports ]]; then
    echo "link-title.sh: imports that no module a title loads exports (null at run time): ${null_imports//$'\n'/ }" >&2
    exit 1
fi
mv "$out.new" "$out"
