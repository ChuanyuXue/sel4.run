/*
 * 5. IPC
 *
 * IPC is *the* seL4 primitive: synchronous, unbuffered message passing
 * through an endpoint. This follows the official IPC tutorial's echo
 * server: a client packs a string into message registers, one character
 * per register, and seL4_Call()s the endpoint; the server prints the
 * message and answers with seL4_ReplyRecv() -- reply to this sender and
 * wait for the next message in a single kernel entry.
 *
 * The official tutorial's environment provides the client as a separate
 * process; here the client is a second thread built by hand, exactly as a
 * TCB is built in example 7. (The badged-endpoint half of the official
 * tutorial appears in example 6.)
 *
 * Official tutorial: https://docs.sel4.systems/Tutorials/ipc.html
 */

#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>
#include <utils/util.h>

static seL4_CPtr endpoint;

static const char *messages[] = { "the", "quick", "brown fox" };

static char client_stack[4096] __attribute__((aligned(16)));

static void client(void)
{
    /* official tutorial client code: one character per message register */
    for (int i = 0; i < ARRAY_SIZE(messages); i++) {
        int j;
        for (j = 0; messages[i][j] != '\0'; j++) {
            seL4_SetMR(j, messages[i][j]);
        }
        seL4_MessageInfo_t info = seL4_MessageInfo_new(0, 0, 0, j);
        seL4_Call(endpoint, info);
    }
    while (1)
        seL4_Yield();
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

int main(int argc, char *argv[])
{
    seL4_BootInfo *info = platsupport_get_bootinfo();

    endpoint = retype(info, seL4_EndpointObject, seL4_EndpointBits);
    seL4_CPtr tcb = retype(info, seL4_TCBObject, seL4_TCBBits);

    /* build the client thread by hand (see example 7 for the guided
     * version). Messages longer than 4 registers travel via the IPC
     * buffer; the client shares ours -- safe, because Call/Reply strictly
     * alternate which thread runs */
    seL4_TCB_Configure(tcb, seL4_CapNull,
                       seL4_CapInitThreadCNode, 0,
                       seL4_CapInitThreadVSpace, 0,
                       (seL4_Word)info->ipcBuffer, seL4_CapInitThreadIPCBuffer);
    seL4_DebugNameThread(tcb, "client");
    seL4_UserContext regs = {0};
    regs.pc = (seL4_Word)client;
    regs.sp = (seL4_Word)(client_stack + sizeof(client_stack));
    __asm__("mv %0, gp" : "=r"(regs.gp));
    __asm__("mv %0, tp" : "=r"(regs.tp));
    seL4_TCB_WriteRegisters(tcb, 0, 0, sizeof(regs) / sizeof(seL4_Word), &regs);
    seL4_TCB_Resume(tcb);
    printf("Client started; echo server waiting on the endpoint\n\n");

    /* official tutorial server code: echo the message, reply, wait */
    seL4_Word sender;
    seL4_MessageInfo_t msg = seL4_Recv(endpoint, &sender);
    for (int n = 0; n < ARRAY_SIZE(messages); n++) {
        for (int i = 0; i < seL4_MessageInfo_get_length(msg); i++) {
            printf("%c", (char) seL4_GetMR(i));
        }
        printf("\n");
        if (n + 1 < ARRAY_SIZE(messages)) {
            /* reply to the sender and wait for the next message */
            msg = seL4_ReplyRecv(endpoint, msg, &sender);
        } else {
            seL4_Reply(msg);
        }
    }

    printf("\nEach line was one seL4_Call: the kernel copied the message\n");
    printf("registers straight from the client to the server -- no shared\n");
    printf("memory, no queues.\n");

    seL4_DebugHalt();
    return 0;
}
