/*
   Source for x86 emulator IdaPro plugin
   File: emufuncs.cpp
   Copyright (c) 2004, Chris Eagle
   
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

#include <windows.h>
#include <winnt.h>

#ifndef USE_STANDARD_FILE_FUNCTIONS
#define USE_STANDARD_FILE_FUNCTIONS 1
#endif

#ifdef CYGWIN
#include <psapi.h>
#endif

#ifdef PACKED
#undef PACKED
#endif

#include "cpu.h"
#include "context.h"
#include "emufuncs.h"
#include "emuheap.h"
#include "hooklist.h"
#include "emuthreads.h"
#include "buffer.h"
#include "peutils.h"
#include "memmgr.h"
#include "linux_syscalls.h"
#include "bsd_syscalls.h"

#include <kernwin.hpp>
#include <bytes.hpp>
#include <name.hpp>
#include <typeinf.hpp>
#include <segment.hpp>

void addVectoredExceptionHandler(bool first, dword handler);
void removeVectoredExceptionHandler(dword handler);

#define FAKE_HANDLE_BASE 0x80000000

extern HWND x86Dlg;

struct HandleList {
   char *moduleName;
   dword handle;
   dword id;
   dword maxAddr;
   dword ordinal_base;
   dword NoF;  //NumberOfFunctions
   dword NoN;  //NumberOfNames
   dword eat;  //AddressOfFunctions  export address table
   dword ent;  //AddressOfNames      export name table
   dword eot;  //AddressOfNameOrdinals  export ordinal table
   HandleList *next;
};

struct FakedImport {
   dword handle;  //module handle the lookup was performed on
   dword addr;    //returned fake import address
   char *name;    //name assigned to this function
   FakedImport *next;
};

static HandleList *moduleHead = NULL;
static FunctionInfo *functionInfoList = NULL;

static FakedImport *fakedImportList = NULL;
static dword fakedImportAddr = 1;

//stick dummy values up in kernel space to distinguish them from
//actual library handles
static dword moduleHandle = FAKE_HANDLE_BASE;    

//persistant module identifier
static dword moduleId = 0x10000;

static unemulatedCB unemulated_cb = NULL;

static int syscallFlavor = SYSCALL_FLAVOR_LINUX;

typedef enum {R_FAKE = -1, R_NO = 0, R_YES = 1} Reply;

int emu_alwaysLoadLibrary = ASK;
int emu_alwaysGetModuleHandle = ASK;

//pointer to til we use to extract function info
til_t *ti = NULL;

HookEntry hookTable[] = {
   {"CreateThread", emu_CreateThread},
   {"VirtualAlloc", emu_VirtualAlloc},
   {"VirtualFree", emu_VirtualFree},
   {"VirtualProtect", emu_VirtualProtect},
   {"LocalAlloc", emu_LocalAlloc},
   {"LocalFree", emu_LocalFree},
   {"GetProcAddress", emu_GetProcAddress},
   {"GetModuleHandleA", emu_GetModuleHandleA},
   {"GetModuleHandleW", emu_GetModuleHandleW},
   {"LoadLibraryA", emu_LoadLibraryA},
   {"LoadLibraryW", emu_LoadLibraryW},
   {"HeapCreate", emu_HeapCreate},
   {"HeapDestroy", emu_HeapDestroy},
   {"GlobalAlloc", emu_GlobalAlloc},
   {"GlobalFree", emu_GlobalFree},
   {"GlobalLock", emu_GlobalLock},
   {"HeapAlloc", emu_HeapAlloc},
   {"RtlAllocateHeap", emu_HeapAlloc},
   {"HeapFree", emu_HeapFree},
   {"GetProcessHeap", emu_GetProcessHeap},
   {"malloc", emu_malloc},
   {"calloc", emu_calloc},
   {"realloc", emu_realloc},
   {"free", emu_free},
   {"IsDebuggerPresent", emu_IsDebuggerPresent},
   {"CheckRemoteDebuggerPresent", emu_CheckRemoteDebuggerPresent},
   {"CloseHandle", emu_CloseHandle},
   {"NtQuerySystemInformation", emu_NtQuerySystemInformation},
   {"NtQueryInformationProcess", emu_NtQueryInformationProcess},
   {"NtSetInformationThread", emu_NtSetInformationThread},
   {"lstrlen", emu_lstrlen},
   {"lstrcpy", emu_lstrcpy},
   {"strncpy", emu_strncpy},
   {"strcpy", emu_strcpy},
   {"lstrcpyW", emu_lstrcpyW},
   {"lstrcat", emu_lstrcat},
   {"strcat", emu_strcat},
   {"_wcsset", emu_wcsset},
   {"_strlwr", emu_strlwr},

   {"GetCurrentProcessId", emu_GetCurrentProcessId},
   {"GetCurrentProcess", emu_GetCurrentProcess},
   {"GetCurrentThreadId", emu_GetCurrentThreadId},
   {"GetThreadContext", emu_GetThreadContext},

   {"RevertToSelf", emu_RevertToSelf},
   {"AreAnyAccessesGranted", emu_AreAnyAccessesGranted},
   {"GetBkMode", emu_GetBkMode},
   {"GdiFlush", emu_GdiFlush},
   {"GetROP2", emu_GetROP2},
   {"GetBkColor", emu_GetBkColor},
   {"GdiGetBatchLimit", emu_GdiGetBatchLimit},

   {"StrChrIW", emu_StrChrIW},
   {"StrChrIA", emu_StrChrIA},
   {"StrCmpIW", emu_StrCmpIW},
   {"StrCmpNIW", emu_StrCmpNIW},
   {"StrCmpW", emu_StrCmpW},
   {"StrCmpNW", emu_StrCmpNW},
   {"StrCpyW", emu_StrCpyW},
   {"StrSpnA", emu_StrSpnA},
   {"StrCSpnIA", emu_StrCSpnIA},
   {"StrCSpnIW", emu_StrCSpnIW},

   {"GetACP", emu_GetACP},
   {"GetClientRect", emu_GetClientRect},
   {"IsCharUpperA", emu_IsCharUpperA},
   {"IsCharAlphaA", emu_IsCharAlphaA},
   {"GetIconInfo", emu_GetIconInfo},
   {"GetWindow", emu_GetWindow},
   {"IsChild", emu_IsChild},
   {"GetTopWindow", emu_GetTopWindow},
   {"GetWindowContextHelpId", emu_GetWindowContextHelpId},
   {"WindowFromDC", emu_WindowFromDC},
   {"GetWindowPlacement", emu_GetWindowPlacement},
   {"CopyIcon", emu_CopyIcon},
   {"IsIconic", emu_IsIconic},
   {"GetGUIThreadInfo", emu_GetGUIThreadInfo},
   {"GetDC", emu_GetDC},
   {"GetTitleBarInfo", emu_GetTitleBarInfo},
   {"IsWindowUnicode", emu_IsWindowUnicode},
   {"IsMenu", emu_IsMenu},
   {"GetWindowRect", emu_GetWindowRect},
   {"IsWindowVisible", emu_IsWindowVisible},
   {"GetForegroundWindow", emu_GetForegroundWindow},
   {"InSendMessage", emu_InSendMessage},
   {"GetWindowTextA", emu_GetWindowTextA},
   {"IsUserAnAdmin", emu_IsUserAnAdmin},

   {"GetVersionExA", emu_GetVersionExA},
   {"GetVersion", emu_GetVersion},
   {"GetTickCount", emu_GetTickCount},

   {"GetSystemTimeAsFileTime", emu_GetSystemTimeAsFileTime},
   {"QueryPerformanceCounter", emu_QueryPerformanceCounter},

   {"NtAllocateVirtualMemory", emu_NtAllocateVirtualMemory},
   {"LdrLoadDll", emu_LdrLoadDll},
   {"LdrGetProcedureAddress", emu_LdrGetProcedureAddress},

   {"InterlockedIncrement", emu_InterlockedIncrement},
   {"InterlockedDecrement", emu_InterlockedDecrement},
   {"EncodePointer", emu_EncodePointer},
   {"DecodePointer", emu_DecodePointer},

   {"InitializeCriticalSection", emu_InitializeCriticalSection},
   {"InitializeCriticalSectionAndSpinCount", emu_InitializeCriticalSectionAndSpinCount},
   {"TryEnterCriticalSection", emu_TryEnterCriticalSection},
   {"EnterCriticalSection", emu_EnterCriticalSection},
   {"LeaveCriticalSection", emu_LeaveCriticalSection},
   {"DeleteCriticalSection", emu_DeleteCriticalSection},
   
   {"AddVectoredExceptionHandler", emu_AddVectoredExceptionHandler},
   {"RemoveVectoredExceptionHandler", emu_RemoveVectoredExceptionHandler},

   {"Sleep", emu_Sleep},

   {"GetLastError", emu_GetLastError},
   {"SetLastError", emu_SetLastError},

   {"TlsAlloc", emu_TlsAlloc},
   {"TlsFree", emu_TlsFree},
   {"TlsGetValue", emu_TlsGetValue},
   {"TlsSetValue", emu_TlsSetValue},

   {"FlsAlloc", emu_FlsAlloc},
   {"FlsFree", emu_TlsFree},
   {"FlsGetValue", emu_TlsGetValue},
   {"FlsSetValue", emu_TlsSetValue},

   {NULL, NULL}
};

void setThreadError(dword err) {
   writeDword(fsBase + TEB_LAST_ERROR, err);
}

/*
 * check for presence of extension, if missing add
 * .dll
 * module must be a malloced pointer
 */
char *checkModuleExtension(char *module) {
   if (module == NULL) return NULL;
   char *result = module;
   char *dot = strchr(module, '.');
   int len = strlen(module);
   if (dot == NULL) {
      result = (char*)realloc(module, len + 5);
      if (result) {
         strcat(result, ".dll");
      }
   }
   else {
      if (dot[1] == 0) {
         // single . used to indicate no extension
         *dot = 0;
      }
   }
   return result;
}

HandleList *findModuleByName(const char *h) {
   HandleList *hl;
   if (h == NULL) return NULL;
   for (hl = moduleHead; hl; hl = hl->next) {
      if (stricmp(h, hl->moduleName) == 0) break;
   }
   return hl;
}

dword myGetModuleHandle(const char *modName) {
   HandleList *h = findModuleByName(modName);
   if (h == NULL) return 0xFFFFFFFF;
   return h->handle;
}

HandleList *findModuleByHandle(dword handle) {
   HandleList *hl;
   for (hl = moduleHead; hl; hl = hl->next) {
      if (hl->handle == handle) break;
      if (hl->id == handle) break;       //for compatibility with old handle assignment style
   }
   return hl;
}

unsigned int getPEoffset(HMODULE mod) {
   IMAGE_DOS_HEADER *hdr = (IMAGE_DOS_HEADER*) mod;
   if (mod >= (HMODULE)FAKE_HANDLE_BASE) return 0;
   if (hdr->e_magic == IMAGE_DOS_SIGNATURE) {
      return hdr->e_lfanew;
   }
   return 0;
}

