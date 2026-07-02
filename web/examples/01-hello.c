/*
 * 1. Hello root task
 *
 * When seL4 finishes booting it starts exactly one user-space program: the
 * root task. The kernel hands it capabilities to every resource it knows
 * about -- all remaining memory, all device regions, its own thread -- and
 * from then on never invents anything new on its own. Everything your
 * system becomes is built from what the root task does next.
 *
 * This program *is* that root task. printf() works because the runtime
 * routes characters to the kernel's debug console (seL4_DebugPutChar),
 * which TinyEmu forwards to this terminal.
 *
 * Official tutorial: https://docs.sel4.systems/Tutorials/hello-world.html
 */

#include <stdio.h>
#include <sel4/sel4.h>

int main(void)
{
    printf("seL4 from your local browser!\n");
    printf("this text was compiled in your browser and is running as the\n");
    printf("seL4 root task inside an emulated RISC-V machine.\n");

    /* Halt the whole machine (debug kernel only). Without this the root
     * task would just spin: there is nothing else to schedule. */
    seL4_DebugHalt();
    while (1) {
        seL4_Yield();
    }
    return 0;
}
