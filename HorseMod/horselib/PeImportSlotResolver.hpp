#pragma once

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    struct PeImportSlot
    {
        void** slot {nullptr};
        void* target {nullptr};

        explicit operator bool() const noexcept
        {
            return slot != nullptr && target != nullptr;
        }
    };

    inline bool PeAsciiEqualsInsensitive(
        const char* lhs, const char* rhs, size_t max_length) noexcept
    {
        if (!lhs || !rhs || max_length == 0) return false;
        for (size_t index = 0; index < max_length; ++index)
        {
            const unsigned char left =
                static_cast<unsigned char>(lhs[index]);
            const unsigned char right =
                static_cast<unsigned char>(rhs[index]);
            if (std::tolower(left) != std::tolower(right)) return false;
            if (left == 0) return true;
        }
        return false;
    }

    inline bool PeAsciiEquals(
        const char* lhs, const char* rhs, size_t max_length) noexcept
    {
        if (!lhs || !rhs || max_length == 0) return false;
        for (size_t index = 0; index < max_length; ++index)
        {
            if (lhs[index] != rhs[index]) return false;
            if (lhs[index] == 0) return true;
        }
        return false;
    }

    inline PeImportSlot ResolvePeImportSlot(
        void* mapped_image,
        size_t mapped_size,
        const char* imported_dll,
        const char* imported_name) noexcept
    {
        PeImportSlot result {};
        if (!mapped_image || !imported_dll || !imported_name
            || mapped_size < sizeof(IMAGE_DOS_HEADER))
            return result;

        auto* const base = static_cast<uint8_t*>(mapped_image);
        const auto* const dos =
            reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0)
            return result;
        const size_t nt_offset = static_cast<size_t>(dos->e_lfanew);
        if (nt_offset > mapped_size
            || mapped_size - nt_offset < sizeof(IMAGE_NT_HEADERS64))
            return result;
        const auto* const nt =
            reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + nt_offset);
        if (nt->Signature != IMAGE_NT_SIGNATURE
            || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
            || nt->OptionalHeader.NumberOfRvaAndSizes
                <= IMAGE_DIRECTORY_ENTRY_IMPORT)
            return result;

        const size_t image_size = (std::min)(
            mapped_size,
            static_cast<size_t>(nt->OptionalHeader.SizeOfImage));
        const IMAGE_DATA_DIRECTORY& directory =
            nt->OptionalHeader.DataDirectory[
                IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (directory.VirtualAddress == 0
            || directory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR)
            || directory.VirtualAddress >= image_size)
            return result;
        const size_t directory_end = (std::min)(
            image_size,
            static_cast<size_t>(directory.VirtualAddress)
                + static_cast<size_t>(directory.Size));

        for (size_t descriptor_offset = directory.VirtualAddress;
             descriptor_offset + sizeof(IMAGE_IMPORT_DESCRIPTOR)
                <= directory_end;
             descriptor_offset += sizeof(IMAGE_IMPORT_DESCRIPTOR))
        {
            const auto* const descriptor =
                reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
                    base + descriptor_offset);
            if (descriptor->Name == 0 && descriptor->FirstThunk == 0
                && descriptor->OriginalFirstThunk == 0)
                break;
            if (descriptor->Name >= image_size
                || descriptor->FirstThunk >= image_size
                || descriptor->OriginalFirstThunk == 0
                || descriptor->OriginalFirstThunk >= image_size)
                continue;
            const char* const dll_name = reinterpret_cast<const char*>(
                base + descriptor->Name);
            if (!PeAsciiEqualsInsensitive(
                    dll_name, imported_dll, image_size - descriptor->Name))
                continue;

            for (size_t index = 0;; ++index)
            {
                const size_t lookup_offset =
                    static_cast<size_t>(descriptor->OriginalFirstThunk)
                    + index * sizeof(IMAGE_THUNK_DATA64);
                const size_t slot_offset =
                    static_cast<size_t>(descriptor->FirstThunk)
                    + index * sizeof(IMAGE_THUNK_DATA64);
                if (lookup_offset > image_size
                    || image_size - lookup_offset
                        < sizeof(IMAGE_THUNK_DATA64)
                    || slot_offset > image_size
                    || image_size - slot_offset
                        < sizeof(IMAGE_THUNK_DATA64))
                    return result;
                const auto* const lookup =
                    reinterpret_cast<const IMAGE_THUNK_DATA64*>(
                        base + lookup_offset);
                if (lookup->u1.AddressOfData == 0) return result;
                if (IMAGE_SNAP_BY_ORDINAL64(lookup->u1.Ordinal)) continue;
                const size_t name_offset = static_cast<size_t>(
                    lookup->u1.AddressOfData);
                if (name_offset > image_size
                    || image_size - name_offset
                        <= offsetof(IMAGE_IMPORT_BY_NAME, Name))
                    return result;
                const auto* const by_name =
                    reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                        base + name_offset);
                const size_t name_capacity = image_size - name_offset
                    - offsetof(IMAGE_IMPORT_BY_NAME, Name);
                if (!PeAsciiEquals(
                        reinterpret_cast<const char*>(by_name->Name),
                        imported_name, name_capacity))
                    continue;
                auto** const slot = reinterpret_cast<void**>(
                    base + slot_offset);
                result.slot = slot;
                std::memcpy(&result.target, slot, sizeof(result.target));
                return result;
            }
        }
        return result;
    }
}
