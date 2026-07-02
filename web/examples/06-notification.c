/*
 * 6. Notifications
 *
 * A notification is a word of bits, like a tiny interrupt controller in
 * kernel space. Signalling ORs the sender's *badge* into the word; waiting
 * returns the accumulated word and clears it. Badges are set when a cap is
 * *minted*: same object, new cap, extra information -- exactly how the
 * official IPC tutorial's server tells its clients apart, too.
 *
 * The official notifications tutorial runs two producer processes and a
 * consumer that distinguishes them by badge (0b01 and 0b10). Here one
 * thread plays every role, which lets the badge mechanics stand alone:
 * two "producers" signal, the consumer waits once and reads both bits.
 *
 * Official tutorial: https://docs.sel4.systems/Tutorials/notifications.html
 */

#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>
#include <utils/util.h>

int main(int argc, char *argv[])
{
    seL4_Error error;
    seL4_BootInfo *info = platsupport_get_bootinfo();

    /* create the notification the consumer waits on (see example 4) */
    seL4_CPtr full = seL4_CapNull;
    seL4_CPtr slot = info->empty.start;
    for (seL4_Word i = 0; i < info->untyped.end - info->untyped.start; i++) {
        if (!info->untypedList[i].isDevice
            && info->untypedList[i].sizeBits >= seL4_NotificationBits
            && seL4_Untyped_Retype(info->untyped.start + i,
                                   seL4_NotificationObject, 0,
                                   seL4_CapInitThreadCNode, 0, 0,
                                   slot, 1) == seL4_NoError) {
            full = slot;
            break;
        }
    }

    /* mint one badged cap per producer -- same object, different badge */
    seL4_CPtr producer_1 = full + 1, producer_2 = full + 2;
    error = seL4_CNode_Mint(seL4_CapInitThreadCNode, producer_1, seL4_WordBits,
                            seL4_CapInitThreadCNode, full, seL4_WordBits,
                            seL4_AllRights, 0b01);
    ZF_LOGF_IF(error, "Failed to mint producer_1");
    error = seL4_CNode_Mint(seL4_CapInitThreadCNode, producer_2, seL4_WordBits,
                            seL4_CapInitThreadCNode, full, seL4_WordBits,
                            seL4_AllRights, 0b10);
    ZF_LOGF_IF(error, "Failed to mint producer_2");
    printf("Minted producer caps with badges 0b01 and 0b10\n\n");

    /* both producers signal before the consumer looks */
    seL4_Signal(producer_1);
    printf("producer 1: signalled\n");
    seL4_Signal(producer_2);
    printf("producer 2: signalled\n");
    seL4_Signal(producer_2);
    printf("producer 2: signalled again (bits just stay set)\n");

    seL4_Word badge;
    seL4_Wait(full, &badge);
    printf("\nconsumer: woke with badge %#lx\n", (unsigned long)badge);

    /* official tutorial code: use the badge to see who signalled you --
     * you may receive more than one signal at a time */
    if (badge & 0b01) {
        printf("consumer: producer 1 has produced\n");
    }
    if (badge & 0b10) {
        printf("consumer: producer 2 has produced\n");
    }

    /* the word was cleared by the wait */
    seL4_Signal(producer_1);
    seL4_Wait(full, &badge);
    printf("\nnext wait: badge %#lx -- only producer 1 this time\n",
           (unsigned long)badge);

    printf("\nNotifications carry no payload, only badge bits: perfect for\n");
    printf("interrupts, completion flags and semaphores.\n");

    seL4_DebugHalt();
    return 0;
}
