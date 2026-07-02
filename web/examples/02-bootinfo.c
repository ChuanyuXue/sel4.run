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
 *
 * Official tutorial: https://docs.sel4.systems/Tutorials/capabilities.html
 */

#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>

int main(void)
{
    seL4_BootInfo *info = platsupport_get_bootinfo();

    printf("=== BootInfo ===\n");
    printf("node %lu of %lu\n",
           (unsigned long)info->nodeID, (unsigned long)info->numNodes);
    printf("IPC buffer at        %p\n", (void *)info->ipcBuffer);
    printf("root CNode size      2^%lu slots\n",
           (unsigned long)info->initThreadCNodeSizeBits);
    printf("empty slots          [%lu, %lu) -- %lu free\n",
           (unsigned long)info->empty.start, (unsigned long)info->empty.end,
           (unsigned long)(info->empty.end - info->empty.start));
    printf("user image frames    [%lu, %lu)\n",
           (unsigned long)info->userImageFrames.start,
           (unsigned long)info->userImageFrames.end);
    printf("untyped caps         [%lu, %lu)\n",
           (unsigned long)info->untyped.start,
           (unsigned long)info->untyped.end);

    /* the same table the official untyped tutorial prints */
    printf("\n=== Untyped memory (what everything else is made of) ===\n");
    printf("    CSlot   \tPaddr           \tSize\tType\n");
    seL4_Word total = 0;
    for (seL4_CPtr slot = info->untyped.start; slot != info->untyped.end; slot++) {
        seL4_UntypedDesc *desc = &info->untypedList[slot - info->untyped.start];
        printf("%8p\t%16p\t2^%d\t%s\n", (void *) slot, (void *) desc->paddr,
               desc->sizeBits, desc->isDevice ? "device untyped" : "untyped");
        if (!desc->isDevice)
            total += (seL4_Word)1 << desc->sizeBits;
    }
    printf("total non-device untyped: %lu MiB\n",
           (unsigned long)(total >> 20));

    seL4_DebugHalt();
    while (1)
        seL4_Yield();
}
