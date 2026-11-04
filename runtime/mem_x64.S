# Memory functions for x64 (D19), assembled with the assembler integrated in
# LLVM. LLVM can emit calls to these functions even when the program does not
# use them, so the runtime must always provide them. CMake assembles the file
# once for each system (D142):
# - Windows: the arguments arrive in rcx, rdx, r8; rdi and rsi must be preserved.
#   The file also has the stack probe __chkstk.
# - Linux: the arguments arrive in rdi, rsi, rdx, and every register used here
#   can be changed. The file also has the entry point _start.
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

#ifdef _WIN32

# void* memcpy(void* dest, const void* src, size_t count)
    .globl memcpy
    .p2align 4
memcpy:
    mov rax, rcx
    mov r9, rdi
    mov r10, rsi
    mov rdi, rcx
    mov rsi, rdx
    mov rcx, r8
    rep movsb
    mov rdi, r9
    mov rsi, r10
    ret

# void* memset(void* dest, int value, size_t count)
    .globl memset
    .p2align 4
memset:
    mov r10, rcx
    mov r9, rdi
    mov rdi, rcx
    movzx eax, dl
    mov rcx, r8
    rep stosb
    mov rax, r10
    mov rdi, r9
    ret

# void* memmove(void* dest, const void* src, size_t count): copies backwards
# when the destination overlaps the end of the source.
    .globl memmove
    .p2align 4
memmove:
    mov rax, rcx
    mov r9, rdi
    mov r10, rsi
    mov rdi, rcx
    mov rsi, rdx
    mov rcx, r8
    cmp rdi, rsi
    jbe .Lmove_forward
    lea r11, [rsi + rcx]
    cmp rdi, r11
    jae .Lmove_forward
    lea rsi, [rsi + rcx - 1]
    lea rdi, [rdi + rcx - 1]
    std
    rep movsb
    cld
    jmp .Lmove_done
.Lmove_forward:
    rep movsb
.Lmove_done:
    mov rdi, r9
    mov rsi, r10
    ret

# int memcmp(const void* a, const void* b, size_t count): 8 bytes at a time; at
# the first word that differs, the words with their bytes swapped compare as the
# bytes in order, which gives the sign.
    .globl memcmp
    .p2align 4
memcmp:
    xor eax, eax
.Lcompare_words:
    cmp r8, 8
    jb .Lcompare_bytes
    mov r9, qword ptr [rcx]
    mov r10, qword ptr [rdx]
    cmp r9, r10
    jne .Lcompare_word_differs
    add rcx, 8
    add rdx, 8
    sub r8, 8
    jmp .Lcompare_words
.Lcompare_word_differs:
    bswap r9
    bswap r10
    cmp r9, r10
    sbb eax, eax
    or eax, 1
    ret
.Lcompare_bytes:
    test r8, r8
    jz .Lcompare_done
    movzx r9d, byte ptr [rcx]
    movzx r10d, byte ptr [rdx]
    sub r9d, r10d
    jnz .Lcompare_byte_differs
    inc rcx
    inc rdx
    dec r8
    jmp .Lcompare_bytes
.Lcompare_byte_differs:
    mov eax, r9d
.Lcompare_done:
    ret

# __chkstk: touches every 4 KB page of a large stack frame, from the top down,
# so that the guard page grows the stack in order. The size arrives in rax;
# every register except r10 and r11 must be preserved.
    .globl __chkstk
    .p2align 4
__chkstk:
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

# void* memcpy(void* dest, const void* src, size_t count)
    .globl memcpy
    .p2align 4
memcpy:
    mov rax, rdi
    mov rcx, rdx
    rep movsb
    ret

# void* memset(void* dest, int value, size_t count)
    .globl memset
    .p2align 4
memset:
    mov r8, rdi
    movzx eax, sil
    mov rcx, rdx
    rep stosb
    mov rax, r8
    ret

# void* memmove(void* dest, const void* src, size_t count): copies backwards
# when the destination overlaps the end of the source.
    .globl memmove
    .p2align 4
memmove:
    mov rax, rdi
    mov rcx, rdx
    cmp rdi, rsi
    jbe .Lmove_forward
    lea r8, [rsi + rcx]
    cmp rdi, r8
    jae .Lmove_forward
    lea rsi, [rsi + rcx - 1]
    lea rdi, [rdi + rcx - 1]
    std
    rep movsb
    cld
    ret
.Lmove_forward:
    rep movsb
    ret

# int memcmp(const void* a, const void* b, size_t count). On Linux LLVM calls
# bcmp when it only needs to know whether the bytes are equal.
# Also 8 bytes at a time, as on Windows.
    .globl memcmp
    .globl bcmp
    .p2align 4
memcmp:
bcmp:
    xor eax, eax
.Lcompare_words:
    cmp rdx, 8
    jb .Lcompare_bytes
    mov rcx, qword ptr [rdi]
    mov r8, qword ptr [rsi]
    cmp rcx, r8
    jne .Lcompare_word_differs
    add rdi, 8
    add rsi, 8
    sub rdx, 8
    jmp .Lcompare_words
.Lcompare_word_differs:
    bswap rcx
    bswap r8
    cmp rcx, r8
    sbb eax, eax
    or eax, 1
    ret
.Lcompare_bytes:
    test rdx, rdx
    jz .Lcompare_done
    movzx ecx, byte ptr [rdi]
    movzx r8d, byte ptr [rsi]
    sub ecx, r8d
    jnz .Lcompare_byte_differs
    inc rdi
    inc rsi
    dec rdx
    jmp .Lcompare_bytes
.Lcompare_byte_differs:
    mov eax, ecx
.Lcompare_done:
    ret

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