IMAGE_NT_HEADERS *getPEHeader(HMODULE mod) {
   unsigned int offset = getPEoffset(mod);
   if (offset == 0) return NULL;
   IMAGE_NT_HEADERS *pe = (IMAGE_NT_HEADERS *)(offset + (char*)mod);
   if (pe->Signature != IMAGE_NT_SIGNATURE) {
      pe = NULL;
   }
   return pe;
}

//find an existing faked import
FakedImport *findFakedImportByAddr(HandleList *mod, dword addr) {
   FakedImport *ff = NULL;
   for (ff = fakedImportList; ff; ff = ff->next) {
      if (ff->handle == mod->handle && ff->addr == addr) break;
   }
   return ff;
}

FakedImport *findFakedImportByName(HandleList *mod, char *procName) {
   FakedImport *ff = NULL;
   for (ff = fakedImportList; ff; ff = ff->next) {
      if (ff->handle == mod->handle && strcmp(ff->name, procName) == 0) break;
   }
   return ff;
}

//add a new faked import after first searching to see if it is already present
FakedImport *addFakedImport(HandleList *mod, char *procName) {
   FakedImport *ff = findFakedImportByName(mod, procName);
   if (ff) return ff;
   ff = (FakedImport*)malloc(sizeof(FakedImport));
   ff->next = fakedImportList;
   ff->addr = mod->maxAddr++;
   ff->name = strdup(procName);
   ff->handle = mod->handle;
   fakedImportList = ff;
   return ff;
}

/*
typedef struct _LDR_MODULE {
   LIST_ENTRY InLoadOrderModuleList;                   +0
   LIST_ENTRY InMemoryOrderModuleList;                 +8
   LIST_ENTRY InInitializationOrderModuleList;         +16
   PVOID BaseAddress;                                  +24
   PVOID EntryPoint;                                   +28
   ULONG SizeOfImage;                                  +32
   UNICODE_STRING FullDllName;                         +36 
   UNICODE_STRING BaseDllName;                         +44
   ULONG Flags;                                        +52
   SHORT LoadCount;                                    +56
   SHORT TlsIndex;                                     +58
   LIST_ENTRY HashTableEntry;                          +60
   ULONG TimeDateStamp;                                +68
} LDR_MODULE, *PLDR_MODULE;
*/

/*
typedef struct _LIST_ENTRY {
  struct _LIST_ENTRY  *Flink;
  struct _LIST_ENTRY  *Blink;
} LIST_ENTRY, *PLIST_ENTRY;
*/

/*
typedef struct _UNICODE_STRING {
  USHORT  Length;
  USHORT  MaximumLength;
  PWSTR  Buffer;
} UNICODE_STRING, *PUNICODE_STRING;
*/

#define PEB_LDR_DATA_SIZE 0x24
#define LDR_MODULE_SIZE 0x48
#define BASE_ADDRESS_OFFSET 24
#define BASE_NAME_OFFSET 44

dword getModuleBase(dword ldrModule) {
   return get_long(ldrModule + BASE_ADDRESS_OFFSET);
}

void insertTail(dword listHead, dword mod, int doff) {
//   msg("Insert tail called to add %x to %x\n", mod, listHead);
   dword handle = getModuleBase(mod);
   dword modFlink = mod + 24 - doff;
   dword blink = listHead;
   dword flink;
   for (flink = get_long(listHead); 
        flink != listHead; 
        flink = get_long(flink)) {
      if (get_long(flink + doff) == handle) return; //already in list
      blink = flink;
   }
   blink = get_long(listHead + 4);
   patch_long(blink, modFlink);
   patch_long(modFlink, listHead);  //point mod fwd to head
   patch_long(modFlink + 4, blink);  //point mod back to former tail
   patch_long(listHead + 4, modFlink);  //link back to mod
}

void insertHead(dword listHead, dword mod, int doff) {
//   msg("Insert head called to add %x to %x\n", mod, listHead);
   dword handle = getModuleBase(mod);
//   msg("handle is %x mod is %x\n", handle, mod);
   dword modFlink = mod + 24 - doff;
//   msg("modFlink is %x\n", modFlink);
   dword blink = listHead;
   dword flink;
//   msg("listHead is %x\n", listHead);
   for (flink = get_long(listHead); 
        flink != listHead; 
        flink = get_long(flink)) {
//      msg("first check flink = %x\n", flink);
      if (get_long(flink + doff) == handle) return; //already in list
      blink = flink;
   }
   flink = get_long(listHead);
   dword back = get_long(flink + 4);
   patch_long(flink + 4, modFlink);
   patch_long(modFlink + 4, listHead);  //point mod back to head
   patch_long(modFlink, flink);  //point mod fwd to former head
   patch_long(listHead, modFlink);  //link fwd to mod
}

void insertInOrder(dword listHead, dword mod, int doff) {
//   msg("Insert in order called to add %x to %x\n", mod, listHead);
   dword handle = getModuleBase(mod);
   dword modFlink = mod + 24 - doff;
   dword blink = listHead;
   dword flink;
   for (flink = get_long(listHead); 
        flink != listHead; 
        flink = get_long(flink)) {
      dword modBase = get_long(flink + doff);
      if (modBase == handle) return; //already in list
      if (modBase > handle) break; //insert before this
      blink = flink;
   }
   //insert prior to flink
   blink = get_long(flink + 4);
   patch_long(blink, modFlink);
   patch_long(modFlink, flink);  //point mod back to head
   patch_long(modFlink + 4, blink);  //point mod fwd to former head
   patch_long(flink + 4, modFlink);  //link fwd to mod
}

bool containsModule(dword listHead, dword mod, int doff) {
   dword blink = listHead;
   dword flink;
   for (flink = get_long(blink); 
        flink != listHead; 
        flink = get_long(flink)) {
      if (get_long(flink + doff) == mod) return true; //already in list
      blink = flink;
   }
   return false;
}

bool cmp_c_to_dbuni(char *cstr, dword uni) {
   dword ustr = get_long(uni + 4);
   if (ustr) {
      do {
         char ch = *cstr++;
         short s = get_word(ustr);
         if (s != ch) break;
         if (!ch) return true;
         ustr += 2;
      } while (1);
   }
   return false;
}

dword allocatePebUnicode(const char *str) {
   segment_t *s = get_segm_by_name(".peb");
   if (s) { 
      dword pebEnd = s->endEA;
      dword uni = pebEnd - 8;
      while (get_long(uni + 4)) {
         dword sz = get_word(uni + 2);
         uni -= sz + 8;
      }
      int len = strlen(str);
      patch_word(uni, len * 2);
      patch_word(uni + 2, len * 2 + 2);
      dword ustart = uni - len * 2 - 2;
      patch_long(uni + 4, ustart);
      for (int i = 0; i <= len; i++) {
         patch_word(ustart, *str++);
         ustart += 2;
      }
      return uni;
   }
   return BADADDR;
}

dword findPebModuleByName(char *name) {
   segment_t *s = get_segm_by_name(".peb");
   if (s) { 
      dword peb = s->startEA;
      dword pebLdr = get_long(peb + 0xC);
      dword list = pebLdr + 0xC;
      dword blink = list;
      dword flink;
      for (flink = get_long(list); 
           flink != list; 
           flink = get_long(flink)) {
         if (cmp_c_to_dbuni(name, flink + BASE_NAME_OFFSET) == 0) {
            return get_long(flink + BASE_ADDRESS_OFFSET);
         }
         blink = flink;
      }
   }
   return BADADDR;   
}

void addModuleToPeb(dword handle, const char *name, bool loading) {
   segment_t *s = get_segm_by_name(".peb");
   if (s) { 
//      msg("adding %s (%x) to PEB\n", name, handle);
      dword peb = s->startEA;
      dword pebLdr = get_long(peb + 0xC);
      dword modCount = get_long(pebLdr - 4);
//      msg("modcount is %d\n", modCount);
      //mod is the address of the LDR_MODULE that we are allocating
      dword mod = pebLdr + PEB_LDR_DATA_SIZE + modCount * LDR_MODULE_SIZE;
      dword pe = handle + get_long(handle + 0x3C);
//      msg("mod allocated at %x\n", mod);
      dword uni = allocatePebUnicode(name);
//      msg("mod name allocated at %x\n", uni);
      patch_long(mod + BASE_ADDRESS_OFFSET, handle);  //BaseAddress
      patch_long(mod + BASE_ADDRESS_OFFSET + 4, handle + get_long(pe + 0x28)); //EntryPoint
      patch_long(mod + BASE_ADDRESS_OFFSET + 8, get_long(pe + 0x50)); //SizeOfImage
//      patch_long(mod + 36, 0);  //FullDllName
      patch_long(mod + 44, get_long(uni));  //BaseDllName
      patch_long(mod + 48, get_long(uni + 4));  //BaseDllName
//      patch_long(mod + 52, 0);  //Flags
      patch_long(mod + 56, 1);  //LoadCount
//      patch_long(mod + 58, 0);  //TlsIndex
//      patch_long(mod + 60, 0);  //HashTableEntry
//      patch_long(mod + 68, 0);  //TimeDateStamp
      
      dword loadOrder = pebLdr + 0xC;
      if (containsModule(loadOrder, handle, 24)) return;

      dword memoryOrder = pebLdr + 0x14;
      dword initOrder = pebLdr + 0x1C;
      if (loading) {
         insertHead(initOrder, mod, 8);
      }
      else {
         insertTail(initOrder, mod, 8);
      }
      insertTail(loadOrder, mod, 24);
      insertInOrder(memoryOrder, mod, 16);
      modCount++;
      patch_long(pebLdr - 4, modCount);
   }
}

HandleList *addModule(const char *mod, bool loading, int id) {
   msg("x86emu: addModule called for %s\n", mod);
   HandleList *m = findModuleByName(mod);
   if (m == NULL) {
      dword h = 0;
      dword len;

      char module_name[260];
      if ((id & FAKE_HANDLE_BASE) != 0) {
         h = id;
      }
      if (h == 0) {
         len = GetSystemDirectory(module_name, sizeof(module_name));
         module_name[len++] = '\\';
         module_name[len] = 0;
#if IDA_SDK_VERSION < 480
         strncat(module_name, mod, sizeof(module_name) - len - 1);
#else
         qstrncat(module_name, mod, sizeof(module_name) - len - 1);
#endif
         module_name[sizeof(module_name) - 1] = 0;
         FILE *f = fopen(module_name, "rb");
         if (f == NULL) {
            int load = R_YES;
            load = askbuttons_c("Yes", "No", "Fake it", 1, "Could not locate %s. Locate it now?", mod);
            if (load == R_YES) {
               char *fname = askfile_c(false, mod, "Please locate dll %s", mod);
               if (fname) {
                  f = fopen(fname, "rb");
               }
            }
         }
         if (f) {
            h = loadIntoIdb(f);
            if (h == 0xFFFFFFFF) h = 0;
            fclose(f);
         }
         if (h == 0) {
            warning("Failure loading %s, faking it.", mod);
            h = FAKE_HANDLE_BASE + moduleId;
            moduleId += 0x10000;
         }
      }
      m = (HandleList*) calloc(1, sizeof(HandleList));
      m->next = moduleHead;
      moduleHead = m;
      m->moduleName = strdup(mod);
      m->handle = (dword) h;
      if (h & FAKE_HANDLE_BASE) {
         //faked module with no loaded export table
         m->maxAddr = h + 1;
      }
      else {  //good module load
         m->id = id ? id : moduleId;
         moduleId += 0x10000;
   
         dword pe_addr = m->handle + get_long(0x3C + m->handle);  //dos.e_lfanew
         m->maxAddr = m->handle + get_long(pe_addr + 0x18 + 0x38); //nt.OptionalHeader.SizeOfImage
   
         dword export_dir = m->handle + get_long(pe_addr + 0x18 + 0x60 + 0x0);  //export dir RVA
    
         m->ordinal_base = get_long(export_dir + 0x10);   //ed.Base
   
         m->NoF = get_long(export_dir + 0x14);  //ed.NumberOfFunctions
         m->NoN = get_long(export_dir + 0x18);  //ed.NumberOfNames
   
         m->eat = m->handle + get_long(export_dir + 0x1C);  //ed.AddressOfFunctions;
         m->ent = m->handle + get_long(export_dir + 0x20);  //ed.AddressOfNames;
         m->eot = m->handle + get_long(export_dir + 0x24);  //ed.AddressOfNameOrdinals;
      }
      if (h) {
         addModuleToPeb(h, mod, loading);
      }
   }
   return m;
}

