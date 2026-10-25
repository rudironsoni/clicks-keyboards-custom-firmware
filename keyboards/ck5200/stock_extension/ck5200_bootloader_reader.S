/* In-place V122 command 08 replacement. No calls or flash writes. */
.section .text
.global bootloader_reader
bootloader_reader:
    li a2, 7
    bne s3, a2, rejected
    lbu a2, 2(s1)
    li a3, 0x52
    bne a2, a3, rejected
    lbu a2, 3(s1)
    li a3, 0x44
    bne a2, a3, rejected
    lbu a2, 4(s1)
    slli a2, a2, 8
    lbu a3, 5(s1)
    or a2, a2, a3
    lbu a4, 6(s1)
    addi a3, a4, -1
    li a5, 15
    bltu a5, a3, rejected
    add a3, a2, a4
    li a5, 0x2000
    bltu a5, a3, rejected
    li a3, 0x00080209
    add a3, a3, a4
    sw a3, 0x10c(s0)
    li a3, 0x4452
    sh a3, 0x110(s0)
    addi a3, s1, 4
    addi a5, s0, 0x112
    li a6, 3
echo:
    lbu a7, 0(a3)
    sb a7, 0(a5)
    addi a3, a3, 1
    addi a5, a5, 1
    addi a6, a6, -1
    bnez a6, echo
    lui a3, 0x8000
    add a2, a2, a3
copy:
    lbu a3, 0(a2)
    sb a3, 0(a5)
    addi a2, a2, 1
    addi a5, a5, 1
    addi a4, a4, -1
    bnez a4, copy
    j 0x358a
rejected:
    j 0x340a
