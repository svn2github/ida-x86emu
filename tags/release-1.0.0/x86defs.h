/*
   Headers for x86 emulator
   Copyright (c) 2003-2010, Chris Eagle
   
   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2 of the License, or (at your option) 
   any later version.
   
   This program is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
   FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for 
   more details.
   
   You should have received a copy of the GNU General Public License along with 
   this program; if not, write to the Free Software Foundation, Inc., 59 Temple 
   Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __X86DEFS_H
#define __X86DEFS_H

#ifndef __IDP__

#ifndef WIN32

#include <sys/types.h>
typedef int64_t quad;
typedef u_int64_t uquad;

#else   //WIN32

typedef __int64 quad;
typedef unsigned __int64 uquad;

typedef unsigned char u_int8_t;
typedef unsigned short u_int16_t;
typedef unsigned int u_int32_t;
typedef int int32_t;

#endif  //WIN32

typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;

// Use printf instead of msg when not using Ida
#define msg printf

#else   //#ifdef __IDP__

#ifndef NO_OBSOLETE_FUNCS
#define NO_OBSOLETE_FUNCS
#endif

#ifndef USE_DANGEROUS_FUNCTIONS
#define USE_DANGEROUS_FUNCTIONS
#endif  // USE_DANGEROUS_FUNCTIONS

#ifndef USE_STANDARD_FILE_FUNCTIONS
#define USE_STANDARD_FILE_FUNCTIONS
#endif

#ifndef __NT__
#define _strdup strdup
#endif

#define PLUGIN_NAME "x86emu"

#ifdef __QT__
#ifndef QT_NAMESPACE
#define QT_NAMESPACE QT
#endif
#endif

/*
#ifndef _MSC_VER
#ifndef USE_DANGEROUS_FUNCTIONS
#define USE_DANGEROUS_FUNCTIONS 1
#endif  // USE_DANGEROUS_FUNCTIONS
#endif  //_MSC_VER

#ifndef _MSC_VER
#ifndef USE_STANDARD_FILE_FUNCTIONS
#define USE_STANDARD_FILE_FUNCTIONS 1
#endif
#endif
*/

#include <ida.hpp>
#include <idp.hpp>
#include <bytes.hpp>
#include <netnode.hpp>

typedef long long quad;
typedef unsigned long long uquad;

typedef unsigned char u_int8_t;
typedef unsigned short u_int16_t;
typedef unsigned int u_int32_t;
typedef int int32_t;

#include "sdk_versions.h"

extern netnode x86emu_node;

#endif

typedef uchar  byte;
typedef ushort word;
typedef uint   dword;
typedef uquad  qword;

union ll_union {
   unsigned long long ll;
   struct {
      unsigned int low;
      unsigned int high;
   };
};

#define xCARRY 0x1
#define xPARITY 0x4
#define xAUX_CARRY 0x10
#define xZERO  0x40
#define xSIGN 0x80
#define xTRAP 0x100
#define xINTERRUPT 0x200
#define xDIRECTION 0x400
#define xOVERFLOW 0x800

#define xCF xCARRY
#define xPF xPARITY
#define xAF xAUX_CARRY
#define xZF xZERO
#define xSF xSIGN
#define xTF xTRAP
#define xIF xINTERRUPT
#define xDF xDIRECTION
#define xOF xOVERFLOW

#define RESERVED_FLAGS 0xFFC0802A
#define RING_3_FLAGS (xCF | xPF | xAF | xZF | xSF | xOF | xDF | xTF)
#define RING_0_FLAGS (0xFFFFFFFF & ~RESERVED_FLAGS)

#define xD (cpu.eflags & xDF)

#define SET(x) (cpu.eflags |= (x))
#define CLEAR(x) (cpu.eflags &= (~x))

#define xO (cpu.eflags & xOF)
#define xNO (!(cpu.eflags & xOF))

#define xB (cpu.eflags & xCF)
#define xC xB
#define xNAE xB
#define xNB (!(cpu.eflags & xCF))
#define xAE xNB
#define xNC xNB

#define xE (cpu.eflags & xZF)
#define xZ xE
#define xNE (!(cpu.eflags & xZF))
#define xNZ xNE

#define xBE (cpu.eflags & (xZF | xCF))
#define xNA xBE
#define xNBE (!(cpu.eflags & (xZF | xCF)))
#define xA xNBE

#define xS (cpu.eflags & xSF)
#define xNS (!(cpu.eflags & xSF))

