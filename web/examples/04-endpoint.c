/*
 * 4. Create an endpoint
 *
 * seL4 has no kmalloc: the kernel never allocates memory on its own.
 * New kernel objects are carved out of *untyped* memory by the
 * seL4_Untyped_Retype syscall -- that is the only way anything is ever
 * created. Here we turn a slice of untyped into an endpoint, the object
 * two threads later use to exchange IPC messages.
 *
 * Official tutorial: https://docs.sel4.systems/Tutorials/untyped.html
 */

#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>

int main(void)
{
    seL4_BootInfo *info = platsupport_get_bootinfo();

    /* pick the smallest usable (non-device) untyped */
    seL4_CPtr untyped = 0;
    unsigned size_bits = 64;
    for (seL4_Word i = 0; i < info->untyped.end - info->untyped.start; i++) {
        seL4_UntypedDesc *u = &info->untypedList[i];
        if (!u->isDevice && u->sizeBits >= seL4_EndpointBits
            && u->sizeBits < size_bits) {
            untyped = info->untyped.start + i;
            size_bits = u->sizeBits;
        }
    }
    printf("using untyped cap %lu (2^%u bytes) as raw material\n",
           (unsigned long)untyped, size_bits);

    /* the new cap needs an empty slot in our CNode */
    seL4_CPtr slot = info->empty.start;
    printf("destination: empty slot %lu\n", (unsigned long)slot);
    printf("before retype, kernel tag there: %lu (null)\n",
           (unsigned long)seL4_DebugCapIdentify(slot));

    seL4_Error err = seL4_Untyped_Retype(
        untyped,                 /* what to carve from                */
        seL4_EndpointObject,     /* object type to create             */
        0,                       /* size (fixed for endpoints)        */
        seL4_CapInitThreadCNode, /* CNode to put the new cap in       */
        0, 0,                    /* addressing: the root CNode itself */
        slot,                    /* first destination slot            */
        1);                      /* how many objects                  */
    if (err != seL4_NoError) {
        printf("retype failed: %d\n", err);
    } else {
        printf("after retype, kernel tag there:  %lu (endpoint)\n",
               (unsigned long)seL4_DebugCapIdentify(slot));
        printf("\nthe endpoint exists! it cost %d bytes of untyped memory.\n",
               1 << seL4_EndpointBits);

        /* authority in action: delete the cap and the object is gone */
        seL4_CNode_Delete(seL4_CapInitThreadCNode, slot, seL4_WordBits);
        printf("after delete, kernel tag:        %lu (null again)\n",
               (unsigned long)seL4_DebugCapIdentify(slot));
    }

    seL4_DebugHalt();
    while (1)
        seL4_Yield();
}
