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
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_COMPILER_FLAGS} -Wno-class-memaccess -Wno-conversion-null -Wno-deprecated-enum-enum-conversion")

    if (NOT(APPLE))
        # In MinSizeRel builds, further reduce file size by removing symbol table and relocation information from the executable
        set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} -s")
        set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -s")

        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -no-pie")
    endif()
endif()

message(STATUS "COMMON_COMPILER_FLAGS = ${COMMON_COMPILER_FLAGS}")
message(STATUS "CMAKE_C_FLAGS = ${CMAKE_C_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS = ${CMAKE_CXX_FLAGS}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")

include_directories("${CMAKE_SOURCE_DIR}/src")
include_directories("${CMAKE_SOURCE_DIR}/src/core")
include_directories("${CMAKE_SOURCE_DIR}/src/dicom")
include_directories("${CMAKE_SOURCE_DIR}/src/imgui")
include_directories("${CMAKE_SOURCE_DIR}/src/isyntax")
include_directories("${CMAKE_SOURCE_DIR}/src/platform")
include_directories("${CMAKE_SOURCE_DIR}/src/tiff")
include_directories("${CMAKE_SOURCE_DIR}/src/utils")
include_directories("${CMAKE_SOURCE_DIR}/src/third_party")
include_directories("${CMAKE_SOURCE_DIR}/deps")
include_directories("${CMAKE_SOURCE_DIR}/deps/jpeg")
include_directories("${CMAKE_SOURCE_DIR}/deps/keycode")
include_directories("${CMAKE_SOURCE_DIR}/deps/hdf5")
include_directories("${CMAKE_SOURCE_DIR}/deps/hdf5/H5FDsubfiling")
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

#set(WITH_SIMD 1) # Set to 0 to disable the SIMD code for libjpeg-turbo
if (NOT DEFINED WITH_SIMD)
    set(WITH_SIMD 1)
endif()
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
    add_subdirectory(deps/jpeg/simd) # If this fails, WITH_SIMD will be reset to 0
endif()
if (WITH_SIMD EQUAL 1)
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

