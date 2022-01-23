cmake_minimum_required(VERSION 2.8.12)
project(slidescape)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")

# Detect CPU type and whether we're building 64-bit or 32-bit code
math(EXPR BITS "${CMAKE_SIZEOF_VOID_P} * 8")
string(TOLOWER ${CMAKE_SYSTEM_PROCESSOR} CMAKE_SYSTEM_PROCESSOR_LC)
if(CMAKE_SYSTEM_PROCESSOR_LC MATCHES "x86_64" OR
        CMAKE_SYSTEM_PROCESSOR_LC MATCHES "amd64" OR
        CMAKE_SYSTEM_PROCESSOR_LC MATCHES "i[0-9]86" OR
        CMAKE_SYSTEM_PROCESSOR_LC MATCHES "x86" OR
        CMAKE_SYSTEM_PROCESSOR_LC MATCHES "ia32")
    if(BITS EQUAL 64 OR CMAKE_C_COMPILER_ABI MATCHES "ELF X32")
        set(CPU_TYPE x86_64)
    else()
        set(CPU_TYPE i386)
    endif()
    if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL ${CPU_TYPE})
        set(CMAKE_SYSTEM_PROCESSOR ${CPU_TYPE})
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR_LC STREQUAL "aarch64" OR
        CMAKE_SYSTEM_PROCESSOR_LC MATCHES "arm*")
    if(BITS EQUAL 64)
        set(CPU_TYPE arm64)
    else()
        set(CPU_TYPE arm)
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR_LC MATCHES "ppc*" OR
        CMAKE_SYSTEM_PROCESSOR_LC MATCHES "powerpc*")
    set(CPU_TYPE powerpc)
else()
    set(CPU_TYPE ${CMAKE_SYSTEM_PROCESSOR_LC})
endif()
message(STATUS "${BITS}-bit build (${CPU_TYPE})")

# Set up compiler and linker flags
if (MSVC)
    set(COMMON_COMPILER_FLAGS "${MSVC_CPU_OPTIONS} ${MSVC_WARN_OPTIONS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_COMPILER_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_COMPILER_FLAGS}")
else()
    set(GCC_WARN_OPTIONS "-Wall -Wno-unused-function -Wno-unused-but-set-variable -Wno-unused-variable -Wno-missing-braces -Wno-format -Wno-strict-aliasing -Wno-shift-negative-value -Wno-sign-compare")
    if (APPLE)
        set(GCC_WARN_OPTIONS "${GCC_WARN_OPTIONS} -Wno-deprecated-declarations -Wno-unknown-warning-option")
    endif()

    if (CPU_TYPE STREQUAL "x86_64" OR CPU_TYPE STREQUAL "i386")
        set(GCC_CPU_OPTIONS "-mmmx -msse -msse2 -msse3 -mssse3 -msse4 -msse4.1 -msse4.2 -mavx") # Sandy Bridge CPU or later
    elseif(CPU_TYPE STREQUAL "arm64" OR CPU_TYPE STREQUAL "arm")
        if (APPLE)
            set(GCC_CPU_OPTIONS "-mcpu=native")
        else()
            set(GCC_CPU_OPTIONS "-march=native -mcpu=native")
        endif()
    endif()
    set(COMMON_COMPILER_FLAGS "${GCC_CPU_OPTIONS} ${GCC_WARN_OPTIONS} -fvisibility=hidden")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_COMPILER_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_COMPILER_FLAGS} -Wno-class-memaccess -Wno-conversion-null")

    if (NOT(APPLE))
        # In MinSizeRel builds, further reduce file size by removing symbol table and relocation information from the executable
        set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} -s")
        set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -s")

        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -no-pie")
    endif()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")

include_directories("${CMAKE_SOURCE_DIR}/src")
include_directories("${CMAKE_SOURCE_DIR}/src/core")
include_directories("${CMAKE_SOURCE_DIR}/src/imgui")
include_directories("${CMAKE_SOURCE_DIR}/src/isyntax")
include_directories("${CMAKE_SOURCE_DIR}/src/platform")
include_directories("${CMAKE_SOURCE_DIR}/src/tiff")
include_directories("${CMAKE_SOURCE_DIR}/src/utils")
include_directories("${CMAKE_SOURCE_DIR}/deps")
include_directories("${CMAKE_SOURCE_DIR}/deps/jpeg")
include_directories("${CMAKE_SOURCE_DIR}/deps/keycode")
if (WIN32)
    include_directories("${CMAKE_SOURCE_DIR}/deps/dirent")
