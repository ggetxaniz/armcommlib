//Copyright (c) 2018-2019, ARM Limited. All rights reserved.
//
//SPDX-License-Identifier:        BSD-3-Clause

/*
Approach - assume we don't want to reload constants, so reserve ~half of vector register file for constants

main loop to act on 4 16B blocks per iteration, and then do modulo of the accumulated intermediate hashes from the 4 blocks

 ____________________________________________________
|                                                    |
| PRE                                                |
|____________________________________________________|
|                |                |                  |
| CTR block 4k+8 | AES block 4k+4 | GHASH block 4k+0 |
|________________|________________|__________________|
|                |                |                  |
| CTR block 4k+9 | AES block 4k+5 | GHASH block 4k+1 |
|________________|________________|__________________|
|                |                |                  |
| CTR block 4k+10| AES block 4k+6 | GHASH block 4k+2 |
|________________|________________|__________________|
|                |                |                  |
| CTR block 4k+11| AES block 4k+7 | GHASH block 4k+3 |
|________________|____(mostly)____|__________________|
|                                                    |
| MODULO                                             |
|____________________________________________________|

PRE:
        Ensure previous generated intermediate hash is aligned and merged with result for GHASH 4k+0
    EXT low_acc, low_acc, low_acc, #8
    EOR res_curr (4k+0), res_curr (4k+0), low_acc   // needs merging after res_curr has been byte reversed

CTR block:
        Increment and byte reverse counter in scalar registers and transfer to SIMD registers
    REV     ctr32, rev_ctr32
    ORR     ctr64, constctr96_top32, ctr32, LSL #32
    INS     ctr_next.d[0], constctr96_bottom64      // Keeping this in scalar registers to free up space in SIMD RF
    INS     ctr_next.d[1], ctr64X
    ADD     rev_ctr32, #1

AES block:
        Do AES encryption on CTR block X and EOR it with input block X
        Doing small trick here of doing final part of decryption in scalar registers, EORing with last key and then storing
        Given we are very constrained in our ASIMD registers this is quite important
    AESE    ctr_curr, k0; AESMC ctr_curr, ctr_curr
    AESE    ctr_curr, k1; AESMC ctr_curr, ctr_curr
    AESE    ctr_curr, k2; AESMC ctr_curr, ctr_curr
    AESE    ctr_curr, k3; AESMC ctr_curr, ctr_curr
    AESE    ctr_curr, k4; AESMC ctr_curr, ctr_curr
    AESE    ctr_curr, k5; AESMC ctr_curr, ctr_curr
    AESE    ctr_curr, k6; AESMC ctr_curr, ctr_curr
    AESE    ctr_curr, k7; AESMC ctr_curr, ctr_curr
    AESE    ctr_curr, k8; AESMC ctr_curr, ctr_curr
    AESE    ctr_curr, k9; AESMC ctr_curr, ctr_curr
    AESE    ctr_curr, k10; AESMC ctr_curr, ctr_curr
    AESE    ctr_curr, k11; AESMC ctr_curr, ctr_curr
    AESE    ctr_curr, k12; AESMC ctr_curr, ctr_curr
    AESE    ctr_curr, k13
    LDR     res_curr, [ input_ptr ], #16
    EOR     res_curr, res_curr, ctr_curr
    MOV     output_low, res_curr.d[0]
    MOV     output_high, res_curr.d[1]
    EOR     output_low, k14_low
    EOR     output_high, k14_high
    STP     output_low, output_high, [ output_ptr ], #16

GHASH block X:
        do 128b karatsuba polynomial multiplication on block

        We only have 64b->128b polynomial multipliers, naively that means we need to do 4 64b multiplies to generate a 128b multiplication:
            Pmull(A,B) == (Pmull(Ah,Bh)<<128 | Pmull(Al,Bl)) ^ (Pmull(Ah,Bl) ^ Pmull(Al,Bh))<<64

        The idea behind Karatsuba multiplication is that we can do just 3 64b multiplies:
            Pmull(A,B) == (Pmull(Ah,Bh)<<128 | Pmull(Al,Bl)) ^ (Pmull(Ah^Al,Bh^Bl) ^ Pmull(Ah,Bh) ^ Pmull(Al,Bl))<<64

        There is some complication here because the bit order of GHASH's PMULL is reversed compared to elsewhere, so we are multiplying with "twisted" powers of H

        Note: We can PMULL directly into the acc_x in first GHASH of the loop
        Note: For scheduling big cores we want to split the processing to happen over two loop iterations - otherwise the critical path latency dominates the performance
              This has a knock on effect on register pressure, so we have to be a bit more clever with our temporary registers than indicated here
    REV64   res_curr, res_curr
    INS     t_m.d[0], res_curr.d[1]
    EOR     t_m.8B, t_m.8B, res_curr.8B
    PMULL2  t_h, res_curr, HX
    PMULL   t_l, res_curr, HX
    PMULL   t_m, t_m, HX_k
    EOR     acc_h, acc_h, t_h
    EOR     acc_l, acc_l, t_l
    EOR     acc_m, acc_m, t_m

MODULO: take the partial accumulators (~representing sum of 256b multiplication results), from GHASH and do modulo reduction on them

        There is some complication here because the bit order of GHASH's PMULL is reversed compared to elsewhere, so we are doing modulo with a reversed constant
    EOR     acc_m, acc_m, acc_h
    EOR     acc_m, acc_m, acc_l                     // Finish off karatsuba processing
    PMULL   t_mod, acc_h, mod_constant
    EXT     acc_h, acc_h, acc_h, #8
    EOR     acc_m, acc_m, acc_h
    EOR     acc_m, acc_m, t_mod
    PMULL   acc_h, acc_m, mod_constant
    EXT     acc_m, acc_m, acc_m, #8
    EOR     acc_l, acc_l, acc_h
    EOR     acc_l, acc_l, acc_m

Scalar RF:
x0  input_ptr   //expected - will use asm input operand
x1  bit_length  //expected - will use asm input
x2  cs_ptr      //expected - will use asm input
x3  output_ptr  //expected - will use asm input
x4  end_input_ptr       // start_input_ptr + (bit_length >> 3)
x5  main_end_input_ptr  // start_input_ptr + 16 + (bit_length >> (3 (bits->bytes)+4 (bytes->blocks)+2 (multiple of 4 blocks)))
x6  output_l
x7  output_h
x9  ctr32
x10 constctr96_top32
x11 constctr96_bot64
x12 rev_ctr32
x13 rk14_l
x14 rk14_h


ASIMD RF layout:
v0-v3 ctr output of CTR and main variable for AES
    v0  ctr0
    v1  ctr1
    v2  ctr2
    v3  ctr3
v4-v7 res output of previous iteration of AES-CTR, input to this round of GHASH
      (also used as temps when they are consumed)
    v4  res0
    v5  res1
    v6  res2
    v7  res3
v8  various_temps / Modulo constant
v9  acc_h
v10 acc_m
v11 acc_l
v12-v17  Constant hash powers
    v12 h1l | h1h
    v13 k2k | h1k
    v14 h2l | h2h
    v15 h3l | h3h
    v16 h4k | h3k
    v17 h4l | h4h
v18-v31 Constant AES round keys (only need 10 for 128b AES, but reserving 4 more for 256b AES for minimal code changes)
    v18 rk0
    v19 rk1
    v20 rk2
    v21 rk3
    v22 rk4
    v23 rk5
    v24 rk6
    v25 rk7
    v26 rk8
    v27 rk9
    v28 rk10
    v29 rk11
    v30 rk12
    v31 rk13
*/

#define end_input_ptr       "x4"
#define main_end_input_ptr  "x5"
#define output_l0     "x6"
#define output_h0     "x7"
#define output_l1     "x19"
#define output_h1     "x20"
#define output_l2     "x21"
#define output_h2     "x22"
#define output_l3     "x23"
#define output_h3     "x24"

#define ctr32w      "w9"
#define ctr32x      "x9"
#define ctr96_b64x  "x10"
#define ctr96_t32w  "w11"
#define ctr96_t32x  "x11"
#define rctr32w     "w12"
#define rctr32x     "x12"

#define rk14_l      "x13"
#define rk14_h      "x14"