void freeModuleList() {
   for (HandleList *p = moduleHead; p; moduleHead = p) {
      p = p->next;
      free(moduleHead->moduleName);
      free(moduleHead);
   }
   for (FakedImport *f = fakedImportList; f; fakedImportList = f) {
      f = f->next;
      free(fakedImportList->name);
      free(fakedImportList);
   }
   fakedImportList = NULL;
   moduleHead = NULL;
   moduleHandle = FAKE_HANDLE_BASE;
}

void loadModuleList(Buffer &b) {
//   freeModuleList();
   int n;
   b.read((char*)&n, sizeof(n));
   for (int i = 0; i < n; i++) {
      dword id, tempid;
      char *name;
      b.read((char*)&id, sizeof(id));
      tempid = id & ~FAKE_HANDLE_BASE;
      if (tempid > moduleId) moduleId = tempid + 1;
      b.readString(&name);
      if (findModuleByName(name) == NULL) {
         HandleList *m = addModule(name, false, id);
         m->next = moduleHead;
         moduleHead = m;
      }
      free(name);
   }
}

void saveModuleList(Buffer &b) {
   int n = 0;
   for (HandleList *p = moduleHead; p; p = p->next) n++;
   b.write((char*)&n, sizeof(n));
   for (HandleList *m = moduleHead; m; m = m->next) {
      dword moduleId = m->id | (m->handle & FAKE_HANDLE_BASE); //set high bit of id if using fake handle
      b.write((char*)&moduleId, sizeof(moduleId));
      b.writeString(m->moduleName);
   }
}

void loadModuleData(Buffer &b) {
   freeModuleList();
   int n = 0;
   b.read((char*)&n, sizeof(n));
   for (int i = 0; i < n; i++) {
      HandleList *m = (HandleList*)malloc(sizeof(HandleList));
      m->next = moduleHead;
      moduleHead = m;

      b.read((char*)&m->handle, sizeof(m->handle));
      b.read((char*)&m->id, sizeof(m->id));
      b.read((char*)&m->maxAddr, sizeof(m->maxAddr));
      b.read((char*)&m->ordinal_base, sizeof(m->ordinal_base));
      b.read((char*)&m->NoF, sizeof(m->NoF));
      b.read((char*)&m->NoN, sizeof(m->NoN));
      b.read((char*)&m->eat, sizeof(m->eat));
      b.read((char*)&m->ent, sizeof(m->ent));
      b.read((char*)&m->eot, sizeof(m->eot));
      b.readString(&m->moduleName);
      
      if (m->id > moduleId) {
         moduleId = m->id + 0x10000;
      }
   }
   b.read((char*)&n, sizeof(n));
   for (int j = 0; j < n; j++) {
      FakedImport *f = (FakedImport*)malloc(sizeof(FakedImport));
      b.read(&f->handle, sizeof(f->handle));  //module handle the lookup was performed on
      b.read(&f->addr, sizeof(f->addr));    //returned fake import address
      b.readString(&f->name);    //name assigned to this function
   }
}

void saveModuleData(Buffer &b) {
   int n = 0;
   for (HandleList *p = moduleHead; p; p = p->next) n++;
   b.write((char*)&n, sizeof(n));
   for (HandleList *m = moduleHead; m; m = m->next) {
      b.write((char*)&m->handle, sizeof(m->handle));
      b.write((char*)&m->id, sizeof(m->id));
      b.write((char*)&m->maxAddr, sizeof(m->maxAddr));
      b.write((char*)&m->ordinal_base, sizeof(m->ordinal_base));
      b.write((char*)&m->NoF, sizeof(m->NoF));
      b.write((char*)&m->NoN, sizeof(m->NoN));
      b.write((char*)&m->eat, sizeof(m->eat));
      b.write((char*)&m->ent, sizeof(m->ent));
      b.write((char*)&m->eot, sizeof(m->eot));
      b.writeString(m->moduleName);
   }
   //now save our FakedImport list as well
   n = 0;
   for (FakedImport *f = fakedImportList; f; f = f->next) n++;
   b.write((char*)&n, sizeof(n));
   for (FakedImport *i = fakedImportList; i; i = i->next) {
      b.write(&i->handle, sizeof(i->handle));  //module handle the lookup was performed on
      b.write(&i->addr, sizeof(i->addr));    //returned fake import address
      b.writeString(i->name);    //name assigned to this function
   }
}

/*
 * Build an ascii C string by reading directly from the database
 * until a NULL is encountered.  Returned value must be free'd
 */

char *getString(dword addr) {
   int size = 16;
   int i = 0;
   byte *str = NULL, ch;
   str = (byte*) malloc(size);
   if (addr) {
      while (ch = get_byte(addr++)) {
         if (i == size) {
            str = (byte*)realloc(str, size + 16);
            size += 16;
         }
         if (ch == 0xFF) break;  //should be ascii, something wrong here
         str[i++] = ch;
      }
      if (i == size) {
         str = (byte*)realloc(str, size + 1);
      }
   }
   str[i] = 0;
   return (char*)str;
}

/*
 * Build an ascii C string from a wchar string by reading 
 * directly from the database
 * until a NULL is encountered.  Returned value must be free'd
 */

char *getStringW(dword addr) {
   int size = 16;
   int i = 0;
   byte *str = NULL;
   short ch;
   str = (byte*) malloc(size);
   if (addr) {
      while (ch = get_word(addr)) {
         if (i == size) {
            str = (byte*)realloc(str, size + 16);
            size += 16;
         }
         if (ch == 0xFF) break;  //should be ascii, something wrong here
         str[i++] = (char)ch;
         addr += 2;
      }
      if (i == size) {
         str = (byte*)realloc(str, size + 1);
      }
   }
   str[i] = 0;
   return (char*)str;
}

/*
 * set the callback function to use when anything that is hooked, but
 * unemulated is called
 */
void setUnemulatedCB(unemulatedCB cb) {
   unemulated_cb = cb;
}

/*
 * This function is used for all unemulated API functions
 */
void unemulated(dword addr) {
   if (unemulated_cb) {
      HookNode *n = findHookByAddr(addr);
      (*unemulated_cb)(addr, n ? n->getName() : NULL);
   }
}

/*
   These functions emulate various API calls.  The idea is
   to invoke them after all parameters have been pushed onto the
   stack.  Each function understands its corresponding parameters
   and calling conventions and leaves the stack in the proper state
   with a result in eax.  Because these are invoked from the emulator
   no return address gets pushed onto the stack and the functions can
   get right at their parameters on top of the stack.
*/

void emu_FlsAlloc(dword addr) {
   //for now this forwards to TlsAlloc
   pop(SIZE_DWORD);  //discard callback func argument
   emu_TlsAlloc(addr);
}

void emu_TlsAlloc(dword addr) {
   //return is dword index of newly allocated value which is initialized to zero
   //fail value is TLS_OUT_OF_INDEXES
   dword peb = readDword(fsBase + TEB_PEB_PTR);
   dword bitmapPtr = readDword(peb + PEB_TLS_BITMAP);
   for (int i = 0; i < 2; i++) {
      dword bits = readDword(bitmapPtr);
      dword bit = 1;
      for (int j = 0; j < 32; j++) {
         if ((bits & bit) == 0) {
            bits |= bit;
            writeDword(bitmapPtr, bits);
            eax = i * 32 + j;
            writeDword(fsBase + TEB_TLS_ARRAY + eax * 4, 0);
            return;
         }
         bit <<= 1;
      }
      bitmapPtr += 4;
   }
   bitmapPtr = readDword(peb + PEB_TLS_EXP_BITMAP);

   dword exp = readDword(fsBase + TEB_TLS_EXPANSION);
   if (exp == 0) {
      exp = EmuHeap::getHeap()->calloc(0x1000, 1);
      writeDword(fsBase + TEB_TLS_EXPANSION, exp);
   }
   if (exp == 0) {
      //error code is STATUS_NO_MEMORY == 0xc0000017
      //no memory available to allocate expansion page
      eax = 0;
      setThreadError(0xc0000017);
      return;
   }

   for (int i = 0; i < 32; i++) {
      dword bits = readDword(bitmapPtr);
      dword bit = 1;
      for (int j = 0; j < 32; j++) {
         if ((bits & bit) == 0) {
            bits |= bit;
            writeDword(bitmapPtr, bits);
            eax = i * 32 + j;
            writeDword(exp + eax * 4, 0);
            eax += 64;
            return;
         }
         bit <<= 1;
      }
      bitmapPtr += 4;
   }
   //error code is STATUS_NO_MEMORY == 0xc0000017
   eax = 0xffffffff;   //TLS_OUT_OF_INDEXES
   setThreadError(0xc0000017);
}

void emu_TlsFree(dword addr) {
   //return is BOOL 0 - fail, 1 - success
   dword dwTlsIndex = pop(SIZE_DWORD);

   dword peb = readDword(fsBase + TEB_PEB_PTR);
   dword bword = dwTlsIndex >> 5;
   dword bbit = 1 << (dwTlsIndex & 0x1F);
   if (bword < 2) {
      dword bitmapPtr = readDword(peb + PEB_TLS_BITMAP);
      dword bits = readDword(bitmapPtr + bword * 4);
      if (bits & bbit) {
         bits &= ~bbit;
         writeDword(bitmapPtr + bword * 4, bits);
         eax = 1;
      }
      else {
         //index was not allocated
         eax = 0;
      }
   }
   else if (bword < 34) {
      bword -= 2;
      dword bitmapPtr = readDword(peb + PEB_TLS_EXP_BITMAP);
      dword bits = readDword(bitmapPtr + bword * 4);
      if (bits & bbit) {
         bits &= ~bbit;
         writeDword(bitmapPtr + bword * 4, bits);
         eax = 1;
      }
      else {
         //index was not allocated
         eax = 0;
      }
   }
   else {
      eax = 0;
   }
   if (!eax) {
      //error code is STATUS_INVALID_PARAMETER == 0xc000000d
      setThreadError(0xc000000d);
   }      
}

