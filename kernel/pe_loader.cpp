#include "pe_loader.h"
#include "win32_shim.h"
#include "../memory/heap.h"
#include "../lib/string.h"
#include "../lib/printf.h"


PELoadResult pe_load(const uint8_t* file_data, uint32_t file_size) {
    PELoadResult res;
    memset(&res, 0, sizeof(res));

    // --- Validate DOS Header ---
    if (file_size < sizeof(IMAGE_DOS_HEADER)) {
        strcpy(res.error, "File too small for DOS header");
        return res;
    }

    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)file_data;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        strcpy(res.error, "Invalid MZ signature");
        return res;
    }

    // --- Validate PE Signature ---
    uint32_t pe_offset = dos->e_lfanew;
    if (pe_offset + 4 > file_size) {
        strcpy(res.error, "PE offset out of bounds");
        return res;
    }

    uint32_t pe_sig = *(const uint32_t*)(file_data + pe_offset);
    if (pe_sig != IMAGE_NT_SIGNATURE) {
        strcpy(res.error, "Invalid PE signature");
        return res;
    }

    // --- COFF Header ---
    uint32_t coff_offset = pe_offset + 4;
    if (coff_offset + sizeof(IMAGE_FILE_HEADER) > file_size) {
        strcpy(res.error, "COFF header truncated");
        return res;
    }

    const IMAGE_FILE_HEADER* coff = (const IMAGE_FILE_HEADER*)(file_data + coff_offset);
    if (coff->Machine != IMAGE_FILE_MACHINE_I386) {
        strcpy(res.error, "Not i386 PE");
        return res;
    }

    // --- Optional Header ---
    uint32_t opt_offset = coff_offset + sizeof(IMAGE_FILE_HEADER);
    if (opt_offset + sizeof(IMAGE_OPTIONAL_HEADER32) > file_size) {
        strcpy(res.error, "Optional header truncated");
        return res;
    }

    const IMAGE_OPTIONAL_HEADER32* opt = (const IMAGE_OPTIONAL_HEADER32*)(file_data + opt_offset);
    if (opt->Magic != IMAGE_OPTIONAL_HDR32_MAGIC) {
        strcpy(res.error, "Not PE32 (64-bit not supported)");
        return res;
    }

    uint32_t image_base = opt->ImageBase;
    uint32_t image_size = opt->SizeOfImage;
    uint32_t section_align = opt->SectionAlignment;
    (void)section_align;

    kprintf("[PE] ImageBase=0x%X SizeOfImage=0x%X Entry=0x%X Subsystem=%d\n",
                  image_base, image_size, opt->AddressOfEntryPoint, opt->Subsystem);
    kprintf("[PE] Sections=%d\n", coff->NumberOfSections);

    // --- Allocate image memory ---
    // Align to 4KB for safety
    uint32_t alloc_size = (image_size + 0xFFF) & ~0xFFF;
    if (alloc_size > 16 * 1024 * 1024) { // 16 MB max
        strcpy(res.error, "Image too large (>16MB)");
        return res;
    }

    uint8_t* base = (uint8_t*)kmalloc_aligned(alloc_size, 4096);
    if (!base) {
        strcpy(res.error, "Out of memory for PE image");
        return res;
    }
    memset(base, 0, alloc_size);

    // --- Copy headers ---
    uint32_t header_size = opt->SizeOfHeaders;
    if (header_size > file_size) header_size = file_size;
    memcpy(base, file_data, header_size);

    // --- Map sections ---
    uint32_t sec_offset = opt_offset + coff->SizeOfOptionalHeader;
    const IMAGE_SECTION_HEADER* sections = (const IMAGE_SECTION_HEADER*)(file_data + sec_offset);

    for (int i = 0; i < coff->NumberOfSections; i++) {
        const IMAGE_SECTION_HEADER* sec = &sections[i];

        char name[9];
        memcpy(name, sec->Name, 8);
        name[8] = 0;

        kprintf("[PE] Section '%s': VA=0x%X VSize=0x%X RawOff=0x%X RawSize=0x%X\n",
                      name, sec->VirtualAddress, sec->VirtualSize,
                      sec->PointerToRawData, sec->SizeOfRawData);

        if (sec->VirtualAddress + sec->VirtualSize > alloc_size) {
            kprintf("[PE] Section '%s' exceeds image bounds, skipping\n", name);
            continue;
        }

        if (sec->SizeOfRawData > 0 && sec->PointerToRawData > 0) {
            uint32_t copy_size = sec->SizeOfRawData;
            if (sec->PointerToRawData + copy_size > file_size)
                copy_size = file_size - sec->PointerToRawData;
            if (copy_size > sec->VirtualSize && sec->VirtualSize > 0)
                copy_size = sec->VirtualSize;
            memcpy(base + sec->VirtualAddress,
                   file_data + sec->PointerToRawData, copy_size);
        }
    }

    // --- Process base relocations (if image not loaded at preferred base) ---
    uint32_t delta = (uint32_t)base - image_base;
    if (delta != 0 && opt->NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_BASERELOC) {
        const IMAGE_DATA_DIRECTORY* reloc_dir = &opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (reloc_dir->VirtualAddress && reloc_dir->Size) {
            kprintf("[PE] Applying relocations (delta=0x%X)\n", delta);

            uint32_t reloc_offset = 0;
            while (reloc_offset < reloc_dir->Size) {
                const IMAGE_BASE_RELOCATION* block =
                    (const IMAGE_BASE_RELOCATION*)(base + reloc_dir->VirtualAddress + reloc_offset);

                if (block->SizeOfBlock == 0) break;

                uint32_t entry_count = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2;
                const uint16_t* entries = (const uint16_t*)((uint8_t*)block + sizeof(IMAGE_BASE_RELOCATION));

                for (uint32_t i = 0; i < entry_count; i++) {
                    uint16_t type = entries[i] >> 12;
                    uint16_t offset = entries[i] & 0xFFF;

                    if (type == IMAGE_REL_BASED_HIGHLOW) {
                        uint32_t* patch = (uint32_t*)(base + block->VirtualAddress + offset);
                        *patch += delta;
                    }
                    // IMAGE_REL_BASED_ABSOLUTE = padding, skip
                }

                reloc_offset += block->SizeOfBlock;
            }
        }
    }

    // --- Process imports ---
    if (opt->NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_IMPORT) {
        const IMAGE_DATA_DIRECTORY* import_dir = &opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (import_dir->VirtualAddress && import_dir->Size) {
            const IMAGE_IMPORT_DESCRIPTOR* imp =
                (const IMAGE_IMPORT_DESCRIPTOR*)(base + import_dir->VirtualAddress);

            while (imp->Name) {
                const char* dll_name = (const char*)(base + imp->Name);
                kprintf("[PE] Import DLL: %s\n", dll_name);

                uint32_t* thunk = (uint32_t*)(base + imp->FirstThunk);
                const uint32_t* orig_thunk = imp->OriginalFirstThunk ?
                    (const uint32_t*)(base + imp->OriginalFirstThunk) : thunk;

                for (int i = 0; orig_thunk[i]; i++) {
                    const char* func_name;
                    if (orig_thunk[i] & 0x80000000) {
                        // Import by ordinal - not supported
                        kprintf("[PE]   Ordinal #%d (unsupported)\n", orig_thunk[i] & 0xFFFF);
                        thunk[i] = 0;
                        continue;
                    }

                    const IMAGE_IMPORT_BY_NAME* hint_name =
                        (const IMAGE_IMPORT_BY_NAME*)(base + orig_thunk[i]);
                    func_name = hint_name->Name;

                    uint32_t addr = win32_resolve(dll_name, func_name);
                    if (addr) {
                        kprintf("[PE]   %s -> 0x%X\n", func_name, addr);
                        thunk[i] = addr;
                    } else {
                        kprintf("[PE]   %s -> UNRESOLVED\n", func_name);
                        thunk[i] = 0; // Will crash if called
                    }
                }

                imp++;
            }
        }
    }

    // --- Success ---
    res.success = true;
    res.image_base = base;
    res.image_size = alloc_size;
    res.entry_point = (uint32_t)base + opt->AddressOfEntryPoint;
    res.subsystem = opt->Subsystem;

    kprintf("[PE] Loaded at 0x%X, entry=0x%X\n",
                  (uint32_t)base, res.entry_point);

    return res;
}

void pe_unload(PELoadResult* result) {
    if (result->image_base) {
        kfree(result->image_base);
        result->image_base = nullptr;
        result->success = false;
    }
}

int pe_execute(PELoadResult* result) {
    if (!result->success || !result->entry_point) return -1;

    kprintf("[PE] Executing entry point at 0x%X\n", result->entry_point);

    // Reset Win32 shim state
    win32_shim_reset();

    // For CUI: entry point is main() or mainCRTStartup()
    // For GUI: entry point is WinMain() or WinMainCRTStartup()
    // Both use cdecl, we call with no args (simplified)
    typedef int (*EntryPoint)();
    EntryPoint entry = (EntryPoint)result->entry_point;

    int ret = entry();

    kprintf("[PE] Process exited with code %d\n", ret);
    return ret;
}