#define ctr0b       "v0.16b"
#define ctr0        "v0"
#define ctr0d       "d0"
#define ctr1b       "v1.16b"
#define ctr1        "v1"
#define ctr1d       "d1"
#define ctr2b       "v2.16b"
#define ctr2        "v2"
#define ctr2d       "d2"
#define ctr3b       "v3.16b"
#define ctr3        "v3"
#define ctr3d       "d3"

#define res0b       "v4.16b"
#define res0        "v4"
#define res0d       "d4"
#define res0q       "q4"
#define res1b       "v5.16b"
#define res1        "v5"
#define res1d       "d5"
#define res1q       "q5"
#define res2b       "v6.16b"
#define res2        "v6"
#define res2d       "d6"
#define res2q       "q6"
#define res3b       "v7.16b"
#define res3        "v7"
#define res3d       "d7"
#define res3q       "q7"

#define acc_hb      "v9.16b"
#define acc_h       "v9"
#define acc_hd      "d9"
#define acc_mb      "v10.16b"
#define acc_m       "v10"
#define acc_md      "d10"
#define acc_lb      "v11.16b"
#define acc_l       "v11"
#define acc_ld      "d11"

#define h1          "v12"
#define h1q         "q12"
#define h2          "v13"
#define h2q         "q13"
#define h3          "v14"
#define h3q         "q14"
#define h4          "v15"
#define h4q         "q15"
#define h12k        "v16"
#define h34k        "v17"

#define t0          "v8" //temp 1st hash (8 free)
#define t0d         "d8"

#define t1          "v4" //temp 2nd hash high (4, 8 free)
#define t1d         "d4"
#define t2          "v8" //temp 2nd hash low
#define t2d         "d8"
#define t3          "v4" //temp 2nd hash mid
#define t3d         "d4"

#define t4          "v4" //temp 3rd hash high (4, 5, 8 free)
#define t4d         "d4"
#define t5          "v5" //temp 3rd hash low
#define t5d         "d5"
#define t6          "v8" //temp 3rd hash mid
#define t6d         "d8"

#define t7          "v5" //temp 4th hash high  (4, 5, 6, 8 free)
#define t7d         "d5"
#define t8          "v4" //temp 4th hash low
#define t8d         "d4"
#define t9          "v6" //temp 4th hash mid
#define t9d         "d6"

#define ctr_t0      "v4"
#define ctr_t0b     "v4.16b"
#define ctr_t1      "v5"
#define ctr_t1b     "v5.16b"
#define ctr_t2      "v6"
#define ctr_t2b     "v6.16b"
#define ctr_t3      "v7"
#define ctr_t3b     "v7.16b"

#define mod_constantd "d8"
#define mod_constant  "v8"
#define mod_t         "v7"

#define rk0         "v18.16b"
#define rk0q        "q18"
#define rk1         "v19.16b"
#define rk1q        "q19"
#define rk2         "v20.16b"
#define rk2q        "q20"
#define rk2q1       "v20.1q"
#define rk3         "v21.16b"
#define rk3q        "q21"
#define rk3q1       "v21.1q"
#define rk4         "v22.16b"
#define rk4q        "q22"
#define rk4v        "v22"
#define rk4d        "d22"
#define rk5         "v23.16b"
#define rk5q        "q23"
#define rk6         "v24.16b"
#define rk6q        "q24"
#define rk7         "v25.16b"
#define rk7q        "q25"
#define rk8         "v26.16b"
#define rk8q        "q26"
#define rk9         "v27.16b"
#define rk9q        "q27"
#define rk10        "v28.16b"
#define rk10q       "q28"
#define rk11        "v29.16b"
#define rk11q       "q29"
#define rk12        "v30.16b"
#define rk12q       "q30"
#define rk13        "v31.16b"
#define rk13q       "q31"

