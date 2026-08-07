#include "PeImportSlotResolver.hpp"

#include <array>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr size_t kImageSize = 0x1000;
    constexpr size_t kNtOffset = 0x80;
    constexpr size_t kImportDirectory = 0x200;
    constexpr size_t kDllName = 0x300;
    constexpr size_t kLookup = 0x400;
    constexpr size_t kIat = 0x500;
    constexpr size_t kImportName = 0x600;
    constexpr uintptr_t kTarget = 0x123456789ABCDEF0ull;

    std::array<uint8_t, kImageSize> MakeImage()
    {
        std::array<uint8_t, kImageSize> image {};
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image.data());
        dos->e_magic = IMAGE_DOS_SIGNATURE;
        dos->e_lfanew = static_cast<LONG>(kNtOffset);
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(
            image.data() + kNtOffset);
        nt->Signature = IMAGE_NT_SIGNATURE;
        nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        nt->OptionalHeader.SizeOfImage = static_cast<DWORD>(image.size());
        nt->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]
            .VirtualAddress = static_cast<DWORD>(kImportDirectory);
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]
            .Size = 2 * sizeof(IMAGE_IMPORT_DESCRIPTOR);
        auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
            image.data() + kImportDirectory);
        descriptor->Name = static_cast<DWORD>(kDllName);
        descriptor->OriginalFirstThunk = static_cast<DWORD>(kLookup);
        descriptor->FirstThunk = static_cast<DWORD>(kIat);
        constexpr char dll[] = "api-ms-win-crt-utility-l1-1-0.dll";
        std::memcpy(image.data() + kDllName, dll, sizeof(dll));
        auto* lookup = reinterpret_cast<IMAGE_THUNK_DATA64*>(
            image.data() + kLookup);
        lookup[0].u1.AddressOfData = kImportName;
        lookup[1].u1.AddressOfData = 0;
        auto* import_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
            image.data() + kImportName);
        import_name->Hint = 0;
        constexpr char name[] = "rand";
        std::memcpy(import_name->Name, name, sizeof(name));
        void* target = reinterpret_cast<void*>(kTarget);
        std::memcpy(image.data() + kIat, &target, sizeof(target));
        return image;
    }
}

int main()
{
    auto image = MakeImage();
    const auto found = Horse::ResolvePeImportSlot(
        image.data(), image.size(),
        "API-MS-WIN-CRT-UTILITY-L1-1-0.DLL", "rand");
    if (!found || found.slot != reinterpret_cast<void**>(
            image.data() + kIat)
        || found.target != reinterpret_cast<void*>(kTarget)) return 1;
    if (Horse::ResolvePeImportSlot(
            image.data(), image.size(), "wrong.dll", "rand")) return 2;
    if (Horse::ResolvePeImportSlot(
            image.data(), image.size(),
            "api-ms-win-crt-utility-l1-1-0.dll", "srand")) return 3;

    auto bad_magic = image;
    reinterpret_cast<IMAGE_DOS_HEADER*>(bad_magic.data())->e_magic = 0;
    if (Horse::ResolvePeImportSlot(
            bad_magic.data(), bad_magic.size(),
            "api-ms-win-crt-utility-l1-1-0.dll", "rand")) return 4;

    auto bad_lookup = image;
    reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
        bad_lookup.data() + kImportDirectory)->OriginalFirstThunk =
            static_cast<DWORD>(bad_lookup.size() - 1);
    if (Horse::ResolvePeImportSlot(
            bad_lookup.data(), bad_lookup.size(),
            "api-ms-win-crt-utility-l1-1-0.dll", "rand")) return 5;

    auto ordinal = image;
    reinterpret_cast<IMAGE_THUNK_DATA64*>(
        ordinal.data() + kLookup)->u1.Ordinal =
            IMAGE_ORDINAL_FLAG64 | 7;
    if (Horse::ResolvePeImportSlot(
            ordinal.data(), ordinal.size(),
            "api-ms-win-crt-utility-l1-1-0.dll", "rand")) return 6;

    std::puts("pe-import-slot-resolver-selftest: ok");
    return 0;
}
