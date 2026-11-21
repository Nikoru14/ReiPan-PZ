# PlayStation 5 (Prospero) homebrew build. It links PS5_Vulkan's RADV, so it
# uses the SDK and compiler RADV was built with (see "PlayStation 5" in the
# README):
#
#   export LLVM_CONFIG=<LLVM 22>/bin/llvm-config
#   cmake -B build-ps5 -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=<PS5_Vulkan>/.deps/native/ps5-payload-sdk/toolchain/prospero.cmake \
#         -DREIPAN_PS5_VULKAN_DIR=<PS5_Vulkan> \
#         -DCMAKE_BUILD_TYPE=RelWithDebInfo -DREIPAN_COMPILE_SHADERS=OFF
#
# The toolchain targets x86_64-sie-ps5, which reports itself as FreeBSD, so
# SDL3 would otherwise look for desktop FreeBSD backends (X11, Wayland, OSS,
# udev, D-Bus...). None of those exist on the console: switch them all off.
# SDL gets the console's own video, audio and controller drivers from ps5/sdl
# (ps5/patches/sdl.patch registers them), and Vulkan and SDL_GPU run on RADV.

if(NOT PS5)
    return()
endif()

message(STATUS "ReiPan: configuring for PlayStation 5 (ps5-payload-sdk)")

function(reipan_ps5_force name value)
    set(${name} ${value} CACHE BOOL "Forced by ReiPan's PS5 build" FORCE)
endfunction()

# --- SDL3 --------------------------------------------------------------------
foreach(sdl_off
        SDL_SHARED SDL_TESTS SDL_TEST_LIBRARY SDL_EXAMPLES SDL_INSTALL
        SDL_X11 SDL_WAYLAND SDL_KMSDRM SDL_RPI SDL_VIVANTE SDL_OFFSCREEN_EGL
        SDL_OPENGL SDL_OPENGLES
        SDL_ALSA SDL_JACK SDL_PIPEWIRE SDL_PULSEAUDIO SDL_SNDIO SDL_OSS SDL_DISKAUDIO
        SDL_DBUS SDL_IBUS SDL_LIBUDEV SDL_HIDAPI SDL_HIDAPI_LIBUSB SDL_LIBURING
        SDL_CAMERA SDL_OFFSCREEN SDL_DUMMYVIDEO SDL_DUMMYAUDIO SDL_VIRTUAL_JOYSTICK)
    reipan_ps5_force(${sdl_off} OFF)
endforeach()

# SDL_UNIX_CONSOLE_BUILD skips SDL's "no X11 or Wayland" desktop check, which
# is the documented switch for Unix-like targets without desktop windows.
foreach(sdl_on
        SDL_STATIC SDL_VULKAN SDL_GPU SDL_RENDER_GPU SDL_UNIX_CONSOLE_BUILD)
    reipan_ps5_force(${sdl_on} ON)
endforeach()

# The console's drivers, outside SDL's tree (cmake/sdl3.cmake applies the
# patch that builds them).
set(REIPAN_PS5_DIR "${CMAKE_SOURCE_DIR}/ps5")
set(SDL_PS5_PORT_DIR "${REIPAN_PS5_DIR}/sdl" CACHE INTERNAL "")
set(SDL_PS5_PLATFORM_DIR "${REIPAN_PS5_DIR}/platform" CACHE INTERNAL "")

# --- Compiler ----------------------------------------------------------------
# Clang's PS5 target writes constructors to .ctors, last to first, and the
# title's CRT runs them first to last, so a file's static initializers would
# run backwards; ps5/tools/cc-init-array.sh compiles with an init array.
set(CMAKE_C_COMPILER_LAUNCHER sh "${REIPAN_PS5_DIR}/tools/cc-init-array.sh")
# Frame pointers everywhere, so the crash report (src/reipan/reipan_ps5.cpp)
# can walk the stack.
add_compile_options(-fno-omit-frame-pointer)
# The PS5 target enables AVX2, so cglm takes its AVX path, whose loads and
# stores need 32-byte alignment; the game's PS2 matrix types only have 16
# (the first identity matrix faulted in sceVu0UnitMatrix). Unaligned
# loads and stores cost nothing measurable on the console's Zen 2.
add_compile_definitions(CGLM_ALL_UNALIGNED)
set(CMAKE_CXX_COMPILER_LAUNCHER sh "${REIPAN_PS5_DIR}/tools/cc-init-array.sh")