void emu_TlsGetValue(dword addr) {
   dword dwTlsIndex = pop(SIZE_DWORD);
   if (dwTlsIndex < 64) {
      eax = readDword(fsBase + TEB_TLS_ARRAY + dwTlsIndex * 4);
      setThreadError(0);
   }
   else if (dwTlsIndex < (1024 + 64)) {
      dword exp = readDword(fsBase + TEB_TLS_EXPANSION);
      dwTlsIndex -= 64;
      if (exp == 0) {
         eax = 0;
      }
      else {
         eax = readDword(exp + dwTlsIndex * 4);
      }
      setThreadError(0);
   }
   else {
      eax = 0;
      setThreadError(0xc000000d);
      //error code is STATUS_INVALID_PARAMETER == 0xc000000d
   }
}

void emu_TlsSetValue(dword addr) {
   dword dwTlsIndex = pop(SIZE_DWORD);
   dword lpTlsValue = pop(SIZE_DWORD);
   //returns BOOL 0 - fail, 1 - success
   //return is BOOL 0 - fail, 1 - success
   //kernel does no checking on whether index is actually allocated
   if (dwTlsIndex < 64) {
      writeDword(fsBase + TEB_TLS_ARRAY + dwTlsIndex * 4, lpTlsValue);
      eax = 1;
   }
   else if (dwTlsIndex < (1024 + 64)) {
      dword exp = readDword(fsBase + TEB_TLS_EXPANSION);
      dwTlsIndex -= 64;
      if (exp == 0) {
         exp = EmuHeap::getHeap()->calloc(0x1000, 1);
         writeDword(fsBase + TEB_TLS_EXPANSION, exp);
      }
      if (exp) {
         writeDword(exp + dwTlsIndex * 4, lpTlsValue);
         eax = 1;
      }
      else {
         //error code is STATUS_NO_MEMORY == 0xc0000017
         //no memory available to allocate expansion page
         eax = 0;
         setThreadError(0xc0000017);
      }
   }
   else {
      eax = 0;
      //error code is STATUS_INVALID_PARAMETER == 0xc000000d
      setThreadError(0xc000000d);
   }
}

void emu_GetLastError(dword addr) {
   eax = readDword(fsBase + TEB_LAST_ERROR);
}

void emu_SetLastError(dword addr) {
   dword err = pop(SIZE_DWORD);
   setThreadError(err);
}

void emu_AddVectoredExceptionHandler(dword addr) {
   dword first = pop(SIZE_DWORD);
   dword handler = pop(SIZE_DWORD);
   addVectoredExceptionHandler(first, handler);
   eax = handler;
}

void emu_RemoveVectoredExceptionHandler(dword addr) {
   dword handler = pop(SIZE_DWORD);
   removeVectoredExceptionHandler(handler);
}

static void initCriticalSection(dword lpcs, dword spinCount) {
   writeDword(lpcs, 0);   //DebugInfo
   writeDword(lpcs + 4, 0);   //LockCount
   writeDword(lpcs + 8, 0);   //RecursionCount
   writeDword(lpcs + 12, 0);   //OwningThread
   writeDword(lpcs + 16, 0);   //LockSemephore
   writeDword(lpcs + 20, spinCount);   //SpinCount
}

void emu_InitializeCriticalSection(dword addr) {
   dword lpCriticalSection = pop(SIZE_DWORD);
   initCriticalSection(lpCriticalSection, 0);
   //add lpCriticalSection to list of active critical sections
}

void emu_InitializeCriticalSectionAndSpinCount(dword addr) {
   dword lpCriticalSection = pop(SIZE_DWORD);
   dword spinCount = pop(SIZE_DWORD);
   initCriticalSection(lpCriticalSection, spinCount);
   //add lpCriticalSection to list of active critical sections

   //prior to vista return os 0 for fail, 1 for success
   //vista+ always returns 1
   eax = 1;
}

bool tryEnterCriticalSection(dword addr) {
   dword lpCriticalSection = pop(SIZE_DWORD);
   //now verify that this is an active critical section
   dword tid = readDword(lpCriticalSection + 12);
   if (tid == 0 || tid == activeThread->handle) {
      dword lockCount = readDword(lpCriticalSection + 4) + 1;
      writeDword(lpCriticalSection + 4, lockCount);
      writeDword(lpCriticalSection + 12, activeThread->handle);
      return true;
   }
   else {
      return false;
   }
}

void emu_EnterCriticalSection(dword addr) {
   bool success = tryEnterCriticalSection(addr);
   if (success) {
   }
   else {
      //current thread can't enter, it needs to wait
      //need to choose another thread to execute
   }
}

void emu_TryEnterCriticalSection(dword addr) {
   eax = tryEnterCriticalSection(addr);
}

void emu_LeaveCriticalSection(dword addr) {
   dword lpCriticalSection = pop(SIZE_DWORD); 
   dword tid = readDword(lpCriticalSection + 12);
   if (tid == activeThread->handle) {
      dword lockCount = readDword(lpCriticalSection + 4) - 1;
      writeDword(lpCriticalSection + 4, lockCount);
      if (lockCount == 0) {
         writeDword(lpCriticalSection + 12, 0);
         //see if any threads are blocking on this critical section
      }
   }
}

void emu_DeleteCriticalSection(dword addr) {
   dword lpCriticalSection = pop(SIZE_DWORD); 
   //remove lpCriticalSection from list of active critical sections
}

void emu_Sleep(dword addr) {
   dword milliSec = pop(SIZE_DWORD);
}

void emu_InterlockedIncrement(dword addr) {
   dword addend = pop(SIZE_DWORD); 
   eax = readDword(addend) + 1;
   writeDword(addend, eax);
}

void emu_InterlockedDecrement(dword addr) {
   dword addend = pop(SIZE_DWORD); 
   eax = readDword(addend) - 1;
   writeDword(addend, eax);
}

void emu_EncodePointer(dword addr) {
   dword ptr = pop(SIZE_DWORD);
   eax = ptr ^ randVal;
}

void emu_DecodePointer(dword addr) {
   dword ptr = pop(SIZE_DWORD);
   eax = ptr ^ randVal;
}

void emu_lstrlen(unsigned int addr) {
   dword str = pop(SIZE_DWORD);
   dword len = 0;
   while (isLoaded(str) && get_byte(str)) {
      len++;
      str++;
   }
   eax = len;
}

void strcpy_common_wide(dword dest, dword src) {
   dword val;
   while (isLoaded(src)) {
      val = get_word(src);
      src += 2;
      patch_word(dest, val);
      dest += 2;
      if (val == 0) break;
   }
}

void emu_lstrcpyW(unsigned int addr) {
   eax = pop(SIZE_DWORD);
   dword src = pop(SIZE_DWORD);
   strcpy_common_wide(eax, src);
}

void strcpy_common(dword dest, dword src) {
   dword val;
   while (isLoaded(src)) {
      val = get_byte(src++);
      patch_byte(dest++, val);
      if (val == 0) break;
   }
}

void emu_lstrcpy(unsigned int addr) {
   eax = pop(SIZE_DWORD);
   dword src = pop(SIZE_DWORD);
   strcpy_common(eax, src);
}

void emu_lstrcat(unsigned int addr) {
   dword dest = pop(SIZE_DWORD);
   eax = dest;
   dword src = pop(SIZE_DWORD);
   //move to end of dest
   while (isLoaded(dest) && get_byte(dest)) dest++;
   strcpy_common(dest, src);
}

void emu_strcat(unsigned int addr) {
   dword dest = readDword(esp);
   eax = readDword(esp);
   dword src = readDword(esp + 4);
   //move to end of dest
   while (isLoaded(dest) && get_byte(dest)) dest++;
   strcpy_common(dest, src);
}

void emu_strcpy(unsigned int addr) {
   eax = readDword(esp);
   strcpy_common(eax, readDword(esp + 4));
}

void strncpy_common(dword dest, dword src, dword n) {
   dword val;
   dword i = 0;
   while (isLoaded(src) && i < n) {
      val = get_byte(src++);
      patch_byte(dest++, val);
      if (val == 0) break;
      i++;
   }
}

void emu_strncpy(unsigned int addr) {
   eax = readDword(esp);
   dword src = readDword(esp + 4);
   dword n = readDword(esp + 8);
   strncpy_common(eax, src, n);
}

void emu_wcsset(unsigned int addr) {
   dword dest = readDword(esp);
   dword val = readDword(esp + 4);
   eax = dest;
   while (isLoaded(dest) && get_word(dest)) {
      patch_word(dest, val);
      dest += 2;
   }
}

void emu_strlwr(unsigned int addr) {
   dword dest = readDword(esp);
   eax = dest;
   while (isLoaded(dest)) {
      dword val = get_byte(dest);
      if (val == 0) break;
      patch_byte(dest++, tolower(val));
   }
}

void emu_RevertToSelf(unsigned int addr) {
   eax = 1;
}

void emu_AreAnyAccessesGranted(unsigned int addr) {
   eax = 1;
   pop(SIZE_DWORD);
   pop(SIZE_DWORD);
}

void emu_GetBkMode(unsigned int addr) {
   eax = 0;
   pop(SIZE_DWORD);
}

void emu_GdiFlush(unsigned int addr) {
   eax = 1;
}

void emu_GetROP2(unsigned int addr) {
   eax = 0;
   pop(SIZE_DWORD);
}

void emu_GetBkColor(unsigned int addr) {
   eax = 0;
   pop(SIZE_DWORD);
}

void emu_GdiGetBatchLimit(unsigned int addr) {
   eax = 20;
}

void emu_StrCmpW(unsigned int addr) {
   dword str1 = pop(SIZE_DWORD);
   dword str2 = pop(SIZE_DWORD);
   eax = 1;
   while (isLoaded(str1) && isLoaded(str2)) {
      dword val1 = get_word(str1);
      dword val2 = get_word(str2);
      int res = val1 - val2;
      if (res) {
         if (res < 0) eax = 0xFFFFFFFF;
         break; 
      }
      else if (val1 == 0) { //end of string
         eax = 0;
         break;
      }
      str1 += 2;
      str2 += 2;      
   }
}

void emu_StrSpnA(unsigned int addr) {
   dword str1 = pop(SIZE_DWORD);
   dword str2 = pop(SIZE_DWORD);
   eax = 0;
}

void emu_StrCmpIW(unsigned int addr) {
   dword str1 = pop(SIZE_DWORD);
   dword str2 = pop(SIZE_DWORD);
   eax = 1;
   while (isLoaded(str1) && isLoaded(str2)) {
      dword val1 = towlower(get_word(str1));
      dword val2 = towlower(get_word(str2));
      int res = val1 - val2;
      if (res) {
         if (res < 0) eax = 0xFFFFFFFF;
         break; 
      }
      else if (val1 == 0) { //end of string
         eax = 0;
         break;
      }
      str1 += 2;
      str2 += 2;      
   }
}

void emu_StrCpyW(unsigned int addr) {
   dword str1 = pop(SIZE_DWORD);
   dword str2 = pop(SIZE_DWORD);
   eax = str1;
   while (isLoaded(str2)) {
      dword val1 = get_word(str2);
      patch_word(str1, val1);
      if (val1 == 0) { //end of string
         break;
      }
      str1 += 2;
      str2 += 2;      
   }
}

