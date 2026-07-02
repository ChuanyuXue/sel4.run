/*
 * 6. Notifications: signal and wait
 *
 * A notification is the other communication object: not a message channel
 * but a word of bits, like a tiny hardware interrupt controller in kernel
 * space. Signalling ORs the sender's *badge* into the word; waiting
 * returns the accumulated word and clears it.
 *
 * Badges are set when a capability is *minted*: same object, new cap,
 * extra information. This is how one receiver can tell many signallers
 * apart -- and it works within a single thread, so we can demo it here
 * without any of example 5's thread machinery.
 *
 * Official tutorial: https://docs.sel4.systems/Tutorials/notifications.html
 */

#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>

int main(void)
{
    seL4_BootInfo *bi = platsupport_get_bootinfo();

    /* retype a notification (see example 4 for the pattern) */
    seL4_CPtr slot = bi->empty.start;
    seL4_CPtr ntfn = seL4_CapNull;
    for (seL4_Word i = 0; i < bi->untyped.end - bi->untyped.start; i++) {
        if (!bi->untypedList[i].isDevice
            && bi->untypedList[i].sizeBits >= seL4_NotificationBits
            && seL4_Untyped_Retype(bi->untyped.start + i,
                                   seL4_NotificationObject, 0,
                                   seL4_CapInitThreadCNode, 0, 0,
                                   slot, 1) == seL4_NoError) {
            ntfn = slot;
            break;
        }
    }
    printf("notification object in slot %lu\n", (unsigned long)ntfn);

    /* mint two new caps to the SAME object, each with its own badge */
    seL4_CPtr sensor = ntfn + 1, button = ntfn + 2;
    seL4_CNode_Mint(seL4_CapInitThreadCNode, sensor, seL4_WordBits,
                    seL4_CapInitThreadCNode, ntfn, seL4_WordBits,
                    seL4_AllRights, 0b01);
    seL4_CNode_Mint(seL4_CapInitThreadCNode, button, seL4_WordBits,
                    seL4_CapInitThreadCNode, ntfn, seL4_WordBits,
                    seL4_AllRights, 0b10);
    printf("minted 'sensor' cap with badge 0b01, 'button' with badge 0b10\n\n");

    seL4_Signal(sensor);
    printf("signalled via 'sensor'\n");
    seL4_Signal(button);
    printf("signalled via 'button'\n");
    seL4_Signal(button);
    printf("signalled via 'button' again (already-set bits just stay set)\n");

    seL4_Word bits;
    seL4_Wait(ntfn, &bits);
    printf("\nseL4_Wait returned bits 0b%lu%lu -> both sources fired\n",
           (unsigned long)(bits >> 1) & 1, (unsigned long)bits & 1);

    seL4_Signal(sensor);
    seL4_Wait(ntfn, &bits);
    printf("next wait: 0b%lu%lu -> the word was cleared by the first wait\n",
           (unsigned long)(bits >> 1) & 1, (unsigned long)bits & 1);

    printf("\nnotifications carry no payload, only badge bits: perfect for\n");
    printf("interrupts, completion flags and semaphores.\n");

    seL4_DebugHalt();
    while (1)
        seL4_Yield();
}