set(HDF5_SOURCE_FILES
        H5.c H5checksum.c H5dbg.c H5mpi.c H5system.c H5timer.c H5trace.c H5lib_settings.c
        H5A.c H5Abtree2.c H5Adense.c H5Adeprec.c H5Aint.c #H5Atest.c
        H5B.c H5Bcache.c H5Bdbg.c
        H5B2.c H5B2cache.c H5B2dbg.c H5B2hdr.c H5B2int.c H5B2internal.c H5B2leaf.c H5B2stat.c H5B2test.c
        H5AC.c H5ACdbg.c H5ACmpio.c H5ACproxy_entry.c
        H5C.c H5Cdbg.c H5Cepoch.c H5Cimage.c H5Clog.c H5Clog_json.c H5Clog_trace.c H5Cmpio.c H5Cprefetched.c H5Cquery.c H5Ctag.c #H5Ctest.c
        H5CX.c
        H5D.c H5Dbtree.c H5Dbtree2.c H5Dchunk.c H5Dcompact.c H5Dcontig.c H5Ddbg.c H5Ddeprec.c H5Dearray.c H5Defl.c H5Dfarray.c H5Dfill.c H5Dint.c H5Dio.c H5Dlayout.c H5Dmpio.c H5Dnone.c H5Doh.c H5Dscatgath.c H5Dselect.c H5Dsingle.c #[[H5Dtest.c]] H5Dvirtual.c
        H5E.c H5Edeprec.c H5Eint.c
        H5EA.c H5EAcache.c H5EAdbg.c H5EAdblkpage.c H5EAdblock.c H5EAhdr.c H5EAiblock.c H5EAint.c H5EAsblock.c H5EAstat.c H5EAtest.c
        H5ES.c H5ESevent.c H5ESint.c H5ESlist.c
        H5F.c H5Faccum.c H5Fcwfs.c H5Fdbg.c H5Fdeprec.c H5Fefc.c H5Ffake.c H5Fint.c H5Fio.c H5Fmount.c H5Fmpi.c H5Fquery.c H5Fsfile.c H5Fspace.c H5Fsuper.c H5Fsuper_cache.c #H5Ftest.c
        H5FA.c H5FAcache.c H5FAdbg.c H5FAdblkpage.c H5FAdblock.c H5FAhdr.c H5FAint.c H5FAstat.c H5FAtest.c
        H5FD.c H5FDcore.c H5FDdirect.c H5FDfamily.c H5FDhdfs.c H5FDint.c H5FDlog.c H5FDmirror.c H5FDmpi.c H5FDmpio.c H5FDmulti.c H5FDonion.c H5FDonion_header.c H5FDonion_history.c H5FDonion_index.c H5FDperform.c H5FDros3.c H5FDs3comms.c H5FDsec2.c H5FDspace.c H5FDsplitter.c H5FDstdio.c #[[H5FDtest.c]] H5FDwindows.c
        H5FL.c
        H5FO.c
        H5FS.c H5FScache.c H5FSdbg.c H5FSint.c H5FSsection.c H5FSstat.c #H5FStest.c
        H5G.c H5Gbtree2.c H5Gcache.c H5Gcompact.c H5Gdense.c H5Gdeprec.c H5Gent.c H5Gint.c H5Glink.c H5Gloc.c H5Gname.c H5Gnode.c H5Gobj.c H5Goh.c H5Groot.c H5Gstab.c #[[H5Gtest.c]] H5Gtraverse.c
        H5HF.c H5HFbtree2.c H5HFcache.c H5HFdbg.c H5HFdblock.c H5HFdtable.c H5HFhdr.c H5HFhuge.c H5HFiblock.c H5HFiter.c H5HFman.c H5HFsection.c H5HFspace.c H5HFstat.c #[[H5HFtest.c]] H5HFtiny.c
        H5HG.c H5HGcache.c H5HGdbg.c H5HGquery.c
        H5HL.c H5HLcache.c H5HLdbg.c H5HLdblk.c H5HLint.c H5HLprfx.c
        H5L.c H5Ldeprec.c H5Lexternal.c H5Lint.c
        H5I.c H5Idbg.c H5Iint.c #H5Itest.c
        H5M.c
        H5MF.c H5MFaggr.c H5MFdbg.c H5MFsection.c
        H5MM.c
        H5O.c H5Oainfo.c H5Oalloc.c H5Oattr.c H5Oattribute.c H5Obogus.c H5Obtreek.c H5Ocache.c H5Ocache_image.c H5Ochunk.c H5Ocont.c H5Ocopy.c H5Ocopy_ref.c H5Odbg.c H5Odeprec.c H5Odrvinfo.c H5Odtype.c H5Oefl.c H5Ofill.c H5Oflush.c H5Ofsinfo.c H5Oginfo.c H5Oint.c H5Olayout.c H5Olinfo.c H5Olink.c H5Omessage.c H5Omtime.c H5Oname.c H5Onull.c H5Opline.c H5Orefcount.c H5Osdspace.c H5Oshared.c H5Oshmesg.c H5Ostab.c #[[H5Otest.c]] H5Ounknown.c
        H5P.c H5Pacpl.c H5Pdapl.c H5Pdcpl.c H5Pdeprec.c H5Pdxpl.c H5Pencdec.c H5Pfapl.c H5Pfcpl.c H5Pfmpl.c H5Pgcpl.c H5Pint.c H5Plapl.c H5Plcpl.c H5Pmapl.c H5Pmcpl.c H5Pocpl.c H5Pocpypl.c H5Pstrcpl.c #H5Ptest.c
        H5PB.c
        H5PL.c H5PLint.c H5PLpath.c H5PLplugin_cache.c
        H5R.c H5Rdeprec.c H5Rint.c
        H5RS.c
        H5S.c H5Sall.c H5Sdbg.c H5Sdeprec.c H5Shyper.c H5Smpio.c H5Snone.c H5Spoint.c H5Sselect.c #H5Stest.c
        H5SL.c
        H5SM.c H5SMbtree2.c H5SMcache.c H5SMmessage.c #H5SMtest.c
        H5T.c H5Tarray.c H5Tbit.c H5Tcommit.c H5Tcompound.c H5Tconv.c H5Tcset.c H5Tdbg.c H5Tdeprec.c H5Tenum.c H5Tfields.c H5Tfixed.c H5Tfloat.c H5Tnative.c H5Toffset.c H5Toh.c H5Topaque.c H5Torder.c H5Tpad.c H5Tprecis.c H5Tref.c H5Tstrpad.c H5Tvisit.c H5Tvlen.c H5Tinit.c # generated
        H5UC.c
        H5VL.c H5VLcallback.c H5VLdyn_ops.c H5VLint.c H5VLnative.c H5VLnative_attr.c H5VLnative_blob.c H5VLnative_dataset.c H5VLnative_datatype.c H5VLnative_file.c H5VLnative_group.c H5VLnative_link.c H5VLnative_introspect.c H5VLnative_object.c H5VLnative_token.c H5VLpassthru.c #H5VLtest.c
        H5VM.c
        H5WB.c
        H5Z.c H5Zdeflate.c H5Zfletcher32.c H5Znbit.c H5Zscaleoffset.c H5Zshuffle.c H5Zszip.c H5Ztrans.c
)
prepend_path(HDF5_SOURCE_FILES deps/hdf5)

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
        platform/work_queue.c
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
        core/image.c
        core/image_registration.c
        dicom/dicom.c
        dicom/dicom_dict.c
        dicom/dicom_wsi.c
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
        imgui/backends/imgui_impl_opengl3.cpp
        imgui/misc/freetype/imgui_freetype.cpp
        third_party/lz4.c
        third_party/yxml.c
        third_party/minfft.c
        third_party/ltalloc.cc
        third_party/miniz.c
        utils/stringutils.c
        utils/mathutils.c
        utils/memrw.c
        utils/triangulate.c
        utils/jpeg_decoder.c
        utils/crc32.c
        utils/block_allocator.c
        utils/timerutils.c
        utils/benaphore.c
        utils/phasecorrelate.c
        utils/hdf5reader.c
        )
