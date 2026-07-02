/*
 * 7. Threads
 *
 * A thread in seL4 is just a TCB object you retype out of untyped memory,
 * point at a CSpace, VSpace, IPC buffer, stack and entry point, and resume.
 * This is the official threads tutorial's solution code; that tutorial runs
 * under the capDL loader, which pre-creates its caps and symbols -- here we
 * are the root task, so the block right below main() derives the very same
 * names (root_cnode, tcb_untyped, tcb_ipc_frame, ...) from BootInfo instead.
 *
 * Watch seL4_DebugDumpScheduler() show the new TCB appear, get a priority,
 * gain a program counter, and finally run.
 *
 * Official tutorial: https://docs.sel4.systems/Tutorials/threads.html
 */

#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>
#include <sel4utils/helpers.h>
#include <utils/util.h>

// the root CNode of the current thread
seL4_CPtr root_cnode = seL4_CapInitThreadCNode;
// VSpace of the current thread
seL4_CPtr root_vspace = seL4_CapInitThreadVSpace;
// TCB of the current thread
seL4_CPtr root_tcb = seL4_CapInitThreadTCB;
// an empty CSlot and an untyped big enough for a TCB (found in main)
seL4_CPtr tcb_cap_slot;
seL4_CPtr tcb_untyped;
// IPC buffer for the new thread, and the cap to the frame backing it
static char thread_ipc_buff_sym[4096] __attribute__((aligned(4096)));
seL4_CPtr tcb_ipc_frame;
// stack for the new thread
static char tcb_stack_base[4096 * 4] __attribute__((aligned(16)));
uintptr_t tcb_stack_top;

int data = 42;

int call_once(int arg) {
    printf("Hello 3 %d\n", arg);
    seL4_DebugHalt();   /* sel4.run: power off instead of spinning */
    return 0;
}

int new_thread(void *arg1, void *arg2, void *arg3) {
    printf("Hello2: arg1 %p, arg2 %p, arg3 %p\n", arg1, arg2, arg3);
    void (*func)(int) = arg1;
    func(*(int *)arg2);
    while (1);
}

int main(int c, char *arbv[]) {

    printf("Hello, World!\n");

    /* what the capDL loader provides in the official tutorial, we derive
     * from BootInfo as the root task: */
    seL4_BootInfo *info = platsupport_get_bootinfo();
    tcb_cap_slot = info->empty.start;
    for (int i = 0; i < (info->untyped.end - info->untyped.start); i++) {
        if (info->untypedList[i].sizeBits >= seL4_TCBBits && !info->untypedList[i].isDevice) {
            tcb_untyped = info->untyped.start + i;
            break;
        }
    }
    /* the kernel maps our whole image and gives us its frame caps in order,
     * so the frame backing the IPC buffer is at a computable offset */
    extern char __executable_start[];
    tcb_ipc_frame = info->userImageFrames.start
        + (((seL4_Word)thread_ipc_buff_sym - (seL4_Word)__executable_start) >> seL4_PageBits);
    tcb_stack_top = (uintptr_t)tcb_stack_base + sizeof(tcb_stack_base);

    seL4_DebugDumpScheduler();

    seL4_Error result = seL4_Untyped_Retype(tcb_untyped, seL4_TCBObject, seL4_TCBBits, root_cnode, 0, 0, tcb_cap_slot, 1);
    ZF_LOGF_IF(result, "Failed to retype thread: %d", result);
    seL4_DebugDumpScheduler();

    result = seL4_TCB_Configure(tcb_cap_slot, seL4_CapNull, root_cnode, 0, root_vspace, 0, (seL4_Word) thread_ipc_buff_sym, tcb_ipc_frame);
    ZF_LOGF_IF(result, "Failed to configure thread: %d", result);

    result = seL4_TCB_SetPriority(tcb_cap_slot, root_tcb, 254);
    ZF_LOGF_IF(result, "Failed to set the priority for the new TCB object.\n");
    seL4_DebugDumpScheduler();

    UNUSED seL4_UserContext regs = {0};
    int error = seL4_TCB_ReadRegisters(tcb_cap_slot, 0, 0, sizeof(regs)/sizeof(seL4_Word), &regs);
    ZF_LOGF_IFERR(error, "Failed to read the new thread's register set.\n");

    sel4utils_arch_init_local_context((void*)new_thread,
                                  (void *)call_once, (void *)&data, (void *)3,
                                  (void *)tcb_stack_top, &regs);
    /* RISC-V only: the C environment also expects the global and thread
     * pointer registers (the capDL loader sets these in the official
     * tutorial) */
    __asm__("mv %0, gp" : "=r"(regs.gp));
    __asm__("mv %0, tp" : "=r"(regs.tp));
    error = seL4_TCB_WriteRegisters(tcb_cap_slot, 0, 0, sizeof(regs)/sizeof(seL4_Word), &regs);
    ZF_LOGF_IFERR(error, "Failed to write the new thread's register set.\n"
                  "\tDid you write the correct number of registers? See arg4.\n");
    seL4_DebugDumpScheduler();

    // resume the new thread
    error = seL4_TCB_Resume(tcb_cap_slot);
    ZF_LOGF_IFERR(error, "Failed to start new thread.\n");

    /* the new thread runs at priority 254, below ours (255); suspend
     * ourselves so it gets the CPU */
    seL4_TCB_Suspend(root_tcb);
    return 0;
}