# --- RADV --------------------------------------------------------------------
set(REIPAN_PS5_VULKAN_DIR "" CACHE PATH
        "PS5_Vulkan checkout with its RADV release archive and native tool built")
if(NOT EXISTS "${REIPAN_PS5_VULKAN_DIR}/tools/radv-link.sh")
    message(FATAL_ERROR "ReiPan: set REIPAN_PS5_VULKAN_DIR to a PS5_Vulkan checkout "
            "(https://github.com/mihawk-99/PS5_Vulkan) with RADV built")
endif()
# The link is PS5_Vulkan's recipe (ps5/tools/link-title.sh), not the compiler's.
set(CMAKE_CXX_LINK_EXECUTABLE
        "bash ${REIPAN_PS5_DIR}/tools/link-title.sh <TARGET> ${CMAKE_BINARY_DIR}/ps5/link ${REIPAN_PS5_VULKAN_DIR} ${PS5_PAYLOAD_SDK} <OBJECTS> <LINK_LIBRARIES>")

# --- Title packaging ---------------------------------------------------------
# The game is linked as a native title (not a payload) and staged as a folder
# that ShadowMount+ can mount:
#
#   build-ps5/app/<TITLE_ID>/
#     eboot.bin              converted and fake-signed by ps5-native-tool
#     sce_sys/param.json     from ps5/sce_sys/param.json.in
#     sce_sys/icon0.png
#     sce_module/libc.prx    clean-room runtime module
#     resources/             the game's own resources
#
# ps5-native-tool and libc.prx are PS5_Vulkan's (tools/rebuild-libc.sh builds
# both).
set(REIPAN_PS5_TITLE_ID "PPSA39393" CACHE STRING
        "PS5 title ID: PPSA followed by five digits, unique on the console")
set(REIPAN_PS5_CONCEPT_ID "39393" CACHE STRING "PS5 concept ID: five digits")
set(REIPAN_PS5_CONTENT_SUFFIX "REIPANPROSPERO01" CACHE STRING
        "Last part of the PS5 content ID: 16 characters A-Z or 0-9")
set(REIPAN_PS5_TITLE_NAME "ReiPan PS5" CACHE STRING "Name shown on the PS5 home screen")
set(REIPAN_PS5_BACKGROUND "" CACHE FILEPATH
        "Optional picture (PNG/JPEG, 16:9) for the home screen and the launch screen; not part of the repository")
set(REIPAN_PS5_LAUNCH_BACKGROUND "" CACHE FILEPATH
        "Optional different picture for the launch screen (defaults to REIPAN_PS5_BACKGROUND)")
set(REIPAN_PS5_BUILD_LABEL "ReiPanPS5-dev" CACHE STRING
        "Build name on the title screen, in the start-up notification and the log")
set(REIPAN_PS5_NATIVE_TOOL "${REIPAN_PS5_VULKAN_DIR}/build/runtime-shim/ps5-native-tool"
        CACHE FILEPATH "ps5-native-tool (PS5_Vulkan's tools/rebuild-libc.sh builds it)")
set(REIPAN_PS5_RUNTIME_MODULE "${REIPAN_PS5_VULKAN_DIR}/runtime/libc.prx" CACHE FILEPATH
        "The clean-room libc.prx runtime module")

if(NOT REIPAN_PS5_TITLE_ID MATCHES "^PPSA[0-9][0-9][0-9][0-9][0-9]$")
    message(FATAL_ERROR "REIPAN_PS5_TITLE_ID must be PPSA followed by five digits")
