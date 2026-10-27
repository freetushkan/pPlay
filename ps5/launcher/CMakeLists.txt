cmake_minimum_required(VERSION 3.10)

#
# pPlay PS5 launcher: PS4 "gd" package containing the PS5 build of pPlay.
#
# Configure with the pacbrew OpenOrbis toolchain:
#   cmake -DCMAKE_TOOLCHAIN_FILE=/opt/pacbrew/ps4/openorbis/cmake/ps4.cmake \
#         -DPKG_DIR=<package root> -DPKG_VERSION=4.5 <this dir>
# <package root> must already contain:
#   sce_sys/icon0.png, sce_module/*.prx
#   ps5/pplay_loader.elf
#   ps5/pplay/...  (pplay.elf, libOSMesa.so.8, data; files > 1MB as .zst, unpacked by the loader)
#

project(pplay_launcher C)

set(PKG_DIR "" CACHE PATH "package root directory")
set(PKG_TITLE "pPlay" CACHE STRING "package title")
set(PKG_TITLE_ID "PPLA00005" CACHE STRING "package title id")
set(PKG_VERSION "1.00" CACHE STRING "package version (xx.yy)")

if (NOT PKG_DIR OR NOT EXISTS "${PKG_DIR}/ps5/pplay_loader.elf")
    message(FATAL_ERROR "PKG_DIR must point to the package root (missing ps5/pplay_loader.elf)")
endif ()

add_executable(${PROJECT_NAME} main.c)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
target_link_libraries(${PROJECT_NAME} SceSystemService SceSysmodule SceNet)

add_self(${PROJECT_NAME})
# category "gd": the PS5 refuses "gde" packages
add_pkg(${PROJECT_NAME} "${PKG_DIR}" ${PKG_TITLE_ID} "${PKG_TITLE}" ${PKG_VERSION} 0 gd)