endif()

# libjpeg-turbo requires these files for JPEG decoding support
set(JPEG_SOURCE_FILES
        jerror.c    jaricom.c   jcapimin.c  jcmarker.c  jcomapi.c
        jdapimin.c  jdapistd.c  jdapistd.c  jdarith.c   jdcoefct.c
        jdcolor.c   jddctmgr.c  jdhuff.c    jdinput.c   jdmainct.c
        jdmarker.c  jdmaster.c  jdmerge.c   jdphuff.c   jdpostct.c
        jdsample.c  jidctflt.c  jidctfst.c  jidctint.c  jidctred.c
        jmemmgr.c   jmemnobs.c  jquant1.c   jquant2.c   jutils.c
)

# extra files needed for JPEG compression support
set(JPEG_SOURCE_FILES ${JPEG_SOURCE_FILES}
        jcapistd.c  jdatadst.c  jcparam.c   jcinit.c    jcmaster.c
        jccolor.c   jcsample.c  jcprepct.c  jcdctmgr.c  jcarith.c
        jctrans.c   jchuff.c    jccoefct.c  jcmainct.c  jcphuff.c
        jfdctflt.c  jfdctfst.c  jfdctint.c
)

# https://stackoverflow.com/questions/13103018/how-to-prepend-cmake-current-source-dir-to-all-files-in-variable
function(prepend_path SOURCE_FILES PREFIX)
    foreach(SOURCE_FILE ${${SOURCE_FILES}})
        set(MODIFIED ${MODIFIED} ${PREFIX}/${SOURCE_FILE})
    endforeach()
    set(${SOURCE_FILES} ${MODIFIED} PARENT_SCOPE)
endfunction()
prepend_path(JPEG_SOURCE_FILES deps/jpeg)

set(WITH_SIMD 1) # Set to 0 to disable the SIMD code for libjpeg-turbo
if (WITH_SIMD EQUAL 1)
    # requirements for deps/jpeg/simd/CMakeLists.txt
    include(CheckCSourceCompiles) # for check_c_source_compiles()
    macro(boolean_number var)
        if(${var})
            set(${var} 1)
        else()
            set(${var} 0)
        endif()
    endmacro()
    add_subdirectory(deps/jpeg/simd)
    set(JPEG_SOURCE_FILES ${JPEG_SOURCE_FILES} $<TARGET_OBJECTS:simd> ${SIMD_OBJS})
else()
    set(JPEG_SOURCE_FILES ${JPEG_SOURCE_FILES} deps/jpeg/jsimd_none.c)
endif()

set(FREETYPE_SOURCE_FILES
        autofit/autofit.c   base/ftbbox.c       base/ftbase.c       base/ftbdf.c        base/ftbitmap.c
        base/ftcid.c        base/ftfstype.c     base/ftgasp.c       base/ftglyph.c      base/ftgxval.c
        base/ftinit.c       base/ftmm.c         base/ftotval.c      base/ftpatent.c     base/ftpfr.c
        base/ftstroke.c     base/ftsynth.c      base/fttype1.c      base/ftwinfnt.c     bdf/bdf.c
        cache/ftcache.c     cff/cff.c           cid/type1cid.c      pcf/pcf.c           pfr/pfr.c
        psaux/psaux.c       pshinter/pshinter.c psnames/psnames.c   raster/raster.c     sfnt/sfnt.c
        smooth/smooth.c     truetype/truetype.c type1/type1.c       type42/type42.c     winfonts/winfnt.c
        base/ftsystem.c
)
prepend_path(FREETYPE_SOURCE_FILES deps/freetype/src)
if (WIN32)
    set(FREETYPE_SOURCE_FILES ${FREETYPE_SOURCE_FILES} deps/freetype/builds/windows/ftdebug.c)
else()
    set(FREETYPE_SOURCE_FILES ${FREETYPE_SOURCE_FILES} deps/freetype/src/base/ftdebug.c)
endif()

