cmake_minimum_required(VERSION 3.10)
#set(CMAKE_VERBOSE_MAKEFILE ON)

####################
# TARGET PLATFORM
####################
set(PLATFORM_LINUX OFF CACHE BOOL "Linux platform target")
set(PLATFORM_WINDOWS OFF CACHE BOOL "Windows platform target (msys/mingw64)")
set(PLATFORM_RPI3 OFF CACHE BOOL "RIP3 platform target")
set(PLATFORM_SWITCH OFF CACHE BOOL "Nintendo Switch platform target")
set(PLATFORM_3DS OFF CACHE BOOL "Nintendo 3DS platform target")
set(PLATFORM_VITA OFF CACHE BOOL "Sony PS Vita platform target")
set(PLATFORM_PS4 OFF CACHE BOOL "Sony PS4 platform target")
set(PLATFORM_PS5 OFF CACHE BOOL "Sony PS5 platform target")

####################
# BUILD OPTIONS
####################
set(OPTION_SMB2 OFF CACHE BOOL "Build with smb2 support")
set(OPTION_LINUX_STATIC OFF CACHE BOOL "Linux: link the static libraries of ci/linux-static-deps.sh")
set(LINUX_STATIC_PREFIX "/opt/pplay-static" CACHE PATH "Linux: ci/linux-static-deps.sh prefix")

if (PLATFORM_LINUX AND OPTION_LINUX_STATIC)
    set(ENV{PKG_CONFIG_PATH} "${LINUX_STATIC_PREFIX}/lib/pkgconfig")
    list(PREPEND CMAKE_PREFIX_PATH ${LINUX_STATIC_PREFIX})
    link_directories(BEFORE ${LINUX_STATIC_PREFIX}/lib)
    set(OPTION_BUILTIN_LIBCONFIG ON CACHE BOOL "Build libconfig from sources" FORCE)
endif ()

###################
# MODULES
###################
add_subdirectory(libcross2d)

##############
# pplay
##############
project(pplay)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# pplay version
set(VERSION_MAJOR "4")
set(VERSION_MINOR "6")
set(PROJECT_AUTHOR "Cpasjuste")
set(TITLE_ID "PPLAY0001")
set(APP_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}")

