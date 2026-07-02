#
# Settings for booting seL4 on TinyEmu (RISC-V64, "spike" platform)
#
cmake_minimum_required(VERSION 3.7.2)

# set the build platform
set(PLATFORM spike CACHE STRING "" FORCE)
set(KernelSel4Arch riscv64 CACHE STRING "" FORCE)

# Tell kernel/gcc.cmake (evaluated as the toolchain file before the kernel
# config is processed) which cross-toolchain family to search for, so that
# init-build.sh needs no extra -D arguments.
set(RISCV64 ON CACHE BOOL "" FORCE)

# build all libs as static
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(project_dir "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(resolved_path ${CMAKE_CURRENT_LIST_FILE} REALPATH)
get_filename_component(repo_dir ${resolved_path} DIRECTORY)

include(${project_dir}/tools/seL4_tools/cmake-tool/helpers/application_settings.cmake)

correct_platform_strings()

include(${project_dir}/kernel/configs/seL4Config.cmake)

function(add_app app)
    set(destination "${CMAKE_BINARY_DIR}/apps/${app}")
    set_property(GLOBAL APPEND PROPERTY apps_property "$<TARGET_FILE:${app}>")
    add_custom_command(
        TARGET ${app} POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${app}> ${destination} BYPRODUCTS ${destination}
    )
endfunction()

# single core, single domain
set(KernelNumDomains 1 CACHE STRING "")
set(KernelMaxNumNodes 1 CACHE STRING "")

# TinyEmu does not implement the 'rdtime' instruction, only the CLINT mtime
# register; read the timestamp directly from CLINT instead of trapping.
set(KernelRiscvUseClintMtime ON CACHE BOOL "" FORCE)

# Elfloader settings that correspond to how the spike platform is set up.
ApplyData61ElfLoaderSettings(${KernelPlatform} ${KernelSel4Arch})

# OpenSBI is built by sel4.run itself (see opensbi.cmake) rather than by
# seL4_tools, so that hosts whose GNU cross linker cannot create PIEs (e.g.
# Homebrew's riscv64-elf-binutils on macOS) can fall back to clang/lld for it.
set(UseRiscVOpenSBI OFF CACHE BOOL "" FORCE)
include(${repo_dir}/opensbi.cmake)

# turn on all the nice features for debugging/console output
set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "" FORCE)
set(KernelVerificationBuild OFF CACHE BOOL "" FORCE)
set(KernelIRQReporting ON CACHE BOOL "" FORCE)
set(KernelPrinting ON CACHE BOOL "" FORCE)
set(KernelDebugBuild ON CACHE BOOL "" FORCE)