if (WIN32)
    set(VIEWER_SOURCE_FILES ${VIEWER_SOURCE_FILES}
            platform/win32_main.cpp
            platform/win32_utils.cpp
            platform/win32_gui.cpp
            platform/glad.c
            imgui/backends/imgui_impl_win32.cpp
            )
else()
    # Note: SDL2 is used for both Linux and macOS
    set(VIEWER_SOURCE_FILES ${VIEWER_SOURCE_FILES}
            platform/linux_main.cpp
            platform/linux_utils.cpp
            platform/linux_gui.cpp
            imgui/backends/imgui_impl_sdl.cpp
            imgui/ImGuiFileDialog.cpp
            )
endif()
prepend_path(VIEWER_SOURCE_FILES src)

set(LIBRARY_SOURCE_FILES
        ${JPEG_SOURCE_FILES}
        ${FREETYPE_SOURCE_FILES}
        ${MBEDTLS_SOURCE_FILES}
        ${KEYCODE_SOURCE_FILES}
        ${HDF5_SOURCE_FILES}
        )

# client only supported on Windows x64 and macOS x64 for now
if (WIN32)
    add_executable(slidescape         ${VIEWER_SOURCE_FILES} ${LIBRARY_SOURCE_FILES} resources/icon/icon.rc)
    add_executable(slidescape_console ${VIEWER_SOURCE_FILES} ${LIBRARY_SOURCE_FILES} resources/icon/icon.rc)

    target_link_libraries(slidescape         user32 gdi32 winmm imm32 dwmapi ws2_32 psapi shlwapi)
    target_link_libraries(slidescape_console user32 gdi32 winmm imm32 dwmapi ws2_32 psapi shlwapi)

    # Hide the console window on (non-console) release builds
    target_link_options(slidescape PRIVATE "$<$<NOT:$<CONFIG:Debug>>:-mwindows>")
    target_compile_definitions(slidescape_console PRIVATE DONT_REGISTER_FILETYPE_ASSOCIATIONS=1) # associate only non-console buils
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
            @executable_path/${ARCHITECTURE}/libSDL2.dylib "${BUNDLE_BINARY_DIR}/Slidescape")
    if (WITH_CODESIGN)
        add_custom_command(TARGET slidescape POST_BUILD
                COMMAND codesign --deep -s "Developer ID Application" ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Slidescape.app)
    endif()
    message(STATUS "CMAKE_C_LINK_FLAGS = ${CMAKE_C_LINK_FLAGS}")
    message(STATUS "CMAKE_CXX_LINK_FLAGS = ${CMAKE_CXX_LINK_FLAGS}")