#define xP (cpu.eflags & xPF)
#define xPE xP
#define xNP (!(cpu.eflags & xPF))
#define xPO xNP

#define xL (((cpu.eflags & (xSF | xOF)) == xSF) || \
           ((cpu.eflags & (xSF | xOF)) == xOF))
#define xNGE xL
#define xNL (((cpu.eflags & (xSF | xOF)) == 0) || \
            ((cpu.eflags & (xSF | xOF)) == (xSF | xOF)))
#define xGE xNL

#define xLE (((cpu.eflags & (xSF | xOF)) == xSF) || \
            ((cpu.eflags & (xSF | xOF)) == xOF)  || xZ)
#define xNG xLE
#define xNLE ((((cpu.eflags & (xSF | xOF)) == 0) || \
             ((cpu.eflags & (xSF | xOF)) == (xSF | xOF))) && xNZ)
#define xG xNLE

#define H_MASK 0x0000FF00

#define EAX 0
#define ECX 1
#define EDX 2
#define EBX 3
#define ESP 4
#define EBP 5
#define ESI 6
#define EDI 7
#define EIP 8
#define EFLAGS 9

#define MIN_REG 0
#define MAX_REG 9

#define eax (cpu.general[EAX])
#define ecx (cpu.general[ECX])
#define edx (cpu.general[EDX])
#define ebx (cpu.general[EBX])
#define esp (cpu.general[ESP])
#define ebp (cpu.general[EBP])
#define esi (cpu.general[ESI])
#define edi (cpu.general[EDI])

#define CS 0
#define SS 1
#define DS 2
#define ES 3
#define FS 4
#define GS 5

#define cs (cpu.segReg[CS])
#define ss (cpu.segReg[SS])
#define ds (cpu.segReg[DS])
#define es (cpu.segReg[ES])
#define fs (cpu.segReg[FS])
#define gs (cpu.segReg[GS])

#define csBase (cpu.segBase[CS])
#define ssBase (cpu.segBase[SS])
#define dsBase (cpu.segBase[DS])
#define esBase (cpu.segBase[ES])
#define fsBase (cpu.segBase[FS])    //FS:[0] -> SEH for Win32
#define gsBase (cpu.segBase[GS])

#define CR0 0
#define CR1 1
#define CR2 2
#define CR3 3
#define CR4 4

#define cr0 (cpu.control[CR0])
#define cr1 (cpu.control[CR1])
#define cr2 (cpu.control[CR2])
#define cr3 (cpu.control[CR3])
#define cr4 (cpu.control[CR4])

#define DR0 0
#define DR1 1
#define DR2 2
#define DR3 3
#define DR4 4
#define DR5 5
#define DR6 6
#define DR7 7

#define dr0 (cpu.debug_regs[DR0])
#define dr1 (cpu.debug_regs[DR1])
#define dr2 (cpu.debug_regs[DR2])
#define dr3 (cpu.debug_regs[DR3])
#define dr4 (cpu.debug_regs[DR4])
#define dr5 (cpu.debug_regs[DR5])
#define dr6 (cpu.debug_regs[DR6])
#define dr7 (cpu.debug_regs[DR7])

#define MOD_0 0
#define MOD_1 0x40
#define MOD_2 0x80
#define MOD_3 0xC0

#define RM(x)     ((x) & 0x07)
#define MOD(x)    ((x) & 0xC0)
#define REG(x) (((x) >> 3) & 0x07)
#define HAS_SIB(x) (RM(x) == 4)

#define SCALE(x) (1 << ((x) >> 6))
#define INDEX(x) (((x) >> 3) & 0x07)
#define BASE(x)  ((x) & 0x07)

#define PREFIX_LOCK  0x01
#define PREFIX_REPNE 0x02
#define PREFIX_REP   0x04
#define PREFIX_CS    0x08
#define PREFIX_SS    0x10
#define PREFIX_DS    0x20
#define PREFIX_ES    0x40
#define PREFIX_FS    0x80
#define PREFIX_GS    0x100
#define PREFIX_SIZE    0x200
#define PREFIX_ADDR    0x400
#define PREFIX_SIMD    0x800

#define SEG_MASK     0x1F8

//operand types
#define TYPE_REG  1
#define TYPE_IMM  2
#define TYPE_MEM  4

//operand sizes
#define SIZE_BYTE 1
#define SIZE_WORD 2
#define SIZE_DWORD 4

