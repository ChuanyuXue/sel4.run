/*
 * 1. Hello root task
 *
 * Yes, another boring project that runs kernel in browser with WebAssembly,
 * but seL4 microkernel this time ✌️
 *
 * The browser boots a RISC-V machine in TinyEmu, OpenSBI starts seL4, and
 * seL4 starts exactly one user-space program: this root task. The root task
 * begins with capabilities to the initial kernel objects and to all remaining
 * untyped memory. From those capabilities, user space can create endpoints,
 * threads, address spaces and every other kernel object.
 *
 * Github: https://github.com/ChuanyuXue/sel4.run
 */

#include <sel4/sel4.h>
#include <stdio.h>

int main(int argc, char* argv[]) {
    printf("hello from the seL4 root task\n\n");

    printf("boot path:\n");
    printf("  browser -> TinyEmu wasm -> OpenSBI -> seL4 -> root task\n\n");

    printf("this program is the first user-space thread created by seL4.\n");
    printf("it starts with capabilities such as:\n");
    printf("  TCB:        slot %lu\n", (unsigned long)seL4_CapInitThreadTCB);
    printf("  CNode:      slot %lu\n", (unsigned long)seL4_CapInitThreadCNode);
    printf("  VSpace:     slot %lu\n", (unsigned long)seL4_CapInitThreadVSpace);
    printf("  BootInfo:   slot %lu\n", (unsigned long)seL4_CapBootInfoFrame);
    printf("  IRQControl: slot %lu\n", (unsigned long)seL4_CapIRQControl);

    printf("\nthere is no kernel malloc, process table or file system here.\n");
    printf("everything later examples create comes from explicit seL4 syscalls\n");
    printf("using capabilities held by this root task.\n");

    seL4_DebugHalt();
    while (1) {
        seL4_Yield();
    }
    return 0;
}