static operation_result_t aes_gcm_dec_256_kernel(uint8_t * ciphertext, uint64_t ciphertext_length, cipher_state_t * restrict cs, uint8_t * plaintext)
{
    if(ciphertext_length == 0) {
        return SUCCESSFUL_OPERATION;
    }

    uint8_t * in_ptr = ciphertext;
    uint8_t * out_ptr = plaintext;
    uint64_t bit_length = ciphertext_length;

    __asm __volatile
    (
// aes_gcm_dec_256_kernel__cycles_82_big3_0_0_ipb_30_st_7200_2019_01_24_025500.c
// cycle 0
"       lsr     "main_end_input_ptr", %[bit_length], #3             \n" // byte_len
"       ldp     "ctr96_b64x", "ctr96_t32x", [%[counter]]            \n" // ctr96_b64, ctr96_t32

// cycle 1
"       ldr     "rk8q", [%[cc], #128]                               \n" // load rk8
"       sub     "main_end_input_ptr", "main_end_input_ptr", #1      \n" // byte_len - 1

// cycle 2
"       ldr     "rk7q", [%[cc], #112]                               \n" // load rk7
"       and     "main_end_input_ptr", "main_end_input_ptr", #0xffffffffffffffc0\n" // number of bytes to be processed in main loop (at least 1 byte must be handled by tail)

// cycle 3
"       add     "end_input_ptr", %[input_ptr], %[bit_length], LSR #3\n" // end_input_ptr
"       ldr     "rk6q", [%[cc], #96]                                \n" // load rk6

// cycle 4
"       lsr     "rctr32x", "ctr96_t32x", #32                        \n"
"       ldr     "rk5q", [%[cc], #80]                                \n" // load rk5
"       orr     "ctr96_t32w", "ctr96_t32w", "ctr96_t32w"            \n"

// cycle 5
"       ldr     "rk3q", [%[cc], #48]                                \n" // load rk3
"       add     "main_end_input_ptr", "main_end_input_ptr", %[input_ptr]\n"
"       rev     "rctr32w", "rctr32w"                                \n" // rev_ctr32

// cycle 6
"       add     "rctr32w", "rctr32w", #1                            \n" // increment rev_ctr32
"       fmov    "ctr3d", "ctr96_b64x"                               \n" // CTR block 3

// cycle 7
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 1
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 1
"       fmov    "ctr1d", "ctr96_b64x"                               \n" // CTR block 1

// cycle 8
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 1
"       ld1     { "ctr0b" }, [%[counter]]                           \n" // special case vector load initial counter so we can start first AES block as quickly as possible

// cycle 9
"       fmov    "ctr1".d[1], "ctr32x"                               \n" // CTR block 1
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 2
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 2

// cycle 10
"       fmov    "ctr2d", "ctr96_b64x"                               \n" // CTR block 2
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 2

// cycle 11
"       fmov    "ctr2".d[1], "ctr32x"                               \n" // CTR block 2
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 3

// cycle 12
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 3
"       ldr     "rk0q", [%[cc], #0]                                 \n" // load rk0

// cycle 13
"       fmov    "ctr3".d[1], "ctr32x"                               \n" // CTR block 3
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 3

// cycle 14
"       ldr     "rk4q", [%[cc], #64]                                \n" // load rk4

// cycle 15
"       ldr     "rk13q", [%[cc], #208]                              \n" // load rk13

// cycle 16
"       ldr     "rk1q", [%[cc], #16]                                \n" // load rk1

// cycle 17
"       aese    "ctr0b", "rk0"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 0
"       ldr     "h3q", [%[cc], #272]                                \n" // load h3l | h3h

// cycle 18
"       aese    "ctr3b", "rk0"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 0
"       ldr     "h4q", [%[cc], #288]                                \n" // load h4l | h4h

// cycle 19
"       aese    "ctr1b", "rk0"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 0
"       ldr     "h2q", [%[cc], #256]                                \n" // load h2l | h2h

// cycle 20
"       aese    "ctr2b", "rk0"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 0
"       ldr     "rk2q", [%[cc], #32]                                \n" // load rk2

// cycle 21
"       aese    "ctr0b", "rk1"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 1
"       ldp     "rk14_l", "rk14_h", [%[cc], #224]                   \n" // load rk14

// cycle 22
"       aese    "ctr1b", "rk1"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 1
"       ld1     { "acc_lb" }, [%[current_tag]]                      \n"

// cycle 23
"       aese    "ctr2b", "rk1"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 1
"       ldr     "rk9q", [%[cc], #144]                               \n" // load rk9

// cycle 24
"       aese    "ctr3b", "rk1"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 1
"       ldr     "rk12q", [%[cc], #192]                              \n" // load rk12

// cycle 25
"       aese    "ctr0b", "rk2"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 2
"       ldr     "h1q", [%[cc], #240]                                \n" // load h1l | h1h

// cycle 26
"       aese    "ctr2b", "rk2"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 2
"       ldr     "rk10q", [%[cc], #160]                              \n" // load rk10

// cycle 27
"       aese    "ctr3b", "rk2"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 2

// cycle 28
"       aese    "ctr0b", "rk3"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 3

// cycle 29
"       aese    "ctr1b", "rk2"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 2

// cycle 30
"       aese    "ctr3b", "rk3"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 3

// cycle 31
"       aese    "ctr0b", "rk4"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 4
"       cmp     %[input_ptr], "main_end_input_ptr"                  \n" // check if we have <= 4 blocks

// cycle 32
"       aese    "ctr2b", "rk3"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 3

// cycle 33
"       aese    "ctr1b", "rk3"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 3

// cycle 34
"       aese    "ctr3b", "rk4"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 4

// cycle 35
"       aese    "ctr2b", "rk4"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 4

// cycle 36
"       aese    "ctr1b", "rk4"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 4

// cycle 37
"       aese    "ctr3b", "rk5"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 5

// cycle 38
"       aese    "ctr0b", "rk5"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 5

// cycle 39
"       aese    "ctr1b", "rk5"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 5

// cycle 40
"       aese    "ctr2b", "rk5"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 5

// cycle 41
"       aese    "ctr0b", "rk6"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 6

// cycle 42
"       aese    "ctr3b", "rk6"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 6

// cycle 43
"       aese    "ctr1b", "rk6"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 6

// cycle 44
"       aese    "ctr2b", "rk6"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 6

// cycle 45
"       aese    "ctr0b", "rk7"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 7

// cycle 46
"       aese    "ctr1b", "rk7"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 7

// cycle 47
"       aese    "ctr3b", "rk7"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 7

// cycle 48
"       aese    "ctr0b", "rk8"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 8

// cycle 49
"       aese    "ctr2b", "rk7"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 7

// cycle 50
"       aese    "ctr3b", "rk8"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 8

// cycle 51
"       aese    "ctr1b", "rk8"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 8

// cycle 52
"       aese    "ctr0b", "rk9"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 9

// cycle 53
"       aese    "ctr2b", "rk8"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 8
"       ldr     "rk11q", [%[cc], #176]                              \n" // load rk11

// cycle 54
"       aese    "ctr1b", "rk9"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 9

// cycle 55
"       aese    "ctr0b", "rk10"  \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 10

// cycle 56
"       aese    "ctr3b", "rk9"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 9

// cycle 57
"       aese    "ctr1b", "rk10"  \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 10

// cycle 58
"       aese    "ctr2b", "rk9"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 9

// cycle 59
"       aese    "ctr3b", "rk10"  \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 10

// cycle 60
"       aese    "ctr0b", "rk11"  \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 11

// cycle 61
"       aese    "ctr2b", "rk10"  \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 10

// cycle 62
"       aese    "ctr3b", "rk11"  \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 11

// cycle 63
"       aese    "ctr1b", "rk11"  \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 11

// cycle 64
"       aese    "ctr2b", "rk11"  \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 11

// cycle 65
"       trn1    "acc_h".2d, "h3".2d,    "h4".2d                     \n" // h4h | h3h

// cycle 66
"       trn2    "h34k".2d,  "h3".2d,    "h4".2d                     \n" // h4l | h3l

// cycle 67
"       trn1    "t0".2d,    "h1".2d,    "h2".2d                     \n" // h2h | h1h
"       trn2    "h12k".2d,  "h1".2d,    "h2".2d                     \n" // h2l | h1l

// cycle 68
"       aese    "ctr1b", "rk12"  \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 12

// cycle 69
"       aese    "ctr0b", "rk12"  \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 12

// cycle 70
"       aese    "ctr2b", "rk12"  \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 12

// cycle 71
"       aese    "ctr3b", "rk12"  \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 12
"       eor     "h34k".16b, "h34k".16b, "acc_h".16b                 \n" // h4k | h3k

// cycle 72
"       aese    "ctr1b", "rk13"                                     \n" // AES block 1 - round 13

// cycle 73
"       aese    "ctr2b", "rk13"                                     \n" // AES block 2 - round 13
"       eor     "h12k".16b, "h12k".16b, "t0".16b                    \n" // h2k | h1k

// cycle 74
"       aese    "ctr3b", "rk13"                                     \n" // AES block 3 - round 13

// cycle 75
"       aese    "ctr0b", "rk13"                                     \n" // AES block 0 - round 13
"       b.ge    2f                                                  \n" // handle tail

// cycle 0
"       ldr     "res0q", [%[input_ptr], #0]                         \n" // AES block 0 - load ciphertext

// cycle 2
"       ldr     "res1q", [%[input_ptr], #16]                        \n" // AES block 1 - load ciphertext

// cycle 3
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 4

// cycle 4
"       eor     "ctr0b", "res0b", "ctr0b"                           \n" // AES block 0 - result

// cycle 6
"       eor     "ctr1b", "res1b", "ctr1b"                           \n" // AES block 1 - result
"       rev64   "res1b", "res1b"                                    \n" // GHASH block 1
"       ldr     "res3q", [%[input_ptr], #48]                        \n" // AES block 3 - load ciphertext

// cycle 7
"       mov     "output_h0", "ctr0".d[1]                            \n" // AES block 0 - mov high

// cycle 8
"       mov     "output_l0", "ctr0".d[0]                            \n" // AES block 0 - mov low
"       rev64   "res0b", "res0b"                                    \n" // GHASH block 0
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 4

// cycle 9
"       fmov    "ctr0d", "ctr96_b64x"                               \n" // CTR block 4
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 4

// cycle 10
"       fmov    "ctr0".d[1], "ctr32x"                               \n" // CTR block 4
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 5
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 5

// cycle 11
"       mov     "output_l1", "ctr1".d[0]                            \n" // AES block 1 - mov low

// cycle 12
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 5
"       mov     "output_h1", "ctr1".d[1]                            \n" // AES block 1 - mov high
"       eor     "output_h0", "output_h0", "rk14_h"                  \n" // AES block 0 - round 14 high

// cycle 13
"       eor     "output_l0", "output_l0", "rk14_l"                  \n" // AES block 0 - round 14 low
"       stp     "output_l0", "output_h0", [%[output_ptr]], #16      \n" // AES block 0 - store result
"       fmov    "ctr1d", "ctr96_b64x"                               \n" // CTR block 5

// cycle 14
"       ldr     "res2q", [%[input_ptr], #32]                        \n" // AES block 2 - load ciphertext
"       add     %[input_ptr], %[input_ptr], #64                     \n" // AES input_ptr update

// cycle 15
"       fmov    "ctr1".d[1], "ctr32x"                               \n" // CTR block 5
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 6
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 6

// cycle 16
"       eor     "output_l1", "output_l1", "rk14_l"                  \n" // AES block 1 - round 14 low
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 6

// cycle 17
"       eor     "output_h1", "output_h1", "rk14_h"                  \n" // AES block 1 - round 14 high
"       stp     "output_l1", "output_h1", [%[output_ptr]], #16      \n" // AES block 1 - store result

// cycle 18
"       eor     "ctr2b", "res2b", "ctr2b"                           \n" // AES block 2 - result
"       cmp     %[input_ptr], "main_end_input_ptr"                  \n" // check if we have <= 8 blocks
"       b.ge    7f                                                  \n" // do prepretail

// loop taken from aes_gcm_dec_256_kernel__cycles_81_big3_0_0_ipb_60_st_7200_2019_01_24_025503.c
// cycle 0
"1:                                                                 \n" // main loop start
"       mov     "output_l2", "ctr2".d[0]                            \n" // AES block 4k+2 - mov low
"       ext     "acc_lb", "acc_lb", "acc_lb", #8                    \n" // PRE 0
"       eor     "ctr3b", "res3b", "ctr3b"                           \n" // AES block 4k+3 - result

// cycle 1
"       aese    "ctr0b", "rk0"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 0
"       mov     "output_h2", "ctr2".d[1]                            \n" // AES block 4k+2 - mov high

// cycle 2
"       aese    "ctr1b", "rk0"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 0
"       fmov    "ctr2d", "ctr96_b64x"                               \n" // CTR block 4k+6

// cycle 3
"       fmov    "ctr2".d[1], "ctr32x"                               \n" // CTR block 4k+6
"       eor     "res0b", "res0b", "acc_lb"                          \n" // PRE 1
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 4k+7

// cycle 4
"       aese    "ctr0b", "rk1"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 1
"       mov     "output_h3", "ctr3".d[1]                            \n" // AES block 4k+3 - mov high

// cycle 5
"       aese    "ctr1b", "rk1"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 1
"       mov     "output_l3", "ctr3".d[0]                            \n" // AES block 4k+3 - mov low

// cycle 6
"       pmull2  "acc_h".1q, "res0".2d, "h4".2d                      \n" // GHASH block 4k - high
"       mov     "t0d", "res0".d[1]                                  \n" // GHASH block 4k - mid
"       fmov    "ctr3d", "ctr96_b64x"                               \n" // CTR block 4k+7

// cycle 7
"       aese    "ctr0b", "rk2"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 2
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 4k+7

// cycle 8
"       aese    "ctr2b", "rk0"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 0
"       fmov    "ctr3".d[1], "ctr32x"                               \n" // CTR block 4k+7

// cycle 9
"       aese    "ctr1b", "rk2"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 2
"       eor     "t0".8b, "t0".8b, "res0".8b                         \n" // GHASH block 4k - mid

// cycle 10
"       aese    "ctr0b", "rk3"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 3
"       eor     "output_h2", "output_h2", "rk14_h"                  \n" // AES block 4k+2 - round 14 high

// cycle 11
"       aese    "ctr2b", "rk1"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 1
"       mov     "acc_md", "h34k".d[1]                               \n" // GHASH block 4k - mid

// cycle 12
"       aese    "ctr1b", "rk3"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 3
"       rev64   "res2b", "res2b"                                    \n" // GHASH block 4k+2

// cycle 13
"       aese    "ctr3b", "rk0"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 0
"       eor     "output_l2", "output_l2", "rk14_l"                  \n" // AES block 4k+2 - round 14 low

// cycle 14
"       aese    "ctr2b", "rk2"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 2
"       stp     "output_l2", "output_h2", [%[output_ptr]], #16      \n" // AES block 4k+2 - store result

// cycle 15
"       pmull   "acc_l".1q, "res0".1d, "h4".1d                      \n" // GHASH block 4k - low

// cycle 16
"       pmull2  "t1".1q, "res1".2d, "h3".2d                         \n" // GHASH block 4k+1 - high

// cycle 17
"       aese    "ctr2b", "rk3"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 3
"       rev64   "res3b", "res3b"                                    \n" // GHASH block 4k+3

// cycle 18
"       pmull   "acc_m".1q, "t0".1d, "acc_m".1d                     \n" // GHASH block 4k - mid
"       eor     "output_l3", "output_l3", "rk14_l"                  \n" // AES block 4k+3 - round 14 low

// cycle 19
"       pmull   "t2".1q, "res1".1d, "h3".1d                         \n" // GHASH block 4k+1 - low
"       eor     "output_h3", "output_h3", "rk14_h"                  \n" // AES block 4k+3 - round 14 high
"       eor     "acc_hb", "acc_hb", "t1".16b                        \n" // GHASH block 4k+1 - high

// cycle 20
"       aese    "ctr2b", "rk4"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 4

// cycle 21
"       aese    "ctr3b", "rk1"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 1
"       mov     "t3d", "res1".d[1]                                  \n" // GHASH block 4k+1 - mid

// cycle 22
"       aese    "ctr0b", "rk4"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 4
"       eor     "acc_lb", "acc_lb", "t2".16b                        \n" // GHASH block 4k+1 - low

// cycle 23
"       aese    "ctr2b", "rk5"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 5
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 4k+7

// cycle 24
"       aese    "ctr3b", "rk2"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 2
"       mov     "t6d", "res2".d[1]                                  \n" // GHASH block 4k+2 - mid

// cycle 25
"       aese    "ctr1b", "rk4"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 4
"       eor     "t3".8b, "t3".8b, "res1".8b                         \n" // GHASH block 4k+1 - mid

// cycle 26
"       pmull   "t5".1q, "res2".1d, "h2".1d                         \n" // GHASH block 4k+2 - low

// cycle 27
"       aese    "ctr3b", "rk3"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 3
"       eor     "t6".8b, "t6".8b, "res2".8b                         \n" // GHASH block 4k+2 - mid

// cycle 28
"       aese    "ctr1b", "rk5"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 5

// cycle 29
"       aese    "ctr0b", "rk5"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 5
"       eor     "acc_lb", "acc_lb", "t5".16b                        \n" // GHASH block 4k+2 - low

// cycle 30
"       pmull   "t3".1q, "t3".1d, "h34k".1d                         \n" // GHASH block 4k+1 - mid
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 4k+8

// cycle 31
"       aese    "ctr1b", "rk6"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 6
"       ins     "t6".d[1], "t6".d[0]                                \n" // GHASH block 4k+2 - mid

// cycle 32
"       aese    "ctr0b", "rk6"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 6
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 4k+8

// cycle 33
"       aese    "ctr3b", "rk4"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 4

// cycle 34
"       aese    "ctr1b", "rk7"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 7
"       eor     "acc_mb", "acc_mb", "t3".16b                        \n" // GHASH block 4k+1 - mid

// cycle 35
"       aese    "ctr0b", "rk7"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 7

// cycle 36
"       pmull2  "t4".1q, "res2".2d, "h2".2d                         \n" // GHASH block 4k+2 - high
"       mov     "t9d", "res3".d[1]                                  \n" // GHASH block 4k+3 - mid

// cycle 37
"       aese    "ctr3b", "rk5"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 5

// cycle 38
"       pmull2  "t6".1q, "t6".2d, "h12k".2d                         \n" // GHASH block 4k+2 - mid

// cycle 39
"       aese    "ctr0b", "rk8"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 8
"       eor     "acc_hb", "acc_hb", "t4".16b                        \n" // GHASH block 4k+2 - high

// cycle 40
"       aese    "ctr3b", "rk6"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 6

// cycle 41
"       pmull   "t8".1q, "res3".1d, "h1".1d                         \n" // GHASH block 4k+3 - low
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 4k+8
"       eor     "acc_mb", "acc_mb", "t6".16b                        \n" // GHASH block 4k+2 - mid

// cycle 42
"       pmull2  "t7".1q, "res3".2d, "h1".2d                         \n" // GHASH block 4k+3 - high

// cycle 43
"       aese    "ctr0b", "rk9"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 9
"       eor     "t9".8b, "t9".8b, "res3".8b                         \n" // GHASH block 4k+3 - mid

// cycle 44
"       aese    "ctr1b", "rk8"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 8

// cycle 45
"       aese    "ctr2b", "rk6"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 6
"       eor     "acc_hb", "acc_hb", "t7".16b                        \n" // GHASH block 4k+3 - high

// cycle 46
"       aese    "ctr0b", "rk10"  \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 10

// cycle 47
"       pmull   "t9".1q, "t9".1d, "h12k".1d                         \n" // GHASH block 4k+3 - mid
"       movi    "mod_constant".8b, #0xc2                            \n"

// cycle 48
"       aese    "ctr2b", "rk7"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 7
"       eor     "acc_lb", "acc_lb", "t8".16b                        \n" // GHASH block 4k+3 - low

// cycle 49
"       aese    "ctr0b", "rk11"  \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 11

// cycle 50
"       aese    "ctr3b", "rk7"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 7
"       shl     "mod_constantd", "mod_constantd", #56               \n" // mod_constant

// cycle 51
"       aese    "ctr2b", "rk8"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 8
"       eor     "acc_mb", "acc_mb", "t9".16b                        \n" // GHASH block 4k+3 - mid

// cycle 52
"       aese    "ctr0b", "rk12"  \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 12

// cycle 53
"       pmull   "mod_t".1q, "acc_h".1d, "mod_constant".1d           \n" // MODULO - top 64b align with mid
"       eor     "t9".16b, "acc_lb", "acc_hb"                        \n" // MODULO - karatsuba tidy up

// cycle 54
"       aese    "ctr1b", "rk9"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 9
"       ldr     "res0q", [%[input_ptr], #0]                         \n" // AES block 4k+4 - load ciphertext

// cycle 55
"       aese    "ctr0b", "rk13"                                     \n" // AES block 4k+4 - round 13
"       ext     "acc_hb", "acc_hb", "acc_hb", #8                    \n" // MODULO - other top alignment

// cycle 57
"       aese    "ctr1b", "rk10"  \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 10
"       eor     "acc_mb", "acc_mb", "t9".16b                        \n" // MODULO - karatsuba tidy up

// cycle 58
"       aese    "ctr2b", "rk9"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 9
"       ldr     "res1q", [%[input_ptr], #16]                        \n" // AES block 4k+5 - load ciphertext

// cycle 59
"       aese    "ctr3b", "rk8"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 8
"       eor     "ctr0b", "res0b", "ctr0b"                           \n" // AES block 4k+4 - result

// cycle 60
"       aese    "ctr1b", "rk11"  \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 11
"       stp     "output_l3", "output_h3", [%[output_ptr]], #16      \n" // AES block 4k+3 - store result

// cycle 61
"       aese    "ctr2b", "rk10"  \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 10
"       eor     "acc_mb", "acc_mb", "mod_t".16b                     \n" // MODULO - fold into mid

// cycle 62
"       aese    "ctr3b", "rk9"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 9
"       ldr     "res3q", [%[input_ptr], #48]                        \n" // AES block 4k+7 - load ciphertext

// cycle 63
"       aese    "ctr1b", "rk12"  \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 12
"       ldr     "res2q", [%[input_ptr], #32]                        \n" // AES block 4k+6 - load ciphertext

// cycle 64
"       aese    "ctr2b", "rk11"  \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 11
"       mov     "output_h0", "ctr0".d[1]                            \n" // AES block 4k+4 - mov high

// cycle 65
"       aese    "ctr3b", "rk10"  \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 10
"       eor     "acc_mb", "acc_mb", "acc_hb"                        \n" // MODULO - fold into mid

// cycle 66
"       aese    "ctr1b", "rk13"                                     \n" // AES block 4k+5 - round 13
"       add     %[input_ptr], %[input_ptr], #64                     \n" // AES input_ptr update
"       mov     "output_l0", "ctr0".d[0]                            \n" // AES block 4k+4 - mov low

// cycle 67
"       aese    "ctr2b", "rk12"  \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 12
"       fmov    "ctr0d", "ctr96_b64x"                               \n" // CTR block 4k+8

// cycle 68
"       aese    "ctr3b", "rk11"  \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 11
"       fmov    "ctr0".d[1], "ctr32x"                               \n" // CTR block 4k+8

// cycle 69
"       pmull   "mod_constant".1q, "acc_m".1d, "mod_constant".1d    \n" // MODULO - mid 64b align with low
"       eor     "ctr1b", "res1b", "ctr1b"                           \n" // AES block 4k+5 - result
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 4k+9

// cycle 70
"       aese    "ctr2b", "rk13"                                     \n" // AES block 4k+6 - round 13
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 4k+9
"       cmp     %[input_ptr], "main_end_input_ptr"                  \n" // LOOP CONTROL

// cycle 71
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 4k+9

// cycle 72
"       eor     "output_l0", "output_l0", "rk14_l"                  \n" // AES block 4k+4 - round 14 low
"       eor     "output_h0", "output_h0", "rk14_h"                  \n" // AES block 4k+4 - round 14 high

// cycle 73
"       mov     "output_h1", "ctr1".d[1]                            \n" // AES block 4k+5 - mov high
"       eor     "ctr2b", "res2b", "ctr2b"                           \n" // AES block 4k+6 - result
"       eor     "acc_lb", "acc_lb", "mod_constant".16b              \n" // MODULO - fold into low

// cycle 74
"       aese    "ctr3b", "rk12"  \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 12
"       mov     "output_l1", "ctr1".d[0]                            \n" // AES block 4k+5 - mov low

// cycle 75
"       fmov    "ctr1d", "ctr96_b64x"                               \n" // CTR block 4k+9
"       ext     "acc_mb", "acc_mb", "acc_mb", #8                    \n" // MODULO - other mid alignment

// cycle 76
"       fmov    "ctr1".d[1], "ctr32x"                               \n" // CTR block 4k+9
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 4k+10
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 4k+10

// cycle 77
"       aese    "ctr3b", "rk13"                                     \n" // AES block 4k+7 - round 13
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 4k+10

// cycle 78
"       rev64   "res1b", "res1b"                                    \n" // GHASH block 4k+5
"       eor     "output_h1", "output_h1", "rk14_h"                  \n" // AES block 4k+5 - round 14 high
"       stp     "output_l0", "output_h0", [%[output_ptr]], #16      \n" // AES block 4k+4 - store result

// cycle 79
"       eor     "output_l1", "output_l1", "rk14_l"                  \n" // AES block 4k+5 - round 14 low
"       stp     "output_l1", "output_h1", [%[output_ptr]], #16      \n" // AES block 4k+5 - store result

// cycle 80
"       rev64   "res0b", "res0b"                                    \n" // GHASH block 4k+4
"       eor     "acc_lb", "acc_lb", "acc_mb"                        \n" // MODULO - fold into low
"       b.lt    1b                                                  \n"


// cycle 0
"7:                                                                 \n" // PREPRETAIL
"       ext     "acc_lb", "acc_lb", "acc_lb", #8                    \n" // PRE 0
"       mov     "output_l2", "ctr2".d[0]                            \n" // AES block 4k+2 - mov low
"       eor     "ctr3b", "res3b", "ctr3b"                           \n" // AES block 4k+3 - result

// cycle 1
"       aese    "ctr0b", "rk0"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 0
"       mov     "output_h2", "ctr2".d[1]                            \n" // AES block 4k+2 - mov high

// cycle 2
"       aese    "ctr1b", "rk0"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 0
"       fmov    "ctr2d", "ctr96_b64x"                               \n" // CTR block 4k+6

// cycle 3
"       fmov    "ctr2".d[1], "ctr32x"                               \n" // CTR block 4k+6
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 4k+7
"       eor     "res0b", "res0b", "acc_lb"                          \n" // PRE 1

// cycle 4
"       rev64   "res2b", "res2b"                                    \n" // GHASH block 4k+2
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 4k+7
"       mov     "output_l3", "ctr3".d[0]                            \n" // AES block 4k+3 - mov low

// cycle 5
"       aese    "ctr1b", "rk1"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 1
"       mov     "output_h3", "ctr3".d[1]                            \n" // AES block 4k+3 - mov high

// cycle 6
"       pmull   "acc_l".1q, "res0".1d, "h4".1d                      \n" // GHASH block 4k - low
"       mov     "t0d", "res0".d[1]                                  \n" // GHASH block 4k - mid
"       fmov    "ctr3d", "ctr96_b64x"                               \n" // CTR block 4k+7

// cycle 7
"       pmull2  "acc_h".1q, "res0".2d, "h4".2d                      \n" // GHASH block 4k - high
"       fmov    "ctr3".d[1], "ctr32x"                               \n" // CTR block 4k+7

// cycle 8
"       aese    "ctr2b", "rk0"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 0
"       mov     "acc_md", "h34k".d[1]                               \n" // GHASH block 4k - mid

// cycle 9
"       aese    "ctr0b", "rk1"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 1
"       eor     "t0".8b, "t0".8b, "res0".8b                         \n" // GHASH block 4k - mid

// cycle 10
"       pmull2  "t1".1q, "res1".2d, "h3".2d                         \n" // GHASH block 4k+1 - high

// cycle 11
"       aese    "ctr2b", "rk1"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 1
"       rev64   "res3b", "res3b"                                    \n" // GHASH block 4k+3

// cycle 12
"       aese    "ctr3b", "rk0"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 0

// cycle 13
"       pmull   "acc_m".1q, "t0".1d, "acc_m".1d                     \n" // GHASH block 4k - mid
"       eor     "acc_hb", "acc_hb", "t1".16b                        \n" // GHASH block 4k+1 - high

// cycle 14
"       pmull   "t2".1q, "res1".1d, "h3".1d                         \n" // GHASH block 4k+1 - low

// cycle 15
"       aese    "ctr3b", "rk1"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 1
"       mov     "t3d", "res1".d[1]                                  \n" // GHASH block 4k+1 - mid

// cycle 16
"       aese    "ctr0b", "rk2"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 2

// cycle 17
"       aese    "ctr1b", "rk2"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 2
"       eor     "acc_lb", "acc_lb", "t2".16b                        \n" // GHASH block 4k+1 - low

// cycle 18
"       aese    "ctr2b", "rk2"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 2

// cycle 19
"       aese    "ctr0b", "rk3"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 3
"       mov     "t6d", "res2".d[1]                                  \n" // GHASH block 4k+2 - mid

// cycle 20
"       aese    "ctr3b", "rk2"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 2
"       eor     "t3".8b, "t3".8b, "res1".8b                         \n" // GHASH block 4k+1 - mid

// cycle 21
"       pmull   "t5".1q, "res2".1d, "h2".1d                         \n" // GHASH block 4k+2 - low

// cycle 22
"       aese    "ctr0b", "rk4"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 4

// cycle 23
"       aese    "ctr3b", "rk3"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 3
"       eor     "t6".8b, "t6".8b, "res2".8b                         \n" // GHASH block 4k+2 - mid

// cycle 24
"       pmull   "t3".1q, "t3".1d, "h34k".1d                         \n" // GHASH block 4k+1 - mid

// cycle 25
"       aese    "ctr0b", "rk5"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 5
"       eor     "acc_lb", "acc_lb", "t5".16b                        \n" // GHASH block 4k+2 - low

// cycle 26
"       aese    "ctr3b", "rk4"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 4

// cycle 27
"       pmull2  "t7".1q, "res3".2d, "h1".2d                         \n" // GHASH block 4k+3 - high
"       eor     "acc_mb", "acc_mb", "t3".16b                        \n" // GHASH block 4k+1 - mid

// cycle 28
"       pmull2  "t4".1q, "res2".2d, "h2".2d                         \n" // GHASH block 4k+2 - high

// cycle 29
"       aese    "ctr3b", "rk5"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 5
"       ins     "t6".d[1], "t6".d[0]                                \n" // GHASH block 4k+2 - mid

// cycle 30
"       aese    "ctr2b", "rk3"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 3

// cycle 31
"       aese    "ctr1b", "rk3"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 3
"       eor     "acc_hb", "acc_hb", "t4".16b                        \n" // GHASH block 4k+2 - high

// cycle 32
"       pmull   "t8".1q, "res3".1d, "h1".1d                         \n" // GHASH block 4k+3 - low

// cycle 33
"       aese    "ctr2b", "rk4"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 4
"       mov     "t9d", "res3".d[1]                                  \n" // GHASH block 4k+3 - mid

// cycle 34
"       aese    "ctr1b", "rk4"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 4

// cycle 35
"       pmull2  "t6".1q, "t6".2d, "h12k".2d                         \n" // GHASH block 4k+2 - mid

// cycle 36
"       aese    "ctr2b", "rk5"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 5
"       eor     "t9".8b, "t9".8b, "res3".8b                         \n" // GHASH block 4k+3 - mid

// cycle 37
"       aese    "ctr1b", "rk5"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 5

// cycle 38
"       aese    "ctr3b", "rk6"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 6
"       eor     "acc_mb", "acc_mb", "t6".16b                        \n" // GHASH block 4k+2 - mid

// cycle 39
"       aese    "ctr2b", "rk6"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 6

// cycle 40
"       aese    "ctr0b", "rk6"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 6
"       movi    "mod_constant".8b, #0xc2                            \n"

// cycle 41
"       aese    "ctr1b", "rk6"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 6
"       eor     "acc_lb", "acc_lb", "t8".16b                        \n" // GHASH block 4k+3 - low

// cycle 42
"       pmull   "t9".1q, "t9".1d, "h12k".1d                         \n" // GHASH block 4k+3 - mid

// cycle 43
"       aese    "ctr3b", "rk7"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 7
"       eor     "acc_hb", "acc_hb", "t7".16b                        \n" // GHASH block 4k+3 - high

// cycle 44
"       aese    "ctr1b", "rk7"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 7

// cycle 45
"       aese    "ctr0b", "rk7"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 7
"       eor     "acc_mb", "acc_mb", "t9".16b                        \n" // GHASH block 4k+3 - mid

// cycle 46
"       aese    "ctr3b", "rk8"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 8

// cycle 47
"       aese    "ctr2b", "rk7"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 7
"       eor     "t9".16b, "acc_lb", "acc_hb"                        \n" // MODULO - karatsuba tidy up

// cycle 48
"       aese    "ctr1b", "rk8"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 8

// cycle 49
"       aese    "ctr0b", "rk8"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 8
"       shl     "mod_constantd", "mod_constantd", #56               \n" // mod_constant

// cycle 50
"       aese    "ctr2b", "rk8"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 8

// cycle 51
"       aese    "ctr1b", "rk9"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 9
"       eor     "acc_mb", "acc_mb", "t9".16b                        \n" // MODULO - karatsuba tidy up

// cycle 52
"       pmull   "mod_t".1q, "acc_h".1d, "mod_constant".1d           \n" // MODULO - top 64b align with mid

// cycle 53
"       aese    "ctr2b", "rk9"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 9
"       ext     "acc_hb", "acc_hb", "acc_hb", #8                    \n" // MODULO - other top alignment

// cycle 54
"       aese    "ctr3b", "rk9"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 9

// cycle 55
"       aese    "ctr0b", "rk9"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 9
"       eor     "acc_mb", "acc_mb", "mod_t".16b                     \n" // MODULO - fold into mid

// cycle 56
"       aese    "ctr2b", "rk10"  \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 10

// cycle 57
"       aese    "ctr3b", "rk10"  \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 10

// cycle 58
"       aese    "ctr0b", "rk10"  \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 10
"       eor     "output_h2", "output_h2", "rk14_h"                  \n" // AES block 4k+2 - round 14 high

// cycle 59
"       aese    "ctr1b", "rk10"  \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 10
"       eor     "output_l3", "output_l3", "rk14_l"                  \n" // AES block 4k+3 - round 14 low

// cycle 60
"       aese    "ctr2b", "rk11"  \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 11
"       eor     "acc_mb", "acc_mb", "acc_hb"                        \n" // MODULO - fold into mid

// cycle 61
"       aese    "ctr0b", "rk11"  \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 11
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 4k+7

// cycle 62
"       aese    "ctr1b", "rk11"  \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 11
"       eor     "output_l2", "output_l2", "rk14_l"                  \n" // AES block 4k+2 - round 14 low

// cycle 63
"       aese    "ctr2b", "rk12"  \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 12

// cycle 64
"       pmull   "mod_constant".1q, "acc_m".1d, "mod_constant".1d    \n" // MODULO - mid 64b align with low
"       eor     "output_h3", "output_h3", "rk14_h"                  \n" // AES block 4k+3 - round 14 high

// cycle 65
"       aese    "ctr3b", "rk11"  \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 11
"       stp     "output_l2", "output_h2", [%[output_ptr]], #16      \n" // AES block 4k+2 - store result

// cycle 66
"       aese    "ctr1b", "rk12"  \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 12
"       ext     "acc_mb", "acc_mb", "acc_mb", #8                    \n" // MODULO - other mid alignment

// cycle 67
"       aese    "ctr0b", "rk12"  \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 12
"       stp     "output_l3", "output_h3", [%[output_ptr]], #16      \n" // AES block 4k+3 - store result

// cycle 68
"       aese    "ctr3b", "rk12"  \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 12
"       eor     "acc_lb", "acc_lb", "mod_constant".16b              \n" // MODULO - fold into low

// cycle 69
"       aese    "ctr1b", "rk13"                                     \n" // AES block 4k+5 - round 13

// cycle 70
"       aese    "ctr0b", "rk13"                                     \n" // AES block 4k+4 - round 13

// cycle 71
"       aese    "ctr3b", "rk13"                                     \n" // AES block 4k+7 - round 13

// cycle 72
"       aese    "ctr2b", "rk13"                                     \n" // AES block 4k+6 - round 13
"       eor     "acc_lb", "acc_lb", "acc_mb"                        \n" // MODULO - fold into low
"2:                                                                 \n" // TAIL

// cycle 0
"       sub     "main_end_input_ptr", "end_input_ptr", %[input_ptr] \n" // main_end_input_ptr is number of bytes left to process
"       ld1     { "res1b" }, [%[input_ptr]], #16                    \n" // AES block 4k+4 - load ciphertext

// cycle 5
"       eor     "ctr0b", "res1b", "ctr0b"                           \n" // AES block 4k+4 - result

// cycle 8
"       mov     "output_l0", "ctr0".d[0]                            \n" // AES block 4k+4 - mov low

// cycle 9
"       mov     "output_h0", "ctr0".d[1]                            \n" // AES block 4k+4 - mov high
"       ext     "t0".16b, "acc_lb", "acc_lb", #8                    \n" // prepare final partial tag

// cycle 12
"       cmp     "main_end_input_ptr", #48                           \n"

// cycle 13
"       eor     "output_l0", "output_l0", "rk14_l"                  \n" // AES block 4k+4 - round 14 low

// cycle 14
"       eor     "output_h0", "output_h0", "rk14_h"                  \n" // AES block 4k+4 - round 14 high
"       b.gt    3f                                                  \n"

// cycle 0
"       sub     "rctr32w", "rctr32w", #1                            \n"
"       mov     "ctr3b", "ctr2b"                                    \n"
"       movi    "acc_m".8b, #0                                      \n"

// cycle 1
"       movi    "acc_l".8b, #0                                      \n"
"       cmp     "main_end_input_ptr", #32                           \n"

// cycle 2
"       movi    "acc_h".8b, #0                                      \n"
"       mov     "ctr2b", "ctr1b"                                    \n"
"       b.gt    4f                                                  \n"

// cycle 0
"       sub     "rctr32w", "rctr32w", #1                            \n"

// cycle 1
"       mov     "ctr3b", "ctr1b"                                    \n"
"       cmp     "main_end_input_ptr", #16                           \n"
"       b.gt    5f                                                  \n"

// cycle 0
"       sub     "rctr32w", "rctr32w", #1                            \n"
"       b       6f                                                  \n"
"3:                                                                 \n" // blocks left >  3
"       rev64   "res0b", "res1b"                                    \n" // GHASH final-3 block
"       ld1     { "res1b" }, [%[input_ptr]], #16                    \n" // AES final-2 block - load ciphertext

// cycle 1
"       stp     "output_l0", "output_h0", [%[output_ptr]], #16      \n" // AES final-3 block  - store result

// cycle 2
"       mov     "acc_md", "h34k".d[1]                               \n" // GHASH final-3 block - mid

// cycle 4
"       eor     "res0b", "res0b", "t0".16b                          \n" // feed in partial tag

// cycle 5
"       eor     "ctr0b", "res1b", "ctr1b"                           \n" // AES final-2 block - result

// cycle 7
"       mov     "rk4d", "res0".d[1]                                 \n" // GHASH final-3 block - mid

// cycle 8
"       mov     "output_l0", "ctr0".d[0]                            \n" // AES final-2 block - mov low

// cycle 9
"       mov     "output_h0", "ctr0".d[1]                            \n" // AES final-2 block - mov high

// cycle 10
"       eor     "rk4v".8b, "rk4v".8b, "res0".8b                     \n" // GHASH final-3 block - mid

// cycle 11
"       movi    "t0".8b, #0                                         \n" // surpress further partial tag feed in

// cycle 12
"       pmull2  "acc_h".1q, "res0".2d, "h4".2d                      \n" // GHASH final-3 block - high

// cycle 13
"       pmull   "acc_m".1q, "rk4v".1d, "acc_m".1d                   \n" // GHASH final-3 block - mid
"       eor     "output_l0", "output_l0", "rk14_l"                  \n" // AES final-2 block - round 14 low

// cycle 14
"       pmull   "acc_l".1q, "res0".1d, "h4".1d                      \n" // GHASH final-3 block - low
"       eor     "output_h0", "output_h0", "rk14_h"                  \n" // AES final-2 block - round 14 high
"4:                                                                 \n" // blocks left >  2

// cycle 0
"       rev64   "res0b", "res1b"                                    \n" // GHASH final-2 block
"       ld1     { "res1b" }, [%[input_ptr]], #16                    \n" // AES final-1 block - load ciphertext

// cycle 3
"       eor     "res0b", "res0b", "t0".16b                          \n" // feed in partial tag
"       stp     "output_l0", "output_h0", [%[output_ptr]], #16      \n" // AES final-2 block  - store result

// cycle 5
"       eor     "ctr0b", "res1b", "ctr2b"                           \n" // AES final-1 block - result

// cycle 6
"       mov     "rk4d", "res0".d[1]                                 \n" // GHASH final-2 block - mid

// cycle 7
"       pmull   "rk3q1", "res0".1d, "h3".1d                         \n" // GHASH final-2 block - low

// cycle 8
"       pmull2  "rk2q1", "res0".2d, "h3".2d                         \n" // GHASH final-2 block - high

// cycle 9
"       eor     "rk4v".8b, "rk4v".8b, "res0".8b                     \n" // GHASH final-2 block - mid
"       mov     "output_l0", "ctr0".d[0]                            \n" // AES final-1 block - mov low

// cycle 10
"       mov     "output_h0", "ctr0".d[1]                            \n" // AES final-1 block - mov high
"       eor     "acc_lb", "acc_lb", "rk3"                           \n" // GHASH final-2 block - low
"       movi    "t0".8b, #0                                         \n" // surpress further partial tag feed in

// cycle 12
"       pmull   "rk4v".1q, "rk4v".1d, "h34k".1d                     \n" // GHASH final-2 block - mid

// cycle 14
"       eor     "acc_hb", "acc_hb", "rk2"                           \n" // GHASH final-2 block - high
"       eor     "output_l0", "output_l0", "rk14_l"                  \n" // AES final-1 block - round 14 low

// cycle 15
"       eor     "acc_mb", "acc_mb", "rk4v".16b                      \n" // GHASH final-2 block - mid
"       eor     "output_h0", "output_h0", "rk14_h"                  \n" // AES final-1 block - round 14 high
"5:                                                                 \n" // blocks left >  1

// cycle 0
"       stp     "output_l0", "output_h0", [%[output_ptr]], #16      \n" // AES final-1 block  - store result
"       rev64   "res0b", "res1b"                                    \n" // GHASH final-1 block

// cycle 2
"       ld1     { "res1b" }, [%[input_ptr]], #16                    \n" // AES final block - load ciphertext

// cycle 3
"       eor     "res0b", "res0b", "t0".16b                          \n" // feed in partial tag
"       movi    "t0".8b, #0                                         \n" // surpress further partial tag feed in

// cycle 6
"       mov     "rk4d", "res0".d[1]                                 \n" // GHASH final-1 block - mid

// cycle 7
"       eor     "ctr0b", "res1b", "ctr3b"                           \n" // AES final block - result

// cycle 8
"       pmull2  "rk2q1", "res0".2d, "h2".2d                         \n" // GHASH final-1 block - high

// cycle 9
"       eor     "rk4v".8b, "rk4v".8b, "res0".8b                     \n" // GHASH final-1 block - mid

// cycle 10
"       pmull   "rk3q1", "res0".1d, "h2".1d                         \n" // GHASH final-1 block - low
"       mov     "output_l0", "ctr0".d[0]                            \n" // AES final block - mov low

// cycle 12
"       ins     "rk4v".d[1], "rk4v".d[0]                            \n" // GHASH final-1 block - mid

// cycle 13
"       mov     "output_h0", "ctr0".d[1]                            \n" // AES final block - mov high

// cycle 15
"       pmull2  "rk4v".1q, "rk4v".2d, "h12k".2d                     \n" // GHASH final-1 block - mid
"       eor     "output_l0", "output_l0", "rk14_l"                  \n" // AES final block - round 14 low

// cycle 16
"       eor     "acc_lb", "acc_lb", "rk3"                           \n" // GHASH final-1 block - low

// cycle 17
"       eor     "acc_hb", "acc_hb", "rk2"                           \n" // GHASH final-1 block - high

// cycle 18
"       eor     "acc_mb", "acc_mb", "rk4v".16b                      \n" // GHASH final-1 block - mid
"       eor     "output_h0", "output_h0", "rk14_h"                  \n" // AES final block - round 14 high
"6:                                                                 \n" // blocks left <= 1

// cycle 0
"       and     %[bit_length], %[bit_length], #127                  \n" // bit_length %= 128
"       mvn     "rk14_h", xzr                                       \n" // rk14_h = 0xffffffffffffffff

// cycle 1
"       sub     %[bit_length], %[bit_length], #128                  \n" // bit_length -= 128
"       mvn     "rk14_l", xzr                                       \n" // rk14_l = 0xffffffffffffffff

// cycle 2
"       ldp     "end_input_ptr", "main_end_input_ptr", [%[output_ptr]]\n" // load existing bytes we need to not overwrite
"       neg     %[bit_length], %[bit_length]                        \n" // bit_length = 128 - #bits in input (in range [1,128])

// cycle 3
"       and     %[bit_length], %[bit_length], #127                  \n" // bit_length %= 128

// cycle 4
"       lsr     "rk14_h", "rk14_h", %[bit_length]                   \n" // rk14_h is mask for top 64b of last block
"       cmp     %[bit_length], #64                                  \n"

// cycle 5
"       csel    "ctr32x", "rk14_l", "rk14_h", lt                    \n"
"       csel    "ctr96_b64x", "rk14_h", xzr, lt                     \n"

// cycle 6
"       fmov    "ctr0d", "ctr32x"                                   \n" // ctr0b is mask for last block
"       and     "output_l0", "output_l0", "ctr32x"                  \n"

// cycle 7
"       mov     "ctr0".d[1], "ctr96_b64x"                           \n"
"       bic     "end_input_ptr", "end_input_ptr", "ctr32x"          \n" // mask out low existing bytes

// cycle 9
"       rev     "ctr32w", "rctr32w"                                 \n"

// cycle 10
"       bic     "main_end_input_ptr", "main_end_input_ptr", "ctr96_b64x"\n" // mask out high existing bytes

// cycle 11
"       orr     "output_l0", "output_l0", "end_input_ptr"           \n"

// cycle 12
"       and     "output_h0", "output_h0", "ctr96_b64x"              \n"

// cycle 13
"       orr     "output_h0", "output_h0", "main_end_input_ptr"      \n"

// cycle 15
"       and     "res1b", "res1b", "ctr0b"                           \n" // possibly partial last block has zeroes in highest bits

// cycle 18
"       rev64   "res0b", "res1b"                                    \n" // GHASH final block

// cycle 21
"       eor     "res0b", "res0b", "t0".16b                          \n" // feed in partial tag

// cycle 24
"       pmull   "rk3q1", "res0".1d, "h1".1d                         \n" // GHASH final block - low

// cycle 25
"       mov     "t0d", "res0".d[1]                                  \n" // GHASH final block - mid

// cycle 28
"       eor     "t0".8b, "t0".8b, "res0".8b                         \n" // GHASH final block - mid

// cycle 29
"       pmull2  "rk2q1", "res0".2d, "h1".2d                         \n" // GHASH final block - high

// cycle 31
"       pmull   "t0".1q, "t0".1d, "h12k".1d                         \n" // GHASH final block - mid

// cycle 32
"       eor     "acc_hb", "acc_hb", "rk2"                           \n" // GHASH final block - high

// cycle 33
"       eor     "acc_lb", "acc_lb", "rk3"                           \n" // GHASH final block - low

// cycle 34
"       eor     "acc_mb", "acc_mb", "t0".16b                        \n" // GHASH final block - mid
"       movi    "mod_constant".8b, #0xc2                            \n"

// cycle 36
"       eor     "t9".16b, "acc_lb", "acc_hb"                        \n" // MODULO - karatsuba tidy up

// cycle 37
"       shl     "mod_constantd", "mod_constantd", #56               \n" // mod_constant

// cycle 39
"       eor     "acc_mb", "acc_mb", "t9".16b                        \n" // MODULO - karatsuba tidy up

// cycle 40
"       pmull   "mod_t".1q, "acc_h".1d, "mod_constant".1d           \n" // MODULO - top 64b align with mid

// cycle 41
"       ext     "acc_hb", "acc_hb", "acc_hb", #8                    \n" // MODULO - other top alignment

// cycle 43
"       eor     "acc_mb", "acc_mb", "mod_t".16b                     \n" // MODULO - fold into mid

// cycle 46
"       eor     "acc_mb", "acc_mb", "acc_hb"                        \n" // MODULO - fold into mid

// cycle 49
"       pmull   "mod_constant".1q, "acc_m".1d, "mod_constant".1d    \n" // MODULO - mid 64b align with low

// cycle 51
"       ext     "acc_mb", "acc_mb", "acc_mb", #8                    \n" // MODULO - other mid alignment

// cycle 52
"       eor     "acc_lb", "acc_lb", "mod_constant".16b              \n" // MODULO - fold into low

// cycle 53
"       stp     "output_l0", "output_h0", [%[output_ptr]]           \n"

// cycle 54
"       str     "ctr32w", [%[counter], #12]                         \n" // store the updated counter

// cycle 55
"       eor     "acc_lb", "acc_lb", "acc_mb"                        \n" // MODULO - fold into low
"       st1     { "acc_l".16b }, [%[current_tag]]                   \n"

        : [input_ptr] "+r" (in_ptr), [output_ptr] "+r" (out_ptr),
          [bit_length] "+r" (bit_length)
        : [cc] "r" (cs->constants->expanded_aes_keys[0].b), [counter] "r" (cs->counter.b), [current_tag] "r" (cs->current_tag.b)
        : "v0",  "v1",  "v2",  "v3",  "v4",  "v5",  "v6",  "v7",  "v8",  "v9",  "v10", "v11", "v12", "v13", "v14", "v15",
          "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",
          "x4",  "x5",  "x6",  "x7",  "x9",  "x10", "x11", "x12", "x13", "x14", "x15", "x19", "x20", "x21", "x22", "x23",
          "x24"
    );

    return SUCCESSFUL_OPERATION;
}

