/*
 * 9. Map a frame
 *
 * Virtual memory in seL4 is manual: a *frame* is a cap to 4KiB of physical
 * memory, and putting it into your address space is an explicit syscall on
 * an explicit page-table object. If an intermediate page table is missing,
 * the kernel does not conjure one up -- the map fails and *you* must
 * retype a page table and map it first.
 *
 * This is the mechanism behind every mmap(), every shared-memory buffer
 * and every device mapping in an seL4 system.
 */

#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>

/* carve one object out of untyped memory (see example 4) */
static seL4_CPtr retype(seL4_BootInfo *bi, seL4_Word type, seL4_Word obj_bits)
{
    static seL4_CPtr next_slot;
    if (!next_slot)
        next_slot = bi->empty.start;
    for (seL4_Word i = 0; i < bi->untyped.end - bi->untyped.start; i++) {
        if (bi->untypedList[i].isDevice
            || bi->untypedList[i].sizeBits < obj_bits)
            continue;
        if (seL4_Untyped_Retype(bi->untyped.start + i, type, 0,
                                seL4_CapInitThreadCNode, 0, 0,
                                next_slot, 1) == seL4_NoError)
            return next_slot++;
    }
    return seL4_CapNull;
}

int main(void)
{
    seL4_BootInfo *bi = platsupport_get_bootinfo();

    seL4_CPtr frame = retype(bi, seL4_RISCV_4K_Page, seL4_PageBits);
    seL4_Word vaddr = 0x40000000;   /* nothing is mapped up here */
    printf("frame cap in slot %lu; trying to map it at %#lx\n",
           (unsigned long)frame, (unsigned long)vaddr);

    seL4_Error err = seL4_RISCV_Page_Map(
        frame, seL4_CapInitThreadVSpace, vaddr,
        seL4_ReadWrite, seL4_RISCV_Default_VMAttributes);

    /* each failure = one missing level of the (sv39) page-table tree */
    while (err == seL4_FailedLookup) {
        printf("  -> seL4_FailedLookup: a page table is missing; making one\n");
        seL4_CPtr pt = retype(bi, seL4_RISCV_PageTableObject, seL4_PageTableBits);
        seL4_RISCV_PageTable_Map(pt, seL4_CapInitThreadVSpace, vaddr,
                                 seL4_RISCV_Default_VMAttributes);
        err = seL4_RISCV_Page_Map(frame, seL4_CapInitThreadVSpace, vaddr,
                                  seL4_ReadWrite,
                                  seL4_RISCV_Default_VMAttributes);
    }
    printf("mapped (err=%d)\n\n", err);

    volatile unsigned long *p = (volatile unsigned long *)vaddr;
    *p = 0xC0FFEE;
    printf("wrote %#lx to %p and read back %#lx\n",
           0xC0FFEEul, (void *)p, *p);

    seL4_RISCV_Page_Unmap(frame);
    printf("frame unmapped again -- the memory still exists, it just has\n");
    printf("no address here anymore. touching %p now would page-fault\n",
           (void *)p);
    printf("(exactly what example 10 is about).\n");

    seL4_DebugHalt();
    while (1)
        seL4_Yield();
}
