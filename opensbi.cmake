#
# Build OpenSBI with the seL4 system image (ElfLoader + kernel + rootserver)
# as its firmware payload, producing the flat binary that TinyEmu boots.
#
# seL4_tools can do this itself (UseRiscVOpenSBI), but its build rule is
# hardwired to the GNU cross toolchain, and OpenSBI requires a linker that
# can create PIEs -- which some prebuilt bare-metal toolchains lack (e.g.
# Homebrew's riscv64-elf-binutils on macOS). Rather than patching seL4_tools,
# we keep UseRiscVOpenSBI off and package the image here, picking whichever
# toolchain on this host can actually link OpenSBI:
#
#   1. the GNU cross toolchain used for the rest of the build, if its
#      linker supports -pie
#   2. otherwise clang + ld.lld, which cross-compile to RISC-V out of the box
#
cmake_minimum_required(VERSION 3.16.0)

function(DeclareOpenSBIImage rootservername)
    if(NOT KernelArchRiscV)
        message(FATAL_ERROR "DeclareOpenSBIImage only supports RISC-V")
    endif()

    set(opensbi_path "${CMAKE_SOURCE_DIR}/tools/opensbi")
    set(opensbi_binary_dir "${CMAKE_BINARY_DIR}/opensbi")
    set(opensbi_payload "${opensbi_binary_dir}/payload")
    set(opensbi_fw_dir "${opensbi_binary_dir}/platform/${KernelOpenSBIPlatform}/firmware")

    # The ElfLoader image produced by DeclareRootserver (UseRiscVOpenSBI is off)
    set(elfloader_image
        "${CMAKE_BINARY_DIR}/images/${rootservername}-image-${KernelArch}-${KernelPlatform}"
    )
    # Flat binary loaded by TinyEmu at the start of RAM
    set(system_image "${elfloader_image}.bin")

    # rv64gc, matching what TinyEmu implements. Zicsr/Zifencei must be spelled
    # out explicitly for binutils >= 2.38 and for clang.
    set(opensbi_make_args
        PLATFORM=${KernelOpenSBIPlatform}
        PLATFORM_RISCV_XLEN=64
        PLATFORM_RISCV_ISA=rv64imafdc_zicsr_zifencei
        PLATFORM_RISCV_ABI=lp64d
    )

    # Prefer the GNU cross toolchain, but OpenSBI links its firmware as a PIE,
    # so only use it if its linker supports that.
    execute_process(
        COMMAND ${CROSS_COMPILER_PREFIX}gcc -fuse-ld=bfd -fPIE -nostdlib -Wl,-pie
                -x c /dev/null -o /dev/null
        RESULT_VARIABLE gnu_pie_unsupported
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(NOT gnu_pie_unsupported)
        list(APPEND opensbi_make_args CROSS_COMPILE=${CROSS_COMPILER_PREFIX})
    else()
        # Fall back to clang/lld. Homebrew keeps them out of the default PATH
        # (and splits them across the llvm and lld kegs), so ask brew where
        # they live before searching the usual locations.
        set(llvm_hints "")
        find_program(HOMEBREW brew)
        if(HOMEBREW)
            foreach(keg llvm lld)
                execute_process(
                    COMMAND ${HOMEBREW} --prefix ${keg}
                    OUTPUT_VARIABLE keg_prefix
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
                )
                if(keg_prefix)
                    list(APPEND llvm_hints "${keg_prefix}/bin")
                endif()
            endforeach()
        endif()
        find_program(OPENSBI_CLANG clang HINTS ${llvm_hints})
        find_program(OPENSBI_LLD ld.lld HINTS ${llvm_hints})
        find_program(OPENSBI_LLVM_AR llvm-ar HINTS ${llvm_hints})
        find_program(OPENSBI_LLVM_OBJCOPY llvm-objcopy HINTS ${llvm_hints})
        if(NOT OPENSBI_CLANG
           OR NOT OPENSBI_LLD
           OR NOT OPENSBI_LLVM_AR
           OR NOT OPENSBI_LLVM_OBJCOPY
        )
            message(
                FATAL_ERROR
                    "The '${CROSS_COMPILER_PREFIX}' toolchain cannot link OpenSBI"
                    " (its linker does not support -pie) and no clang/lld fallback"
                    " was found. Install LLVM, e.g. 'brew install llvm lld' or"
                    " 'apt install clang lld'."
            )
        endif()
        list(
            APPEND
                opensbi_make_args
                CC=${OPENSBI_CLANG}
                LD=${OPENSBI_LLD}
                AR=${OPENSBI_LLVM_AR}
                OBJCOPY=${OPENSBI_LLVM_OBJCOPY}
                # pin the linker clang uses, both for the firmware link and for
                # OpenSBI's configure-time linker checks
                "USE_LD_FLAG=--ld-path=${OPENSBI_LLD}"
        )
    endif()

    add_custom_command(
        OUTPUT "${system_image}"
        COMMAND mkdir -p "${opensbi_binary_dir}"
        # OpenSBI embeds the payload at build time and its makefiles do not
        # track it as a dependency, so always start from a clean tree.
        COMMAND make -s -C "${opensbi_path}" O="${opensbi_binary_dir}" ${opensbi_make_args}
                clean
        COMMAND ${CMAKE_OBJCOPY} -O binary "${elfloader_image}" "${opensbi_payload}"
        COMMAND make -s -C "${opensbi_path}" O="${opensbi_binary_dir}" ${opensbi_make_args}
                FW_PAYLOAD_PATH="${opensbi_payload}"
        COMMAND ${CMAKE_COMMAND} -E copy "${opensbi_fw_dir}/fw_payload.bin" "${system_image}"
        DEPENDS rootserver_image "${elfloader_image}"
    )
    add_custom_target(opensbi_image ALL DEPENDS "${system_image}")
endfunction()