void emu_StrChrIA(unsigned int addr) {
   dword str1 = pop(SIZE_DWORD);
   int match = tolower(pop(SIZE_DWORD));
   dword val = get_byte(str1);
   eax = 0;
   while (isLoaded(str1) && val) {
      if (tolower(val) == match) {
         eax = str1;
         break;
      }
      val = get_byte(++str1);
   }
}

void emu_StrCSpnIA(unsigned int addr) {
   dword str1 = pop(SIZE_DWORD);
   dword str2 = pop(SIZE_DWORD);
   eax = 0;
}

void emu_StrChrIW(unsigned int addr) {
   dword str1 = pop(SIZE_DWORD);
   dword match = towlower(pop(SIZE_DWORD));
   dword val = get_word(str1);
   eax = 0;
   while (isLoaded(str1) && val) {
      if (towlower(val) == match) {
         eax = str1;
         break;
      }
      str1 += 2;
      val = get_word(str1);
   }
}

void emu_StrCmpNW(unsigned int addr) {
   dword str1 = pop(SIZE_DWORD);
   dword str2 = pop(SIZE_DWORD);
   int n = pop(SIZE_DWORD);
   eax = 0;
   for (int i = 0; i < n && isLoaded(str1) && isLoaded(str2); i++) {
      dword val1 = get_word(str1);
      dword val2 = get_word(str2);
      int res = val1 - val2;
      if (res) {
         eax = res < 0 ? 0xFFFFFFFF : 1;
         break; 
      }
      else if (val1 == 0) { //end of string
         break;
      }
      str1 += 2;
      str2 += 2;      
   }
}

void emu_StrCmpNIW(unsigned int addr) {
   dword str1 = pop(SIZE_DWORD);
   dword str2 = pop(SIZE_DWORD);
   int n = pop(SIZE_DWORD);
   eax = 0;
   for (int i = 0; i < n && isLoaded(str1) && isLoaded(str2); i++) {
      dword val1 = towlower(get_word(str1));
      dword val2 = towlower(get_word(str2));
      int res = val1 - val2;
      if (res) {
         eax = (res < 0) ? 0xFFFFFFFF : 1;
         break; 
      }
      else if (val1 == 0) { //end of string
         break;
      }
      str1 += 2;
      str2 += 2;      
   }
}

void emu_StrCSpnIW(unsigned int addr) {
   dword str1 = pop(SIZE_DWORD);
   dword str2 = pop(SIZE_DWORD);
   eax = 0;
}

