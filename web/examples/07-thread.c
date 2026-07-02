/*
 * 7. Create a thread, the comfortable way
 *
 * Example 5 built a thread by hand; real code uses libraries layered on
 * exactly those syscalls:
 *
 *   simple    - answers questions about bootinfo ("what's my CNode?")
 *   allocman  - allocator managing untyped memory and cap slots
 *   vka       - the allocation *interface* (retype X for me)
 *   vspace    - manages this address space, maps frames on demand
 *   sel4utils - stacks, IPC buffers, TLS: whole threads
 *
 * Every object below is still retyped from the untypeds you saw in
 * example 2 -- just not by us line-by-line.
 */

#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4platsupport/bootinfo.h>
#include <simple-default/simple-default.h>
#include <allocman/bootstrap.h>
#include <allocman/vka.h>
#include <sel4utils/vspace.h>
#include <sel4utils/thread.h>

static char pool[4 * 1024 * 1024];   /* memory for allocman's bookkeeping */

static vka_object_t done;

static void worker(void *arg0, void *arg1, void *ipc_buf)
{
    /* this thread has its own stack, IPC buffer and TLS, so printf just
     * works -- compare with example 5's register-only child */
    printf("  [worker] hello! args: %p %p, my IPC buffer: %p\n",
           arg0, arg1, ipc_buf);
    printf("  [worker] signalling the main thread and exiting\n");
    seL4_Signal(done.cptr);
    while (1)
        seL4_Yield();
}

int main(void)
{
    seL4_BootInfo *bi = platsupport_get_bootinfo();

    simple_t simple;
    simple_default_init_bootinfo(&simple, bi);

    allocman_t *alloc = bootstrap_use_current_simple(&simple, sizeof(pool), pool);
    vka_t vka;
    allocman_make_vka(&vka, alloc);

    vspace_t vspace;
    sel4utils_alloc_data_t vdata;
    sel4utils_bootstrap_vspace_with_bootinfo_leaky(
        &vspace, &vdata, simple_get_pd(&simple), &vka, bi);
    printf("allocator and vspace bootstrapped from bootinfo\n");

    vka_alloc_notification(&vka, &done);

    sel4utils_thread_t thread;
    int err = sel4utils_configure_thread(
        &vka, &vspace, &vspace, seL4_CapNull,
        simple_get_cnode(&simple), 0, &thread);
    printf("thread configured (err=%d): TCB, 64KiB stack, IPC buffer, TLS\n",
           err);

    sel4utils_start_thread(&thread, worker, (void *)0x1234, (void *)0x5678, 1);
    printf("worker started, waiting for its signal...\n\n");

    seL4_Word bits;
    seL4_Wait(done.cptr, &bits);
    printf("\nworker checked in. behind the scenes vka retyped a TCB,\n");
    printf("stack frames and an IPC buffer frame, and vspace mapped them.\n");

    seL4_DebugHalt();
    while (1)
        seL4_Yield();
}