# short commit hash, shown in the logs and the interface only (packages keep APP_VERSION)
set(APP_COMMIT "")
find_package(Git QUIET)
if (GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE APP_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif ()
if (NOT APP_COMMIT AND DEFINED ENV{GITHUB_SHA})
    string(SUBSTRING "$ENV{GITHUB_SHA}" 0 8 APP_COMMIT)
endif ()
if (APP_COMMIT)
    set(APP_VERSION_FULL "${APP_VERSION}-${APP_COMMIT}")
else ()
    set(APP_VERSION_FULL "${APP_VERSION}")
endif ()
message(STATUS "pPlay version: ${APP_VERSION_FULL}")

# deps
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
find_package(BZip2 REQUIRED)
find_package(MPV REQUIRED)
find_package(ffmpeg COMPONENTS avfilter avcodec avformat avutil swscale swresample)
find_package(ass REQUIRED)
find_package(CURL REQUIRED)

# sources
file(GLOB PPLAY_SRC
        src/*.c*
        src/filer/*.c*
        src/filer/Browser/*.c*
        src/filer/ftplib/*.c*
        src/menus/*.c*
        src/player/*.c*
        src/app/*.c*
        src/net/*.c*
    )

# includes
set(PPLAY_INC
        src
        src/filer
        src/filer/Browser
        src/filer/ftplib
        src/menus
        src/player
        src/app
        src/net
        ${MPV_INCLUDE_DIRS}
        ${ASS_INCLUDE_DIRS}
        ${FFMPEG_INCLUDE_DIRS}
        ${BZIP2_INCLUDE_DIR}
        ${CURL_INCLUDE_DIRS}
    )

# cflags
set(PPLAY_CFLAGS
        -D__PPLAY__
        -D__VERSION_MAJOR__=${VERSION_MAJOR}
        -D__VERSION_MINOR__=${VERSION_MINOR}
        #-DM_PI=3.14159265358979323846
        #-DM_SQRT2=1.41421356237309504880
    )

# ldflags
set(PPLAY_LDFLAGS
        ${MPV_LIBRARY}
        ${ASS_LIBRARIES}
        ${FFMPEG_LIBRARIES}
        ${BZIP2_LIBRARIES}
        ${CURL_LIBRARIES}
    )



#####################
# PLATORM SPECIFIC
#####################
if (PLATFORM_SWITCH)
    #####################
    # SWITCH PLATORM
    #####################
    list(APPEND PPLAY_CFLAGS -D__SWITCH__)
    list(APPEND PPLAY_INC)
    list(APPEND PPLAY_LDFLAGS
            archive zstd turbojpeg placebo dav1d postproc m
            swscale swresample avformat avfilter avcodec avutil
            lzma opus vpx ass freetype fribidi png bz2 usbhsfs ntfs-3g lwext4
        )
elseif (PLATFORM_PS4)
    #####################
    # PS4 PLATORM
    #####################
    list(APPEND PPLAY_CFLAGS -D__PS4__)
    set(PS4_PKG_TITLE "pPlay @ cpasjuste")
    set(PS4_PKG_TITLE_ID "PPLA00001")
    set(PS4_PKG_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}")
    list(APPEND PPLAY_INC)
    list(APPEND PPLAY_LDFLAGS
            archive turbojpeg opus freetype fribidi png bz2
        )
    # clock ids, socket flags, timezone
    list(APPEND PPLAY_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/ps4/ps4_compat.c)
    # /data/pplay/crash.log
    list(APPEND PPLAY_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/ps4/crashlog.c)
    # param.sfo ATTRIBUTE 0x20: system enter button in the common dialogs (IME)
    set(PS4_PKG_ATTRIBUTE "32" CACHE STRING "PS4 param.sfo ATTRIBUTE (decimal)")
    set(PS4_PKG_CATEGORY "gde" CACHE STRING "PS4 param.sfo CATEGORY")
    # optional: root credentials (libjbc)
    set(OPTION_PS4_JAILBREAK OFF CACHE BOOL "PS4: jailbreak the process at startup (needs src/libjbc)")
    if (OPTION_PS4_JAILBREAK)
        if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/libjbc/jailbreak.c")
            message(FATAL_ERROR "OPTION_PS4_JAILBREAK needs libjbc sources in src/libjbc")
        endif ()
        message(STATUS "PS4: jailbreak enabled (libjbc)")
        list(APPEND PPLAY_SRC
                ${CMAKE_CURRENT_SOURCE_DIR}/src/ps4/orbis_jbc.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/libjbc/jailbreak.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/libjbc/kernelrw.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/libjbc/utils.c
            )
        list(APPEND PPLAY_CFLAGS -DPPLAY_PS4_JAILBREAK)
    endif ()
elseif (PLATFORM_PS5)
    #####################
    # PS5 PLATORM
    #####################
    list(APPEND PPLAY_CFLAGS -D__PS5__)
    list(APPEND PPLAY_LDFLAGS
            archive zstd turbojpeg placebo expat
            avdevice avfilter avcodec avformat avutil swscale swresample
            ass fribidi freetype fontconfig harfbuzz png bz2 lzma
            curl psl ssl crypto m
        )
    # full static link line from pkg-config (portlibs dependencies change with the SDK)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PS5_PC REQUIRED
            mpv libavdevice libavfilter libavformat libavcodec
            libswscale libswresample libavutil libass libcurl)
    list(APPEND PPLAY_LDFLAGS ${PS5_PC_STATIC_LIBRARIES})
elseif (PLATFORM_LINUX AND OPTION_LINUX_STATIC)
    #####################
    # LINUX PLATORM, almost static: the media libraries from the static prefix
    # (pkg-config --static), the system ones (libc, libGL) stay shared
    #####################
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LINUX_STATIC REQUIRED mpv libcurl)
    set(PPLAY_LDFLAGS ${LINUX_STATIC_STATIC_LDFLAGS} -static-libstdc++ -static-libgcc)
elseif (PLATFORM_LINUX)
    #####################
    # LINUX PLATORM
    #####################
    list(APPEND PPLAY_LDFLAGS 
        ${ASS_LIBRARY} ${SWRESAMPLE_LIBRARY}
        ${AVFORMAT_LIBRARY} ${AVFILTER_LIBRARY}
        ${AVCODEC_LIBRARY} ${AVUTIL_LIBRARY}
        ${SWSCALE_LIBRARY}
        -Wl,-Bstatic
        config mbedtls mbedcrypto mbedx509
        -Wl,-Bdynamic
        fribidi freetype harfbuzz
        png16 z bz2 lzma xml2 fontconfig
        mpv ssl crypto curl unibreak m
    )
    set(CMAKE_SET_RPATH ON)
    set(CMAKE_INSTALL_RPATH "/usr/lib:/usr/local/lib:/usr/lib/x86_64-linux-gnu:\$ORIGIN")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
endif ()

if (OPTION_SMB2)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(OPENSSL_INCLUDE_DIR "" CACHE STRING "" FORCE)
    add_subdirectory(libsmb2)
    list(APPEND PPLAY_CFLAGS -D__SMB2__)
    list(APPEND PPLAY_INC libsmb2/include libsmb2/include/smb2)
    list(APPEND PPLAY_LDFLAGS smb2)
endif ()

#####################
# pplay executable
#####################
add_executable(${CMAKE_PROJECT_NAME} ${PPLAY_SRC})
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${PPLAY_INC})
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE ${PPLAY_CFLAGS})
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
if (PLATFORM_PS4 OR PLATFORM_PS5)
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
    if (TARGET cross2d)
        target_compile_options(cross2d PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
    endif ()
endif ()
set(PLATFORM_LIBS)
if (PLATFORM_SWITCH)
    list(APPEND PLATFORM_LIBS mbedtls mbedx509 mbedcrypto nx)
elseif (PLATFORM_PS4)
    list(APPEND PLATFORM_LIBS mbedtls mbedcrypto mbedx509
        SceNet SceImeDialog SceUserService SceSystemService SceSysmodule ScePad
    )
elseif (PLATFORM_PS5)
    list(APPEND PLATFORM_LIBS SceNet SceImeDialog SceUserService SceSystemService)
    set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES SUFFIX ".elf")
    set(CMAKE_EXECUTABLE_SUFFIX ".elf")
    # payload started by the launcher package (ps5/launcher)
    include(${CMAKE_CURRENT_SOURCE_DIR}/ps5/title_id.cmake)
    set(PS5_PKG_TITLE_ID "${PS5_TITLE_ID}")
    add_executable(pplay_loader ps5/loader/loader.c
        ps5/loader/hbldr/hbldr.c ps5/loader/hbldr/elfldr.c ps5/loader/hbldr/pt.c)
    set_target_properties(pplay_loader PROPERTIES SUFFIX ".elf")
    target_link_libraries(pplay_loader zstd kernel_sys SceSystemService SceUserService)
    target_compile_definitions(pplay_loader PRIVATE PPLAY_TITLE_ID="${PS5_PKG_TITLE_ID}")
elseif (PLATFORM_LINUX)
    list(APPEND PLATFORM_LIBS mbedtls mbedcrypto)
endif ()
target_link_libraries(${CMAKE_PROJECT_NAME} cross2d ${PPLAY_LDFLAGS} ${PLATFORM_LIBS})
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE 
    APP_VERSION="${APP_VERSION}"
)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE APP_VERSION_FULL="${APP_VERSION_FULL}")
if(NOT MSVC)
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "LINKER:--allow-multiple-definition")
    if(CMAKE_LINKER MATCHES "lld")
        target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "LINKER:--error-limit=0")
    endif()
endif()
if (PLATFORM_PS4)
    # see src/ps4/ps4_compat.c
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
            "LINKER:--wrap=clock_gettime"
            "LINKER:--wrap=clock_getres"
            "LINKER:--wrap=pthread_condattr_setclock"
            "LINKER:--wrap=send"
            "LINKER:--wrap=recv"
            "LINKER:--wrap=sendto"
            "LINKER:--wrap=recvfrom")
    # libcross2d calls add_pkg() without attribute/category: forward ours
    function(add_pkg project pkgdir title_id title version)
        _add_pkg(${project} ${pkgdir} ${title_id} "${title}" ${version} ${PS4_PKG_ATTRIBUTE} ${PS4_PKG_CATEGORY})
    endfunction()
endif ()

#####################
# targets
#####################
if (TARGET smb2)
    if (PLATFORM_SWITCH)
        target_compile_options(smb2 PRIVATE -D__SWITCH__)
    elseif (PLATFORM_PS4)
        target_compile_options(smb2 PRIVATE -DPS4_PLATFORM)
    elseif (PLATFORM_PS5)
        target_compile_options(smb2 PRIVATE -DPS5_PLATFORM)
    endif ()
endif ()
include(libcross2d/cmake/targets.cmake)
