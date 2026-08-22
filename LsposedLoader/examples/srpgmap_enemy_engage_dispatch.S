.syntax unified
.thumb
.text
.align 2

.global HookBuildUnitDispatch
.hidden HookBuildUnitDispatch
.type HookBuildUnitDispatch, %function
.thumb_func
HookBuildUnitDispatch:
    // Keep the source, destination unit, caller r4, and return address in an
    // outer frame. The original function will restore r4 with the caller's
    // return address, then return to HookBuildUnitPost through its own epilogue.
    push {r0, r1, r4, lr}
    mov r4, lr
    bl GetBuildUnitResume
    mov r12, r0
    ldr r0, [sp, #0]
    ldr r1, [sp, #4]
    adr r3, HookBuildUnitPost
    mov lr, r3

    // Exact first eight bytes from the supported BuildUnit prologue.
    push {r4, r5, r6, r7, lr}
    add r7, sp, #12
    push.w {r4, r5, r6, r7, r8, r9, r10}
    bx r12

.size HookBuildUnitDispatch, . - HookBuildUnitDispatch

.global HookBuildUnitPost
.hidden HookBuildUnitPost
.type HookBuildUnitPost, %function
.thumb_func
HookBuildUnitPost:
    ldr r1, [sp, #0]
    ldr r2, [sp, #4]
    bl HookBuildUnitPostImpl
    ldr r3, [sp, #12]
    ldr r4, [sp, #8]
    add sp, #16
    bx r3
.size HookBuildUnitPost, . - HookBuildUnitPost
