# Memory functions for x64 (D19), assembled with the assembler integrated in
# LLVM. LLVM can emit calls to these functions even when the program does not
# use them, so the runtime must always provide them. CMake assembles the file
# once for each system (D142):
# - Windows: the arguments arrive in rcx, rdx, r8; rdi and rsi must be preserved.
#   The file also has the stack probe __chkstk.
# - Linux: the arguments arrive in rdi, rsi, rdx. The file also has the entry
#   point _start.
# The memory functions themselves are written once, for both.
# On both systems it has the instructions that the runtime written in Cromo
# cannot write (D393).

    .intel_syntax noprefix
    .text

# void cromo_pause(void): the hint of a loop that waits for another thread.
    .globl cromo_pause
    .p2align 4
cromo_pause:
    pause
    ret

# ---- memcpy, memmove, memset and memcmp (D19, D579) ----
#
# With SSE2, which every x86-64 has. The body of each is the same on the two
# systems, with the arguments in rcx, rdx and r8, as Windows gives them: on Linux
# the first lines move them there. Only rax, rcx, rdx, r8-r11 and xmm0-xmm5 are
# changed, which a call may change on both systems; rdi and rsi, which Windows
# wants preserved, are used by rep movsb and rep stosb, and put back there.
# On Linux LLVM calls bcmp when it only needs to know whether the bytes are equal:
# it is memcmp.

# Between these sizes the processor copies and fills faster by itself, with rep movsb
# and rep stosb, than 16 bytes at a time (D579).
#define REP_MIN 1024
#define REP_MAX 0x80000
#define SET_REP_MIN 2048
#define SET_REP_MAX 0x80000

# void* memcpy(void* dest, const void* src, size_t count)
# void* memmove(void* dest, const void* src, size_t count): the same code. Up to 64
# bytes are read whole before any is written; more are copied 64 at a time, from the
# start, or from the end when the destination overlaps the end of the source, and
# what is left, at most 64 bytes, as the short ones.
    .globl memcpy
    .globl memmove
    .p2align 4
memcpy:
memmove:
#ifndef _WIN32
    mov rcx, rdi
    mov r8, rdx
    mov rdx, rsi
#endif
    mov rax, rcx
    cmp r8, 64
    ja .Lcopy_long
.Lcopy_short:
    cmp r8, 16
    jb .Lcopy_under_16
    movdqu xmm0, xmmword ptr [rdx]
    movdqu xmm1, xmmword ptr [rdx + r8 - 16]
    cmp r8, 32
    ja .Lcopy_33_64
    movdqu xmmword ptr [rcx], xmm0
    movdqu xmmword ptr [rcx + r8 - 16], xmm1
    ret
.Lcopy_33_64:
    movdqu xmm2, xmmword ptr [rdx + 16]
    movdqu xmm3, xmmword ptr [rdx + r8 - 32]
    movdqu xmmword ptr [rcx], xmm0
    movdqu xmmword ptr [rcx + 16], xmm2
    movdqu xmmword ptr [rcx + r8 - 32], xmm3
    movdqu xmmword ptr [rcx + r8 - 16], xmm1
    ret
.Lcopy_under_16:
    cmp r8, 8
    jb .Lcopy_under_8
    mov r9, qword ptr [rdx]
    mov r10, qword ptr [rdx + r8 - 8]
    mov qword ptr [rcx], r9
    mov qword ptr [rcx + r8 - 8], r10
    ret
.Lcopy_under_8:
    cmp r8, 4
    jb .Lcopy_under_4
    mov r9d, dword ptr [rdx]
    mov r10d, dword ptr [rdx + r8 - 4]
    mov dword ptr [rcx], r9d
    mov dword ptr [rcx + r8 - 4], r10d
    ret
.Lcopy_under_4:
    cmp r8, 2
    jb .Lcopy_under_2
    movzx r9d, word ptr [rdx]
    movzx r10d, word ptr [rdx + r8 - 2]
    mov word ptr [rcx], r9w
    mov word ptr [rcx + r8 - 2], r10w
    ret
.Lcopy_under_2:
    test r8, r8
    jz .Lcopy_done
    movzx r9d, byte ptr [rdx]
    mov byte ptr [rcx], r9b
.Lcopy_done:
    ret

    .p2align 4
