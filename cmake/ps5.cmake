set(DEPS "${CMAKE_SOURCE_DIR}/build/ps5-deps")
set(OP "$ENV{OPENPROSPERO_SDK}")
set(MESA_SOURCE "${CMAKE_SOURCE_DIR}/../../PS5_Mesa" CACHE PATH "PS5_Mesa Vulkan headers")
set(RADV "${DEPS}/lib/libvulkan_radeon.a")
set(IMGUI "${CMAKE_SOURCE_DIR}/third_party/imgui")
set(PLATFORM "${CMAKE_SOURCE_DIR}/build/ps5-platform-source/platform")
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/build/broker-config/broker_config.hpp")
    message(FATAL_ERROR "Run tools/build-shared-broker.ps1 before building the console client")
endif()
set(CMAKE_ASM_COMPILER clang-19)
set(CMAKE_ASM_FLAGS "--target=x86_64-sie-ps5")
enable_language(ASM)
add_library(plist STATIC
    ${CMAKE_SOURCE_DIR}/reference/libplist/libcnary/node.c
    ${CMAKE_SOURCE_DIR}/reference/libplist/libcnary/node_list.c)
set(PLIST_SOURCES base64 bytearray hashtable ptrarray time64 xplist bplist jsmn jplist oplist out-default out-plutil out-limd plist)
foreach(SOURCE ${PLIST_SOURCES})
    target_sources(plist PRIVATE "${CMAKE_SOURCE_DIR}/reference/libplist/src/${SOURCE}.c")
endforeach()
target_include_directories(plist PUBLIC "${CMAKE_SOURCE_DIR}/reference/libplist/include" PRIVATE "${CMAKE_SOURCE_DIR}/reference/libplist" "${CMAKE_SOURCE_DIR}/reference/libplist/src" "${CMAKE_SOURCE_DIR}/reference/libplist/libcnary/include")
target_compile_definitions(plist PRIVATE PACKAGE_VERSION="2.3.0" HAVE_ATTRIBUTE_CONSTRUCTOR=1 HAVE_TM_TM_GMTOFF=1 HAVE_TM_TM_ZONE=1 HAVE_STRNDUP=1)
set(UX "${CMAKE_SOURCE_DIR}/third_party/uxplay/lib")
file(GLOB AIRPLAY_SOURCES "${UX}/*.c" "${UX}/playfair/*.c" "${UX}/llhttp/*.c" "${UX}/mdnsd/*.c")
add_library(airplay STATIC ${AIRPLAY_SOURCES})
target_sources(airplay PRIVATE src/platform/numeric_address.c)
target_include_directories(airplay PRIVATE src)
target_include_directories(airplay PUBLIC "${UX}" PRIVATE "${UX}/playfair" "${UX}/llhttp" "${UX}/mdnsd" "${DEPS}/include" "${CMAKE_SOURCE_DIR}/reference/libplist/include")
target_compile_definitions(airplay PRIVATE MIRROR_PS5 SYS_ENDIAN_H PLIST_210 PLIST_230 NOHOLD OPENSSL_API_COMPAT=0x10101000L)
target_link_libraries(airplay PUBLIC plist "${DEPS}/lib/libcrypto.a")
add_executable(MirrorPSX
    src/platform/ps5.cpp src/platform/ps5_input.cpp src/platform/video_texture.cpp src/ui/screen.cpp src/ui/assets.cpp
    src/platform/ps5_identity.c src/platform/ps5_identity_gate.S
    src/platform/ps5_random.cpp
    src/platform/ps5_broker.cpp
    src/receiver/airplay.cpp src/receiver/decoder.cpp src/receiver/frame_converter.cpp
    src/receiver/dlna.cpp src/receiver/media_player.cpp src/platform/audio_queue.cpp
    ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_vulkan.cpp third_party/volk/volk.c)
