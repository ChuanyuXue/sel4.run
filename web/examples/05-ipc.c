/*
 * 5. IPC ping-pong
 *
 * IPC is *the* seL4 primitive: synchronous, unbuffered message passing
 * through an endpoint. To show it we need a second thread -- and in seL4 a
 * thread is not magic, it is just another kernel object (a TCB) that we
 * retype out of untyped memory and point at some code and a stack.
 *
 * The child has no IPC buffer and no TLS of its own, so it uses
 * seL4_CallWithMRs, which passes the message purely in CPU registers.
 */

#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>

static char child_stack[4096] __attribute__((aligned(16)));
static seL4_CPtr endpoint;

static void pinger(void)
{
    for (seL4_Word n = 1;; n++) {
        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
        seL4_Word mr0 = n;
        /* send "n", block until the main thread replies */
        seL4_CallWithMRs(endpoint, tag, &mr0, NULL, NULL, NULL);
    }
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
    printf("out of untyped memory!\n");
    return seL4_CapNull;
}

int main(void)
{
    seL4_BootInfo *bi = platsupport_get_bootinfo();

    endpoint = retype(bi, seL4_EndpointObject, seL4_EndpointBits);
    seL4_CPtr tcb = retype(bi, seL4_TCBObject, seL4_TCBBits);
    printf("endpoint in slot %lu, new TCB in slot %lu\n",
           (unsigned long)endpoint, (unsigned long)tcb);

    /* the child shares our CSpace and address space */
    seL4_TCB_Configure(tcb, seL4_CapNull,
                       seL4_CapInitThreadCNode, 0,
                       seL4_CapInitThreadVSpace, 0,
                       0, 0 /* no IPC buffer */);
    seL4_DebugNameThread(tcb, "pinger");

    /* a thread is: a program counter, a stack pointer, and (on RISC-V)
     * the gp/tp registers the C environment expects */
    seL4_UserContext ctx = {0};
    ctx.pc = (seL4_Word)pinger;
    ctx.sp = (seL4_Word)(child_stack + sizeof(child_stack));
    __asm__("mv %0, gp" : "=r"(ctx.gp));
    __asm__("mv %0, tp" : "=r"(ctx.tp));
    seL4_TCB_WriteRegisters(tcb, 0, 0,
                            sizeof(ctx) / sizeof(seL4_Word), &ctx);
    seL4_TCB_Resume(tcb);
    printf("child resumed; waiting for its calls\n\n");

    for (int i = 0; i < 3; i++) {
        seL4_Word badge;
        seL4_Recv(endpoint, &badge);        /* block until the child Calls */
        seL4_Word n = seL4_GetMR(0);
        printf("ping %lu received -> ponging back\n", (unsigned long)n);
        seL4_Reply(seL4_MessageInfo_new(0, 0, 0, 0));
    }

    printf("\nthree round trips, each one: Call -> Recv -> Reply.\n");
    printf("no shared memory, no queues: the kernel copies registers\n");
    printf("directly from one thread to the other.\n");

    seL4_DebugHalt();
    while (1)
        seL4_Yield();
}
