# Memory functions and stack probe for Windows x64 (D19), assembled with the
# assembler integrated in LLVM. LLVM can emit calls to these functions even when
# the program does not use them, so the runtime must always provide them.
# Arguments arrive in rcx, rdx, r8; rdi and rsi must be preserved.

    .intel_syntax noprefix
    .text

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

# int memcmp(const void* a, const void* b, size_t count)
    .globl memcmp
    .p2align 4
memcmp:
    xor eax, eax
    test r8, r8
    jz .Lcompare_done
    mov r9, rdi
    mov r10, rsi
    mov rsi, rcx
    mov rdi, rdx
    mov rcx, r8
    repe cmpsb
    je .Lcompare_restore
    movzx eax, byte ptr [rsi - 1]
    movzx edx, byte ptr [rdi - 1]
    sub eax, edx
.Lcompare_restore:
    mov rdi, r9
    mov rsi, r10
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
