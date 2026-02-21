cmake_minimum_required(VERSION 3.22.1)
project("mt7921_driver" CXX C)

# ── libusb for Android ─────────────────────────────────────────────────
# We use a prebuilt libusb. Either:
#  Option A) Build libusb from source (add it as an ExternalProject or
#            include it via libusb/android/jni).
#  Option B) Use the prebuilt .aar from the usb-serial-for-android project
#            or build it from https://github.com/libusb/libusb.
#
# For a quick start the simplest approach is to clone libusb into
# app/src/main/cpp/libusb/ and add its sources here directly.
# That is what this CMakeLists.txt assumes.
#
# To do it:
#   cd app/src/main/cpp
#   git clone https://github.com/libusb/libusb.git
# ──────────────────────────────────────────────────────────────────────

set(LIBUSB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/libusb)

# libusb Android-specific config (skip auto-detection)
add_definitions(
    -DLIBUSB_DESCRIBE=""
    -DPLATFORM_POSIX
    -DOS_LINUX
    -D_GNU_SOURCE
)

# libusb core sources we need
set(LIBUSB_SOURCES
    ${LIBUSB_SRC}/libusb/core.c
    ${LIBUSB_SRC}/libusb/descriptor.c
    ${LIBUSB_SRC}/libusb/hotplug.c
    ${LIBUSB_SRC}/libusb/io.c
    ${LIBUSB_SRC}/libusb/sync.c
    ${LIBUSB_SRC}/libusb/strerror.c
    ${LIBUSB_SRC}/libusb/os/linux_usbfs.c
    ${LIBUSB_SRC}/libusb/os/linux_netlink.c
    ${LIBUSB_SRC}/libusb/os/events_posix.c
    ${LIBUSB_SRC}/libusb/os/threads_posix.c
)

add_library(usb1.0 STATIC ${LIBUSB_SOURCES})
target_include_directories(usb1.0 PUBLIC
    ${LIBUSB_SRC}/libusb
    ${LIBUSB_SRC}/libusb/os
    ${LIBUSB_SRC}            # for config.h
)

# ── Our shared library ────────────────────────────────────────────────
add_library(
    mt7921_driver
    SHARED
    native_bridge.cpp
    mt7921_shim.cpp
)

target_include_directories(mt7921_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBUSB_SRC}/libusb
)

target_compile_options(mt7921_driver PRIVATE
    -O2
    -Wall
    -Wno-unused-variable
    -Wno-missing-field-initializers
    -fvisibility=hidden
)

target_link_libraries(mt7921_driver
    usb1.0
    android
    log
)