.Lcopy_long:
    mov r9, rcx
    sub r9, rdx
    cmp r9, r8
    jb .Lcopy_backward  # the destination starts inside the source
    mov r10, REP_MIN
    cmp r8, r10
    jb .Lcopy_forward
    mov r10, REP_MAX
    cmp r8, r10
    ja .Lcopy_forward
    # In the middle sizes the processor copies faster by itself.
#ifdef _WIN32
    mov r9, rdi
    mov r10, rsi
#endif
    mov rdi, rcx
    mov rsi, rdx
    mov rcx, r8
    rep movsb
#ifdef _WIN32
    mov rdi, r9
    mov rsi, r10
#endif
    ret
    .p2align 4
.Lcopy_forward:
    movdqu xmm0, xmmword ptr [rdx]
    movdqu xmm1, xmmword ptr [rdx + 16]
    movdqu xmm2, xmmword ptr [rdx + 32]
    movdqu xmm3, xmmword ptr [rdx + 48]
    movdqu xmmword ptr [rcx], xmm0
    movdqu xmmword ptr [rcx + 16], xmm1
    movdqu xmmword ptr [rcx + 32], xmm2
    movdqu xmmword ptr [rcx + 48], xmm3
    add rdx, 64
    add rcx, 64
    sub r8, 64
    cmp r8, 64
    ja .Lcopy_forward
    jmp .Lcopy_short
    .p2align 4
.Lcopy_backward:
    movdqu xmm0, xmmword ptr [rdx + r8 - 16]
    movdqu xmm1, xmmword ptr [rdx + r8 - 32]
    movdqu xmm2, xmmword ptr [rdx + r8 - 48]
    movdqu xmm3, xmmword ptr [rdx + r8 - 64]
    movdqu xmmword ptr [rcx + r8 - 16], xmm0
    movdqu xmmword ptr [rcx + r8 - 32], xmm1
    movdqu xmmword ptr [rcx + r8 - 48], xmm2
    movdqu xmmword ptr [rcx + r8 - 64], xmm3
    sub r8, 64
    cmp r8, 64
    ja .Lcopy_backward
    jmp .Lcopy_short

# void* memset(void* dest, int value, size_t count): the byte fills a register; up to
# 64 bytes are stores that overlap, more are 64 at a time at aligned addresses.
    .globl memset
    .p2align 4
memset:
#ifndef _WIN32
    mov rcx, rdi
    mov r8, rdx
    mov edx, esi
#endif
    mov rax, rcx
    movzx edx, dl
    mov r9, 0x0101010101010101
    imul rdx, r9
    cmp r8, 16
    jb .Lset_under_16
    movq xmm0, rdx
    punpcklqdq xmm0, xmm0
    cmp r8, 32
    ja .Lset_over_32
    movdqu xmmword ptr [rcx], xmm0
    movdqu xmmword ptr [rcx + r8 - 16], xmm0
    ret
.Lset_over_32:
    cmp r8, 64
    ja .Lset_long
    movdqu xmmword ptr [rcx], xmm0
    movdqu xmmword ptr [rcx + 16], xmm0
    movdqu xmmword ptr [rcx + r8 - 32], xmm0
    movdqu xmmword ptr [rcx + r8 - 16], xmm0
    ret
.Lset_under_16:
    cmp r8, 8
    jb .Lset_under_8
    mov qword ptr [rcx], rdx
    mov qword ptr [rcx + r8 - 8], rdx
    ret
.Lset_under_8:
    cmp r8, 4
    jb .Lset_under_4
    mov dword ptr [rcx], edx
    mov dword ptr [rcx + r8 - 4], edx
    ret
.Lset_under_4:
    cmp r8, 2
    jb .Lset_under_2
    mov word ptr [rcx], dx
    mov word ptr [rcx + r8 - 2], dx
    ret
.Lset_under_2:
    test r8, r8
    jz .Lset_done
    mov byte ptr [rcx], dl
.Lset_done:
    ret

    .p2align 4
.Lset_long:
    mov r10, SET_REP_MIN
    cmp r8, r10
    jb .Lset_vectors
    mov r10, SET_REP_MAX
    cmp r8, r10
    ja .Lset_vectors
#ifdef _WIN32
    mov r9, rdi
#endif
    mov r10, rax
    mov rdi, rcx
    mov eax, edx
    mov rcx, r8
    rep stosb
    mov rax, r10
#ifdef _WIN32
    mov rdi, r9
#endif
    ret
