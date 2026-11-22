include(FetchContent)

if(PS5)
    # sdl.patch: the console's video, audio and controller drivers (ps5/sdl),
    # registered with SDL and built when the toolchain says PS5.
    # sdl-gpu-conformance.patch: SDL_GPU takes RADV, which reports no Vulkan
    # conformance version on the console yet.
    # sdl-ps5-timer.patch: SDL's performance counter reads the CPU's time
    # stamp counter instead of clock_gettime, a system call on the console.
    # Each is applied once: the check skips a tree that already has it.
    set(REIPAN_SDL_PATCH PATCH_COMMAND
            sh -c "for p in \"$@\"; do git apply --reverse --check \"$p\" 2>/dev/null || git apply --whitespace=nowarn \"$p\" || exit 1; done"
            sh
            "${CMAKE_SOURCE_DIR}/ps5/patches/sdl.patch"
            "${CMAKE_SOURCE_DIR}/ps5/patches/sdl-gpu-conformance.patch"
            "${CMAKE_SOURCE_DIR}/ps5/patches/sdl-ps5-timer.patch")
else()
    set(REIPAN_SDL_PATCH "")
endif()

FetchContent_Declare(
        sdl3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.12
        ${REIPAN_SDL_PATCH}
)
if(ANDROID)
    set(SDL_STATIC OFF)
    set(SDL_SHARED ON)
    set(SDL_ANDROID_JAR ON)
else()
    set(SDL_STATIC ON)
    set(SDL_SHARED OFF)
endif()
FetchContent_MakeAvailable(sdl3)

foreach(sdl_target SDL3-static SDL3-shared SDL3_test)
    if(TARGET ${sdl_target})
        set_target_properties(${sdl_target} PROPERTIES
                DISABLE_PRECOMPILE_HEADERS ON)
    endif()
endforeach()

if(ANDROID)
    set(REIPAN_SDL_ACTIVITY_JAVA
            "${sdl3_SOURCE_DIR}/android-project/app/src/main/java/org/libsdl/app/SDLActivity.java")
    if(EXISTS "${REIPAN_SDL_ACTIVITY_JAVA}")
        file(READ "${REIPAN_SDL_ACTIVITY_JAVA}" REIPAN_SDL_ACTIVITY_CONTENT)
        set(REIPAN_SDL_ACTIVITY_OPEN_FD_OLD [=[
        } catch (FileNotFoundException e) {
            e.printStackTrace();
            return -1;
        }
]=])
        set(REIPAN_SDL_ACTIVITY_OPEN_FD_NEW [=[
        } catch (Exception e) {
            e.printStackTrace();
            return -1;
        }
]=])
        if(REIPAN_SDL_ACTIVITY_CONTENT MATCHES "catch \\(FileNotFoundException e\\)")
            string(REPLACE "${REIPAN_SDL_ACTIVITY_OPEN_FD_OLD}"
                    "${REIPAN_SDL_ACTIVITY_OPEN_FD_NEW}"
                    REIPAN_SDL_ACTIVITY_CONTENT
                    "${REIPAN_SDL_ACTIVITY_CONTENT}")
            file(WRITE "${REIPAN_SDL_ACTIVITY_JAVA}" "${REIPAN_SDL_ACTIVITY_CONTENT}")
        endif()
    endif()

    set(REIPAN_SDL_ANDROID_C
            "${sdl3_SOURCE_DIR}/src/core/android/SDL_android.c")
    if(EXISTS "${REIPAN_SDL_ANDROID_C}")
        file(READ "${REIPAN_SDL_ANDROID_C}" REIPAN_SDL_ANDROID_CONTENT)
        set(REIPAN_SDL_OPEN_FD_JNI_OLD [=[
    jint fd = (*env)->CallStaticIntMethod(env, mActivityClass, midOpenFileDescriptor, jstringUri, jstringMode);
    (*env)->DeleteLocalRef(env, jstringUri);
]=])
        set(REIPAN_SDL_OPEN_FD_JNI_NEW [=[
    jint fd = (*env)->CallStaticIntMethod(env, mActivityClass, midOpenFileDescriptor, jstringUri, jstringMode);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        fd = -1;
    }
    (*env)->DeleteLocalRef(env, jstringUri);
]=])
        string(FIND "${REIPAN_SDL_ANDROID_CONTENT}"
                "${REIPAN_SDL_OPEN_FD_JNI_NEW}"
                REIPAN_SDL_OPEN_FD_JNI_ALREADY_PATCHED)
        if(REIPAN_SDL_OPEN_FD_JNI_ALREADY_PATCHED EQUAL -1)
            string(REPLACE "${REIPAN_SDL_OPEN_FD_JNI_OLD}"
                    "${REIPAN_SDL_OPEN_FD_JNI_NEW}"
                    REIPAN_SDL_ANDROID_CONTENT
                    "${REIPAN_SDL_ANDROID_CONTENT}")
            file(WRITE "${REIPAN_SDL_ANDROID_C}" "${REIPAN_SDL_ANDROID_CONTENT}")
        endif()
    endif()
endif()
