/*
 * 3. Capabilities
 *
 * A capability is a unique, unforgeable token that gives the possessor
 * permission to access an entity or object in the system -- think of it as
 * a pointer with access rights. Caps live in CSlots of CNodes; the full
 * set reachable by a thread is its CSpace. Every syscall names its target
 * by a CSlot address: there are no global handles or PIDs in seL4.
 *
 * This is the official capabilities tutorial's solution code, unmodified:
 * it measures the root CNode, copies the TCB cap around, deletes the
 * copies by revoking the original, and finally suspends the root task by
 * invoking its own TCB. The kernel error messages you will see in the
 * output are part of the exercise -- read them!
 *
 * Official tutorial: https://docs.sel4.systems/Tutorials/capabilities.html
 */

#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>
#include <utils/util.h>

int main(int argc, char *argv[]) {

    /* parse the location of the seL4_BootInfo data structure from
    the environment variables set up by the default crt0.S */
    seL4_BootInfo *info = platsupport_get_bootinfo();

    size_t initial_cnode_object_size = BIT(info->initThreadCNodeSizeBits);
    printf("Initial CNode is %zu slots in size\n", initial_cnode_object_size);

    size_t initial_cnode_object_size_bytes = initial_cnode_object_size * (1u << seL4_SlotBits);
    printf("The CNode is %zu bytes in size\n", initial_cnode_object_size_bytes);

    seL4_CPtr first_free_slot = info->empty.start;
    seL4_Error error = seL4_CNode_Copy(seL4_CapInitThreadCNode, first_free_slot, seL4_WordBits,
                                       seL4_CapInitThreadCNode, seL4_CapInitThreadTCB, seL4_WordBits,
                                       seL4_AllRights);
    ZF_LOGF_IF(error, "Failed to copy cap!");
    seL4_CPtr last_slot = info->empty.end - 1;

    /* use seL4_CNode_Copy to make another copy of the initial TCB capability to the last slot in the CSpace */
    error = seL4_CNode_Copy(seL4_CapInitThreadCNode, last_slot, seL4_WordBits,
                      seL4_CapInitThreadCNode, first_free_slot, seL4_WordBits, seL4_AllRights);
    ZF_LOGF_IF(error, "Failed to copy cap!");

    /* set the priority of the root task */
    error = seL4_TCB_SetPriority(last_slot, last_slot, 10);
    ZF_LOGF_IF(error, "Failed to set priority");

    /* delete the created TCB capabilities */
    seL4_CNode_Revoke(seL4_CapInitThreadCNode, seL4_CapInitThreadTCB, seL4_WordBits);

    /* check first_free_slot is empty */
    error = seL4_CNode_Move(seL4_CapInitThreadCNode, first_free_slot, seL4_WordBits,
                            seL4_CapInitThreadCNode, first_free_slot, seL4_WordBits);
    ZF_LOGF_IF(error != seL4_FailedLookup, "first_free_slot is not empty");

    /* check last_slot is empty */
    error = seL4_CNode_Move(seL4_CapInitThreadCNode, last_slot, seL4_WordBits,
                            seL4_CapInitThreadCNode, last_slot, seL4_WordBits);
    ZF_LOGF_IF(error != seL4_FailedLookup, "last_slot is not empty");

    printf("Suspending current thread\n");
    seL4_TCB_Suspend(seL4_CapInitThreadTCB);
    ZF_LOGF("Failed to suspend current thread\n");

    return 0;
}
