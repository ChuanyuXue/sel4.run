/*
 * 10. Fault handling
 *
 * When a thread faults, seL4 does not kill it: the kernel suspends it and
 * delivers the fault *as an IPC message* to the endpoint registered as the
 * thread's fault handler. The handler can fix the problem and reply -- the
 * reply restarts the faulting instruction as if nothing happened.
 *
 * This follows the official fault-handlers tutorial: the faulter invokes an
 * empty CSlot (a *capability fault*), the handler reads the faulting CPtr
 * from the message, installs a real endpoint at exactly that address, and
 * replies. The faulter's interrupted seL4_Call restarts and now succeeds.
 * The official tutorial uses two processes; here handler and faulter are
 * two threads sharing the root task's CSpace and VSpace.
 *
 * Official tutorial: https://docs.sel4.systems/Tutorials/fault-handlers.html
 */

#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>
#include <utils/util.h>

#define FAULTER_BADGE_VALUE (0xBEEF)

/* handler and faulter share one CSpace/VSpace in this adaptation */
seL4_CPtr handler_cspace_root = seL4_CapInitThreadCNode;
seL4_CPtr faulter_cspace_root = seL4_CapInitThreadCNode;
seL4_CPtr faulter_vspace_root = seL4_CapInitThreadVSpace;

seL4_CPtr faulter_fault_ep_cap;          /* raw fault endpoint */
seL4_CPtr badged_faulter_fault_ep_cap;   /* badged copy, installed in the TCB */
seL4_CPtr sequencing_ep_cap;             /* what the faulter *wanted* to invoke */
seL4_CPtr empty_slot_cap;                /* the CPtr the faulter will fault on */

static char faulter_stack[4096] __attribute__((aligned(16)));

static void faulter_main(void)
{
    /* receive on a CSlot that is empty: this raises a capability fault,
     * which the kernel converts into an IPC to our fault endpoint. (The
     * official faulter uses seL4_NBRecv for exactly this reason: receive-
     * style syscalls fault on a bad cap, send-style ones just return an
     * error.) */
    seL4_NBRecv(empty_slot_cap, NULL);

    /* if the handler repaired the slot and replied, the NBRecv above was
     * restarted and has now SUCCEEDED -- so this Send goes through the
     * endpoint the handler installed */
    seL4_Send(empty_slot_cap, seL4_MessageInfo_new(0, 0, 0, 0));
    while (1)
        seL4_Yield();
}

/* carve one object out of untyped memory (see example 4) */
static seL4_CPtr retype(seL4_BootInfo *info, seL4_Word type, seL4_Word obj_bits)
{
    static seL4_CPtr next_slot;
    if (!next_slot)
        next_slot = info->empty.start;
    for (seL4_Word i = 0; i < info->untyped.end - info->untyped.start; i++) {
        if (info->untypedList[i].isDevice
            || info->untypedList[i].sizeBits < obj_bits)
            continue;
        if (seL4_Untyped_Retype(info->untyped.start + i, type, 0,
                                seL4_CapInitThreadCNode, 0, 0,
                                next_slot, 1) == seL4_NoError)
            return next_slot++;
    }
    return seL4_CapNull;
}

int main(int argc, char *argv[]) {
    seL4_Error error;
    seL4_BootInfo *info = platsupport_get_bootinfo();

    faulter_fault_ep_cap = retype(info, seL4_EndpointObject, seL4_EndpointBits);
    sequencing_ep_cap = retype(info, seL4_EndpointObject, seL4_EndpointBits);
    seL4_CPtr faulter_tcb_cap = retype(info, seL4_TCBObject, seL4_TCBBits);
    /* fresh empty slots: one for the badged copy, and -- far away at the end
     * of the CSpace, like the capabilities tutorial's last_slot -- the one
     * the faulter will fault on */
    badged_faulter_fault_ep_cap = faulter_tcb_cap + 1;
    empty_slot_cap = info->empty.end - 1;

    /* give the fault endpoint a badge, so the handler can tell whose fault
     * arrives (official tutorial code) */
    error = seL4_CNode_Mint(
        handler_cspace_root,
        badged_faulter_fault_ep_cap,
        seL4_WordBits,
        handler_cspace_root,
        faulter_fault_ep_cap,
        seL4_WordBits,
        seL4_AllRights, FAULTER_BADGE_VALUE);
    ZF_LOGF_IF(error, "Failed to mint badged fault ep");

    /* register the badged endpoint as the faulter's fault handler
     * (official tutorial code) */
    error = seL4_TCB_SetSpace(
        faulter_tcb_cap,
        badged_faulter_fault_ep_cap,
        faulter_cspace_root,
        0,
        faulter_vspace_root,
        0);
    ZF_LOGF_IF(error, "Failed to set fault handler");
    seL4_DebugNameThread(faulter_tcb_cap, "faulter");

    seL4_UserContext regs = {0};
    regs.pc = (seL4_Word)faulter_main;
    regs.sp = (seL4_Word)(faulter_stack + sizeof(faulter_stack));
    __asm__("mv %0, gp" : "=r"(regs.gp));
    __asm__("mv %0, tp" : "=r"(regs.tp));
    seL4_TCB_WriteRegisters(faulter_tcb_cap, 0, 0,
                            sizeof(regs) / sizeof(seL4_Word), &regs);
    printf("Starting the faulter: it will receive on empty CSlot %lu\n",
           (unsigned long)empty_slot_cap);
    seL4_TCB_Resume(faulter_tcb_cap);

    /* the crash arrives here as a message */
    seL4_Word badge;
    seL4_MessageInfo_t fault = seL4_Recv(faulter_fault_ep_cap, &badge);
    printf("Got a fault: label %lu (%s), badge %#lx\n",
           (unsigned long)seL4_MessageInfo_get_label(fault),
           seL4_MessageInfo_get_label(fault) == seL4_Fault_CapFault
               ? "seL4_Fault_CapFault" : "other",
           (unsigned long)badge);

    /* which CPtr did the faulter try to use? (official tutorial code) */
    seL4_CPtr foreign_faulter_capfault_cap = seL4_GetMR(seL4_CapFault_Addr);
    printf("Faulting CPtr is %lu -- installing a real endpoint there\n",
           (unsigned long)foreign_faulter_capfault_cap);

    /* handle the fault by installing an endpoint at the faulting address
     * (official tutorial code) */
    error = seL4_CNode_Copy(
        faulter_cspace_root,
        foreign_faulter_capfault_cap,
        seL4_WordBits,
        handler_cspace_root,
        sequencing_ep_cap,
        seL4_WordBits,
        seL4_AllRights);
    ZF_LOGF_IF(error, "Failed to copy endpoint");

    /* replying to a fault message restarts the faulted instruction */
    seL4_Reply(seL4_MessageInfo_new(0, 0, 0, 0));

    /* the faulter's seL4_Call was restarted and now lands here */
    seL4_Recv(sequencing_ep_cap, &badge);
    printf("Message received: the faulter recovered and completed its Call!\n");
    printf("Faults are just messages -- handle them, or don't.\n");

    seL4_DebugHalt();
    return 0;
}
