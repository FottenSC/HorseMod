#pragma once

#include "Types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Horse::Deterministic
{
inline constexpr std::size_t hgcpu_stream_capacity = 0x28018;

struct HgCpuGenerationContext
{
    std::uint64_t build_id{};
    std::uint64_t schema_id{};
    std::uint64_t session_generation{};
    std::uint64_t round_generation{};
    std::uint64_t fighter_generations[2]{};
    std::uint64_t camera_generation{};

    friend bool operator==(
        const HgCpuGenerationContext&,
        const HgCpuGenerationContext&) = default;
};

struct HgCpuLocalImage
{
    HgCpuGenerationContext context{};
    std::size_t cursor{};
    std::uint64_t checksum{};
    std::vector<std::byte> bytes;
};

class HgCpuStreamShim;
using HgCpuExecFn = void* (__fastcall*)(HgCpuStreamShim*);

class HgCpuStreamShim
{
public:
    HgCpuStreamShim() noexcept;

    Status Capture(
        HgCpuExecFn writer,
        const HgCpuGenerationContext& context,
        HgCpuLocalImage& output) noexcept;
    Status Restore(
        HgCpuExecFn reader,
        const HgCpuGenerationContext& current,
        const HgCpuLocalImage& image) noexcept;

private:
    using DtorFn = void (__fastcall*)(HgCpuStreamShim*);
    using BeginFn = void (__fastcall*)(HgCpuStreamShim*, std::int64_t);
    using TransferFn = std::int64_t (__fastcall*)(
        HgCpuStreamShim*, void*, std::size_t);
    using CursorFn = std::int64_t (__fastcall*)(HgCpuStreamShim*);
    using ValidateFn = std::int32_t (__fastcall*)(HgCpuStreamShim*);

    struct VTable
    {
        DtorFn dtor1;
        DtorFn dtor2;
        DtorFn init;
        BeginFn begin_write;
        BeginFn begin_read;
        TransferFn write;
        TransferFn read;
        CursorFn get_cursor;
        ValidateFn validate;
    };

    void Retarget(std::byte* data, std::size_t capacity) noexcept;
    static std::uint64_t Checksum(const HgCpuLocalImage& image) noexcept;
    static bool ValidContext(const HgCpuGenerationContext& context) noexcept;

    static void __fastcall Dtor(HgCpuStreamShim*) noexcept;
    static void __fastcall Init(HgCpuStreamShim* self) noexcept;
    static void __fastcall Begin(
        HgCpuStreamShim* self, std::int64_t offset) noexcept;
    static std::int64_t __fastcall Write(
        HgCpuStreamShim* self, void* source, std::size_t bytes) noexcept;
    static std::int64_t __fastcall Read(
        HgCpuStreamShim* self, void* destination, std::size_t bytes) noexcept;
    static std::int64_t __fastcall Cursor(HgCpuStreamShim* self) noexcept;
    static std::int32_t __fastcall Validate(HgCpuStreamShim* self) noexcept;

    static const VTable vtable_;
    const VTable* vtable_pointer_;
    std::byte* data_{};
    std::size_t capacity_{};
    std::size_t cursor_{};
    bool overflow_{};
};
}
