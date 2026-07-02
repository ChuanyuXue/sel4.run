/*
 * 10. Handle a fault
 *
 * When a thread crashes in seL4 the kernel does not kill it. It suspends
 * the thread and *sends an IPC message* -- to whatever endpoint was
 * registered as that thread's fault handler. Crashes become messages;
 * a parent can log them, patch things up and resume, or tear the thread
 * down. This is how seL4 systems isolate failures.
 *
 * We build a thread exactly like example 5, register a fault endpoint for
 * it, and let it dereference a wild pointer.
 */

#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>

static char child_stack[4096] __attribute__((aligned(16)));

static void crasher(void)
{
    *(volatile int *)0xDEAD0000 = 42;   /* nothing is mapped there */
    while (1)
        ;
}

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

    seL4_CPtr fault_ep = retype(bi, seL4_EndpointObject, seL4_EndpointBits);
    seL4_CPtr tcb = retype(bi, seL4_TCBObject, seL4_TCBBits);

    /* same as example 5, except: arg 2 registers the fault endpoint */
    seL4_TCB_Configure(tcb, fault_ep,
                       seL4_CapInitThreadCNode, 0,
                       seL4_CapInitThreadVSpace, 0, 0, 0);
    seL4_DebugNameThread(tcb, "crasher");

    seL4_UserContext ctx = {0};
    ctx.pc = (seL4_Word)crasher;
    ctx.sp = (seL4_Word)(child_stack + sizeof(child_stack));
    __asm__("mv %0, gp" : "=r"(ctx.gp));
    seL4_TCB_WriteRegisters(tcb, 0, 0,
                            sizeof(ctx) / sizeof(seL4_Word), &ctx);
    printf("starting a thread that dereferences 0xDEAD0000...\n");
    seL4_TCB_Resume(tcb);

    /* the crash arrives here as a message */
    seL4_Word badge;
    seL4_MessageInfo_t info = seL4_Recv(fault_ep, &badge);

    printf("\ngot a fault message! label %lu (%s)\n",
           (unsigned long)seL4_MessageInfo_get_label(info),
           seL4_MessageInfo_get_label(info) == seL4_Fault_VMFault
               ? "seL4_Fault_VMFault" : "other fault");
    printf("  faulting pc:   %#lx  (== crasher = %p)\n",
           (unsigned long)seL4_GetMR(seL4_VMFault_IP), (void *)crasher);
    printf("  fault address: %#lx\n",
           (unsigned long)seL4_GetMR(seL4_VMFault_Addr));

    printf("\nthe crasher is suspended, not dead: we hold its TCB cap and\n");
    printf("could map a frame at the fault address and seL4_Reply to resume\n");
    printf("it. crashes are just messages to be handled -- or not.\n");

    seL4_DebugHalt();
    while (1)
        seL4_Yield();
}