#undef end_input_ptr
#undef main_end_input_ptr
#undef output_l0
#undef output_h0
#undef output_l1
#undef output_h1
#undef output_l2
#undef output_h2
#undef output_l3
#undef output_h3

#undef ctr32w
#undef ctr32x
#undef ctr96_b64x
#undef ctr96_t32w
#undef ctr96_t32x
#undef rctr32w
#undef rctr32x

#undef rk14_l
#undef rk14_h

#undef ctr0b
#undef ctr0
#undef ctr0d
#undef ctr1b
#undef ctr1
#undef ctr1d
#undef ctr2b
#undef ctr2
#undef ctr2d
#undef ctr3b
#undef ctr3
#undef ctr3d

#undef res0b
#undef res0
#undef res0d
#undef res0q
#undef res1b
#undef res1
#undef res1d
#undef res1q
#undef res2b
#undef res2
#undef res2d
#undef res2q
#undef res3b
#undef res3
#undef res3d
#undef res3q

#undef acc_hb
#undef acc_h
#undef acc_hd
#undef acc_mb
#undef acc_m
#undef acc_md
#undef acc_lb
#undef acc_l
#undef acc_ld

#undef h1
#undef h1q
#undef h2
#undef h2q
#undef h3
#undef h3q
#undef h4
#undef h4q
#undef h12k
#undef h34k

#undef t0
#undef t0d

#undef t1
#undef t1d
#undef t2
#undef t2d
#undef t3
#undef t3d

#undef t4
#undef t4d
#undef t5
#undef t5d
#undef t6
#undef t6d

#undef t7
#undef t7d
#undef t8
#undef t8d
#undef t9
#undef t9d

#undef ctr_t0
#undef ctr_t0b
#undef ctr_t1
#undef ctr_t1b
#undef ctr_t2
#undef ctr_t2b
#undef ctr_t3
#undef ctr_t3b

#undef mod_constantd
#undef mod_constant
#undef mod_t

#undef rk0
#undef rk0q
#undef rk1
#undef rk1q
#undef rk2
#undef rk2q
#undef rk2q1
#undef rk3
#undef rk3q
#undef rk3q1
#undef rk4
#undef rk4q
#undef rk4v
#undef rk4d
#undef rk5
#undef rk5q
#undef rk6
#undef rk6q
#undef rk7
#undef rk7q
#undef rk8
#undef rk8q
#undef rk9
#undef rk9q
#undef rk10
#undef rk10q
#undef rk11
#undef rk11q
#undef rk12
#undef rk12q
#undef rk13
#undef rk13q
