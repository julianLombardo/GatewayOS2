#pragma once

#include "../lib/types.h"

// ============================================================
// PE32 Portable Executable Structures
// ============================================================

// DOS Header (MZ)
struct PACKED IMAGE_DOS_HEADER {
    uint16_t e_magic;       // "MZ" = 0x5A4D
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;      // Offset to PE signature
};

// COFF File Header
struct PACKED IMAGE_FILE_HEADER {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};

// Data Directory Entry
struct PACKED IMAGE_DATA_DIRECTORY {
    uint32_t VirtualAddress;
    uint32_t Size;
};

// PE32 Optional Header (32-bit)
struct PACKED IMAGE_OPTIONAL_HEADER32 {
    uint16_t Magic;                 // 0x10B for PE32
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint32_t BaseOfData;
    uint32_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint32_t SizeOfStackReserve;
    uint32_t SizeOfStackCommit;
    uint32_t SizeOfHeapReserve;
    uint32_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    IMAGE_DATA_DIRECTORY DataDirectory[16];
};

// Section Header
struct PACKED IMAGE_SECTION_HEADER {
    char     Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
};

// Import Directory
struct PACKED IMAGE_IMPORT_DESCRIPTOR {
    uint32_t OriginalFirstThunk; // RVA to INT (Import Name Table)
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;               // RVA to DLL name string
    uint32_t FirstThunk;         // RVA to IAT (Import Address Table)
};

// Import By Name
struct PACKED IMAGE_IMPORT_BY_NAME {
    uint16_t Hint;
    char     Name[1];  // Variable length
};

// Base Relocation Block
struct PACKED IMAGE_BASE_RELOCATION {
    uint32_t VirtualAddress;
    uint32_t SizeOfBlock;
    // uint16_t TypeOffset[] follows
};

// PE Constants
#define IMAGE_DOS_SIGNATURE     0x5A4D      // "MZ"
#define IMAGE_NT_SIGNATURE      0x00004550  // "PE\0\0"
#define IMAGE_FILE_MACHINE_I386 0x014C
#define IMAGE_OPTIONAL_HDR32_MAGIC 0x10B

// Subsystem
#define IMAGE_SUBSYSTEM_WINDOWS_GUI 2
#define IMAGE_SUBSYSTEM_WINDOWS_CUI 3

// Directory indices
#define IMAGE_DIRECTORY_ENTRY_IMPORT    1
#define IMAGE_DIRECTORY_ENTRY_BASERELOC 5

// Relocation types
#define IMAGE_REL_BASED_ABSOLUTE 0
#define IMAGE_REL_BASED_HIGHLOW  3

// Section characteristics
#define IMAGE_SCN_MEM_EXECUTE 0x20000000
#define IMAGE_SCN_MEM_READ    0x40000000
#define IMAGE_SCN_MEM_WRITE   0x80000000
#define IMAGE_SCN_CNT_CODE    0x00000020

// ============================================================
// PE Loader API
// ============================================================

// Result of loading a PE
struct PELoadResult {
    bool     success;
    uint8_t* image_base;       // Allocated image memory
    uint32_t image_size;       // Total size
    uint32_t entry_point;      // Absolute address of entry point
    uint16_t subsystem;        // GUI or CUI
    char     error[64];        // Error message if !success
};

// Load PE from raw buffer (already read from disk)
PELoadResult pe_load(const uint8_t* file_data, uint32_t file_size);

// Unload PE (free memory)
void pe_unload(PELoadResult* result);

// Execute loaded PE (calls entry point)
int pe_execute(PELoadResult* result);
