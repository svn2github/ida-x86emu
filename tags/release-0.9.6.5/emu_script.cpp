/*
   Scripting support for the x86 emulator IdaPro plugin
   Copyright (c) 2008 Chris Eagle
   
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

#include <ida.hpp>
#include <expr.hpp>

#include "cpu.h"
#include "emu_script.h"
#include "sdk_versions.h"

#if IDA_SDK_VERSION < 520
typedef value_t idc_value_t;
#endif

/*
 * prototypes for functions in x86emu.cpp that we use
 * to implement some of the scripted behavior
 */
void run();
void trace();
void stepOne();
void traceOne();
void emuSyncDisplay();
void setIdcRegister(dword idc_reg_num, dword newVal);

/*
 * native implementation of EmuRun.
 */
static error_t idaapi idc_emu_run(idc_value_t *argv, idc_value_t *res) {
   run();
   return eOk;
}

/*
 * native implementation of EmuStepOne.
 */
static error_t idaapi idc_emu_step(idc_value_t *argv, idc_value_t *res) {
   stepOne();
   return eOk;
}

/*
 * native implementation of EmuTraceOne.
 */
static error_t idaapi idc_emu_trace_one(idc_value_t *argv, idc_value_t *res) {
   traceOne();
   return eOk;
}

/*
 * native implementation of EmuTrace.
 */
static error_t idaapi idc_emu_trace(idc_value_t *argv, idc_value_t *res) {
   trace();
   return eOk;
}

/*
 * native implementation of EmuSync.
 */
static error_t idaapi idc_emu_sync(idc_value_t *argv, idc_value_t *res) {
   emuSyncDisplay();
   return eOk;
}

/*
 * native implementation of EmuGetReg.  Converts a register constant
 * into the appropriate offset into the cpu struct and returns the
 * value of the indicated register.  Returns -1 if an invalid register
 * number is specified.
 */
static error_t idaapi idc_emu_getreg(idc_value_t *argv, idc_value_t *res) {
   res->vtype = VT_LONG;
   if (argv[0].vtype == VT_LONG) {
      dword regnum = argv[0].num;
      switch (regnum) {
         case EAX_REG: case ECX_REG: case EDX_REG: case EBX_REG:
         case ESP_REG: case EBP_REG: case ESI_REG: case EDI_REG:
            res->num = cpu.general[regnum - EAX_REG];
            break;
         case EIP_REG:
            res->num = cpu.eip;
            break;
         case EFLAGS_REG:
            res->num = cpu.eflags;
            break;
         case CS_REG: case SS_REG: case DS_REG: case ES_REG: case FS_REG: case GS_REG:
            res->num = cpu.segReg[regnum - CS_REG];
            break;
         case CS_BASE: case SS_BASE: case DS_BASE: case ES_BASE: case FS_BASE: case GS_BASE:
            res->num = cpu.segBase[regnum - CS_BASE];
            break;
         case DR0_REG: case DR1_REG: case DR2_REG: case DR3_REG:
         case DR4_REG: case DR5_REG: case DR6_REG: case DR7_REG:
            res->num = cpu.debug_regs[regnum - DR0_REG];
            break;
         default:
            res->num = -1;
            break;
      }
   }
   else {
      res->num = -1;
   }
   return eOk;
}

/*
 * native implementation of EmuSetReg.  Converts a register constant
 * into the appropriate offset into the cpu struct and sets the
 * value of the indicated register.  Returns 0 on success and -1 if an
 * invalid register number is specified.
 */
static error_t idaapi idc_emu_setreg(idc_value_t *argv, idc_value_t *res) {
   res->vtype = VT_LONG;
   res->num = 0;
   if (argv[0].vtype == VT_LONG && argv[1].vtype == VT_LONG) {
      dword regnum = argv[0].num;
      dword regval = argv[1].num;
      switch (regnum) {
         case EAX_REG: case ECX_REG: case EDX_REG: case EBX_REG:
         case ESP_REG: case EBP_REG: case ESI_REG: case EDI_REG:
         case EIP_REG:
         case EFLAGS_REG:
            //these registers are all displayed so we need to update the
            //respective control as well as set the register
            setIdcRegister(regnum, regval);
            break;
         case CS_REG: case SS_REG: case DS_REG: case ES_REG: case FS_REG: case GS_REG:
            cpu.segReg[regnum - CS_REG] = regval;
            break;
         case CS_BASE: case SS_BASE: case DS_BASE: case ES_BASE: case FS_BASE: case GS_BASE:
            cpu.segBase[regnum - CS_BASE] = regval;
            break;
         case DR0_REG: case DR1_REG: case DR2_REG: case DR3_REG:
         case DR4_REG: case DR5_REG: case DR6_REG: case DR7_REG:
            cpu.debug_regs[regnum - DR0_REG] = regval;
            break;
         default:
            res->num = -1;
            break;
      }
   }
   else {
      res->num = -1;
   }
   return eOk;
}

/*
 * Register new IDC functions for use with the emulator
 */
void register_funcs() {
   static const char idc_void[] = { 0 };
   static const char idc_str_args[] = { VT_STR, 0 };
   static const char idc_long[] = { VT_LONG, 0 };
   static const char idc_long_long[] = { VT_LONG, VT_LONG, 0 };
   set_idc_func("EmuRun", idc_emu_run, idc_void);
   set_idc_func("EmuTrace", idc_emu_trace, idc_void);
   set_idc_func("EmuStepOne", idc_emu_step, idc_void);
   set_idc_func("EmuTraceOne", idc_emu_trace_one, idc_void);
   set_idc_func("EmuSync", idc_emu_sync, idc_void);
   set_idc_func("EmuGetReg", idc_emu_getreg, idc_long);
   set_idc_func("EmuSetReg", idc_emu_setreg, idc_long_long);
}

/*
 * Unregister IDC functions when the plugin is unloaded
 */
void unregister_funcs() {
   set_idc_func("EmuRun", NULL, NULL);
   set_idc_func("EmuTrace", NULL, NULL);
   set_idc_func("EmuStepOne", NULL, NULL);
   set_idc_func("EmuTraceOne", NULL, NULL);
   set_idc_func("EmuSync", NULL, NULL);
   set_idc_func("EmuGetReg", NULL, NULL);
   set_idc_func("EmuSetReg", NULL, NULL);
}
