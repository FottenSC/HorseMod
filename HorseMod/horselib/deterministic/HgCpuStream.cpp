#include "HgCpuStream.hpp"
#include "LocalImageChecksum.hpp"

#include <bit>
#include <cstring>

#if defined(_MSC_VER)
#include <Windows.h>
#endif

namespace Horse::Deterministic
{
namespace
{
bool invoke_exec(HgCpuExecFn function, HgCpuStreamShim* shim, void*& result) noexcept
{
#if defined(_MSC_VER)
    __try
    {
        result = function(shim);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    try
    {
        result = function(shim);
        return true;
    }
    catch (...)
    {
        return false;
    }
#endif
}
}

const HgCpuStreamShim::VTable HgCpuStreamShim::vtable_{
    &HgCpuStreamShim::Dtor,
    &HgCpuStreamShim::Dtor,
    &HgCpuStreamShim::Init,
    &HgCpuStreamShim::Begin,
    &HgCpuStreamShim::Begin,
    &HgCpuStreamShim::Write,
    &HgCpuStreamShim::Read,
    &HgCpuStreamShim::Cursor,
    &HgCpuStreamShim::Validate,
};

HgCpuStreamShim::HgCpuStreamShim() noexcept
    : vtable_pointer_(&vtable_)
{
}

void HgCpuStreamShim::Retarget(std::byte* data, std::size_t capacity) noexcept
{
    data_ = data;
    capacity_ = capacity;
    cursor_ = 0;
    overflow_ = false;
}

bool HgCpuStreamShim::ValidContext(const HgCpuGenerationContext& context) noexcept
{
    return context.build_id != 0 && context.schema_id != 0
        && context.session_generation != 0 && context.round_generation != 0
        && context.fighter_generations[0] != 0
        && context.fighter_generations[1] != 0
        && context.stage_generation != 0
        && context.camera_generation != 0
        && context.allocation_generation != 0;
}

std::uint64_t HgCpuStreamShim::Checksum(const HgCpuLocalImage& image) noexcept
{
    LocalImageChecksum checksum;
    checksum.Add(&image.serializer_id, sizeof(image.serializer_id));
    checksum.Add(&image.serializer_version, sizeof(image.serializer_version));
    checksum.Add(&image.context, sizeof(image.context));
    checksum.Add(&image.cursor, sizeof(image.cursor));
    checksum.Add(image.bytes.data(), image.bytes.size());
    return checksum.Finish();
}

Status HgCpuStreamShim::Capture(
    HgCpuExecFn writer,
    const HgCpuGenerationContext& context,
    HgCpuLocalImage& output,
    HgCpuWriteTrace* trace) noexcept
{
    output.serializer_id = LocalSerializerId::HgCpuDirect;
    output.serializer_version = hgcpu_direct_serializer_version;
    output.context = {};
    output.cursor = 0;
    output.checksum = 0;
    if (trace != nullptr)
    {
        trace->count = 0;
        trace->truncated = false;
    }
    if (writer == nullptr || !ValidContext(context))
        return Status::failure(FailureCode::ContextUnavailable);
    try { output.bytes.resize(hgcpu_stream_capacity); }
    catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
    Retarget(output.bytes.data(), output.bytes.size());
    trace_ = trace;
    void* result = nullptr;
    if (!invoke_exec(writer, this, result))
    {
        Retarget(nullptr, 0);
        trace_ = nullptr;
        return Status::failure(FailureCode::CaptureFailed);
    }
    if (result != this || overflow_ || cursor_ == 0
        || cursor_ > output.bytes.size())
    {
        const bool overflowed = overflow_;
        Retarget(nullptr, 0);
        trace_ = nullptr;
        return Status::failure(
            overflowed ? FailureCode::CapacityExceeded : FailureCode::CaptureFailed);
    }
    output.bytes.resize(cursor_);
    output.context = context;
    output.serializer_id = LocalSerializerId::HgCpuDirect;
    output.serializer_version = hgcpu_direct_serializer_version;
    output.cursor = cursor_;
    output.checksum = Checksum(output);
    Retarget(nullptr, 0);
    trace_ = nullptr;
    return Status::success();
}

Status HgCpuStreamShim::Restore(
    HgCpuExecFn reader,
    const HgCpuGenerationContext& current,
    const HgCpuLocalImage& image) noexcept
{
    if (reader == nullptr || !ValidContext(current)
        || image.serializer_id != LocalSerializerId::HgCpuDirect
        || image.serializer_version != hgcpu_direct_serializer_version
        || current != image.context || image.cursor == 0
        || image.cursor != image.bytes.size()
        || image.cursor > hgcpu_stream_capacity
        || image.checksum != Checksum(image))
    {
        return Status::failure(FailureCode::RestorePreflightFailed);
    }
    Retarget(const_cast<std::byte*>(image.bytes.data()), image.bytes.size());
    trace_ = nullptr;
    void* result = nullptr;
    if (!invoke_exec(reader, this, result))
    {
        Retarget(nullptr, 0);
        return Status::failure(FailureCode::RestoreWriteFailed);
    }
    const bool valid = result == this && !overflow_ && cursor_ == image.cursor;
    Retarget(nullptr, 0);
    return valid
        ? Status::success()
        : Status::failure(FailureCode::RestoreVerificationFailed);
}

void __fastcall HgCpuStreamShim::Dtor(HgCpuStreamShim*) noexcept
{
}

void __fastcall HgCpuStreamShim::Init(HgCpuStreamShim* self) noexcept
{
    if (self != nullptr)
    {
        self->cursor_ = 0;
        self->overflow_ = false;
    }
}

void __fastcall HgCpuStreamShim::Begin(
    HgCpuStreamShim* self, std::int64_t offset) noexcept
{
    if (self == nullptr) return;
    self->cursor_ = offset > 0 ? static_cast<std::size_t>(offset) : 0;
    self->overflow_ = self->cursor_ > self->capacity_;
}

std::int64_t __fastcall HgCpuStreamShim::Write(
    HgCpuStreamShim* self, void* source, std::size_t bytes) noexcept
{
    if (self == nullptr || self->data_ == nullptr || source == nullptr)
        return 0;
    if (self->cursor_ > self->capacity_ || bytes > self->capacity_ - self->cursor_)
    {
        self->overflow_ = true;
        return 0;
    }
    const auto previous = self->cursor_;
    if (self->trace_ != nullptr)
    {
        auto& trace = *self->trace_;
        if (trace.count < trace.storage.size())
        {
            trace.storage[trace.count++] = {
                reinterpret_cast<std::uintptr_t>(source), previous, bytes};
        }
        else
        {
            trace.truncated = true;
        }
    }
    std::memcpy(self->data_ + self->cursor_, source, bytes);
    self->cursor_ += bytes;
    return static_cast<std::int64_t>(previous);
}

std::int64_t __fastcall HgCpuStreamShim::Read(
    HgCpuStreamShim* self, void* destination, std::size_t bytes) noexcept
{
    if (self == nullptr || self->data_ == nullptr || destination == nullptr)
        return 0;
    if (self->cursor_ > self->capacity_ || bytes > self->capacity_ - self->cursor_)
    {
        self->overflow_ = true;
        return 0;
    }
    const auto previous = self->cursor_;
    std::memcpy(destination, self->data_ + self->cursor_, bytes);
    self->cursor_ += bytes;
    return static_cast<std::int64_t>(previous);
}

std::int64_t __fastcall HgCpuStreamShim::Cursor(HgCpuStreamShim* self) noexcept
{
    return self == nullptr ? 0 : static_cast<std::int64_t>(self->cursor_);
}

std::int32_t __fastcall HgCpuStreamShim::Validate(HgCpuStreamShim* self) noexcept
{
    return self != nullptr && !self->overflow_ ? 1 : 0;
}

bool HgCpuStreamShim::ValidateLocalImage(const HgCpuLocalImage& image) noexcept
{
    return ValidateLocalImageMetadata(image)
        && image.checksum == Checksum(image);
}

bool HgCpuStreamShim::ValidateLocalImageMetadata(
    const HgCpuLocalImage& image) noexcept
{
    return image.serializer_id == LocalSerializerId::HgCpuDirect
        && image.serializer_version == hgcpu_direct_serializer_version
        && ValidContext(image.context) && image.cursor != 0
        && image.cursor == image.bytes.size()
        && image.cursor <= hgcpu_stream_capacity
        && image.checksum != 0;
}
}
