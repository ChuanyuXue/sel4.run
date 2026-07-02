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
