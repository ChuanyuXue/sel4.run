/*
 * 9. Mapping
 *
 * Virtual memory in seL4 is manual: a *frame* is a cap to 4KiB of physical
 * memory, and putting it into an address space is an explicit syscall on an
 * explicit paging object. Missing intermediate structures are not conjured
 * up by the kernel -- the map fails and *you* retype and map them.
 *
 * This follows the official mapping tutorial. That tutorial is written for
 * x86_64 (PDPT + PageDirectory + PageTable); on RISC-V sv39 every
 * intermediate level is a page-table object, so where the official code
 * maps three different object types we map two page tables, using the
 * arch-generic seL4_ARCH_* aliases the tutorials use elsewhere.
 *
 * Official tutorial: https://docs.sel4.systems/Tutorials/mapping.html
 */

#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>
#include <utils/util.h>
#include <vspace/arch/page.h>

/* somewhere far away from anything the root task already has mapped */
#define TEST_VADDR 0x1000000000

int main(int argc, char *argv[]) {
    seL4_Error error;
    seL4_BootInfo *info = platsupport_get_bootinfo();

    /* carve a frame and two page tables out of untyped memory
     * (the official tutorial's environment pre-allocates these) */
    seL4_CPtr frame = info->empty.start;
    seL4_CPtr pt_1 = frame + 1;
    seL4_CPtr pt_2 = frame + 2;
    seL4_CPtr parent_untyped = 0;
    for (int i = 0; i < (info->untyped.end - info->untyped.start); i++) {
        if (info->untypedList[i].sizeBits >= seL4_PageBits + 2 && !info->untypedList[i].isDevice) {
            parent_untyped = info->untyped.start + i;
            break;
        }
    }
    seL4_Untyped_Retype(parent_untyped, seL4_RISCV_4K_Page, 0,
                        seL4_CapInitThreadCNode, 0, 0, frame, 1);
    seL4_Untyped_Retype(parent_untyped, seL4_RISCV_PageTableObject, 0,
                        seL4_CapInitThreadCNode, 0, 0, pt_1, 1);
    seL4_Untyped_Retype(parent_untyped, seL4_RISCV_PageTableObject, 0,
                        seL4_CapInitThreadCNode, 0, 0, pt_2, 1);

    /* map a read-only page at TEST_VADDR */
    error = seL4_ARCH_Page_Map(frame, seL4_CapInitThreadVSpace, TEST_VADDR,
                               seL4_CanRead, seL4_ARCH_Default_VMAttributes);
    printf("first Page_Map attempt: error %d (seL4_FailedLookup: no page table)\n", error);

    /* map a page table object at each missing sv39 level */
    error = seL4_ARCH_PageTable_Map(pt_1, seL4_CapInitThreadVSpace, TEST_VADDR,
                                    seL4_ARCH_Default_VMAttributes);
    assert(error == seL4_NoError);
    error = seL4_ARCH_PageTable_Map(pt_2, seL4_CapInitThreadVSpace, TEST_VADDR,
                                    seL4_ARCH_Default_VMAttributes);
    assert(error == seL4_NoError);

    /* now the read-only mapping succeeds */
    error = seL4_ARCH_Page_Map(frame, seL4_CapInitThreadVSpace, TEST_VADDR,
                               seL4_CanRead, seL4_ARCH_Default_VMAttributes);
    assert(error == seL4_NoError);

    seL4_Word *x = (seL4_Word *) TEST_VADDR;
    /* read from the newly mapped page */
    printf("Read x: %lu\n", *x);

    /* remap the page read-write, and write to it */
    error = seL4_ARCH_Page_Map(frame, seL4_CapInitThreadVSpace, TEST_VADDR,
                               seL4_ReadWrite, seL4_ARCH_Default_VMAttributes);
    assert(error == seL4_NoError);
    *x = 5;
    printf("Set x to 5\n");
    printf("Read x: %lu\n", *x);
    printf("Success\n");

    seL4_DebugHalt();   /* sel4.run: power the machine off */
    return 0;
}
