#include <stdio.h>
#include <sel4/sel4.h>

int main(void)
{
    printf("hello from seL4 on TinyEmu!\n");
    printf("seL4 root task is alive and running.\n");

    seL4_DebugHalt();
    while (1) {
        seL4_Yield();
    }
    return 0;
}