# mbedtls
set(MBEDTLS_CRYPTO_SOURCE_FILES
        aes.c                   aesni.c                         arc4.c                  aria.c              asn1parse.c
        asn1write.c             base64.c                        bignum.c                blowfish.c          camellia.c
        ccm.c                   chacha20.c                      chachapoly.c            cipher.c            cipher_wrap.c
        cmac.c                  ctr_drbg.c                      des.c                   dhm.c               ecdh.c
        ecdsa.c                 ecjpake.c                       ecp.c                   ecp_curves.c        entropy.c
        entropy_poll.c          error.c                         gcm.c                   havege.c            hkdf.c
        hmac_drbg.c             md.c                            md2.c                   md4.c               md5.c
        memory_buffer_alloc.c   nist_kw.c                       oid.c                   padlock.c           pem.c
        pk.c                    pk_wrap.c                       pkcs12.c                pkcs5.c             pkparse.c
        pkwrite.c               platform.c                      platform_util.c         poly1305.c          psa_crypto.c
        psa_crypto_se.c         psa_crypto_slot_management.c    psa_crypto_storage.c    psa_its_file.c      ripemd160.c
        rsa.c                   rsa_internal.c                  sha1.c                  sha256.c            sha512.c
        threading.c             timing.c                        version.c               version_features.c  xtea.c
        psa_crypto_client.c     psa_crypto_driver_wrappers.c    psa_crypto_ecp.c        psa_crypto_rsa.c
)
set(MBEDTLS_X509_SOURCE_FILES
        certs.c                 pkcs11.c                        x509.c                  x509_create.c       x509_crl.c
        x509_crt.c              x509_csr.c                      x509write_crt.c         x509write_csr.c
)
set(MBEDTLS_TLS_SOURCE_FILES
        debug.c                 net_sockets.c                   ssl_cache.c             ssl_ciphersuites.c  ssl_cli.c
        ssl_cookie.c            ssl_msg.c                       ssl_srv.c               ssl_ticket.c        ssl_tls.c
)
set(MBEDTLS_SOURCE_FILES ${MBEDTLS_CRYPTO_SOURCE_FILES} ${MBEDTLS_X509_SOURCE_FILES} ${MBEDTLS_TLS_SOURCE_FILES})
prepend_path(MBEDTLS_SOURCE_FILES deps/mbedtls/library)

if (WIN32)
    set(KEYCODE_SOURCE_FILES keycode_id.c windows_fromhid.c windows_lparam.c windows_name.c windows_rawname.c windows_tohid.c)
elseif(APPLE)
    set(KEYCODE_SOURCE_FILES keycode_id.c macos_fromhid.c macos_modifier.c macos_name.c macos_rawname.c macos_tohid.c)
else()
    set(KEYCODE_SOURCE_FILES keycode_id.c linux_fromhid.c linux_name.c linux_rawname.c linux_tohid.c)
endif()
prepend_path(KEYCODE_SOURCE_FILES deps/keycode)

set(VIEWER_SOURCE_FILES
        platform/platform.c
        platform/shader.c
        platform/openslide_api.c
        core/viewer.cpp
        core/scene.cpp
        core/gui.cpp
        core/console.cpp
        core/annotation.cpp
        core/caselist.cpp
        core/coco.cpp
        core/ini.c
        core/remote.c
        tiff/tiff.c
        tiff/tiff_write.c
        tiff/tiff_xml.c
        tiff/tif_lzw.c
        isyntax/isyntax.c
        isyntax/isyntax_streamer.c
        imgui/imgui.cpp
        imgui/imgui_demo.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_widgets.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_impl_opengl3.cpp
        imgui/imgui_freetype.cpp
        utils/stringutils.c
        utils/mathutils.c
        utils/lz4.c
        utils/yxml.c
        utils/jpeg_decoder.c
        )
if (WIN32)
    set(VIEWER_SOURCE_FILES ${VIEWER_SOURCE_FILES}
            platform/win32_main.cpp
            platform/win32_platform.cpp
            platform/win32_gui.cpp
            platform/glad.c
            imgui/imgui_impl_win32.cpp
            )
else()
    # Note: SDL2 is used for both Linux and macOS
    set(VIEWER_SOURCE_FILES ${VIEWER_SOURCE_FILES}
            platform/linux_main.cpp
            platform/linux_platform.cpp
            imgui/imgui_impl_sdl.cpp
            imgui/ImGuiFileDialog.cpp
            )
