/*
 * s16-pi - configuration of Musashi (third_party/musashi, unmodified) for the
 * System 16 main CPU: a 68000 with separate opcode reads (the FD1094 boards
 * read decrypted opcodes and encrypted data from the same addresses).
 */
#ifndef S16_M68KCONF_H
#define S16_M68KCONF_H

#define OPT_OFF             0
#define OPT_ON              1
#define OPT_SPECIFY_HANDLER 2

#define M68K_COMPILE_FOR_MAME       OPT_OFF
#define M68K_EMULATE_010            OPT_OFF
#define M68K_EMULATE_EC020          OPT_OFF
#define M68K_EMULATE_020            OPT_OFF
#define M68K_EMULATE_030            OPT_OFF
#define M68K_EMULATE_040            OPT_OFF
#define M68K_SEPARATE_READS         OPT_ON
#define M68K_SIMULATE_PD_WRITES     OPT_OFF
#define M68K_EMULATE_INT_ACK        OPT_SPECIFY_HANDLER
int s16_m68k_int_ack (int nLevel);
#define M68K_INT_ACK_CALLBACK(A)    s16_m68k_int_ack(A)
#define M68K_EMULATE_BKPT_ACK       OPT_OFF
#define M68K_EMULATE_TRACE          OPT_OFF
#define M68K_EMULATE_RESET          OPT_OFF
#define M68K_CMPILD_HAS_CALLBACK    OPT_OFF
#define M68K_RTE_HAS_CALLBACK       OPT_OFF
#define M68K_TAS_HAS_CALLBACK       OPT_OFF
#define M68K_ILLG_HAS_CALLBACK      OPT_OFF
#define M68K_EMULATE_FC             OPT_OFF
#define M68K_MONITOR_PC             OPT_OFF
#define M68K_INSTRUCTION_HOOK       OPT_OFF
#define M68K_EMULATE_PREFETCH       OPT_OFF
#define M68K_EMULATE_ADDRESS_ERROR  OPT_OFF
#define M68K_LOG_ENABLE             OPT_OFF
#define M68K_LOG_1010_1111          OPT_OFF
#define M68K_EMULATE_PMMU           OPT_OFF
#define M68K_USE_64_BIT             OPT_ON

#endif