else()
    add_executable(slidescape ${VIEWER_SOURCE_FILES} ${LIBRARY_SOURCE_FILES})
    find_package(SDL2 REQUIRED)
    include_directories(${SDL2_INCLUDE_DIRS})
    target_link_libraries(slidescape pthread dl m GL GLEW SDL2)

    # Create slidescape.desktop and install/uninstall scripts, and copy them to the output directory
    configure_file(resources/slidescape.desktop.in slidescape.desktop)
    add_custom_command(TARGET slidescape POST_BUILD
            COMMAND cp -f slidescape.desktop "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/slidescape.desktop"
            COMMAND cp -f "${CMAKE_SOURCE_DIR}/resources/install_desktop_file.sh.in" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/install_desktop_file.sh"
            COMMAND cp -f "${CMAKE_SOURCE_DIR}/resources/install_desktop_file.sh.in" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/uninstall_desktop_file.sh"
            COMMAND chmod +x "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/slidescape.desktop"
            COMMAND chmod +x "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/install_desktop_file.sh"
            COMMAND chmod +x "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/uninstall_desktop_file.sh")
endif()
target_compile_definitions(slidescape PRIVATE IS_SERVER=0)


# Separately compiled tools:
# slideserver    - server application for streaming WSIs, currently only TIFF format [WIP]
# dicom_dict_gen - tool for generating a DICOM dictionary (dicom_dict.h and dicom_dict.c) by parsing the DICOM standard.

# Some files are always required for the separately compiled tools
set(BASE_FILES
        src/platform/platform.c
        )
if (WIN32)
    list(APPEND BASE_FILES src/platform/win32_utils.cpp)
else()
    list(APPEND BASE_FILES src/platform/linux_utils.cpp)
endif()

set(SERVER_SOURCE_FILES
        ${BASE_FILES}
        src/server.c
        src/tiff/tiff.c
        src/utils/jpeg_decoder.c
        src/utils/stringutils.c
        src/utils/memrw.c
        src/third_party/lz4.c
        src/third_party/ltalloc.cc
        )
add_executable(slideserver ${SERVER_SOURCE_FILES} ${JPEG_SOURCE_FILES} ${MBEDTLS_SOURCE_FILES})
target_compile_definitions(slideserver PRIVATE IS_SERVER=1)

if (WIN32)
    target_link_libraries(slideserver ws2_32 pthread)
else()
    target_link_libraries(slideserver pthread m)
endif()

add_executable(dicom_dict_gen
        ${BASE_FILES}
        src/dicom/dicom_dict_gen.c
        src/utils/memrw.c
        src/third_party/yxml.c
        src/third_party/lz4.c
        src/third_party/ltalloc.cc
        )
target_compile_definitions(dicom_dict_gen PRIVATE IS_SERVER=1)
