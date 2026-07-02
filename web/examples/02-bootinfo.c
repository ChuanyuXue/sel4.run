/*
 * 2. Read BootInfo
 *
 * The kernel describes everything it gives the root task in one structure:
 * seL4_BootInfo. It lists which capability slots are already filled, which
 * are empty, and -- most importantly -- the "untyped" capabilities: chunks
 * of physical memory that all kernel objects will later be created from.
 *
 * Nothing here is discovered at runtime by probing hardware; it is the
 * kernel telling you, precisely, what exists.
 */

#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>

int main(void)
{
    seL4_BootInfo *bi = platsupport_get_bootinfo();

    printf("=== BootInfo ===\n");
    printf("node %lu of %lu\n",
           (unsigned long)bi->nodeID, (unsigned long)bi->numNodes);
    printf("IPC buffer at        %p\n", (void *)bi->ipcBuffer);
    printf("root CNode size      2^%lu slots\n",
           (unsigned long)bi->initThreadCNodeSizeBits);
    printf("empty slots          [%lu, %lu) -- %lu free\n",
           (unsigned long)bi->empty.start, (unsigned long)bi->empty.end,
           (unsigned long)(bi->empty.end - bi->empty.start));
    printf("user image frames    [%lu, %lu)\n",
           (unsigned long)bi->userImageFrames.start,
           (unsigned long)bi->userImageFrames.end);
    printf("untyped caps         [%lu, %lu)\n",
           (unsigned long)bi->untyped.start,
           (unsigned long)bi->untyped.end);

    printf("\n=== Untyped memory (what everything else is made of) ===\n");
    printf("%5s %18s %6s %s\n", "slot", "paddr", "size", "device");
    seL4_Word total = 0;
    for (seL4_Word i = 0; i < bi->untyped.end - bi->untyped.start; i++) {
        seL4_UntypedDesc *u = &bi->untypedList[i];
        printf("%5lu %#18lx 2^%-4u %s\n",
               (unsigned long)(bi->untyped.start + i),
               (unsigned long)u->paddr, u->sizeBits,
               u->isDevice ? "yes" : "");
        if (!u->isDevice)
            total += (seL4_Word)1 << u->sizeBits;
    }
    printf("total non-device untyped: %lu MiB\n",
           (unsigned long)(total >> 20));

    seL4_DebugHalt();
    while (1)
        seL4_Yield();
}
