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

####################
# BUILD OPTIONS
####################
set(OPTION_SMB2 OFF CACHE BOOL "Build with smb2 support")
set(OPTION_SCRAPPING OFF CACHE BOOL "Build with TMDB scraping support")

###################
# MODULES
###################
if (PLATFORM_PS4)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-include> $<$<COMPILE_LANGUAGE:CXX>:cstdlib>)
endif ()

add_subdirectory(libcross2d)

##############
# pplay
##############
project(pplay)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# pplay version
set(VERSION_MAJOR "4")
set(VERSION_MINOR "4")
set(PROJECT_AUTHOR "Cpasjuste")
set(TITLE_ID "PPLAY0001")
set(APP_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}")

# deps
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
find_package(BZip2 REQUIRED)
find_package(MPV REQUIRED)
find_package(ffmpeg COMPONENTS avfilter avcodec avformat avutil swscale swresample)
find_package(ass REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# sources
file(GLOB PPLAY_SRC
        src/*.c*
        src/filer/*.c*
        src/filer/Browser/*.c*
        src/filer/ftplib/*.c*
        src/menus/*.c*
        src/player/*.c*
        )

# includes
set(PPLAY_INC
        src
        src/filer
        src/filer/Browser
        src/filer/ftplib
        src/menus
        src/player
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
        Threads::Threads
        )


if (NOT OPTION_SCRAPPING)
    list(REMOVE_ITEM PPLAY_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/filer/scrap_view.cpp)
endif ()

if (OPTION_SCRAPPING)
    add_subdirectory(pscrap)
    file(GLOB PPLAY_SCRAPPER_SRC src/scrapper/*.c*)
    list(APPEND PPLAY_SRC ${PPLAY_SCRAPPER_SRC})
    list(APPEND PPLAY_INC src/scrapper)
    list(APPEND PPLAY_CFLAGS -DPPLAY_ENABLE_SCRAPPING)
    list(APPEND PPLAY_LDFLAGS pscrap)
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
# PLATORM SPECIFIC
#####################
if (PLATFORM_SWITCH)
    #####################
    # SWITCH PLATORM
    #####################
    set(MPV_LIBRARIES mpv)
    list(APPEND PPLAY_INC)
    list(APPEND PPLAY_LDFLAGS
            swscale swresample avformat avfilter avcodec avutil
            lzma opus vpx ass freetype fribidi png bz2 usbhsfs ntfs-3g lwext4
            )
elseif (PLATFORM_PS4)
    set(PS4_PKG_TITLE "pPlay @ cpasjuste")
    set(PS4_PKG_TITLE_ID "PPLA00001")
    set(PS4_PKG_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}")
    list(APPEND PPLAY_INC)
    list(APPEND PPLAY_LDFLAGS opus freetype fribidi png bz2)
elseif (PLATFORM_LINUX)
    #####################
    # LINUX PLATORM
    #####################
endif ()

#####################
# openscreen (Cast support)
#####################
set(OPENSCREEN_ROOT "${CMAKE_SOURCE_DIR}/openscreen" CACHE STRING "OpenScreen root directory")
set(OPENSCREEN_GEN "${OPENSCREEN_ROOT}/out/Default/gen")
set(OPENSCREEN_PROTOBUF_SRC "${OPENSCREEN_ROOT}/third_party/protobuf/src")
set(OPENSCREEN_ABSEIL_SRC "${OPENSCREEN_ROOT}/third_party/abseil/src")
set(OPENSCREEN_JSON_SRC "${OPENSCREEN_ROOT}/third_party/jsoncpp/src/include")

set(OPENSCREEN_BORINGSSL_INCLUDE "${OPENSCREEN_ROOT}/third_party/boringssl/include")
set(OPENSCREEN_POLYFILLS_INCLUDE "${OPENSCREEN_ROOT}/CXX20_polyfills")

if(EXISTS "${OPENSCREEN_ROOT}" AND EXISTS "${OPENSCREEN_GEN}")
    add_compile_definitions(__PS4__)
    add_compile_definitions(OS_FREEBSD)
    add_compile_definitions(__x86_64__)

    include_directories(BEFORE 
        ${OPENSCREEN_ROOT}
        ${OPENSCREEN_ROOT}/openscreen
        ${OPENSCREEN_GEN}
        ${OPENSCREEN_GEN}/cast/common/channel/proto
        ${OPENSCREEN_PROTOBUF_SRC}
        ${OPENSCREEN_ABSEIL_SRC}
        ${OPENSCREEN_JSON_SRC}
        ${OPENSCREEN_BORINGSSL_INCLUDE}
        ${OPENSCREEN_POLYFILLS_INCLUDE}
        ${CMAKE_SOURCE_DIR}/ps4-openssl/boringssl/include
    )

    list(APPEND PPLAY_SRC 
        ${OPENSCREEN_ROOT}/platform/impl/task_runner.cc
        ${OPENSCREEN_ROOT}/platform/impl/platform_client_posix.cc
        ${OPENSCREEN_ROOT}/platform/impl/socket_handle_waiter_posix.cc
        ${OPENSCREEN_ROOT}/platform/impl/tls_data_router_posix.cc
        ${OPENSCREEN_ROOT}/platform/impl/logging_posix.cc
        ${OPENSCREEN_ROOT}/platform/base/error.cc
        ${OPENSCREEN_ROOT}/platform/impl/network_interface.cc
        ${OPENSCREEN_ROOT}/platform/base/interface_info.cc
        ${OPENSCREEN_ROOT}/platform/base/ip_address.cc
        ${OPENSCREEN_ROOT}/platform/impl/time.cc
        ${OPENSCREEN_ROOT}/platform/base/tls_credentials.cc
        ${OPENSCREEN_ROOT}/cast/standalone_receiver/cast_service.cc

        ${OPENSCREEN_ROOT}/platform/impl/network_interface.cc
        # ${OPENSCREEN_ROOT}/platform/impl/network_interface_linux.cc
        ${OPENSCREEN_ROOT}/platform/base/socket_handle_waiter.cc
        ${OPENSCREEN_ROOT}/platform/impl/timeval_posix.cc
        ${OPENSCREEN_ROOT}/platform/impl/udp_socket_reader_posix.cc
        ${OPENSCREEN_ROOT}/cast/receiver/channel/static_credentials.cc
    )

    list(APPEND PPLAY_INC 
        ${OPENSCREEN_ROOT}
        ${OPENSCREEN_ROOT}/openscreen
        ${OPENSCREEN_GEN}
        ${OPENSCREEN_GEN}/cast/common/channel/proto
        ${OPENSCREEN_PROTOBUF_SRC}
        ${OPENSCREEN_ABSEIL_SRC}
        ${OPENSCREEN_JSON_SRC}
        ${OPENSCREEN_BORINGSSL_INCLUDE}
        ${OPENSCREEN_POLYFILLS_INCLUDE}
    )

    # Link OpenScreen libraries
    list(APPEND PPLAY_LDFLAGS ssl crypto)

    message(STATUS "OpenScreen support enabled at ${OPENSCREEN_ROOT}")
else()
    message(WARNING "OpenScreen not found or not built at ${OPENSCREEN_ROOT}")
    message(STATUS "Cast support will be disabled - continuing build without it")
endif()

#####################
# pplay executable
#####################
add_executable(${CMAKE_PROJECT_NAME} ${PPLAY_SRC})
target_include_directories(${CMAKE_PROJECT_NAME} BEFORE PRIVATE ${PPLAY_INC})
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE ${PPLAY_CFLAGS})
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
set(PLATFORM_LIBS)
if (PLATFORM_SWITCH)
    list(APPEND PLATFORM_LIBS mbedtls mbedcrypto mbedx509 nx)
elseif (PLATFORM_PS4)
    list(APPEND PLATFORM_LIBS mbedtls mbedcrypto mbedx509 SceNet)
endif ()
target_link_libraries(${CMAKE_PROJECT_NAME} cross2d ${PPLAY_LDFLAGS} ${PLATFORM_LIBS})
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE 
    APP_VERSION="${APP_VERSION}"
)
if(NOT MSVC)
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "LINKER:--allow-multiple-definition")
endif()

#####################
# targets
#####################
include(libcross2d/cmake/targets.cmake)
