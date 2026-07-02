cmake_minimum_required(VERSION 3.7.2)

project(hello C)

add_executable(hello EXCLUDE_FROM_ALL src/main.c)

target_link_libraries(
    hello
    sel4_autoconf
    sel4runtime
    muslc
    sel4
    sel4muslcsys
    sel4utils
    sel4platsupport
    sel4vka
    sel4allocman
    sel4vspace
    sel4simple
    sel4simple-default
    sel4debug
    sel4sync
    utils
)

# Set this image as the rootserver
DeclareRootserver(hello)

# Wrap the system image in OpenSBI, producing the .bin that TinyEmu boots
DeclareOpenSBIImage(hello)

# TinyEmu resolves the "bios" path relative to the config file, so place the
# config next to the image it boots
configure_file(tinyemu.cfg ${CMAKE_BINARY_DIR}/images/tinyemu.cfg COPYONLY)