//FPU Flags
#define FPU_INVALID 0x1
#define FPU_DENORMALIZED 0x2
#define FPU_ZERODIVIDE 0x4
#define FPU_OVERFLOW 0x8
#define FPU_UNDERFLOW 0x10
#define FPU_PRECISION  0x20
#define FPU_STACKFAULT 0x40
#define FPU_ERRORSUMMARY 0x80
#define FPU_CONDITIONS 0x4700
#define FPU_C0 0x100
#define FPU_C1 0x200
#define FPU_C2 0x400
#define FPU_C3 0x4000
#define FPU_TOS 0x3800
#define FPU_BUSY 0x8000

#define FPU_SET(x) (fpu.status |= (x))
#define FPU_GET(x) (fpu.status & (x))
#define FPU_MASK_GET(x) (fpu.control & (x))
#define FPU_CLEAR(x) (fpu.status &= (~x))

#define FPU_TAG0 0x3
#define FPU_TAG1 0xC
#define FPU_TAG2 0x30
#define FPU_TAG3 0xC0
#define FPU_TAG4 0x300
#define FPU_TAG5 0xC00
#define FPU_TAG6 0x3000
#define FPU_TAG7 0xC000

#define FPU_ZERO_TAG 0
#define FPU_VALID_TAG 1
#define FPU_SPECIAL_TAG 2
#define FPU_EMPTY_TAG 3

/*
#define R0 0
#define R1 1
#define R2 2
#define R3 3
#define R4 4
#define R5 5
#define R6 6
#define R7 7

#define r0 (fpu.r[0])
#define r1 (fpu.r[1])
#define r2 (fpu.r[2])
#define r3 (fpu.r[3])
#define r4 (fpu.r[4])
#define r5 (fpu.r[5])
#define r6 (fpu.r[6])
#define r7 (fpu.r[7])

#define r(n) (fpu.r[n])
*/
#define fpuStackTop ((fpu.status >> 11) & 7)

void getSystemBaseTime(dword *timeLow, dword *timeHigh);
void getRandomBytes(void *buf, unsigned int len);

extern bool doTrace;
extern bool doTrack;
extern unsigned int randVal;

#define INTx80_MAGIC 0xBABEF00D

#ifdef __IDP__
//if building an IDA plugin, then here are some defines

//heap personality type values
#define LEGACY_HEAP 100
#define DLMALLOC_2_7_2_HEAP 101
#define JEMALLOC_HEAP 102
#define PHKMALLOC_HEAP 103
#define RTL_HEAP 104

//various emulator related altval indicies
#define X86_EMU_INIT 1
#define HEAP_PERSONALITY 5
#define OS_PERSONALITY 6
#define CPU_PERSONALITY 7
#define X86_MINEA 10
#define X86_MAXEA 11
#define X86_RANDVAL 12
#define SYSTEM_TIME_LOW 13
#define SYSTEM_TIME_HIGH 14
//this would be a kernel32 variable pointing into the heap
#define EMU_COMMAND_LINE 15

//various supvals
#define SYS_DLL_DIR 100
#define LAST_DIR 101
#endif

//callback type for function argument list generator
typedef void (*argcallback_t)(const char *func, const char *arg, int idx, void *user);

void getRandomBytes(void *buf, unsigned int len);
void traceLog(char *entry);
void closeTrace();
void openTraceFile();
void setTitle();
void updateRegister(int r, dword val);
void forceCode();
void codeCheck(void);
dword parseNumber(char *numb);
void dumpRange();
bool isStringPointer(char *type_str);
void skip();
void grabStackBlock();
void grabHeapBlock();
void grabMmapBlock();
void stepOne();
void syncDisplay();
void emuSyncDisplay();
void traceOne();
void run();
unsigned int *getRegisterPointer(int reg);
unsigned int getRegisterValue(int reg);
void setRegisterValue(int reg, unsigned int val);
void pushData();
void dumpRange(dword low, dword hi);
void dumpEmbededPE();
void switchThread(int tidx);
void destroyThread(int tidx);
void memLoadFile(dword start);
void dumpHeap();
void doReset();
void jumpToCursor();
void runToCursor();
void setTracking(bool track);
bool getTracking();
void setTracing(bool trace);
bool getTracing();
void tagImportAddressSavePoint();
void setBreakpoint();
void clearBreakpoint();
void generateMemoryException();
void doExportLookup();
void generateArgList(const char *func, argcallback_t cb, void *user);

#ifdef __NT__
#define DIR_SEP '\\'
#define aDIR_SEP "\\"
#else
#define DIR_SEP '/'
#define aDIR_SEP "/"
#endif

#endif
