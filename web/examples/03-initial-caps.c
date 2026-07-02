/*
 * 3. Print the initial capabilities
 *
 * A capability ("cap") is an unforgeable token that both names a kernel
 * object and grants rights to it. Caps live in slots of CNodes, and every
 * syscall names its target by a slot address -- there are no global handles
 * or PIDs in seL4.
 *
 * The slots below are fixed by the seL4 API: the kernel guarantees the root
 * task finds these objects at these addresses in its root CNode.
 * seL4_DebugCapIdentify asks the (debug) kernel what actually sits in a
 * slot; the number it returns is the kernel-internal cap type tag.
 *
 * Official tutorial: https://docs.sel4.systems/Tutorials/capabilities.html
 */

#include <stdio.h>
#include <sel4/sel4.h>

static void show(seL4_Word slot, const char *name, const char *what)
{
    printf("slot %2lu  %-28s kernel tag %-3lu  %s\n",
           (unsigned long)slot, name,
           (unsigned long)seL4_DebugCapIdentify(slot), what);
}

int main(void)
{
    printf("well-known capabilities of the root task:\n\n");
    show(seL4_CapNull, "seL4_CapNull",
         "the empty slot; using it always fails");
    show(seL4_CapInitThreadTCB, "seL4_CapInitThreadTCB",
         "this very thread's TCB");
    show(seL4_CapInitThreadCNode, "seL4_CapInitThreadCNode",
         "the CNode this table lives in");
    show(seL4_CapInitThreadVSpace, "seL4_CapInitThreadVSpace",
         "top-level page table of this address space");
    show(seL4_CapIRQControl, "seL4_CapIRQControl",
         "mint per-IRQ handler caps from this");
    show(seL4_CapASIDControl, "seL4_CapASIDControl",
         "create ASID pools (address space IDs)");
    show(seL4_CapInitThreadASIDPool, "seL4_CapInitThreadASIDPool",
         "the pool our VSpace's ASID comes from");
    show(seL4_CapBootInfoFrame, "seL4_CapBootInfoFrame",
         "the frame holding BootInfo itself");
    show(seL4_CapInitThreadIPCBuffer, "seL4_CapInitThreadIPCBuffer",
         "frame of this thread's IPC buffer");
    show(seL4_CapDomain, "seL4_CapDomain",
         "assign threads to scheduling domains");

    printf("\na cap is *authority*: if it is not in your CNode, you cannot\n");
    printf("even express the request. delete a cap and the object is gone\n");
    printf("from your world.\n");

    seL4_DebugHalt();
    while (1)
        seL4_Yield();
}
