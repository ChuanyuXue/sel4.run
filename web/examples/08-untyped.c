/*
 * 8. Untyped memory: carve, exhaust, revoke
 *
 * Untyped memory is seL4's whole resource story. Each untyped cap covers a
 * power-of-two region; retyping carves objects from it front to back (a
 * watermark moves forward -- nothing is ever freed piecemeal). To reclaim
 * the region you *revoke* the untyped, which destroys every object carved
 * from it, and start over.
 *
 * That is the entire memory-management API: no free(), no GC. Whoever
 * holds untyped caps controls all future allocation -- which is how seL4
 * systems partition memory between untrusting components.
 *
 * Official tutorial: https://docs.sel4.systems/Tutorials/untyped.html
 */

#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>

int main(void)
{
    seL4_BootInfo *bi = platsupport_get_bootinfo();

    /* find the smallest untyped that can hold at least one endpoint */
    seL4_CPtr ut = 0;
    unsigned bits = 64;
    for (seL4_Word i = 0; i < bi->untyped.end - bi->untyped.start; i++) {
        seL4_UntypedDesc *u = &bi->untypedList[i];
        if (!u->isDevice && u->sizeBits >= seL4_EndpointBits
            && u->sizeBits < bits) {
            ut = bi->untyped.start + i;
            bits = u->sizeBits;
        }
    }
    unsigned capacity = 1u << (bits - seL4_EndpointBits);
    if (capacity > 8)
        capacity = 8;   /* keep the demo readable */
    printf("untyped %lu: 2^%u bytes -> room for %u endpoints (2^%u each)\n\n",
           (unsigned long)ut, bits, capacity, (unsigned)seL4_EndpointBits);

    seL4_CPtr slot = bi->empty.start;
    seL4_Error err;

    /* carve until the region is full */
    unsigned made = 0;
    do {
        err = seL4_Untyped_Retype(ut, seL4_EndpointObject, 0,
                                  seL4_CapInitThreadCNode, 0, 0,
                                  slot + made, 1);
        if (err == seL4_NoError) {
            printf("retype #%-2u -> slot %lu (watermark moves forward)\n",
                   made + 1, (unsigned long)(slot + made));
            made++;
        }
    } while (err == seL4_NoError && made < capacity);

    err = seL4_Untyped_Retype(ut, seL4_EndpointObject, 0,
                              seL4_CapInitThreadCNode, 0, 0,
                              slot + made, 1);
    printf("\none more retype -> error %d (seL4_NotEnoughMemory):\n", err);
    printf("the untyped is exhausted; the kernel refuses.\n\n");

    /* revoke: destroy all children, reset the watermark */
    seL4_CNode_Revoke(seL4_CapInitThreadCNode, ut, seL4_WordBits);
    printf("seL4_CNode_Revoke(untyped) destroyed all %u endpoints at once\n",
           made);
    printf("(check: old slot now tag %lu = null)\n",
           (unsigned long)seL4_DebugCapIdentify(slot));

    err = seL4_Untyped_Retype(ut, seL4_EndpointObject, 0,
                              seL4_CapInitThreadCNode, 0, 0, slot, 1);
    printf("retype after revoke -> %s: the memory is reusable again\n",
           err == seL4_NoError ? "success" : "unexpected failure");

    seL4_DebugHalt();
    while (1)
        seL4_Yield();
}