endif()
if(NOT REIPAN_PS5_CONCEPT_ID MATCHES "^[0-9][0-9][0-9][0-9][0-9]$")
    message(FATAL_ERROR "REIPAN_PS5_CONCEPT_ID must be five digits")
endif()
if(NOT REIPAN_PS5_CONTENT_SUFFIX MATCHES "^[A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9]$")
    message(FATAL_ERROR "REIPAN_PS5_CONTENT_SUFFIX must be 16 characters A-Z or 0-9")
endif()
set(REIPAN_PS5_CONTENT_ID
        "UP9000-${REIPAN_PS5_TITLE_ID}_00-${REIPAN_PS5_CONTENT_SUFFIX}")

# Loader and container constants used by ps5-native-app-boilerplate, which it
# validated on firmware 6.02 and 12.70.
set(REIPAN_PS5_MODULE_SDK 0x02000009)
set(REIPAN_PS5_COMPANION_SDK 0x08050001)
set(REIPAN_PS5_FSELF_MAGIC 0x1D3D154F)

function(reipan_ps5_title target)
    set(native "${REIPAN_PS5_DIR}/native")

    # The console's C files are C: everything under src/ is compiled as C++.
    enable_language(C)
    target_sources(${target} PRIVATE
            "${native}/app_crt.cpp"
            "${REIPAN_PS5_DIR}/platform/platform.c"
            "${REIPAN_PS5_DIR}/platform/shims.c"
            "${REIPAN_PS5_DIR}/platform/libc_extras.c"
            "${CMAKE_SOURCE_DIR}/src/reipan/reipan_ps5.cpp")
    target_include_directories(${target} PRIVATE "${REIPAN_PS5_DIR}/platform"
            "${REIPAN_PS5_VULKAN_DIR}/.deps/native/radv-release/include")
    set_source_files_properties("${CMAKE_SOURCE_DIR}/src/reipan/reipan_ps5.cpp"
            PROPERTIES COMPILE_DEFINITIONS "REIPAN_PS5_TITLE_NAME=\"${REIPAN_PS5_TITLE_NAME}\"")
    target_compile_definitions(${target} PRIVATE
            "REIPAN_PS5_BUILD_LABEL=\"${REIPAN_PS5_BUILD_LABEL}\"")
    # In an optimised build the game itself gets -O1 with the guards that
    # code decompiled from the PS2 relies on (the desktop release build has
    # known issues); the libraries keep the build type's -O2.
    target_compile_options(${target} PRIVATE
            $<$<NOT:$<CONFIG:Debug>>:-O1 -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks>)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
            "${native}/app-symbols.map" "${native}/ps5-pie.ld"
            "${REIPAN_PS5_DIR}/tools/link-title.sh"
            "${REIPAN_PS5_VULKAN_DIR}/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a")

    if(NOT REIPAN_PS5_NATIVE_TOOL OR NOT REIPAN_PS5_RUNTIME_MODULE)
        message(STATUS "ReiPan: PS5 title folder skipped; set "
                "REIPAN_PS5_NATIVE_TOOL and REIPAN_PS5_RUNTIME_MODULE to build it")
        return()
    endif()
    foreach(required REIPAN_PS5_NATIVE_TOOL REIPAN_PS5_RUNTIME_MODULE)
        if(NOT EXISTS "${${required}}")
            message(FATAL_ERROR "${required} does not exist: ${${required}}")
        endif()
    endforeach()

    set(app "${CMAKE_BINARY_DIR}/app/${REIPAN_PS5_TITLE_ID}")
    set(eboot_elf "${CMAKE_BINARY_DIR}/ps5/eboot.elf")
    set(param "${CMAKE_BINARY_DIR}/ps5/param.json")
    configure_file("${REIPAN_PS5_DIR}/sce_sys/param.json.in" "${param}" @ONLY)

    # Home screen art: pic0.dds behind the title when it is selected,
    # pic1.dds while it starts; 3840x2160 BC7 DDS made from a picture the
    # builder supplies (ps5/tools/png-to-title-dds.py, needs numpy and Pillow).
    set(background_copies)
    if(REIPAN_PS5_BACKGROUND)
        set(launch "${REIPAN_PS5_LAUNCH_BACKGROUND}")
        if(NOT launch)
            set(launch "${REIPAN_PS5_BACKGROUND}")
        endif()
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(art_outputs)
        foreach(pair "pic0|${REIPAN_PS5_BACKGROUND}" "pic1|${launch}")
            string(REPLACE "|" ";" pair "${pair}")
            list(GET pair 0 name)
            list(GET pair 1 picture)
            if(NOT EXISTS "${picture}")
                message(FATAL_ERROR "PS5 background picture does not exist: ${picture}")
            endif()
            set(dds "${CMAKE_BINARY_DIR}/ps5/${name}.dds")
            add_custom_command(OUTPUT "${dds}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/ps5"
                    COMMAND Python3::Interpreter "${REIPAN_PS5_DIR}/tools/png-to-title-dds.py"
                            "${picture}" "${dds}"
                    DEPENDS "${picture}" "${REIPAN_PS5_DIR}/tools/png-to-title-dds.py"
                    COMMENT "Converting ${picture} to ${name}.dds"
                    VERBATIM)
            list(APPEND art_outputs "${dds}")
            list(APPEND background_copies
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dds}"
                            "${app}/sce_sys/${name}.dds")
        endforeach()
        add_custom_target(reipan_ps5_art DEPENDS ${art_outputs})
        add_dependencies(${target} reipan_ps5_art)
        message(STATUS "ReiPan: PS5 home screen art from ${REIPAN_PS5_BACKGROUND}")
    endif()

    set(tool "${REIPAN_PS5_NATIVE_TOOL}")
    add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${CMAKE_BINARY_DIR}/ps5" "${app}/sce_sys" "${app}/sce_module"
            COMMAND "${tool}" link --in "$<TARGET_FILE:${target}>" --out "${eboot_elf}"
                    --stub-dir "${PS5_PAYLOAD_SDK}/target/lib"
                    --stub "${CMAKE_BINARY_DIR}/ps5/link/stubs/libSceAgc.so"
                    --stub "${CMAKE_BINARY_DIR}/ps5/link/stubs/libSceAgcDriver.so"
                    --module-sdk ${REIPAN_PS5_MODULE_SDK}
                    --companion-sdk ${REIPAN_PS5_COMPANION_SDK}
                    --file-name eboot.elf
            COMMAND "${tool}" self --sign --in "${eboot_elf}" --out "${app}/eboot.bin"
                    --magic ${REIPAN_PS5_FSELF_MAGIC}
            COMMAND "${tool}" self --inspect --file "${app}/eboot.bin"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${param}"
                    "${app}/sce_sys/param.json"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${REIPAN_PS5_DIR}/sce_sys/icon0.png" "${app}/sce_sys/icon0.png"
            ${background_copies}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${REIPAN_PS5_RUNTIME_MODULE}" "${app}/sce_module/libc.prx"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_SOURCE_DIR}/resources" "${app}/resources"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${app}/resources/fonts"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${REIPAN_RMLUI_FONT_FILES} "${app}/resources/fonts"
            COMMENT "Staging PS5 title folder ${app}"
            VERBATIM)
    message(STATUS "ReiPan: PS5 title folder ${app}")
endfunction()

# --- FFmpeg ------------------------------------------------------------------
# Passed to FFmpeg's own configure script by cmake/ffmpeg.cmake.
set(REIPAN_FFMPEG_CROSS_ARGS
        --enable-cross-compile
        --target-os=freebsd
        --arch=x86_64
        --ar=${CMAKE_AR}
        --ranlib=${CMAKE_RANLIB}
        --nm=${PS5_PAYLOAD_SDK}/bin/prospero-nm
        --strip=${CMAKE_STRIP}
        --host-cc=cc
        --pkg-config=false)