.Lset_vectors:
    # The first 16 bytes as they are, then from the next aligned address; the last
    # 64 bytes at the end, wherever the loop stopped.
    movdqu xmmword ptr [rcx], xmm0
    lea r9, [rcx + r8 - 64]  # the last 64 bytes
    add rcx, 16
    and rcx, -16
    cmp rcx, r9
    jae .Lset_tail
    .p2align 4
.Lset_loop:
    movdqa xmmword ptr [rcx], xmm0
    movdqa xmmword ptr [rcx + 16], xmm0
    movdqa xmmword ptr [rcx + 32], xmm0
    movdqa xmmword ptr [rcx + 48], xmm0
    add rcx, 64
    cmp rcx, r9
    jb .Lset_loop
.Lset_tail:
    movdqu xmmword ptr [r9], xmm0
    movdqu xmmword ptr [r9 + 16], xmm0
    movdqu xmmword ptr [r9 + 32], xmm0
    movdqu xmmword ptr [r9 + 48], xmm0
    ret

# int memcmp(const void* a, const void* b, size_t count), and bcmp on Linux: 64 bytes at a time while
# they are equal, then 16, then the last 16 of the block; the first byte that differs
# gives the sign. From 8 to 15 bytes, two words; under 8, one byte at a time.
    .globl memcmp
    .p2align 4
memcmp:
#ifndef _WIN32
    .globl bcmp
bcmp:
#endif
#ifndef _WIN32
    mov rcx, rdi
    mov r8, rdx
    mov rdx, rsi
#endif
    xor eax, eax
    cmp r8, 16
    jb .Lcompare_bytes
    cmp r8, 64
    jb .Lcompare_16
    .p2align 4
.Lcompare_64:
    movdqu xmm0, xmmword ptr [rcx]
    movdqu xmm1, xmmword ptr [rcx + 16]
    movdqu xmm2, xmmword ptr [rcx + 32]
    movdqu xmm3, xmmword ptr [rcx + 48]
    movdqu xmm4, xmmword ptr [rdx]
    movdqu xmm5, xmmword ptr [rdx + 16]
    pcmpeqb xmm0, xmm4
    pcmpeqb xmm1, xmm5
    movdqu xmm4, xmmword ptr [rdx + 32]
    movdqu xmm5, xmmword ptr [rdx + 48]
    pcmpeqb xmm2, xmm4
    pcmpeqb xmm3, xmm5
    pand xmm0, xmm1
    pand xmm2, xmm3
    pand xmm0, xmm2
    pmovmskb r9d, xmm0
    cmp r9d, 0xFFFF
    jne .Lcompare_16  # one of the 64 differs: found 16 at a time
    add rcx, 64
    add rdx, 64
    sub r8, 64
    cmp r8, 64
    jae .Lcompare_64
    cmp r8, 16
    jb .Lcompare_last
.Lcompare_16:
    movdqu xmm0, xmmword ptr [rcx]
    movdqu xmm1, xmmword ptr [rdx]
    pcmpeqb xmm0, xmm1
    pmovmskb r9d, xmm0
    cmp r9d, 0xFFFF
    jne .Lcompare_differs
    add rcx, 16
    add rdx, 16
    sub r8, 16
    cmp r8, 16
    jae .Lcompare_16
.Lcompare_last:
    test r8, r8
    jz .Lcompare_done
    # The last 16 bytes of the block: those before the ones left are equal.
    lea rcx, [rcx + r8 - 16]
    lea rdx, [rdx + r8 - 16]
    movdqu xmm0, xmmword ptr [rcx]
    movdqu xmm1, xmmword ptr [rdx]
    pcmpeqb xmm0, xmm1
    pmovmskb r9d, xmm0
    cmp r9d, 0xFFFF
    jne .Lcompare_differs
.Lcompare_done:
    ret
.Lcompare_differs:
    not r9d
    bsf r9d, r9d
    movzx eax, byte ptr [rcx + r9]
    movzx r10d, byte ptr [rdx + r9]
    sub eax, r10d
    ret
.Lcompare_bytes:
    cmp r8, 8
    jb .Lcompare_under_8
    # 8 to 15 bytes: the first 8 and the last 8; the words with their bytes swapped
    # compare as the bytes in order.
    mov r9, qword ptr [rcx]
    mov r10, qword ptr [rdx]
    cmp r9, r10
    jne .Lcompare_word_differs
    mov r9, qword ptr [rcx + r8 - 8]
    mov r10, qword ptr [rdx + r8 - 8]
    cmp r9, r10
    jne .Lcompare_word_differs
    ret
