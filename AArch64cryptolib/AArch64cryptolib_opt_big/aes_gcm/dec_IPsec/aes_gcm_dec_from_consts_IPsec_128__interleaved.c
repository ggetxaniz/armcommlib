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
    AESE    ctr_curr, k9
    LDR     res_curr, [ input_ptr ], #16
    EOR     res_curr, res_curr, ctr_curr
    MOV     output_low, res_curr.d[0]
    MOV     output_high, res_curr.d[1]
    EOR     output_low, k10_low
    EOR     output_high, k10_high
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
x1  ciphertext_length  //expected - will use asm input
x2  cs_ptr      //expected - will use asm input
x3  output_ptr  //expected - will use asm input
x4  end_input_ptr       // start_input_ptr + (ciphertext_length >> 3)
x5  main_end_input_ptr  // start_input_ptr + 16 + (ciphertext_length >> (3 (bits->bytes)+4 (bytes->blocks)+2 (multiple of 4 blocks)))
x6  output_l
x7  output_h
x9  ctr32
x10 constctr96_top32
x11 constctr96_bot64
x12 rev_ctr32
x13 rk10_l
x14 rk10_h


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
v28-v31 various temps
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

#define rk10_l      "x13"
#define rk10_h      "x14"
#define tmp_checksum    "x15"

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

#define t1          "v28" //temp 2nd hash high (4, 8 free)
#define t1d         "d28"
#define t2          "v29" //temp 2nd hash low
#define t2d         "d29"
#define t3          "v30" //temp 2nd hash mid
#define t3d         "d30"

#define t4          "v8" //temp 3rd hash high (4, 5, 8 free)
#define t4d         "d8"
#define t5          "v28" //temp 3rd hash low
#define t5d         "d28"
#define t6          "v31" //temp 3rd hash mid
#define t6d         "d31"

#define t7          "v4" //temp 4th hash high  (4, 5, 6, 8 free)
#define t7d         "d4"
#define t8          "v29" //temp 4th hash low
#define t8d         "d29"
#define t9          "v30" //temp 4th hash mid
#define t9d         "d30"

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
#define mod_t         "v31"

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