void emu_GetClientRect(unsigned int addr) {
   pop(SIZE_DWORD);
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_GetACP(unsigned int addr) {
   eax = 1252;
}

void emu_IsCharUpperA(unsigned int addr) {
   eax = isupper(pop(SIZE_DWORD));
}

void emu_IsCharAlphaA(unsigned int addr) {
   eax = isalpha(pop(SIZE_DWORD));
}

void emu_GetIconInfo(unsigned int addr) {
   pop(SIZE_DWORD);
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_GetWindow(unsigned int addr) {
   pop(SIZE_DWORD);
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_IsChild(unsigned int addr) {
   pop(SIZE_DWORD);
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_GetTopWindow(unsigned int addr) {
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_GetWindowContextHelpId(unsigned int addr) {
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_WindowFromDC(unsigned int addr) {
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_GetWindowPlacement(unsigned int addr) {
   pop(SIZE_DWORD);
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_CopyIcon(unsigned int addr) {
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_IsIconic(unsigned int addr) {
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_GetGUIThreadInfo(unsigned int addr) {
   pop(SIZE_DWORD);
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_GetDC(unsigned int addr) {
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_GetTitleBarInfo(unsigned int addr) {
   pop(SIZE_DWORD);
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_IsWindowUnicode(unsigned int addr) {
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_IsMenu(unsigned int addr) {
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_GetWindowRect(unsigned int addr) {
   pop(SIZE_DWORD);
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_IsWindowVisible(unsigned int addr) {
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_GetForegroundWindow(unsigned int addr) {
   eax = 0x12345678;
}

void emu_InSendMessage(unsigned int addr) {
   eax = 0;
}

void emu_GetWindowTextA(unsigned int addr) {
   pop(SIZE_DWORD);
   pop(SIZE_DWORD);
   pop(SIZE_DWORD);
   eax = 0;
}

void emu_IsUserAnAdmin(unsigned int addr) {
   eax = 0;
}

#define WINDOWS_XP_MAJOR 5
#define WINDOWS_XP_MINOR 1
#ifndef VER_PLATFORM_WIN32_NT
#define VER_PLATFORM_WIN32_NT 2
#endif

void emu_GetVersionExA(unsigned int addr) {
   dword ptr = pop(SIZE_DWORD);
   dword sz = get_long(ptr);
   eax = 1;
   if (sz != 0x94 && sz != 0x9C) {
      eax = 0;
   }
   else {
      patch_byte(ptr + 4, WINDOWS_XP_MAJOR);
      patch_byte(ptr + 8, WINDOWS_XP_MINOR);
      patch_byte(ptr + 12, 0xa28);
      patch_byte(ptr + 16, VER_PLATFORM_WIN32_NT);
      patch_many_bytes(ptr + 20, "Service Pack 3", 15);
      if (sz == 0x114) { //file in EX related stuff beginning at 0x94
      }
   }
}

void emu_GetVersion(unsigned int addr) {
   eax = 0xa280105;
}

void emu_GetTickCount(unsigned int addr) {
   eax = (dword)(tsc / 1000000);
}

void emu_GetSystemTimeAsFileTime(dword addr) {
   dword lpSystemTimeAsFileTime = pop(SIZE_DWORD);
   quad time = tsc / 100;  //tsc is roughly nanosec counter
   dword tbuf[2];
   quad *t = (quad*)tbuf;
   getSystemBaseTime(tbuf, tbuf + 1);
   t += time;
   writeDword(lpSystemTimeAsFileTime, tbuf[0]);
   writeDword(lpSystemTimeAsFileTime + 4, tbuf[1]);
}

void emu_QueryPerformanceCounter(dword addr) {
   dword lpPerformanceCount = pop(SIZE_DWORD);
   dword *tbuf = (dword*)&tsc;
   writeDword(lpPerformanceCount, tbuf[0]);
   writeDword(lpPerformanceCount + 4, tbuf[1]);
}

void emu_IsDebuggerPresent(dword addr) {
   eax = 0;
   msg("x86emu: IsDebuggerPresent called\n");
}

void emu_CheckRemoteDebuggerPresent(dword addr) {
   eax = 1;
   /*dword hProcess = */pop(SIZE_DWORD);
   dword pbDebuggerPresent = pop(SIZE_DWORD);
   writeMem(pbDebuggerPresent, 0, SIZE_DWORD);
   msg("x86emu: CheckRemoteDebuggerPresent called\n");
}

void emu_CloseHandle(dword addr) {
   dword hObject = pop(SIZE_DWORD);
   msg("x86emu: CloseHandle(0x%x) called\n", hObject);
/*
   if (isValidHandle(hObject)) {
      eax = 1;
      doCloseHandle(hObject);
   }
   else {
      eax = 0;
      //set lastError to 0xC0000008L == STATUS_INVALID_HANDLE
   }
*/   
   eax = 0;    //always fail for now
}

//from winternl.h
typedef enum _SYSTEM_INFORMATION_CLASS {
   SystemBasicInformation,
   SystemProcessorInformation,
   SystemPerformanceInformation,
   SystemTimeOfDayInformation,
   SystemPathInformation,
   SystemProcessInformation,
   SystemCallCountInformation,
   SystemDeviceInformation,
   SystemProcessorPerformanceInformation,
   SystemFlagsInformation,
   SystemCallTimeInformation,
   SystemModuleInformation,
} SYSTEM_INFORMATION_CLASS, *PSYSTEM_INFORMATION_CLASS;

#define MAXIMUM_FILENAME_LENGTH 256

typedef struct _SYSTEM_MODULE {
   ULONG Reserved1;
   ULONG Reserved2;
   PVOID ImageBaseAddress;
   ULONG ImageSize;
   ULONG Flags;
   WORD Id;
   WORD Rank;
   WORD w018;
   WORD NameOffset;
   BYTE Name[MAXIMUM_FILENAME_LENGTH];
} SYSTEM_MODULE, *PSYSTEM_MODULE;

typedef struct _SYSTEM_MODULE_INFORMATION {
   ULONG ModulesCount;
   SYSTEM_MODULE Modules[0];
} SYSTEM_MODULE_INFORMATION, *PSYSTEM_MODULE_INFORMATION;

void emu_NtQuerySystemInformation(dword addr) {
   eax = 0; //success
   dword SystemInformationClass = pop(SIZE_DWORD);
   dword pSystemInformation = pop(SIZE_DWORD);
   dword SystemInformationLength = pop(SIZE_DWORD);
   dword pReturnLength = pop(SIZE_DWORD);
   msg("x86emu: NtQuerySystemInformation(%d, 0x%x, %d, 0x%x) called\n", 
       SystemInformationClass, pSystemInformation, SystemInformationLength, pReturnLength);

   switch (SystemInformationClass) {
      case SystemBasicInformation:
         break;
      case SystemProcessorInformation:
         break;
      case SystemPerformanceInformation:
         break;
      case SystemTimeOfDayInformation:
         break;
      case SystemPathInformation:
         break;
      case SystemProcessInformation:
         break;
      case SystemCallCountInformation:
         break;
      case SystemDeviceInformation:
         break;
      case SystemProcessorPerformanceInformation:
         break;
      case SystemFlagsInformation:
         break;
      case SystemCallTimeInformation:
         break;
      case SystemModuleInformation: {
         HandleList *hl;
         dword count = 0;
         for (hl = moduleHead; hl; hl = hl->next) {
            count++;
         }
         dword size = count * sizeof(SYSTEM_MODULE) + 4;
         if (SystemInformationLength < size) {
            eax = 0xC0000004;
         }
         else if (pSystemInformation) {
            int i = 0;
            writeMem(pSystemInformation, count, SIZE_DWORD);
            for (hl = moduleHead; hl; hl = hl->next) {
               writeMem(pSystemInformation + 4 + i * sizeof(SYSTEM_MODULE), 
                        0, SIZE_DWORD);
               writeMem(pSystemInformation + 4 + i * sizeof(SYSTEM_MODULE) + 4, 
                        0, SIZE_DWORD);
               writeMem(pSystemInformation + 4 + i * sizeof(SYSTEM_MODULE) + 8, 
                        hl->handle, SIZE_DWORD);
               writeMem(pSystemInformation + 4 + i * sizeof(SYSTEM_MODULE) + 26, 
                        0, SIZE_WORD);
               patch_many_bytes(pSystemInformation + 4 + i * sizeof(SYSTEM_MODULE) + 28, 
                        hl->moduleName, strlen(hl->moduleName) + 1);
            }
         }
         if (pReturnLength) {
            writeMem(pReturnLength, size, SIZE_DWORD);
         }
         break;
      }
      default:
         eax = 0xC0000001; //STATUS_UNSUCCESSFUL
                            //could use STATUS_NOT_IMPLEMENTED 0xC0000002
         break;
   }
//   writeMem(pbDebuggerPresent, 0, SIZE_DWORD);
}

typedef enum _THREADINFOCLASS {
   ThreadBasicInformation,
   ThreadTimes,
   ThreadPriority,
   ThreadBasePriority,
   ThreadAffinityMask,
   ThreadImpersonationToken,
   ThreadDescriptorTableEntry,
   ThreadEnableAlignmentFaultFixup,
   ThreadEventPair,
   ThreadQuerySetWin32StartAddress,
   ThreadZeroTlsCell,
   ThreadPerformanceCount,
   ThreadAmILastThread,
   ThreadIdealProcessor,
   ThreadPriorityBoost,
   ThreadSetTlsArrayAddress,
   ThreadIsIoPending,
   ThreadHideFromDebugger
} THREADINFOCLASS, *PTHREADINFOCLASS;

void emu_NtSetInformationThread(dword addr) {
   eax = 0; //success
   dword ThreadHandle = pop(SIZE_DWORD);
   dword ThreadInformationClass = pop(SIZE_DWORD);
   dword pThreadInformation = pop(SIZE_DWORD);
   dword ThreadInformationLength = pop(SIZE_DWORD);
   msg("x86emu: NtSetInformationThread(0x%08x, %d, 0x%x, %d) called\n", 
       ThreadHandle, ThreadInformationClass, pThreadInformation, ThreadInformationLength);

   switch (ThreadInformationClass) {
      case ThreadHideFromDebugger: {
         break;
      }
   }
}

typedef enum _PROCESSINFOCLASS {
   ProcessBasicInformation = 0,
   ProcessDebugPort = 7
} PROCESSINFOCLASS;

//#define STATUS_INFO_LENGTH_MISMATCH 0xC0000004

void emu_NtQueryInformationProcess(dword addr) {
   eax = 0; //success
   dword ProcessHandle = pop(SIZE_DWORD);
   dword ProcessInformationClass = pop(SIZE_DWORD);
   dword pProcessInformation = pop(SIZE_DWORD);
   dword ProcessInformationLength = pop(SIZE_DWORD);
   dword pReturnLength = pop(SIZE_DWORD);

   msg("x86emu: NtQueryInformationProcess(0x%x, %d, 0x%x, %d, 0x%x) called\n", 
       ProcessHandle, ProcessInformationClass, pProcessInformation, 
       ProcessInformationLength, pReturnLength);

   switch (ProcessInformationClass) {
      case ProcessBasicInformation:
         if (ProcessInformationLength < 24) {
            eax = 0xC0000004;
         }
         else if (pProcessInformation) {
            //get peb base address
            writeMem(pProcessInformation + 4, get_long(fsBase + 0x30), SIZE_DWORD);
            //process id
            writeMem(pProcessInformation + 16, get_long(fsBase + 0x20), SIZE_DWORD);
         }
         if (pReturnLength) {
            writeMem(pReturnLength, 24, SIZE_DWORD);
         }
         break;
      case ProcessDebugPort:
         if (ProcessInformationLength < 4) {
            eax = 0xC0000004;
         }
         else if (pProcessInformation) {
            writeMem(pProcessInformation, 0, SIZE_DWORD);
         }
         if (pReturnLength) {
            writeMem(pReturnLength, 4, SIZE_DWORD);
         }
         break;
      case 30: //RemoveProcessDebugPort ???
         eax = 0xC0000353; //STATUS_PORT_NOT_SET
         break;
      default:
         eax = 0xC0000003;  //STATUS_INVALID_INFO_CLASS
         break;
   }
}

void emu_GetCurrentProcess(dword addr) {
   eax = 0xffffffff;
}

void emu_GetCurrentProcessId(dword addr) {
   eax = get_long(fsBase + TEB_PROCESS_ID);
}

void emu_GetCurrentThreadId(dword addr) {
   eax = get_long(fsBase + TEB_THREAD_ID);
}

void emu_GetThreadContext(dword addr) {
   dword hThread = pop(SIZE_DWORD);
   dword lpContext = pop(SIZE_DWORD);
   WIN_CONTEXT ctx;
   initContext(&ctx);
   ThreadNode *tn = findThread(hThread);
   if (tn && tn != activeThread) {
      regsToContext(&tn->regs, &ctx);
   }
   else {  //take current cpu registers if this is active thread
      //should probably just be an error here
      regsToContext(&cpu, &ctx);
   }
   copyContextToMem(&ctx, lpContext);
   eax = 1;    //non-zero on success,  0 on fail
}

//need to allocate new TEB here and link to PEB
void emu_CreateThread(dword addr) {
   /*LPSECURITY_ATTRIBUTES lpThreadAttributes = */ pop(SIZE_DWORD);
   /*SIZE_T dwStackSize = */ pop(SIZE_DWORD);
   dword lpStartAddress = pop(SIZE_DWORD);
   dword lpParameter = pop(SIZE_DWORD);
   /*DWORD dwCreationFlags = */ pop(SIZE_DWORD);
   dword lpThreadId = pop(SIZE_DWORD);
   
   ThreadNode *tn = emu_create_thread(lpStartAddress, lpParameter);
   
   if (lpThreadId) {
      writeMem(lpThreadId, tn->id, SIZE_DWORD);
   }
   eax = tn->handle;
   msg("x86emu: CreateThread called: ThreadFunc is 0x%x\n", lpStartAddress);
}

void emu_HeapCreate(dword addr) {
   /* DWORD flOptions =*/ pop(SIZE_DWORD); 
   /* SIZE_T dwInitialSize =*/ pop(SIZE_DWORD);
   dword dwMaximumSize = pop(SIZE_DWORD);
   //we are not going to try to do growable heaps here
   if (dwMaximumSize == 0) dwMaximumSize = 0x01000000;
   eax = EmuHeap::addHeap(dwMaximumSize);
}

void emu_HeapDestroy(dword addr) {
   dword hHeap = pop(SIZE_DWORD); 
   eax = EmuHeap::destroyHeap(hHeap);
}

void emu_GetProcessHeap(dword addr) {
   eax = EmuHeap::getPrimaryHeap();
}

void emu_HeapAlloc(dword addr) {
   dword hHeap = pop(SIZE_DWORD); 
   /* DWORD dwFlags =*/ pop(SIZE_DWORD);
   dword dwBytes = pop(SIZE_DWORD);
   EmuHeap *h = EmuHeap::findHeap(hHeap);
   //are HeapAlloc  blocks zero'ed?
   eax = h ? h->calloc(dwBytes, 1) : 0;
}

void emu_HeapFree(dword addr) {
   dword hHeap = pop(SIZE_DWORD); 
   /* DWORD dwFlags =*/ pop(SIZE_DWORD);
   dword lpMem = pop(SIZE_DWORD);
   EmuHeap *h = EmuHeap::findHeap(hHeap);
   eax = h ? h->free(lpMem) : 0;
}

void emu_GlobalAlloc(dword addr) {
   /*dword uFlags =*/ pop(SIZE_DWORD); 
   dword dwSize = pop(SIZE_DWORD);
   EmuHeap *p = EmuHeap::getHeap();
   eax = p->calloc(dwSize, 1);
}

void emu_GlobalFree(dword addr) {
   EmuHeap *p = EmuHeap::getHeap();
   eax = p->free(pop(SIZE_DWORD));
}

void emu_GlobalLock(dword addr) {
   eax = pop(SIZE_DWORD);
}

void emu_NtAllocateVirtualMemory(dword addr) {
   dword procHandle = pop(SIZE_DWORD); 
   dword pBaseAddress = pop(SIZE_DWORD); 
   dword zeroBits = pop(SIZE_DWORD); 
   dword pRegionSize = pop(SIZE_DWORD);
   dword flAllocationType = pop(SIZE_DWORD);
   /*dword flProtect =*/ pop(SIZE_DWORD);
   dword rbase = get_long(pBaseAddress);
   dword dwSize = get_long(pRegionSize);
   dword base = rbase & 0xFFFFF000;
   if (rbase) {
      dword end = (rbase + dwSize + 0xFFF) & 0xFFFFF000;
      dwSize = end - rbase;
   }
   else {
      dwSize = (dwSize + 0xFFF) & 0xFFFFF000;
   }
   dword maddr = MemMgr::mmap(base, dwSize, 0, 0);
   patch_long(pRegionSize, dwSize);
   patch_long(pBaseAddress, maddr);
   eax = 0;   //NTSTATUS
//   msg("x86emu: NtVirtualAllocateMemory called: %d bytes allocated at 0x%x\n", dwSize, addr);
}

void emu_VirtualAlloc(dword addr) {
   dword lpAddress = pop(SIZE_DWORD); 
   dword dwSize = pop(SIZE_DWORD);
   dword flAllocationType = pop(SIZE_DWORD);
   /*dword flProtect =*/ pop(SIZE_DWORD);
   dword base = lpAddress & 0xFFFFF000;
   if (lpAddress) {
      dword end = (lpAddress + dwSize + 0xFFF) & 0xFFFFF000;
      dwSize = end - lpAddress;
   }
   else {
      dwSize = (dwSize + 0xFFF) & 0xFFFFF000;
   }
   eax = MemMgr::mmap(base, dwSize, 0, 0);
   msg("x86emu: VirtualAlloc called: %d bytes allocated at 0x%x\n", dwSize, eax);
}

void emu_VirtualFree(dword addr) {
   addr = pop(SIZE_DWORD);
   dword dwSize = pop(SIZE_DWORD);
   /*dword dwFreeType =*/ pop(SIZE_DWORD);
   eax = MemMgr::munmap(addr, dwSize);   
   msg("x86emu: VirtualFree(0x%08x, %d) called: 0x%x\n", addr, dwSize, eax);
}

void emu_VirtualProtect(dword addr) {
   dword lpAddress = pop(SIZE_DWORD); 
   dword dwSize = pop(SIZE_DWORD);
   dword flNewProtect = pop(SIZE_DWORD);
   dword lpflOldProtect = pop(SIZE_DWORD);
   msg("x86emu: VirtualProtect(0x%08x, %d, 0x%x, 0x%08x)\n", 
       lpAddress, dwSize, flNewProtect, lpflOldProtect);
   eax = 1;
}

void emu_LocalAlloc(dword addr) {
   /*dword uFlags =*/ pop(SIZE_DWORD); 
   dword dwSize = pop(SIZE_DWORD);
   EmuHeap *p = EmuHeap::getHeap();
   eax = p->malloc(dwSize);
}

void emu_LocalFree(dword addr) {
   EmuHeap *p = EmuHeap::getHeap();
   eax = p->free(pop(SIZE_DWORD));
}

//funcName should be a library function name, and funcAddr its address
hookfunc checkForHook(char *funcName, dword funcAddr, dword moduleId) {
   int i = 0;
   for (i = 0; hookTable[i].fName; i++) {
      if (!strcmp(hookTable[i].fName, funcName)) {
         //if there is an emulation, hook it
         return addHook(funcName, funcAddr, hookTable[i].func, moduleId);
      }
   }
   //there is no emulation, pass all calls to the "unemulated" stub
   return addHook(funcName, funcAddr, unemulated, moduleId);
}

dword myGetProcAddress(dword hModule, dword lpProcName) {
   dword h = 0;
   char *procName = NULL;
   HandleList *m = findModuleByHandle(hModule);
   if (m == NULL) return 0;
   if (lpProcName < 0x10000) {
      //getting function by ordinal value
      char *dot;
      int len = strlen(m->moduleName) + 16;
      procName = (char*) malloc(len);
      qsnprintf(procName, len, "%s_0x%4.4X", m->moduleName, m->handle);
      dot = strchr(procName, '.');
      if (dot) *dot = '_';
      if ((m->handle & FAKE_HANDLE_BASE) == 0) {
         lpProcName -= m->ordinal_base;
         h = get_long(m->eat + lpProcName * 4) + m->handle;
      }
      else {
         //need a fake procaddress when faking module handle
         FakedImport *f = addFakedImport(m, procName);
         h = f->addr;
      }
   }
   else {
      //getting function by name
      procName = getString(lpProcName);
      if ((m->handle & FAKE_HANDLE_BASE) == 0) {
         //binary search through export table to match lpProcName
         int hi = m->NoN - 1;
         int lo = 0;
         while (lo <= hi) {
            int mid = (hi + lo) / 2;
            char *name = getString(get_long(m->ent + mid * 4) + m->handle);
            int res = strcmp(name, procName);
            if (res == 0) {
               free(name);
               lpProcName = get_word(m->eot + mid * 2); // - m->ordinal_base;
               h = get_long(m->eat + lpProcName * 4) + m->handle;
               break;
            }
            else if (res < 0) lo = mid + 1;
            else hi = mid - 1;
            free(name);
         }
      }
      else {
         //need a fake procaddress when faking module handle
         FakedImport *f = addFakedImport(m, procName);
         h = f->addr;
      }         
   }
   free(procName);
   return h;
}

dword myGetProcAddress(dword hModule, const char *procName) {
   dword h = 0;
   HandleList *m = findModuleByHandle(hModule);
   if (m == NULL) return 0;
   if ((m->handle & FAKE_HANDLE_BASE) == 0) {
      //binary search through export table to match lpProcName
      int hi = m->NoN - 1;
      int lo = 0;
      while (lo <= hi) {
         int mid = (hi + lo) / 2;
         char *name = getString(get_long(m->ent + mid * 4) + m->handle);
         int res = strcmp(name, procName);
         if (res == 0) {
            free(name);
            dword lpProcName = get_word(m->eot + mid * 2); // - m->ordinal_base;
            h = get_long(m->eat + lpProcName * 4) + m->handle;
            break;
         }
         else if (res < 0) lo = mid + 1;
         else hi = mid - 1;
         free(name);
      }
   }
   return h;
}

//FARPROC __stdcall GetProcAddress(HMODULE hModule,LPCSTR lpProcName)
void emu_GetProcAddress(dword addr) {
   static dword address = 0x80000000;
   static dword bad = 0xFFFFFFFF;
   dword hModule = pop(SIZE_DWORD); 
   dword lpProcName = pop(SIZE_DWORD);
   char *procName = NULL;
   int i;
   eax = myGetProcAddress(hModule, lpProcName);
   HandleList *m = findModuleByHandle(hModule);
   procName = reverseLookupExport(eax);
   msg("x86emu: GetProcAddress called: %s", procName);
   //first see if this function is already hooked
   if (procName && findHookByAddr(eax) == NULL) {
      //this is where we need to check if auto hooking is turned on else if (autohook) {
      //if it wasn't hooked, see if there is an emulation for it
      //use h to replace "address" and "bad" below
      for (i = 0; hookTable[i].fName; i++) {
         if (!strcmp(hookTable[i].fName, procName)) {
            //if there is an emulation, hook it
            if (eax == 0) eax = address++;
            addHook(procName, eax, hookTable[i].func, m ? m->id : 0);
            break;
         }
      }
      if (hookTable[i].fName == NULL) {
         //there is no emulation, pass all calls to the "unemulated" stub
         if (eax == 0) eax = bad--;
         addHook(procName, eax, unemulated, m ? m->id : 0);
      }
   }
   else {
   }
   msg(" (0x%X)\n", eax);
   free(procName);
}

/*
 * This is how we build import tables based on calls to 
 * GetProcAddress: create a label at addr from lastProcName.
 */

void makeImportLabel(dword addr, dword val) {
   for (dword cnt = 0; cnt < 4; cnt++) {
      do_unknown(addr + cnt, true); //undefine it
   }
   doDwrd(addr, 4);
   char *name = reverseLookupExport(val);
   if (name && !set_name(addr, name, SN_NOCHECK | SN_NOWARN)) { //failed, probably duplicate name
      //undefine old name and retry once
      dword oldName = get_name_ea(BADADDR, name);
      if (oldName != BADADDR && del_global_name(oldName)) {
         set_name(addr, name, SN_NOCHECK | SN_NOWARN);
      }
   }
   free(name);
}

HandleList *moduleCommonA(dword addr) {
   dword lpModName = pop(SIZE_DWORD);
   char *modName = getString(lpModName);
   modName = checkModuleExtension(modName);
   HandleList *m = findModuleByName(modName);
   if (m) {
      free(modName);
   }
   else {
      m = addModule(modName, false, 0);
   }
   if (m) {
      msg(" called: %s (%X)\n", m->moduleName, m->handle);
   }
   return m;
}

HandleList *moduleCommonW(dword addr) {
   dword lpModName = pop(SIZE_DWORD);
   char *modName = getStringW(lpModName);
   modName = checkModuleExtension(modName);
   HandleList *m = findModuleByName(modName);
   if (m) {
      free(modName);
   }
   else {
      m = addModule(modName, false, 0);
   }
   if (m) {
      msg(" called: %s (%X)\n", m->moduleName, m->handle);
   }
   return m;
}

HandleList *moduleCommon(char **modName) {
   *modName = checkModuleExtension(*modName);
   HandleList *m = findModuleByName(*modName);
   if (m == NULL) {
      m = addModule(*modName, false, 0);
   }
   if (m) {
      msg(" called: %s (%X)\n", m->moduleName, m->handle);
   }
   return m;
}

/*
 * To do: Need to mimic actual GetModuleHandle
 *          add .dll extension if no extension provided
 *          return first occurrence if duplicate suffix
 */

//HMODULE __stdcall GetModuleHandleA(LPCSTR lpModuleName)
void emu_GetModuleHandleA(dword addr) {
   msg("x86emu: GetModuleHandle");
   if (readMem(esp, SIZE_DWORD) == 0) {
      dword peb = readDword(fsBase + TEB_PEB_PTR);
      eax = readDword(peb + PEB_IMAGE_BASE);
      pop(SIZE_DWORD);
   }
   else {
      HandleList *m = moduleCommonA(addr);
      eax = m->handle;
   }
}

//HMODULE __stdcall GetModuleHandleW(LPWSTR lpModuleName)
void emu_GetModuleHandleW(dword addr) {
   msg("x86emu: GetModuleHandle");
   if (readMem(esp, SIZE_DWORD) == 0) {
      dword peb = readDword(fsBase + TEB_PEB_PTR);
      eax = readDword(peb + PEB_IMAGE_BASE);
      pop(SIZE_DWORD);
   }
   else {
      HandleList *m = moduleCommonW(addr);
      eax = m->handle;
   }
}

void emu_LdrLoadDll(dword addr) {
   msg("x86emu: LdrLoadDll");
   dword PathToFile = pop(SIZE_DWORD); 
   dword Flags  = pop(SIZE_DWORD); 
   dword pModuleFileName = pop(SIZE_DWORD);   //PUNICODE_STRING
   dword pModuleHandle = pop(SIZE_DWORD);

   dword len = get_word(pModuleFileName);
   dword buf = get_long(pModuleFileName + 4);
   char *modName = (char*)malloc(len + 1);
   for (dword i = 0; i < len; i++) {
      modName[i] = (char)get_word(buf + i * 2);
   }
   modName[len] = 0;

   HandleList *m = moduleCommon(&modName);
   patch_long(pModuleHandle, m->handle);
   eax = 0;
   free(modName);
}

void emu_LdrGetProcedureAddress(dword addr) {
   static dword address = 0x80000000;
   static dword bad = 0xFFFFFFFF;

   dword hModule = pop(SIZE_DWORD); 
   dword pFunctionName = pop(SIZE_DWORD);   //PANSI_STRING
   dword Ordinal = pop(SIZE_DWORD);  
   dword pFunctionAddress = pop(SIZE_DWORD);

   char *procName = NULL;
   int i;
   dword func;
   if (pFunctionName) {
      func = myGetProcAddress(hModule, get_long(pFunctionName + 4));
   }
   else {
      func = myGetProcAddress(hModule, Ordinal);
   }
   HandleList *m = findModuleByHandle(hModule);
   procName = reverseLookupExport(func);
   msg("x86emu: LdrGetProcedureAddress called: %s", procName);
   //first see if this function is already hooked
   if (procName && findHookByAddr(func) == NULL) {
      //this is where we need to check if auto hooking is turned on else if (autohook) {
      //if it wasn't hooked, see if there is an emulation for it
      //use h to replace "address" and "bad" below
      for (i = 0; hookTable[i].fName; i++) {
         if (!strcmp(hookTable[i].fName, procName)) {
            //if there is an emulation, hook it
            if (func == 0) func = address++;
            addHook(procName, func, hookTable[i].func, m ? m->id : 0);
            break;
         }
      }
      if (hookTable[i].fName == NULL) {
         //there is no emulation, pass all calls to the "unemulated" stub
         if (func == 0) func = bad--;
         addHook(procName, func, unemulated, m ? m->id : 0);
      }
   }
   else {
   }
   msg(" (0x%X)\n", func);
   free(procName);
   patch_long(pFunctionAddress, func);
   eax = func ? 0 : 1;  //need an actual error code here
}

//HMODULE __stdcall LoadLibraryA(LPCSTR lpLibFileName)
void emu_LoadLibraryA(dword addr) {
   msg("x86emu: LoadLibrary");
   HandleList *m = moduleCommonA(addr);
   eax = m->handle;
}

//HMODULE __stdcall LoadLibraryW(LPCSTR lpLibFileName)
void emu_LoadLibraryW(dword addr) {
   msg("x86emu: LoadLibrary");
   HandleList *m = moduleCommonW(addr);
   eax = m->handle;
}

void emu_malloc(dword addr) {
   EmuHeap *p = EmuHeap::getHeap();
   eax = p->malloc(readDword(esp));
}

void emu_calloc(dword addr) {
   EmuHeap *p = EmuHeap::getHeap();
   eax = p->calloc(readDword(esp), readDword(esp + 4));
}

void emu_realloc(dword addr) {
   EmuHeap *p = EmuHeap::getHeap();
   eax = p->realloc(readDword(esp), readDword(esp + 4));
}

void emu_free(dword addr) {
   EmuHeap *p = EmuHeap::getHeap();
   p->free(readDword(esp));
}

void doImports(PETables &pe) {
   for (thunk_rec *tr = pe.imports; tr; tr = tr->next) {
      HandleList *m = addModule(tr->dll_name, false, 0);

      dword slot = tr->iat_base + pe.base;
//      msg("processing %s imports slot = %x\n", tr->dll_name, slot);
      for (int i = 0; tr->iat[i]; i++, slot += 4) {
         dword fname = pe.base + tr->iat[i] + 2;
         dword f = 0;
         if (m->handle & FAKE_HANDLE_BASE) {
            f = slot;
         }
         else {  //need to deal with ordinals here
            f = myGetProcAddress(m->handle, fname);
//            reverseLookupExport((dword)f);
         }
//         msg("found %x for %s slot %x\n", f, fname, slot);
         do_unknown(slot, 0);
         doDwrd(slot, 4);
         put_long(slot, f);
         makeImportLabel(slot, f);
         if (f) {
            char *funcname = getString(fname);
            checkForHook(funcname, f, m->id);
            free(funcname);
         }
      }      
   }   
}

//okay to call for ELF, but module list should be empty
HandleList *moduleFromAddress(dword addr) {
   HandleList *hl, *result = NULL;
   for (hl = moduleHead; hl; hl = hl->next) {
//#ifdef CYGWIN
      if (addr < hl->maxAddr && addr >= hl->handle) {
         result = hl;
         break;
      }
/*
#else
      //Because MS does not include psapi stuff in Visual Studio
      dword min = 0;
      if (addr > min && addr < hl->maxAddr && addr >= hl->handle) {
         result = hl;
         min = hl->handle;
      }
#endif
*/
   }
   return result;
}

bool isModuleAddress(dword addr) {
   return moduleFromAddress(addr) != NULL;
}

int reverseLookupFunc(dword EAT, dword func, dword max, dword base) {
   for (dword i = 0; i < max; i++) {
      if ((get_long(EAT + i * 4) + base) == func) return (int)i;
   }
   return -1;
}

int reverseLookupOrd(dword EOT, word ord, dword max) {
   for (dword i = 0; i < max; i++) {
      if (get_word(EOT + i * 2) == ord) return (int)i;
   }
   return -1;
}

//need to add fake_list check for lookups that have been faked
char *reverseLookupExport(dword addr) {
   HandleList *hl;
   char *fname = NULL;
   for (hl = moduleHead; hl; hl = hl->next) {
      if (addr < hl->maxAddr && addr >= hl->handle) break;
   }
   if (hl) {
      if (hl->handle & FAKE_HANDLE_BASE) {
         FakedImport *f = findFakedImportByAddr(hl, addr);
         if (f) {
            fname = strdup(f->name);
         }
      }
      else {
         int idx = reverseLookupFunc(hl->eat, addr, hl->NoF, hl->handle);
         if (idx != -1) {
            idx = reverseLookupOrd(hl->eot, idx, hl->NoN);
            if (idx != -1) {
               fname = getString(get_long(hl->ent + idx * 4) + hl->handle);
   //            msg("x86emu: reverseLookupExport: %X == %s\n", addr, fname);
            }
         }
      }
   }
   return fname;
}

FunctionInfo *newFunctionInfo(const char *name) {
   FunctionInfo *f = (FunctionInfo*)calloc(1, sizeof(FunctionInfo));
   f->fname = (char*)malloc(strlen(name) + 1);
   strcpy(f->fname, name);
   f->next = functionInfoList;
   functionInfoList = f;
   return f;
}   

void clearFunctionInfoList() {
   FunctionInfo *f;
   while (functionInfoList) {
      f = functionInfoList;
      functionInfoList = functionInfoList->next;
      free(f->fname);
      free(f);
   }
   functionInfoList = NULL;
}

void getIdaTypeInfo(FunctionInfo *f) {
   const type_t *type;
   const p_list *fields;
   if (get_named_type(ti, f->fname, NTF_SYMU, &type, &fields) > 0) {
      f->type = type;
      f->fields = fields;
#if IDA_SDK_VERSION >= 540
      uint32 arglocs[20];
#else
      ulong arglocs[20];
#endif
      type_t *types[20];
      char *names[20];
      f->stackItems = build_funcarg_arrays(type,
                            fields,
                            arglocs,        // pointer to array of parameter locations
                            types,        // pointer to array of parameter types
                            names,          // pointer to array of parameter names
                            20,           // size of these arrays
                            true);// remove constness from 
/*                            
         for (int i = 0; i < nargs; i++) {
            print_type_to_one_line(buf, sizeof(buf), NULL, types[i]);
            msg("%d: %s %s\n", i, buf, names[i] ? names[i] : "");
         }
*/
      if (f->stackItems) {
         free_funcarg_arrays(types, names, f->stackItems);   
      }
/*
         type_t rettype[512];
         
         type_t *ret = extract_func_ret_type(t, rettype, sizeof(rettype));
         if (ret) {
            print_type_to_one_line(buf, sizeof(buf), NULL, rettype);
            msg("returns: %s\n", buf);
         }
*/
      cm_t cc = get_cc(type[1]);
      if (cc == CM_CC_STDCALL || cc == CM_CC_FASTCALL) {
         f->callingConvention = CALL_STDCALL;
      }
      else {  //if (cc == CM_CC_CDECL || cc == CM_CC_VOIDARG) {
         f->callingConvention = CALL_CDECL;
      }
   }
}

char *getFunctionPrototype(FunctionInfo *f) {
   char *result = NULL;
   if (f && f->type) {
#if IDA_SDK_VERSION >= 540
      uint32 arglocs[20];
#else
      ulong arglocs[20];
#endif
      type_t *types[20];
      char *names[20];
      char buf[512];

      type_t rettype[512];   
      type_t *ret = extract_func_ret_type(f->type, rettype, sizeof(rettype));
      if (ret) {
         print_type_to_one_line(buf, sizeof(buf), ti, rettype);
         result = strdup(buf);
      }
      result = (char*)realloc(result, strlen(result) + 3 + strlen(f->fname));
      strcat(result, " ");
      strcat(result, f->fname);
      strcat(result, "(");

      if (f->stackItems) {
         build_funcarg_arrays(f->type, f->fields, arglocs,
                              types, names, 20, true);
      }
      for (unsigned int i = 0; i < f->stackItems; i++) {
         //change to incorporate what we know from Ida
         print_type_to_one_line(buf, sizeof(buf), NULL, types[i]);
         result = (char*)realloc(result, strlen(result) + 3 + strlen(buf));
         if (i) {
            strcat(result, ",");
         }
         strcat(result, buf);
      }
      result = (char*)realloc(result, strlen(result) + 2);
      strcat(result, ")");

      if (f->stackItems) {
         free_funcarg_arrays(types, names, f->stackItems);   
      }
   }
   return result;
}

char *getFunctionReturnType(FunctionInfo *f) {
   if (f && f->type) {
      char buf[512];
      type_t rettype[512];   
      type_t *ret = extract_func_ret_type(f->type, rettype, sizeof(rettype));
      if (ret) {
         print_type_to_one_line(buf, sizeof(buf), ti, rettype);
         return strdup(buf);
      }
   }
   return NULL;
}

FunctionInfo *getFunctionInfo(const char *name) {
   FunctionInfo *f;
   for (f = functionInfoList; f; f = f->next) {
      if (!strcmp(name, f->fname)) break;
   }
   if (f == NULL) {
      const type_t *type;
      const p_list *fields;
      msg("calling get_named_type for %s\n", name);
      if (get_named_type(ti, name, NTF_SYMU, &type, &fields) > 0) {
         msg("get_named_type returned non-zero for %s\n", name);
         f = newFunctionInfo(name);
         getIdaTypeInfo(f);
      }
   }
   return f;
}

void addFunctionInfo(const char *name, dword result, dword nitems, dword callType) {
   FunctionInfo *f;
   for (f = functionInfoList; f; f = f->next) {
      if (!strcmp(name, f->fname)) break;
   }
   if (f == NULL) {
      f = newFunctionInfo(name);
   }
   f->result = result;
   f->stackItems = nitems;
   f->callingConvention = callType;
}

void saveFunctionInfo(Buffer &b) {
   int count = 0;
   FunctionInfo *f;
   for (f = functionInfoList; f; f = f->next) count++;
   b.write(&count, sizeof(count));
   for (f = functionInfoList; f; f = f->next) {
      count = strlen(f->fname) + 1;  //account for null
      b.write(&count, sizeof(count));
      b.write(f->fname, count);  //note this writes the null
      b.write(&f->result, sizeof(f->result));
      b.write(&f->stackItems, sizeof(f->stackItems));
      b.write(&f->callingConvention, sizeof(f->callingConvention));
   }
}

void loadFunctionInfo(Buffer &b) {
   int count = 0, len;
   FunctionInfo *f;
   clearFunctionInfoList();
   b.read(&count, sizeof(count));
   for (; count; count--) {
      f = (FunctionInfo*)calloc(1, sizeof(FunctionInfo));
      b.read(&len, sizeof(len));
      f->fname = (char*)malloc(len);
      b.read(f->fname, len);
      b.read(&f->result, sizeof(f->result));
      b.read(&f->stackItems, sizeof(f->stackItems));
      b.read(&f->callingConvention, sizeof(f->callingConvention));
      f->next = functionInfoList;
      getIdaTypeInfo(f);
      functionInfoList = f;
   }
}

void init_til(const char *tilFile) {
   char err[256];
   *err = 0;
#if IDA_SDK_VERSION < 470
   char *tilpath = get_tilpath();     
#else
   char tilpath[260];
   get_tilpath(tilpath, sizeof(tilpath));     
#endif
   ti = load_til(tilpath, tilFile, err, sizeof(err));
}

void emu_exit(dword retval) {
}

void emu_read(dword fd, dword buf, dword len) {
}

void emu_write(dword fd, dword buf, dword len) {
}

void emu_open(dword fname, dword flags, dword mode) {
}

void emu_close(dword fd) {
}

void syscall() {
   int syscallNum = eax;
   switch (syscallFlavor) {
      case SYSCALL_FLAVOR_LINUX:
         switch (syscallNum) {
            case LINUX_SYS_EXIT:
               emu_exit(ebx);
               break;
            case LINUX_SYS_FORK:
               break;
            case LINUX_SYS_READ:
               emu_read(ebx, ecx, edx);
               break;
            case LINUX_SYS_WRITE:
               emu_write(ebx, ecx, edx);
               break;
            case LINUX_SYS_OPEN:
               emu_open(ebx, ecx, edx);
               break;
            case LINUX_SYS_CLOSE:
               emu_close(ebx);
               break;
            case LINUX_SYS_MMAP:
               break;
            case LINUX_SYS_MUNMAP:
               break;
         }
         break;
      case SYSCALL_FLAVOR_BSD:
         switch (syscallNum) {
            case BSD_SYS_EXIT:
               emu_exit(get_long(esp + 4));
               break;
            case BSD_SYS_FORK:
               break;
            case BSD_SYS_READ:
               emu_read(get_long(esp + 4), get_long(esp + 8), get_long(esp + 12));
               break;
            case BSD_SYS_WRITE:
               emu_write(get_long(esp + 4), get_long(esp + 8), get_long(esp + 12));
               break;
            case BSD_SYS_OPEN:
               emu_open(get_long(esp + 4), get_long(esp + 8), get_long(esp + 12));
               break;
            case BSD_SYS_CLOSE:
               emu_close(get_long(esp + 4));
               break;
         }
         break;
   }
}