.Lcompare_word_differs:
    bswap r9
    bswap r10
    cmp r9, r10
    sbb eax, eax
    or eax, 1
    ret
.Lcompare_under_8:
    test r8, r8
    jz .Lcompare_done
.Lcompare_byte:
    movzx eax, byte ptr [rcx]
    movzx r9d, byte ptr [rdx]
    sub eax, r9d
    jnz .Lcompare_done
    inc rcx
    inc rdx
    dec r8
    jnz .Lcompare_byte
    ret

#ifdef _WIN32

# __chkstk: touches every 4 KB page of a large stack frame, from the top down,
# so that the guard page grows the stack in order. The size arrives in rax;
# every register except r10 and r11 must be preserved. _alloca_probe is the name
# that the code of MSVC gives it, in the C and C++ libraries linked with a program.
    .globl __chkstk
    .globl _alloca_probe
    .p2align 4
__chkstk:
_alloca_probe:
    push rcx
    push rax
    cmp rax, 0x1000
    lea rcx, [rsp + 24]
    jb .Lprobe_last
.Lprobe_page:
    sub rcx, 0x1000
    test qword ptr [rcx], rcx
    sub rax, 0x1000
    cmp rax, 0x1000
    ja .Lprobe_page
.Lprobe_last:
    sub rcx, rax
    test qword ptr [rcx], rcx
    pop rax
    pop rcx
    ret

#else

# _start: the kernel enters here with the stack aligned to 16 bytes and without a
# return address. The stack holds the number of arguments and their addresses,
# which the runtime reads from cromo_initial_stack. cromo_start, generated by the
# compiler, expects the alignment of a call, and it never returns.
#
# The register fs of every thread points to its block in the runtime, which says
# whether it is a worker of the tasks (D269): for the main thread, arch_prctl
# with ARCH_SET_FS before anything else.
#
# With glibc (D428), fs is that of glibc, and _start calls __libc_start_main as
# the crt1.o of glibc does: with the main and the constructors of runtime/glibc.c,
# the function of the dynamic loader to call at the exit, from rdx, and the end of
# the stack. With Bionic (P43), fs is that of Bionic, and _start gives the stack
# to cromo_bionic_start of runtime/bionic.c, as crtbegin_dynamic.o of the NDK.
    .globl _start
    .p2align 4
_start:
#if defined(CROMO_BIONIC)
    xor ebp, ebp
    mov qword ptr [rip + cromo_initial_stack], rsp
    mov rdi, rsp
    and rsp, -16
    call cromo_bionic_start
    ud2
#elif defined(CROMO_GLIBC)
    xor ebp, ebp
    mov qword ptr [rip + cromo_initial_stack], rsp
    mov r9, rdx
    mov rsi, qword ptr [rsp]
    lea rdx, [rsp + 8]
    and rsp, -16
    push rax
    push rsp
    xor r8d, r8d
    lea rcx, [rip + cromo_glibc_init]
    lea rdi, [rip + cromo_glibc_main]
    call __libc_start_main@PLT
    ud2
#else
    xor ebp, ebp
    mov qword ptr [rip + cromo_initial_stack], rsp
    and rsp, -16
    mov eax, 158
    mov edi, 0x1002
    lea rsi, [rip + cromo_main_block]
    syscall
    call cromo_start
    ud2
#endif

# void cromo_switch(void** save, void* next): leaves the stack of the caller,
# keeping its stack pointer in *save, and continues on the stack `next` where
# another cromo_switch left it (D269). The registers that a call preserves, and
# the control words of SSE and x87, stay on each stack.
    .globl cromo_switch
    .p2align 4
cromo_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8
    stmxcsr dword ptr [rsp]
    fnstcw word ptr [rsp + 4]
    mov qword ptr [rdi], rsp
    mov rsp, rsi
    ldmxcsr dword ptr [rsp]
    fldcw word ptr [rsp + 4]
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

# Where a new stack of a task starts: its first cromo_switch returns here, with
# the stack in rbx and the stack pointer aligned to 16 bytes.
    .globl cromo_task_entry
    .p2align 4
cromo_task_entry:
    mov rdi, rbx
    call cromo_task_run
    ud2

# The return of a handler of a signal, rt_sigreturn: the C library gives it to
# rt_sigaction as SA_RESTORER, and so does the runtime (D339).
    .globl cromo_sigreturn
    .p2align 4
cromo_sigreturn:
    mov eax, 15
    syscall
    ud2

    .section .note.GNU-stack, "", @progbits

#endif