// Given a set up cipher_constants_t and the IPsec salt and ESPIV, perform decryption in place and checksum on the produced plaintext
static operation_result_t decrypt_from_constants_IPsec_128(
    //Inputs
    const armv8_cipher_constants_t * cc,
    uint32_t salt,
    uint64_t ESPIV,
    const uint8_t * restrict aad, uint64_t aad_byte_length,
        //aad zero padded to 16B, aad_byte_length 8 or 12
    uint8_t * ciphertext,   uint64_t ciphertext_byte_length,
        //in place operation - ciphertext becomes plaintext
        //assumed that plaintext can be written in 8B blocks - will write up to 7B of 0s beyond the end of the ciphertext
    const uint8_t * tag,
        //tag_byte_length specified in cipher_constants
        //tag is read before last block of plaintext is written and written back afterwards, so tag will be preserved
        //supported tag sizes are 8B, 12B and 16B, and precisely the size specified will be read/written
    //Output
    uint64_t * checksum
        //one's complement sum of all 64b words in the plaintext
    )
{
    uint64_t result;
    uint8_t * in_ptr = ciphertext;
    uint8_t * out_ptr = ciphertext;
    const uint8_t tag_mask[32] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t tag_copy[16];
    //setup zero padded tag
    if(cc->tag_byte_length == 16) {
        memcpy(tag_copy, tag, 16);
    } else if (cc->tag_byte_length == 8) {
        memcpy(tag_copy, tag, 8);
    } else if (cc->tag_byte_length == 12) {
        memcpy(tag_copy, tag, 12);
    } else {
        memcpy(tag_copy, tag, cc->tag_byte_length);
    }

    // moved this to separate inline asm as ran out of inputs
    __asm __volatile
    (
        //// Set initial ctr ////
        "       mov     "rctr32w", #1                                       \n"
        "       lsr     "ctr96_t32x", %[ESPIV], #32                         \n"
        "       orr     "ctr96_b64x", %[salt], %[ESPIV], lsl #32            \n"
        :
        : [salt] "r" ((uint64_t) salt), [ESPIV] "r" (ESPIV)
        : rctr32w, ctr96_t32x, ctr96_b64x
    );

    __asm __volatile
    (
// aes_gcm_dec_from_consts_IPsec_128__cycles_72_big3_0_0_ipb_50_st_720_2019_01_24_025503.c
// cycle 0
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR final+1 block
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR final+1 block
"       ldr     "rk0q", [%[cc], #0]                                 \n" // load rk0

// cycle 1
"       ldr     "rk1q", [%[cc], #16]                                \n" // load rk1
"       mov     "tmp_checksum", #0                                  \n" // zero out tmp_checksum
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR final+1 block

// cycle 2
"       lsr     "main_end_input_ptr", %[ciphertext_length], #3      \n" // byte_len
"       fmov    "res0d", "ctr96_b64x"                               \n" // CTR final+1 block

// cycle 3
"       sub     "main_end_input_ptr", "main_end_input_ptr", #1      \n" // byte_len - 1
"       fmov    "res0".d[1], "ctr32x"                               \n" // CTR final+1 block
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 0

// cycle 4
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 0
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 0
"       fmov    "ctr0d", "ctr96_b64x"                               \n" // CTR block 0

// cycle 5
"       fmov    "ctr0".d[1], "ctr32x"                               \n" // CTR block 0
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 1

// cycle 6
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 1
"       fmov    "ctr1d", "ctr96_b64x"                               \n" // CTR block 1
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 1

// cycle 7
"       fmov    "ctr1".d[1], "ctr32x"                               \n" // CTR block 1
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 2
"       and     "main_end_input_ptr", "main_end_input_ptr", #0xffffffffffffffc0\n" // number of bytes to be processed in main loop (at least 1 byte must be handled by tail)

// cycle 8
"       aese    "res0b", "rk0"   \n  aesmc   "res0b", "res0b"       \n" // AES block final+1 - round 0
"       ld1     { "res1b" }, [%[aad_ptr]]                           \n" // GHASH aad block

// cycle 9
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 2
"       ld1     { "res2b" }, [%[aad_mask]]                          \n" // GHASH aad block
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 2

// cycle 10
"       aese    "ctr0b", "rk0"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 0
"       fmov    "ctr2d", "ctr96_b64x"                               \n" // CTR block 2

// cycle 11
"       add     "end_input_ptr", %[input_ptr], %[ciphertext_length], LSR #3\n" // end_input_ptr
"       fmov    "ctr2".d[1], "ctr32x"                               \n" // CTR block 2
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 3

// cycle 12
"       aese    "res0b", "rk1"   \n  aesmc   "res0b", "res0b"       \n" // AES block final+1 - round 1
"       ldr     "h1q", [%[cc], #240]                                \n" // load h1l | h1h

// cycle 13
"       ldr     "rk2q", [%[cc], #32]                                \n" // load rk2
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 3

// cycle 14
"       fmov    "ctr3d", "ctr96_b64x"                               \n" // CTR block 3
"       and     "res1b", "res1b", "res2".16b                        \n" // GHASH aad block

// cycle 15
"       aese    "ctr1b", "rk0"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 0
"       ldr     "h2q", [%[cc], #256]                                \n" // load h2l | h2h

// cycle 16
"       aese    "ctr2b", "rk0"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 0
"       fmov    "ctr3".d[1], "ctr32x"                               \n" // CTR block 3

// cycle 17
"       rev64   "res1b", "res1b"                                    \n" // GHASH aad block
"       ldr     "rk3q", [%[cc], #48]                                \n" // load rk3

// cycle 18
"       aese    "res0b", "rk2"   \n  aesmc   "res0b", "res0b"       \n" // AES block final+1 - round 2
"       ldr     "rk4q", [%[cc], #64]                                \n" // load rk4

// cycle 19
"       ldr     "h4q", [%[cc], #288]                                \n" // load h4l | h4h
"       trn1    "t0".2d,    "h1".2d,    "h2".2d                     \n" // h2h | h1h

// cycle 20
"       ldr     "h3q", [%[cc], #272]                                \n" // load h3l | h3h
"       trn2    "h12k".2d,  "h1".2d,    "h2".2d                     \n" // h2l | h1l

// cycle 21
"       aese    "ctr3b", "rk0"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 0
"       mov     "acc_md", "res1".d[1]                               \n" // GHASH aad block

// cycle 22
"       aese    "ctr1b", "rk1"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 1

// cycle 23
"       ldr     "rk7q", [%[cc], #112]                               \n" // load rk7
"       eor     "h12k".16b, "h12k".16b, "t0".16b                    \n" // h2k | h1k

// cycle 24
"       trn1    "acc_h".2d, "h3".2d,    "h4".2d                     \n" // h4h | h3h
"       ldr     "rk5q", [%[cc], #80]                                \n" // load rk5
"       trn2    "h34k".2d,  "h3".2d,    "h4".2d                     \n" // h4l | h3l

// cycle 25
"       aese    "ctr1b", "rk2"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 2
"       ldr     "rk6q", [%[cc], #96]                                \n" // load rk6

// cycle 26
"       aese    "ctr0b", "rk1"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 1
"       eor     "acc_m".8b, "acc_m".8b, "res1".8b                   \n" // GHASH aad block

// cycle 27
"       aese    "ctr3b", "rk1"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 1

// cycle 28
"       aese    "ctr1b", "rk3"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 3
"       eor     "h34k".16b, "h34k".16b, "acc_h".16b                 \n" // h4k | h3k

// cycle 29
"       aese    "ctr0b", "rk2"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 2

// cycle 30
"       pmull2  "acc_h".1q, "res1".2d, "h1".2d                      \n" // GHASH aad block
"       movi    "mod_constant".8b, #0xc2                            \n"

// cycle 31
"       aese    "ctr1b", "rk4"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 4

// cycle 32
"       aese    "ctr0b", "rk3"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 3

// cycle 33
"       pmull   "acc_m".1q, "acc_m".1d, "h12k".1d                   \n" // GHASH aad block
"       shl     "mod_constantd", "mod_constantd", #56               \n" // mod_constant

// cycle 34
"       aese    "ctr1b", "rk5"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 5
"       ldr     "rk8q", [%[cc], #128]                               \n" // load rk8

// cycle 35
"       aese    "ctr0b", "rk4"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 4

// cycle 36
"       pmull   "res2".1q, "acc_h".1d, "mod_constant".1d            \n" // GHASH aad block

// cycle 37
"       aese    "ctr1b", "rk6"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 6

// cycle 38
"       aese    "ctr0b", "rk5"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 5
"       eor     "acc_mb", "acc_mb", "acc_hb"                        \n" // GHASH aad block

// cycle 39
"       pmull   "acc_l".1q, "res1".1d, "h1".1d                      \n" // GHASH aad block

// cycle 40
"       aese    "ctr3b", "rk2"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 2

// cycle 41
"       aese    "res0b", "rk3"   \n  aesmc   "res0b", "res0b"       \n" // AES block final+1 - round 3

// cycle 42
"       aese    "ctr2b", "rk1"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 1
"       eor     "acc_mb", "acc_mb", "acc_lb"                        \n" // GHASH aad block

// cycle 43
"       aese    "ctr0b", "rk6"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 6

// cycle 44
"       aese    "res0b", "rk4"   \n  aesmc   "res0b", "res0b"       \n" // AES block final+1 - round 4
"       ext     "acc_hb", "acc_hb", "acc_hb", #8                    \n" // GHASH aad block

// cycle 45
"       aese    "ctr2b", "rk2"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 2
"       ldr     "rk9q", [%[cc], #144]                               \n" // load rk9

// cycle 46
"       aese    "ctr3b", "rk3"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 3

// cycle 47
"       aese    "res0b", "rk5"   \n  aesmc   "res0b", "res0b"       \n" // AES block final+1 - round 5
"       eor     "acc_mb", "acc_mb", "res2".16b                      \n" // GHASH aad block

// cycle 48
"       aese    "ctr2b", "rk3"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 3
"       ldp     "rk10_l", "rk10_h", [%[cc], #160]                   \n" // load rk10

// cycle 49
"       aese    "ctr3b", "rk4"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 4

// cycle 50
"       aese    "res0b", "rk6"   \n  aesmc   "res0b", "res0b"       \n" // AES block final+1 - round 6

// cycle 51
"       aese    "ctr2b", "rk4"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 4

// cycle 52
"       aese    "ctr3b", "rk5"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 5

// cycle 53
"       aese    "res0b", "rk7"   \n  aesmc   "res0b", "res0b"       \n" // AES block final+1 - round 7

// cycle 54
"       aese    "ctr2b", "rk5"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 5

// cycle 55
"       aese    "ctr3b", "rk6"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 6
"       eor     "acc_mb", "acc_mb", "acc_hb"                        \n" // GHASH aad block

// cycle 56
"       aese    "res0b", "rk8"   \n  aesmc   "res0b", "res0b"       \n" // AES block final+1 - round 8

// cycle 57
"       aese    "ctr2b", "rk6"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 6

// cycle 58
"       aese    "ctr1b", "rk7"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 7

// cycle 59
"       pmull   "res2".1q, "acc_m".1d, "mod_constant".1d            \n" // GHASH aad block

// cycle 60
"       aese    "ctr0b", "rk7"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 7

// cycle 61
"       aese    "res0b", "rk9"                                      \n" // AES block final+1 - round 9
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 3

// cycle 62
"       aese    "ctr1b", "rk8"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 1 - round 8
"       ext     "acc_mb", "acc_mb", "acc_mb", #8                    \n" // GHASH aad block

// cycle 63
"       aese    "ctr2b", "rk7"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 7

// cycle 64
"       aese    "ctr3b", "rk7"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 7
"       mov     "output_l0", "res0".d[0]                            \n" // AES final+1 block - mov low

// cycle 65
"       aese    "ctr1b", "rk9"                                      \n" // AES block 1 - round 9
"       mov     "output_h0", "res0".d[1]                            \n" // AES final+1 block - mov high
"       eor     "acc_lb", "acc_lb", "res2".16b                      \n" // GHASH aad block

// cycle 66
"       aese    "ctr0b", "rk8"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 0 - round 8

// cycle 67
"       aese    "ctr3b", "rk8"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 3 - round 8

// cycle 68
"       aese    "ctr2b", "rk8"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 2 - round 8
"       add     "main_end_input_ptr", "main_end_input_ptr", %[input_ptr]\n"

// cycle 69
"       aese    "ctr0b", "rk9"                                      \n" // AES block 0 - round 9
"       eor     "output_l0", "output_l0", "rk10_l"                  \n" // AES final+1 block - round 10 low
"       eor     "acc_lb", "acc_lb", "acc_mb"                        \n" // GHASH aad block

// cycle 70
"       aese    "ctr3b", "rk9"                                      \n" // AES block 3 - round 9
"       eor     "output_h0", "output_h0", "rk10_h"                  \n" // AES final+1 block - round 10 high
"       stp     "output_l0", "output_h0", [sp, #-16]!               \n" // AES final+1 block - round 10 high

// cycle 71
"       aese    "ctr2b", "rk9"                                      \n" // AES block 2 - round 9
"       cmp     %[input_ptr], "main_end_input_ptr"                  \n" // check if we have <= 4 blocks
"       cbz     %[ciphertext_length], 8f                            \n"

// cycle 0
"       b.ge    2f                                                  \n" // handle tail
"       ldr     "res0q", [%[input_ptr], #0]                         \n" // AES block 0 - load ciphertext

// cycle 1
"       ldr     "res1q", [%[input_ptr], #16]                        \n" // AES block 1 - load ciphertext

// cycle 4
"       eor     "ctr0b", "res0b", "ctr0b"                           \n" // AES block 0 - result
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 4

// cycle 5
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 4

// cycle 6
"       ldr     "res3q", [%[input_ptr], #48]                        \n" // AES block 3 - load ciphertext
"       rev64   "res0b", "res0b"                                    \n" // GHASH block 0

// cycle 7
"       mov     "output_h0", "ctr0".d[1]                            \n" // AES block 0 - mov high
"       eor     "ctr1b", "res1b", "ctr1b"                           \n" // AES block 1 - result

// cycle 8
"       mov     "output_l0", "ctr0".d[0]                            \n" // AES block 0 - mov low

// cycle 9
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 4
"       fmov    "ctr0d", "ctr96_b64x"                               \n" // CTR block 4

// cycle 10
"       fmov    "ctr0".d[1], "ctr32x"                               \n" // CTR block 4
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 5

// cycle 11
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 5
"       mov     "output_l1", "ctr1".d[0]                            \n" // AES block 1 - mov low
"       rev64   "res1b", "res1b"                                    \n" // GHASH block 1

// cycle 12
"       eor     "output_h0", "output_h0", "rk10_h"                  \n" // AES block 0 - round 10 high
"       mov     "output_h1", "ctr1".d[1]                            \n" // AES block 1 - mov high
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 5

// cycle 13
"       eor     "output_l0", "output_l0", "rk10_l"                  \n" // AES block 0 - round 10 low
"       stp     "output_l0", "output_h0", [%[output_ptr]], #16      \n" // AES block 0 - store result
"       fmov    "ctr1d", "ctr96_b64x"                               \n" // CTR block 5

// cycle 14
"       ldr     "res2q", [%[input_ptr], #32]                        \n" // AES block 2 - load ciphertext
"       add     %[input_ptr], %[input_ptr], #64                     \n" // AES input_ptr update
"       cmp     %[input_ptr], "main_end_input_ptr"                  \n" // check if we have <= 8 blocks

// cycle 15
"       fmov    "ctr1".d[1], "ctr32x"                               \n" // CTR block 5
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 6

// cycle 16
"       eor     "output_l1", "output_l1", "rk10_l"                  \n" // AES block 1 - round 10 low
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 6

// cycle 17
"       eor     "output_h1", "output_h1", "rk10_h"                  \n" // AES block 1 - round 10 high
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 6

// cycle 18
"       eor     "ctr2b", "res2b", "ctr2b"                           \n" // AES block 2 - result
"       stp     "output_l1", "output_h1", [%[output_ptr]], #16      \n" // AES block 1 - store result
"       b.ge    7f                                                  \n" // do prepretail

// cycle 0
"1:                                                                 \n" // main loop start
"       aese    "ctr1b", "rk0"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 0
"       eor     "ctr3b", "res3b", "ctr3b"                           \n" // AES block 4k+3 - result

// cycle 1
"       ext     "acc_lb", "acc_lb", "acc_lb", #8                    \n" // PRE 0
"       adds    "tmp_checksum", "tmp_checksum", "output_l0"         \n" // checksum calc 4k low

// cycle 2
"       pmull2  "t1".1q, "res1".2d, "h3".2d                         \n" // GHASH block 4k+1 - high
"       mov     "output_h2", "ctr2".d[1]                            \n" // AES block 4k+2 - mov high

// cycle 3
"       rev64   "res3b", "res3b"                                    \n" // GHASH block 4k+3
"       mov     "output_l2", "ctr2".d[0]                            \n" // AES block 4k+2 - mov low
"       mov     "acc_md", "h34k".d[1]                               \n" // GHASH block 4k - mid

// cycle 4
"       mov     "output_l3", "ctr3".d[0]                            \n" // AES block 4k+3 - mov low
"       eor     "res0b", "res0b", "acc_lb"                          \n" // PRE 1
"       rev64   "res2b", "res2b"                                    \n" // GHASH block 4k+2

// cycle 5
"       aese    "ctr0b", "rk0"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 0
"       fmov    "ctr2d", "ctr96_b64x"                               \n" // CTR block 4k+6

// cycle 6
"       aese    "ctr1b", "rk1"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 1

// cycle 7
"       pmull2  "acc_h".1q, "res0".2d, "h4".2d                      \n" // GHASH block 4k - high
"       mov     "t3d", "res1".d[1]                                  \n" // GHASH block 4k+1 - mid
"       fmov    "ctr2".d[1], "ctr32x"                               \n" // CTR block 4k+6

// cycle 8
"       aese    "ctr0b", "rk1"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 1
"       mov     "t0d", "res0".d[1]                                  \n" // GHASH block 4k - mid

// cycle 9
"       aese    "ctr1b", "rk2"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 2

// cycle 10
"       pmull   "t2".1q, "res1".1d, "h3".1d                         \n" // GHASH block 4k+1 - low
"       eor     "t3".8b, "t3".8b, "res1".8b                         \n" // GHASH block 4k+1 - mid
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 4k+7

// cycle 11
"       aese    "ctr0b", "rk2"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 2

// cycle 12
"       aese    "ctr2b", "rk0"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 0
"       eor     "t0".8b, "t0".8b, "res0".8b                         \n" // GHASH block 4k - mid

// cycle 13
"       pmull   "acc_l".1q, "res0".1d, "h4".1d                      \n" // GHASH block 4k - low
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 4k+7
"       mov     "output_h3", "ctr3".d[1]                            \n" // AES block 4k+3 - mov high

// cycle 14
"       pmull2  "t7".1q, "res3".2d, "h1".2d                         \n" // GHASH block 4k+3 - high
"       mov     "t6d", "res2".d[1]                                  \n" // GHASH block 4k+2 - mid
"       fmov    "ctr3d", "ctr96_b64x"                               \n" // CTR block 4k+7

// cycle 15
"       aese    "ctr0b", "rk3"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 3
"       fmov    "ctr3".d[1], "ctr32x"                               \n" // CTR block 4k+7

// cycle 16
"       eor     "acc_hb", "acc_hb", "t1".16b                        \n" // GHASH block 4k+1 - high

// cycle 17
"       pmull   "acc_m".1q, "t0".1d, "acc_m".1d                     \n" // GHASH block 4k - mid

// cycle 18
"       aese    "ctr2b", "rk1"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 1
"       eor     "t6".8b, "t6".8b, "res2".8b                         \n" // GHASH block 4k+2 - mid

// cycle 19
"       pmull   "t5".1q, "res2".1d, "h2".1d                         \n" // GHASH block 4k+2 - low

// cycle 20
"       aese    "ctr3b", "rk0"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 0
"       eor     "acc_lb", "acc_lb", "t2".16b                        \n" // GHASH block 4k+1 - low

// cycle 21
"       pmull   "t3".1q, "t3".1d, "h34k".1d                         \n" // GHASH block 4k+1 - mid

// cycle 22
"       pmull   "t8".1q, "res3".1d, "h1".1d                         \n" // GHASH block 4k+3 - low
"       ins     "t6".d[1], "t6".d[0]                                \n" // GHASH block 4k+2 - mid

// cycle 23
"       pmull2  "t4".1q, "res2".2d, "h2".2d                         \n" // GHASH block 4k+2 - high
"       eor     "output_h3", "output_h3", "rk10_h"                  \n" // AES block 4k+3 - round 10 high
"       ldr     "res2q", [%[input_ptr], #32]                        \n" // AES block 4k+6 - load ciphertext

// cycle 24
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 4k+7
"       eor     "acc_mb", "acc_mb", "t3".16b                        \n" // GHASH block 4k+1 - mid
"       mov     "t9d", "res3".d[1]                                  \n" // GHASH block 4k+3 - mid

// cycle 25
"       aese    "ctr1b", "rk3"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 3
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 4k+8

// cycle 26
"       aese    "ctr3b", "rk1"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 1
"       eor     "acc_hb", "acc_hb", "t4".16b                        \n" // GHASH block 4k+2 - high

// cycle 27
"       aese    "ctr0b", "rk4"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 4
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 4k+8

// cycle 28
"       aese    "ctr2b", "rk2"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 2
"       eor     "t9".8b, "t9".8b, "res3".8b                         \n" // GHASH block 4k+3 - mid

// cycle 29
"       aese    "ctr3b", "rk2"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 2
"       ldr     "res1q", [%[input_ptr], #16]                        \n" // AES block 4k+5 - load ciphertext

// cycle 30
"       aese    "ctr1b", "rk4"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 4
"       eor     "acc_lb", "acc_lb", "t5".16b                        \n" // GHASH block 4k+2 - low

// cycle 31
"       pmull2  "t6".1q, "t6".2d, "h12k".2d                         \n" // GHASH block 4k+2 - mid
"       ldr     "res3q", [%[input_ptr], #48]                        \n" // AES block 4k+7 - load ciphertext
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 4k+8

// cycle 32
"       pmull   "t9".1q, "t9".1d, "h12k".1d                         \n" // GHASH block 4k+3 - mid
"       eor     "acc_hb", "acc_hb", "t7".16b                        \n" // GHASH block 4k+3 - high

// cycle 33
"       aese    "ctr1b", "rk5"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 5

// cycle 34
"       aese    "ctr2b", "rk3"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 3
"       movi    "mod_constant".8b, #0xc2                            \n"

// cycle 35
"       eor     "acc_mb", "acc_mb", "t6".16b                        \n" // GHASH block 4k+2 - mid
"       eor     "acc_lb", "acc_lb", "t8".16b                        \n" // GHASH block 4k+3 - low

// cycle 36
"       aese    "ctr1b", "rk6"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 6

// cycle 37
"       aese    "ctr2b", "rk4"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 4
"       shl     "mod_constantd", "mod_constantd", #56               \n" // mod_constant

// cycle 38
"       aese    "ctr0b", "rk5"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 5
"       eor     "acc_mb", "acc_mb", "t9".16b                        \n" // GHASH block 4k+3 - mid

// cycle 39
"       aese    "ctr1b", "rk7"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 7

// cycle 40
"       aese    "ctr2b", "rk5"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 5
"       eor     "t9".16b, "acc_lb", "acc_hb"                        \n" // MODULO - karatsuba tidy up

// cycle 41
"       aese    "ctr0b", "rk6"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 6
"       eor     "output_h2", "output_h2", "rk10_h"                  \n" // AES block 4k+2 - round 10 high

// cycle 42
"       aese    "ctr3b", "rk3"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 3
"       eor     "output_l2", "output_l2", "rk10_l"                  \n" // AES block 4k+2 - round 10 low

// cycle 43
"       pmull   "mod_t".1q, "acc_h".1d, "mod_constant".1d           \n" // MODULO - top 64b align with mid
"       eor     "output_l3", "output_l3", "rk10_l"                  \n" // AES block 4k+3 - round 10 low
"       ldr     "res0q", [%[input_ptr], #0]                         \n" // AES block 4k+4 - load ciphertext

// cycle 44
"       aese    "ctr0b", "rk7"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 7
"       eor     "acc_mb", "acc_mb", "t9".16b                        \n" // MODULO - karatsuba tidy up

// cycle 45
"       aese    "ctr3b", "rk4"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 4
"       stp     "output_l2", "output_h2", [%[output_ptr]], #16      \n" // AES block 4k+2 - store result

// cycle 46
"       aese    "ctr1b", "rk8"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 8
"       ext     "acc_hb", "acc_hb", "acc_hb", #8                    \n" // MODULO - other top alignment

// cycle 47
"       aese    "ctr0b", "rk8"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 8

// cycle 48
"       aese    "ctr3b", "rk5"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 5
"       eor     "acc_mb", "acc_mb", "mod_t".16b                     \n" // MODULO - fold into mid

// cycle 49
"       aese    "ctr2b", "rk6"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 6
"       adcs    "tmp_checksum", "tmp_checksum", "output_h0"         \n" // checksum calc 4k high

// cycle 50
"       aese    "ctr0b", "rk9"                                      \n" // AES block 4k+4 - round 9
"       adcs    "tmp_checksum", "tmp_checksum", "output_l1"         \n" // checksum calc 4k+1 low
"       stp     "output_l3", "output_h3", [%[output_ptr]], #16      \n" // AES block 4k+3 - store result

// cycle 51
"       aese    "ctr3b", "rk6"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 6
"       adcs    "tmp_checksum", "tmp_checksum", "output_h1"         \n" // checksum calc 4k+1 high

// cycle 52
"       aese    "ctr2b", "rk7"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 7
"       eor     "acc_mb", "acc_mb", "acc_hb"                        \n" // MODULO - fold into mid

// cycle 53
"       aese    "ctr1b", "rk9"                                      \n" // AES block 4k+5 - round 9
"       adcs    "tmp_checksum", "tmp_checksum", "output_l2"         \n" // checksum calc 4k+2 low

// cycle 54
"       aese    "ctr3b", "rk7"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 7
"       eor     "ctr0b", "res0b", "ctr0b"                           \n" // AES block 4k+4 - result

// cycle 55
"       aese    "ctr2b", "rk8"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 8
"       adcs    "tmp_checksum", "tmp_checksum", "output_h2"         \n" // checksum calc 4k+2 high

// cycle 56
"       pmull   "mod_constant".1q, "acc_m".1d, "mod_constant".1d    \n" // MODULO - mid 64b align with low
"       eor     "ctr1b", "res1b", "ctr1b"                           \n" // AES block 4k+5 - result
"       adcs    "tmp_checksum", "tmp_checksum", "output_l3"         \n" // checksum calc 4k+3 low

// cycle 57
"       ext     "acc_mb", "acc_mb", "acc_mb", #8                    \n" // MODULO - other mid alignment
"       adcs    "tmp_checksum", "tmp_checksum", "output_h3"         \n" // checksum calc 4k+3 high

// cycle 58
"       aese    "ctr2b", "rk9"                                      \n" // AES block 4k+6 - round 9
"       mov     "output_h0", "ctr0".d[1]                            \n" // AES block 4k+4 - mov high
"       adc     "tmp_checksum", "tmp_checksum", xzr                 \n" // checksum calc 4k+3 final carry

// cycle 59
"       aese    "ctr3b", "rk8"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 8
"       mov     "output_l0", "ctr0".d[0]                            \n" // AES block 4k+4 - mov low

// cycle 60
"       fmov    "ctr0d", "ctr96_b64x"                               \n" // CTR block 4k+8
"       rev64   "res0b", "res0b"                                    \n" // GHASH block 4k+4
"       add     %[input_ptr], %[input_ptr], #64                     \n" // AES input_ptr update

// cycle 61
"       mov     "output_h1", "ctr1".d[1]                            \n" // AES block 4k+5 - mov high
"       eor     "ctr2b", "res2b", "ctr2b"                           \n" // AES block 4k+6 - result

// cycle 62
"       eor     "acc_lb", "acc_lb", "mod_constant".16b              \n" // MODULO - fold into low
"       fmov    "ctr0".d[1], "ctr32x"                               \n" // CTR block 4k+8
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 4k+9

// cycle 63
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 4k+9
"       mov     "output_l1", "ctr1".d[0]                            \n" // AES block 4k+5 - mov low
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 4k+9

// cycle 64
"       cmp     %[input_ptr], "main_end_input_ptr"                  \n" // LOOP CONTROL
"       fmov    "ctr1d", "ctr96_b64x"                               \n" // CTR block 4k+9
"       eor     "output_h0", "output_h0", "rk10_h"                  \n" // AES block 4k+4 - round 10 high

// cycle 65
"       aese    "ctr3b", "rk9"                                      \n" // AES block 4k+7 - round 9
"       fmov    "ctr1".d[1], "ctr32x"                               \n" // CTR block 4k+9
"       eor     "output_l0", "output_l0", "rk10_l"                  \n" // AES block 4k+4 - round 10 low

// cycle 66
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 4k+10
"       stp     "output_l0", "output_h0", [%[output_ptr]], #16      \n" // AES block 4k+4 - store result

// cycle 67
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 4k+10
"       eor     "acc_lb", "acc_lb", "acc_mb"                        \n" // MODULO - fold into low
"       eor     "output_h1", "output_h1", "rk10_h"                  \n" // AES block 4k+5 - round 10 high

// cycle 68
"       rev64   "res1b", "res1b"                                    \n" // GHASH block 4k+5
"       eor     "output_l1", "output_l1", "rk10_l"                  \n" // AES block 4k+5 - round 10 low
"       stp     "output_l1", "output_h1", [%[output_ptr]], #16      \n" // AES block 4k+5 - store result

// cycle 69
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 4k+10
"       b.lt    1b                                                  \n"

// cycle 0
"7:                                                                 \n" // PREPRETAIL
"       ext     "acc_lb", "acc_lb", "acc_lb", #8                    \n" // PRE 0
"       mov     "t3d", "res1".d[1]                                  \n" // GHASH block 4k+1 - mid
"       mov     "output_h2", "ctr2".d[1]                            \n" // AES block 4k+2 - mov high

// cycle 1
"       mov     "output_l2", "ctr2".d[0]                            \n" // AES block 4k+2 - mov low
"       rev64   "res2b", "res2b"                                    \n" // GHASH block 4k+2
"       eor     "ctr3b", "res3b", "ctr3b"                           \n" // AES block 4k+3 - result

// cycle 2
"       aese    "ctr0b", "rk0"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 0
"       fmov    "ctr2d", "ctr96_b64x"                               \n" // CTR block 4k+6

// cycle 3
"       fmov    "ctr2".d[1], "ctr32x"                               \n" // CTR block 4k+6
"       eor     "res0b", "res0b", "acc_lb"                          \n" // PRE 1
"       eor     "t3".8b, "t3".8b, "res1".8b                         \n" // GHASH block 4k+1 - mid

// cycle 4
"       pmull   "t2".1q, "res1".1d, "h3".1d                         \n" // GHASH block 4k+1 - low
"       mov     "output_h3", "ctr3".d[1]                            \n" // AES block 4k+3 - mov high
"       rev     "ctr32w", "rctr32w"                                 \n" // CTR block 4k+7

// cycle 5
"       pmull2  "t1".1q, "res1".2d, "h3".2d                         \n" // GHASH block 4k+1 - high
"       orr     "ctr32x", "ctr96_t32x", "ctr32x", lsl #32           \n" // CTR block 4k+7
"       mov     "output_l3", "ctr3".d[0]                            \n" // AES block 4k+3 - mov low

// cycle 6
"       pmull   "acc_l".1q, "res0".1d, "h4".1d                      \n" // GHASH block 4k - low
"       fmov    "ctr3d", "ctr96_b64x"                               \n" // CTR block 4k+7
"       rev64   "res3b", "res3b"                                    \n" // GHASH block 4k+3

// cycle 7
"       pmull   "t3".1q, "t3".1d, "h34k".1d                         \n" // GHASH block 4k+1 - mid
"       fmov    "ctr3".d[1], "ctr32x"                               \n" // CTR block 4k+7

// cycle 8
"       aese    "ctr2b", "rk0"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 0
"       mov     "t0d", "res0".d[1]                                  \n" // GHASH block 4k - mid

// cycle 9
"       aese    "ctr1b", "rk0"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 0
"       eor     "acc_lb", "acc_lb", "t2".16b                        \n" // GHASH block 4k+1 - low

// cycle 10
"       pmull   "t8".1q, "res3".1d, "h1".1d                         \n" // GHASH block 4k+3 - low

// cycle 11
"       mov     "acc_md", "h34k".d[1]                               \n" // GHASH block 4k - mid
"       eor     "t0".8b, "t0".8b, "res0".8b                         \n" // GHASH block 4k - mid

// cycle 12
"       aese    "ctr3b", "rk0"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 0
"       mov     "t6d", "res2".d[1]                                  \n" // GHASH block 4k+2 - mid

// cycle 13
"       pmull2  "acc_h".1q, "res0".2d, "h4".2d                      \n" // GHASH block 4k - high

// cycle 14
"       pmull   "acc_m".1q, "t0".1d, "acc_m".1d                     \n" // GHASH block 4k - mid

// cycle 15
"       eor     "t6".8b, "t6".8b, "res2".8b                         \n" // GHASH block 4k+2 - mid

// cycle 16
"       aese    "ctr0b", "rk1"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 1
"       eor     "acc_hb", "acc_hb", "t1".16b                        \n" // GHASH block 4k+1 - high

// cycle 17
"       pmull2  "t4".1q, "res2".2d, "h2".2d                         \n" // GHASH block 4k+2 - high
"       eor     "output_l3", "output_l3", "rk10_l"                  \n" // AES block 4k+3 - round 10 low

// cycle 18
"       aese    "ctr2b", "rk1"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 1
"       ins     "t6".d[1], "t6".d[0]                                \n" // GHASH block 4k+2 - mid

// cycle 19
"       eor     "output_h3", "output_h3", "rk10_h"                  \n" // AES block 4k+3 - round 10 high
"       eor     "output_l2", "output_l2", "rk10_l"                  \n" // AES block 4k+2 - round 10 low

// cycle 20
"       pmull2  "t7".1q, "res3".2d, "h1".2d                         \n" // GHASH block 4k+3 - high
"       adds    "tmp_checksum", "tmp_checksum", "output_l0"         \n" // checksum calc 4k low
"       eor     "acc_mb", "acc_mb", "t3".16b                        \n" // GHASH block 4k+1 - mid

// cycle 21
"       eor     "output_h2", "output_h2", "rk10_h"                  \n" // AES block 4k+2 - round 10 high
"       mov     "t9d", "res3".d[1]                                  \n" // GHASH block 4k+3 - mid

// cycle 22
"       eor     "acc_hb", "acc_hb", "t4".16b                        \n" // GHASH block 4k+2 - high

// cycle 23
"       pmull   "t5".1q, "res2".1d, "h2".1d                         \n" // GHASH block 4k+2 - low

// cycle 24
"       pmull2  "t6".1q, "t6".2d, "h12k".2d                         \n" // GHASH block 4k+2 - mid
"       eor     "t9".8b, "t9".8b, "res3".8b                         \n" // GHASH block 4k+3 - mid

// cycle 25
"       aese    "ctr3b", "rk1"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 1

// cycle 26
"       aese    "ctr1b", "rk1"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 1
"       eor     "acc_lb", "acc_lb", "t5".16b                        \n" // GHASH block 4k+2 - low

// cycle 27
"       aese    "ctr2b", "rk2"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 2

// cycle 28
"       aese    "ctr0b", "rk2"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 2
"       movi    "mod_constant".8b, #0xc2                            \n"

// cycle 29
"       aese    "ctr1b", "rk2"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 2
"       eor     "acc_hb", "acc_hb", "t7".16b                        \n" // GHASH block 4k+3 - high

// cycle 30
"       pmull   "t9".1q, "t9".1d, "h12k".1d                         \n" // GHASH block 4k+3 - mid
"       adcs    "tmp_checksum", "tmp_checksum", "output_h0"         \n" // checksum calc 4k high
"       stp     "output_l2", "output_h2", [%[output_ptr]], #16      \n" // AES block 4k+2 - store result

// cycle 31
"       aese    "ctr2b", "rk3"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 3
"       shl     "mod_constantd", "mod_constantd", #56               \n" // mod_constant

// cycle 32
"       aese    "ctr1b", "rk3"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 3
"       eor     "acc_mb", "acc_mb", "t6".16b                        \n" // GHASH block 4k+2 - mid

// cycle 33
"       aese    "ctr3b", "rk2"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 2
"       stp     "output_l3", "output_h3", [%[output_ptr]], #16      \n" // AES block 4k+3 - store result

// cycle 34
"       pmull   "mod_t".1q, "acc_h".1d, "mod_constant".1d           \n" // MODULO - top 64b align with mid
"       eor     "acc_lb", "acc_lb", "t8".16b                        \n" // GHASH block 4k+3 - low

// cycle 35
"       aese    "ctr1b", "rk4"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 4

// cycle 36
"       aese    "ctr0b", "rk3"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 3
"       eor     "acc_mb", "acc_mb", "t9".16b                        \n" // GHASH block 4k+3 - mid

// cycle 37
"       aese    "ctr3b", "rk3"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 3

// cycle 38
"       aese    "ctr1b", "rk5"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 5
"       eor     "t9".16b, "acc_lb", "acc_hb"                        \n" // MODULO - karatsuba tidy up

// cycle 39
"       aese    "ctr2b", "rk4"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 4

// cycle 40
"       aese    "ctr3b", "rk4"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 4
"       adcs    "tmp_checksum", "tmp_checksum", "output_l1"         \n" // checksum calc 4k+1 low

// cycle 41
"       aese    "ctr1b", "rk6"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 6
"       ext     "acc_hb", "acc_hb", "acc_hb", #8                    \n" // MODULO - other top alignment

// cycle 42
"       eor     "acc_mb", "acc_mb", "t9".16b                        \n" // MODULO - karatsuba tidy up

// cycle 43
"       aese    "ctr3b", "rk5"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 5

// cycle 44
"       aese    "ctr1b", "rk7"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 7

// cycle 45
"       aese    "ctr0b", "rk4"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 4
"       eor     "acc_mb", "acc_mb", "mod_t".16b                     \n" // MODULO - fold into mid

// cycle 46
"       aese    "ctr3b", "rk6"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 6

// cycle 47
"       aese    "ctr1b", "rk8"   \n  aesmc   "ctr1b", "ctr1b"       \n" // AES block 4k+5 - round 8

// cycle 48
"       aese    "ctr2b", "rk5"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 5

// cycle 49
"       aese    "ctr0b", "rk5"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 5
"       eor     "acc_mb", "acc_mb", "acc_hb"                        \n" // MODULO - fold into mid

// cycle 50
"       aese    "ctr3b", "rk7"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 7

// cycle 51
"       aese    "ctr2b", "rk6"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 6

// cycle 52
"       aese    "ctr0b", "rk6"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 6

// cycle 53
"       pmull   "mod_constant".1q, "acc_m".1d, "mod_constant".1d    \n" // MODULO - mid 64b align with low

// cycle 54
"       aese    "ctr2b", "rk7"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 7

// cycle 55
"       aese    "ctr0b", "rk7"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 7
"       adcs    "tmp_checksum", "tmp_checksum", "output_h1"         \n" // checksum calc 4k+1 high

// cycle 56
"       aese    "ctr3b", "rk8"   \n  aesmc   "ctr3b", "ctr3b"       \n" // AES block 4k+7 - round 8
"       ext     "acc_mb", "acc_mb", "acc_mb", #8                    \n" // MODULO - other mid alignment

// cycle 57
"       aese    "ctr2b", "rk8"   \n  aesmc   "ctr2b", "ctr2b"       \n" // AES block 4k+6 - round 8
"       adcs    "tmp_checksum", "tmp_checksum", "output_l2"         \n" // checksum calc 4k+2 low

// cycle 58
"       aese    "ctr0b", "rk8"   \n  aesmc   "ctr0b", "ctr0b"       \n" // AES block 4k+4 - round 8
"       eor     "acc_lb", "acc_lb", "mod_constant".16b              \n" // MODULO - fold into low

// cycle 59
"       aese    "ctr3b", "rk9"                                      \n" // AES block 4k+7 - round 9
"       add     "rctr32w", "rctr32w", #1                            \n" // CTR block 4k+7

// cycle 60
"       aese    "ctr1b", "rk9"                                      \n" // AES block 4k+5 - round 9
"       adcs    "tmp_checksum", "tmp_checksum", "output_h2"         \n" // checksum calc 4k+2 high

// cycle 61
"       aese    "ctr0b", "rk9"                                      \n" // AES block 4k+4 - round 9
"       adcs    "tmp_checksum", "tmp_checksum", "output_l3"         \n" // checksum calc 4k+3 low

// cycle 62
"       aese    "ctr2b", "rk9"                                      \n" // AES block 4k+6 - round 9
"       adcs    "tmp_checksum", "tmp_checksum", "output_h3"         \n" // checksum calc 4k+3 high

// cycle 63
"       adc     "tmp_checksum", "tmp_checksum", xzr                 \n" // checksum calc 4k+3 final carry
"       eor     "acc_lb", "acc_lb", "acc_mb"                        \n" // MODULO - fold into low
"2:                                                                 \n" // TAIL

// cycle 0
"       sub     "main_end_input_ptr", "end_input_ptr", %[input_ptr] \n" // main_end_input_ptr is number of bytes left to process
"       ld1     { "res1b" }, [%[input_ptr]], #16                    \n" // AES block 4k+4 - load ciphertext

// cycle 5
"       eor     "ctr0b", "res1b", "ctr0b"                           \n" // AES block 4k+4 - result

// cycle 8
"       mov     "output_h0", "ctr0".d[1]                            \n" // AES block 4k+4 - mov high

// cycle 9
"       mov     "output_l0", "ctr0".d[0]                            \n" // AES block 4k+4 - mov low

// cycle 13
"       eor     "output_h0", "output_h0", "rk10_h"                  \n" // AES block 4k+4 - round 10 high
"       cmp     "main_end_input_ptr", #48                           \n"

// cycle 14
"       eor     "output_l0", "output_l0", "rk10_l"                  \n" // AES block 4k+4 - round 10 low
"       ext     "t0".16b, "acc_lb", "acc_lb", #8                    \n" // prepare final partial tag
"       b.gt    3f                                                  \n"

// cycle 0
"       cmp     "main_end_input_ptr", #32                           \n"
"       movi    "acc_l".8b, #0                                      \n"
"       mov     "ctr3b", "ctr2b"                                    \n"

// cycle 1
"       sub     "rctr32w", "rctr32w", #1                            \n"
"       mov     "ctr2b", "ctr1b"                                    \n"

// cycle 2
"       movi    "acc_m".8b, #0                                      \n"
"       movi    "acc_h".8b, #0                                      \n"
"       b.gt    4f                                                  \n"

// cycle 0
"       cmp     "main_end_input_ptr", #16                           \n"

// cycle 1
"       mov     "ctr3b", "ctr1b"                                    \n"
"       sub     "rctr32w", "rctr32w", #1                            \n"
"       b.gt    5f                                                  \n"

// cycle 0
"       sub     "rctr32w", "rctr32w", #1                            \n"
"       b       6f                                                  \n"
"3:                                                                 \n" // blocks left >  3
"       rev64   "res0b", "res1b"                                    \n" // GHASH final-3 block
"       stp     "output_l0", "output_h0", [%[output_ptr]], #16      \n" // AES final-3 block  - store result

// cycle 1
"       mov     "acc_md", "h34k".d[1]                               \n" // GHASH final-3 block - mid
"       adds    "tmp_checksum", "tmp_checksum", "output_l0"         \n" // checksum calc final-3 low

// cycle 2
"       adcs    "tmp_checksum", "tmp_checksum", "output_h0"         \n" // checksum calc final-3 high

// cycle 3
"       adc     "tmp_checksum", "tmp_checksum", xzr                 \n" // checksum calc final-3 final carry
"       eor     "res0b", "res0b", "t0".16b                          \n" // feed in partial tag
"       ld1     { "res1b" }, [%[input_ptr]], #16                    \n" // AES final-2 block - load ciphertext

// cycle 6
"       mov     "rk4d", "res0".d[1]                                 \n" // GHASH final-3 block - mid

// cycle 8
"       eor     "ctr0b", "res1b", "ctr1b"                           \n" // AES final-2 block - result

// cycle 9
"       eor     "rk4v".8b, "rk4v".8b, "res0".8b                     \n" // GHASH final-3 block - mid

// cycle 11
"       mov     "output_h0", "ctr0".d[1]                            \n" // AES final-2 block - mov high

// cycle 12
"       mov     "output_l0", "ctr0".d[0]                            \n" // AES final-2 block - mov low

// cycle 13
"       pmull   "acc_m".1q, "rk4v".1d, "acc_m".1d                   \n" // GHASH final-3 block - mid

// cycle 14
"       pmull   "acc_l".1q, "res0".1d, "h4".1d                      \n" // GHASH final-3 block - low

// cycle 15
"       pmull2  "acc_h".1q, "res0".2d, "h4".2d                      \n" // GHASH final-3 block - high

// cycle 16
"       eor     "output_h0", "output_h0", "rk10_h"                  \n" // AES final-2 block - round 10 high

// cycle 17
"       movi    "t0".8b, #0                                         \n" // surpress further partial tag feed in
"       eor     "output_l0", "output_l0", "rk10_l"                  \n" // AES final-2 block - round 10 low
"4:                                                                 \n" // blocks left >  2

// cycle 0
"       rev64   "res0b", "res1b"                                    \n" // GHASH final-2 block
"       stp     "output_l0", "output_h0", [%[output_ptr]], #16      \n" // AES final-2 block  - store result

// cycle 1
"       adds    "tmp_checksum", "tmp_checksum", "output_l0"         \n" // checksum calc final-2 low

// cycle 2
"       adcs    "tmp_checksum", "tmp_checksum", "output_h0"         \n" // checksum calc final-2 high

// cycle 3
"       eor     "res0b", "res0b", "t0".16b                          \n" // feed in partial tag
"       adc     "tmp_checksum", "tmp_checksum", xzr                 \n" // checksum calc final-2 final carry
"       ld1     { "res1b" }, [%[input_ptr]], #16                    \n" // AES final-1 block - load ciphertext

// cycle 6
"       mov     "rk4d", "res0".d[1]                                 \n" // GHASH final-2 block - mid

// cycle 7
"       pmull2  "rk2q1", "res0".2d, "h3".2d                         \n" // GHASH final-2 block - high

// cycle 8
"       eor     "ctr0b", "res1b", "ctr2b"                           \n" // AES final-1 block - result

// cycle 9
"       eor     "rk4v".8b, "rk4v".8b, "res0".8b                     \n" // GHASH final-2 block - mid

// cycle 10
"       pmull   "rk3q1", "res0".1d, "h3".1d                         \n" // GHASH final-2 block - low

// cycle 11
"       eor     "acc_hb", "acc_hb", "rk2"                           \n" // GHASH final-2 block - high
"       mov     "output_l0", "ctr0".d[0]                            \n" // AES final-1 block - mov low

// cycle 12
"       pmull   "rk4v".1q, "rk4v".1d, "h34k".1d                     \n" // GHASH final-2 block - mid
"       mov     "output_h0", "ctr0".d[1]                            \n" // AES final-1 block - mov high

// cycle 13
"       eor     "acc_lb", "acc_lb", "rk3"                           \n" // GHASH final-2 block - low

// cycle 15
"       eor     "acc_mb", "acc_mb", "rk4v".16b                      \n" // GHASH final-2 block - mid

// cycle 16
"       eor     "output_l0", "output_l0", "rk10_l"                  \n" // AES final-1 block - round 10 low

// cycle 17
"       movi    "t0".8b, #0                                         \n" // surpress further partial tag feed in
"       eor     "output_h0", "output_h0", "rk10_h"                  \n" // AES final-1 block - round 10 high
"5:                                                                 \n" // blocks left >  1

// cycle 0
"       stp     "output_l0", "output_h0", [%[output_ptr]], #16      \n" // AES final-1 block  - store result
"       rev64   "res0b", "res1b"                                    \n" // GHASH final-1 block

// cycle 1
"       adds    "tmp_checksum", "tmp_checksum", "output_l0"         \n" // checksum calc final-1 low

// cycle 2
"       adcs    "tmp_checksum", "tmp_checksum", "output_h0"         \n" // checksum calc final-1 high

// cycle 3
"       adc     "tmp_checksum", "tmp_checksum", xzr                 \n" // checksum calc final-1 final carry
"       eor     "res0b", "res0b", "t0".16b                          \n" // feed in partial tag
"       ld1     { "res1b" }, [%[input_ptr]], #16                    \n" // AES final block - load ciphertext

// cycle 6
"       mov     "rk4d", "res0".d[1]                                 \n" // GHASH final-1 block - mid

// cycle 7
"       pmull2  "rk2q1", "res0".2d, "h2".2d                         \n" // GHASH final-1 block - high

// cycle 8
"       eor     "ctr0b", "res1b", "ctr3b"                           \n" // AES final block - result

// cycle 9
"       eor     "rk4v".8b, "rk4v".8b, "res0".8b                     \n" // GHASH final-1 block - mid

// cycle 10
"       eor     "acc_hb", "acc_hb", "rk2"                           \n" // GHASH final-1 block - high

// cycle 11
"       pmull   "rk3q1", "res0".1d, "h2".1d                         \n" // GHASH final-1 block - low

// cycle 12
"       ins     "rk4v".d[1], "rk4v".d[0]                            \n" // GHASH final-1 block - mid
"       mov     "output_l0", "ctr0".d[0]                            \n" // AES final block - mov low

// cycle 13
"       mov     "output_h0", "ctr0".d[1]                            \n" // AES final block - mov high
"       movi    "t0".8b, #0                                         \n" // surpress further partial tag feed in

// cycle 15
"       pmull2  "rk4v".1q, "rk4v".2d, "h12k".2d                     \n" // GHASH final-1 block - mid

// cycle 17
"       eor     "output_l0", "output_l0", "rk10_l"                  \n" // AES final block - round 10 low
"       eor     "acc_lb", "acc_lb", "rk3"                           \n" // GHASH final-1 block - low

// cycle 18
"       eor     "acc_mb", "acc_mb", "rk4v".16b                      \n" // GHASH final-1 block - mid
"       eor     "output_h0", "output_h0", "rk10_h"                  \n" // AES final block - round 10 high
"6:                                                                 \n" // blocks left <= 1

// cycle 0
"       and     %[input_ptr], %[ciphertext_length], #127            \n" // ciphertext_length %= 128

// cycle 1
"       mvn     "rk10_h", xzr                                       \n" // rk10_h = 0xffffffffffffffff
"       sub     %[input_ptr], %[input_ptr], #128                    \n" // input_ptr -= 128

// cycle 2
"       mvn     "rk10_l", xzr                                       \n" // rk10_l = 0xffffffffffffffff
"       neg     %[input_ptr], %[input_ptr]                          \n" // input_ptr = 128 - #bits in input (in range [1,128])

// cycle 3
"       and     %[input_ptr], %[input_ptr], #127                    \n" // input_ptr %= 128

// cycle 4
"       lsr     "rk10_h", "rk10_h", %[input_ptr]                    \n" // rk10_h is mask for top 64b of last block
"       cmp     %[input_ptr], #64                                   \n"

// cycle 5
"       csel    "ctr32x", "rk10_l", "rk10_h", lt                    \n"
"       csel    "ctr96_b64x", "rk10_h", xzr, lt                     \n"

// cycle 6
"       fmov    "ctr0d", "ctr32x"                                   \n" // ctr0b is mask for last block

// cycle 7
"       and     "output_l0", "output_l0", "ctr32x"                  \n"
"       mov     "ctr0".d[1], "ctr96_b64x"                           \n"

// cycle 12
"       and     "output_h0", "output_h0", "ctr96_b64x"              \n"
"       adds    "tmp_checksum", "tmp_checksum", "output_l0"         \n" // checksum calc final low
"       ldp     "end_input_ptr", "main_end_input_ptr", [%[output_ptr]]\n" // load existing bytes we need to not overwrite

// cycle 15
"       adcs    "tmp_checksum", "tmp_checksum", "output_h0"         \n" // checksum calc final high
"       and     "res1b", "res1b", "ctr0b"                           \n" // possibly partial last block has zeroes in highest bits

// cycle 16
"       bic     "end_input_ptr", "end_input_ptr", "ctr32x"          \n" // mask out low existing bytes

// cycle 17
"       orr     "output_l0", "output_l0", "end_input_ptr"           \n"

// cycle 18
"       adc     "tmp_checksum", "tmp_checksum", xzr                 \n" // checksum calc final final carry

// cycle 19
"       rev64   "res0b", "res1b"                                    \n" // GHASH final block
"       bic     "main_end_input_ptr", "main_end_input_ptr", "ctr96_b64x"\n" // mask out high existing bytes

// cycle 22
"       eor     "res0b", "res0b", "t0".16b                          \n" // feed in partial tag

// cycle 25
"       mov     "t0d", "res0".d[1]                                  \n" // GHASH final block - mid
"       orr     "output_h0", "output_h0", "main_end_input_ptr"      \n"

// cycle 26
"       pmull2  "rk2q1", "res0".2d, "h1".2d                         \n" // GHASH final block - high

// cycle 27
"       pmull   "rk3q1", "res0".1d, "h1".1d                         \n" // GHASH final block - low

// cycle 28
"       eor     "t0".8b, "t0".8b, "res0".8b                         \n" // GHASH final block - mid

// cycle 29
"       eor     "acc_hb", "acc_hb", "rk2"                           \n" // GHASH final block - high

// cycle 30
"       eor     "acc_lb", "acc_lb", "rk3"                           \n" // GHASH final block - low

// cycle 31
"       pmull   "t0".1q, "t0".1d, "h12k".1d                         \n" // GHASH final block - mid

// cycle 33
"       eor     "t9".16b, "acc_lb", "acc_hb"                        \n" // MODULO - karatsuba tidy up

// cycle 34
"       eor     "acc_mb", "acc_mb", "t0".16b                        \n" // GHASH final block - mid
"       movi    "mod_constant".8b, #0xc2                            \n"

// cycle 37
"       shl     "mod_constantd", "mod_constantd", #56               \n" // mod_constant

// cycle 39
"       eor     "acc_mb", "acc_mb", "t9".16b                        \n" // MODULO - karatsuba tidy up

// cycle 40
"       pmull   "mod_t".1q, "acc_h".1d, "mod_constant".1d           \n" // MODULO - top 64b align with mid

// cycle 42
"       ext     "acc_hb", "acc_hb", "acc_hb", #8                    \n" // MODULO - other top alignment

// cycle 43
"       eor     "acc_mb", "acc_mb", "mod_t".16b                     \n" // MODULO - fold into mid

// cycle 46
"       eor     "acc_mb", "acc_mb", "acc_hb"                        \n" // MODULO - fold into mid

// cycle 49
"       pmull   "acc_h".1q, "acc_m".1d, "mod_constant".1d           \n" // MODULO - mid 64b align with low

// cycle 50
"       ext     "acc_mb", "acc_mb", "acc_mb", #8                    \n" // MODULO - other mid alignment

// cycle 52
"       eor     "acc_lb", "acc_lb", "acc_hb"                        \n" // MODULO - fold into low

// cycle 55
"       eor     "acc_lb", "acc_lb", "acc_mb"                        \n" // MODULO - fold into low
"8:                                                                 \n"

// cycle 0
"       fmov    "ctr1d", %[aad_length]                              \n" // GHASH final+1 block

// cycle 1
"       fmov    "ctr1".d[1], %[ciphertext_length]                   \n" // GHASH final+1 block

// cycle 3
"       ext     "res2".16b, "acc_lb", "acc_lb", #8                  \n" // GHASH final+1 block

// cycle 6
"       eor     "ctr1b", "ctr1b", "res2".16b                        \n" // GHASH final+1 block

// cycle 9
"       mov     "acc_md", "ctr1".d[1]                               \n" // GHASH final+1 block

// cycle 11
"       pmull2  "acc_h".1q, "ctr1".2d, "h1".2d                      \n" // GHASH final+1 block

// cycle 12
"       eor     "acc_m".8b, "acc_m".8b, "ctr1".8b                   \n" // GHASH final+1 block

// cycle 15
"       pmull   "acc_m".1q, "acc_m".1d, "h12k".1d                   \n" // GHASH final+1 block

// cycle 16
"       pmull   "acc_l".1q, "ctr1".1d, "h1".1d                      \n" // GHASH final+1 block

// cycle 17
"       pmull   "res2".1q, "acc_h".1d, "mod_constant".1d            \n" // GHASH final+1 block

// cycle 18
"       eor     "acc_mb", "acc_mb", "acc_hb"                        \n" // GHASH final+1 block

// cycle 19
"       ext     "acc_hb", "acc_hb", "acc_hb", #8                    \n" // GHASH final+1 block

// cycle 21
"       eor     "acc_mb", "acc_mb", "acc_lb"                        \n" // GHASH final+1 block

// cycle 24
"       eor     "acc_mb", "acc_mb", "res2".16b                      \n" // GHASH final+1 block
"       ld1     { "rk1" }, [%[tag]]                                 \n" // load stashed tag
"       stp     "output_l0", "output_h0", [%[tag]]                  \n" // store full final block (to be trimmed by memcpy)
"       movi    "rk0", #0                                           \n" // finalize ensure that we remove sensitive data from stack

// cycle 25
"       ld1     { "ctr0b" }, [sp]                                   \n" // finalize
"       st1     { "rk0" }, [sp], #16                                \n" // finalize ensure that we remove sensitive data from stack

// cycle 27
"       str     "tmp_checksum", [%[checksum]]                       \n" // store plaintext's checksum
"       eor     "acc_mb", "acc_mb", "acc_hb"                        \n" // GHASH final+1 block

// cycle 30
"       pmull   "res2".1q, "acc_m".1d, "mod_constant".1d            \n" // GHASH final+1 block

// cycle 32
"       ext     "acc_mb", "acc_mb", "acc_mb", #8                    \n" // GHASH final+1 block

// cycle 33
"       eor     "acc_lb", "acc_lb", "res2".16b                      \n" // GHASH final+1 block

// cycle 36
"       eor     "acc_lb", "acc_lb", "acc_mb"                        \n" // GHASH final+1 block

// cycle 39
"       rev64   "acc_lb", "acc_lb"                                  \n" // finalize

// cycle 42
"       ext     "acc_lb", "acc_lb", "acc_lb", #8                    \n" // finalize

// cycle 45
"       eor     "acc_lb", "acc_lb", "ctr0b"                         \n" // finalize

// cycle 46
"       ld1     { "rk2" }, [%[tag_mask]]                            \n" // load tag mask

// cycle 48
"       eor     "acc_hb", "acc_lb", "rk1"                           \n" // compared tag

// cycle 51
"       and     "acc_hb", "acc_hb", "rk2"                           \n"

// cycle 54
"       smaxp   "acc_hb", "acc_hb", "acc_hb"                        \n" // reduced to 8b

// cycle 57
"       mov     %[result], "acc_h".d[0]                             \n"
"       cmp     %[result], #0                                       \n"

// cycle 58
"       cset    %[result], ne                                       \n" // result is 0 if tags match and 1 is tags don't match


        : [input_ptr] "+r" (in_ptr), [output_ptr] "+r" (out_ptr), [cc] "+r" (cc), [result] "=r" (result)
        : [aad_ptr] "r" (aad),
          [ciphertext_length] "r" (ciphertext_byte_length<<3), [aad_length] "r" (aad_byte_length<<3), [aad_mask] "r" (tag_mask+16-aad_byte_length),
          [tag] "r" (tag_copy), [tag_mask] "r" (tag_mask+16-(cc->tag_byte_length)), [checksum] "r" (checksum)
        : "v0",  "v1",  "v2",  "v3",  "v4",  "v5",  "v6",  "v7",  "v8",  "v9",  "v10", "v11", "v12", "v13", "v14", "v15",
          "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",
          "x4",  "x5",  "x6",  "x7",  "x9",  "x10", "x11", "x12", "x13", "x14", "x15", "x19", "x20", "x21", "x22", "x23",
          "x24", "memory"
    );

    if(ciphertext_byte_length)
    {
        if(((ciphertext_byte_length-1) & 15) <= 7) {
            memcpy(out_ptr, tag_copy, 8);
        } else {
            memcpy(out_ptr, tag_copy, 16);
        }
    }

    return result;
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

#undef rk10_l
#undef rk10_h
#undef tmp_checksum

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