endif()
prepend_path(VIEWER_SOURCE_FILES src)

set(LIBRARY_SOURCE_FILES
        ${JPEG_SOURCE_FILES}
        ${FREETYPE_SOURCE_FILES}
        ${MBEDTLS_SOURCE_FILES}
        ${KEYCODE_SOURCE_FILES}
        deps/ltalloc.cc)

# client only supported on Windows x64 and macOS x64 for now
if (WIN32)
    add_executable(slidescape ${VIEWER_SOURCE_FILES} ${LIBRARY_SOURCE_FILES} resources/icon/icon.rc)
    target_link_libraries(slidescape user32 gdi32 winmm imm32 dwmapi ws2_32 psapi shlwapi)
    # Hide the console window on release builds
    target_link_options(slidescape PRIVATE "$<$<NOT:$<CONFIG:Debug>>:-mwindows>")
elseif(APPLE)
    set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -headerpad_max_install_names")
    find_package(SDL2 REQUIRED)
    include_directories(${SDL2_INCLUDE_DIRS})
    set(MACOSX_BUNDLE_ICON_FILE "resources/icon/icon.icns")
    set_source_files_properties(${MACOSX_BUNDLE_ICON_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    add_executable(slidescape MACOSX_BUNDLE ${VIEWER_SOURCE_FILES} ${LIBRARY_SOURCE_FILES} ${MACOSX_BUNDLE_ICON_FILE})
    set_target_properties(slidescape PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/src/Info.plist")
    target_link_libraries(slidescape "-framework Cocoa" "-framework Quartz" "-framework OpenGL" "-framework ApplicationServices")
    target_link_libraries(slidescape ${SDL2_LIBRARIES})
    set_target_properties(slidescape PROPERTIES OUTPUT_NAME Slidescape)


    # Copy the .dylib file for SDL2 into the slidescape.app/Contents/MacOS folder.
    # And modify the executable, so that the packaged .dylib files are loaded at runtime.
    set(BUNDLE_BINARY_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Slidescape.app/Contents/MacOS")
    execute_process(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE)
    execute_process(COMMAND sdl2-config --prefix COMMAND tr -d '\n' OUTPUT_VARIABLE SDL2_PREFIX)
    set(FIND_LINKED_DYLIB_COMMAND otool -L ${BUNDLE_BINARY_DIR}/slidescape | grep SDL2- -m 1 | cut -d' ' -f1)
    add_custom_command(TARGET slidescape POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_BINARY_DIR}/${ARCHITECTURE}"
            COMMAND cp -f "${SDL2_PREFIX}/lib/libSDL2.dylib" "${BUNDLE_BINARY_DIR}/${ARCHITECTURE}/libSDL2.dylib"
            COMMAND cp -f "${CMAKE_SOURCE_DIR}/src/Info.plist" "${BUNDLE_BINARY_DIR}/../Info.plist"
            COMMAND install_name_tool -change `${FIND_LINKED_DYLIB_COMMAND}`
            @executable_path/${ARCHITECTURE}/libSDL2.dylib "${BUNDLE_BINARY_DIR}/slidescape")
    if (WITH_CODESIGN)
        add_custom_command(TARGET slidescape POST_BUILD
                COMMAND codesign --deep -s "Developer ID Application" ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Slidescape.app)
    endif()
else()
    add_executable(slidescape ${VIEWER_SOURCE_FILES} ${LIBRARY_SOURCE_FILES})
    target_link_libraries(slidescape pthread dl m GL GLEW SDL2)
endif()
target_compile_definitions(slidescape PRIVATE IS_SERVER=0)

set(SERVER_SOURCE_FILES
        platform/platform.c
        server.c
        tiff/tiff.c
        utils/jpeg_decoder.c
        utils/lz4.c
        utils/stringutils.c
)
prepend_path(SERVER_SOURCE_FILES src)
add_executable(slideserver ${SERVER_SOURCE_FILES} ${JPEG_SOURCE_FILES} ${MBEDTLS_SOURCE_FILES} deps/ltalloc.cc)
target_compile_definitions(slideserver PRIVATE IS_SERVER=1)

if (WIN32)
    target_link_libraries(slideserver ws2_32 pthread)
else()
    target_link_libraries(slideserver pthread m)
endif()