target_include_directories(MirrorPSX PRIVATE src ${IMGUI} ${IMGUI}/backends third_party/volk "${MESA_SOURCE}/include" "${DEPS}/include" "${DEPS}/include/libxml2")
target_compile_definitions(MirrorPSX PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK VK_NO_PROTOTYPES MIRROR_PS5)
target_link_libraries(MirrorPSX PRIVATE airplay "${DEPS}/lib/libavformat.a" "${DEPS}/lib/libavcodec.a" "${DEPS}/lib/libswscale.a" "${DEPS}/lib/libswresample.a" "${DEPS}/lib/libavutil.a" "${DEPS}/lib/libxml2.a" "${DEPS}/lib/libssl.a" "${DEPS}/lib/libcrypto.a" "-Wl,--whole-archive" "${RADV}" "-Wl,--no-whole-archive" SceAgc SceAgcDriver SceVideoOut ScePad SceUserService SceSystemService SceSysmodule SceNet SceNetCtl SceAudioOut)
set_target_properties(MirrorPSX PROPERTIES SUFFIX ".elf")
target_include_directories(MirrorPSX PRIVATE "${PLATFORM}/include")
target_include_directories(MirrorPSX PRIVATE "${OP}/../include" "${CMAKE_SOURCE_DIR}/build/broker-config")
target_link_libraries(MirrorPSX PRIVATE "${OP}/target/lib/libopenprospero_companion.a")
target_link_libraries(MirrorPSX PRIVATE "${PLATFORM}/build/ps5/libps5platform.a" "${PLATFORM}/build/ps5/libclang_rt.builtins-x86_64.a" "${DEPS}/lib/libps5_libc.a")
foreach(FUNCTION pthread_create pthread_join pthread_detach fclose fflush getuid geteuid getgid getegid)
    target_link_options(MirrorPSX PRIVATE "-Wl,--wrap=${FUNCTION}")
endforeach()
# Use the same RADV platform ABI bindings as Hyperion. These functions are not
# reliably supplied by the console's system modules, even when stubs link.
set(PLATFORM_FUNCTIONS
    qsort_r mkstemps openlog popen pclose open_memstream __xuname __assert
    __memset_chk regcomp regexec regfree regerror localtime_r newlocale freelocale
    strtod_l strtof_l dladdr utimensat localeconv_l strtoll_l strtoull_l strtold_l
    snprintf_l sscanf_l asprintf_l strcoll_l strxfrm_l strftime_l wcscoll_l wcsxfrm_l
    btowc_l wctob_l iswctype_l mbrlen_l mbrtowc_l mbsrtowcs_l mbsnrtowcs_l wcrtomb_l
    wcsnrtombs_l mbtowc_l ___mb_cur_max_l ___runetype_l ___tolower_l ___toupper_l
    __runes_for_locale catopen catgets catclose backtrace backtrace_symbols_fd
    __cxa_thread_atexit_impl arc4random arc4random_buf arc4random_uniform
    gmtime_r statvfs fstatvfs futimens clock_nanosleep getaddrinfo freeaddrinfo
    if_nameindex if_freenameindex opendir fdopendir readdir rewinddir dirfd closedir
    nl_langinfo nl_langinfo_l getpwuid_r posix_fallocate access openat unlinkat
    fchmodat fstatat mkdirat renameat memfd_create)
set(PLATFORM_MAP "{\nlocal:\n")
foreach(FUNCTION ${PLATFORM_FUNCTIONS})
    target_link_options(MirrorPSX PRIVATE "-Wl,--defsym=${FUNCTION}=ps5_${FUNCTION}")
    string(APPEND PLATFORM_MAP "  ${FUNCTION};\n")
endforeach()
string(APPEND PLATFORM_MAP "};\n")
file(WRITE "${CMAKE_BINARY_DIR}/radv-platform.map" "${PLATFORM_MAP}")
target_link_options(MirrorPSX PRIVATE "-Wl,--version-script=${CMAKE_BINARY_DIR}/radv-platform.map")
# The SDK libc's dl wrappers test these optional weak hooks before calling them.
# Static RADV has no loader to supply them; preserve ELF's missing-weak = null
# semantics instead of asking the PS5 import converter to invent providers.
foreach(HOOK __dlopen __dlsym __dlclose __dlerror __dladdr kernel_mprotect)
    target_link_options(MirrorPSX PRIVATE "-Wl,--defsym=${HOOK}=0")
endforeach()
foreach(TARGET MirrorPSX airplay plist)
    target_compile_options(${TARGET} PRIVATE -ffunction-sections -fdata-sections)
endforeach()
