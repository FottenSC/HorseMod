#pragma once
#include <cstdint>
#include <span>
#ifdef _WIN32
#include <array>
#include <cstring>
#include <Windows.h>
#endif

namespace Horse::Deterministic {
// Plan 1414F5500's sparse removals in private storage. Native publication still
// calls that function; this independently validates its resulting free chain.
// No mutation on rejected metadata/indices, and no allocation during planning.
inline bool PlanReplaySparseRemoval(std::span<std::uint64_t> slots,
    std::span<std::uint32_t> flags, std::int32_t& head, std::int32_t& free_count,
    std::span<const std::int32_t> removed) noexcept
{
    if (slots.size() > 65536 || flags.size() != (slots.size()+31)/32
        || free_count < 0 || static_cast<std::size_t>(free_count) > slots.size()) return false;
    const auto used = [&](std::size_t i) { return (flags[i/32] & (1u << (i%32))) != 0; };
    std::size_t vacant{};
    for (std::size_t i = 0; i < slots.size(); ++i) vacant += !used(i);
    if (vacant != static_cast<std::size_t>(free_count)) return false;
    auto cursor = head;
    std::int32_t previous = -1;
    for (std::int32_t i = 0; i < free_count; ++i) {
        if (cursor < 0 || static_cast<std::size_t>(cursor) >= slots.size() || used(cursor)
            || static_cast<std::int32_t>(slots[cursor]) != previous) return false;
        previous = cursor;
        cursor = static_cast<std::int32_t>(slots[cursor] >> 32);
    }
    if (cursor != -1) return false;
    for (std::size_t i = 0; i < removed.size(); ++i) {
        const auto index = removed[i];
        if (index < 0 || static_cast<std::size_t>(index) >= slots.size() || !used(index)) return false;
        for (std::size_t j = 0; j < i; ++j) if (removed[j] == index) return false;
    }
    for (const auto index : removed) {
        if (free_count) slots[head] = (slots[head] & 0xffffffff00000000ull) | static_cast<std::uint32_t>(index);
        slots[index] = 0xffffffffull | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(free_count ? head : -1)) << 32);
        head = index;
        ++free_count;
        flags[index/32] &= ~(1u << (index%32));
    }
    return true;
}

#ifdef _WIN32
// Owns the two native allocations behind the GPU emitter sparse registry.
// The caller owns render-thread admission, emitter lifetimes and render indices.
// This storage transaction alone never grants permission to execute simulation.
class ReplaySparseRegistryStorage final {
public:
    struct Heap {
        void* context{};
        void* (*allocate)(void*, std::size_t){};
        void (*release)(void*, void*){};
        std::size_t (*charge)(void*, std::size_t){};
    };
    ReplaySparseRegistryStorage() = default;
    ReplaySparseRegistryStorage(const ReplaySparseRegistryStorage&) = delete;
    ReplaySparseRegistryStorage& operator=(const ReplaySparseRegistryStorage&) = delete;
    ~ReplaySparseRegistryStorage() {
        // A live/unsettled graph can still be referenced by native work.
        // Abandonment retains it; it is never reported as successful cleanup.
        if (!published_) Free(current_);
    }
    std::size_t owned_bytes() const noexcept { return original_charge_ + (executing_ ? execution_ceiling_ : current_charge_); }
    bool pending() const noexcept { return published_; }
    bool settled() const noexcept { return !executing_ || settled_; }
    bool DiscardPreparation() noexcept {
        if (published_) return false;
        Free(current_); Reset(); return true;
    }
    bool Prepare(void* root, Heap heap, std::size_t budget) noexcept {
        if (root_ || !root || !heap.allocate || !heap.release || !heap.charge) return false;
        Image before{};
        if (!Read(root, before) || !Valid(root, before)) return false;
        const auto size = Charge(heap, before);
        if (size == SIZE_MAX || size > budget || size > budget - size) return false;
        heap_ = heap; root_ = root; original_ = before; current_ = before;
        original_charge_ = size; current_charge_ = 0;
        // Register each allocation before copying. Never free the borrowed B
        // backing if preparation of a later allocation fails.
        current_.data = current_.flags = nullptr;
        for (unsigned i = 0; i < 2; ++i) {
            const auto bytes = Extent(before, i);
            auto*& destination = i ? current_.flags : current_.data;
            const auto* source = i ? before.flags : before.data;
            if (!bytes) continue;
            destination = heap_.allocate(heap_.context, bytes);
            if (!destination || !Copy(destination, source, bytes)) { Free(current_); Reset(); return false; }
        }
        current_charge_ = size;
        if (!Disjoint(root, current_, original_) || !Fingerprint(original_, original_hash_)
            || !Fingerprint(current_, current_hash_)) { Free(current_); Reset(); return false; }
        return true;
    }
    bool Publish() noexcept {
        if (!root_ || published_ || !Same(root_, original_) || !Matches(original_, original_hash_)
            || !Matches(current_, current_hash_)) return false;
        published_ = true;
        write_complete_ = Copy(root_, &current_, sizeof(current_));
        return write_complete_ && Same(root_, current_);
    }
    bool BeginExecution(std::size_t current_storage_ceiling) noexcept {
        if (!published_ || executing_ || publication_write_ || !write_complete_ || !Same(root_, current_)
            || !Matches(current_, current_hash_) || !Matches(original_, original_hash_)
            || current_storage_ceiling < current_charge_ || current_storage_ceiling > SIZE_MAX - original_charge_) return false;
        executing_ = true; execution_ceiling_ = current_storage_ceiling;
        // Native registration can free/reallocate these addresses. Retain only
        // B ownership until settlement adopts the actual current graph.
        current_.data = current_.flags = nullptr;
        current_charge_ = 0;
        return true;
    }
    // Native sparse removal changes free links and flags in owned A storage.
    // It does not reallocate. Declare this bounded write before calling it so
    // an interrupted publication can recover B without requiring valid A data.
    bool BeginPublicationWrite() noexcept {
        if (!published_ || executing_ || publication_write_ || !write_complete_
            || !Same(root_, current_) || !Matches(current_, current_hash_) || !Matches(original_, original_hash_)) return false;
        publication_write_ = true; return true;
    }
    bool SealPublicationWrite() noexcept {
        Image actual{}; std::uint64_t hash{};
        if (!publication_write_ || !Read(root_, actual) || !SameAllocations(actual, current_)
            || !Valid(root_, actual) || !Matches(original_, original_hash_) || !Fingerprint(actual, hash)) return false;
        current_ = actual; current_hash_ = hash; publication_write_ = false; return true;
    }
    bool SettleExecution() noexcept {
        if (!executing_ || undo_started_ || !Matches(original_, original_hash_)) return false;
        Image actual{};
        if (!Read(root_, actual) || !Valid(root_, actual) || !Disjoint(root_, actual, original_)) return false;
        const auto charged = Charge(heap_, actual);
        std::uint64_t fingerprint{};
        if (charged == SIZE_MAX || charged > execution_ceiling_ || !Fingerprint(actual, fingerprint)) return false;
        current_ = actual; current_charge_ = charged; current_hash_ = fingerprint;
        settled_ = true;
        return true;
    }
    bool ContinueExecution() noexcept {
        if (!executing_ || !settled_ || undo_started_ || !published_ || publication_write_
            || !Same(root_, current_) || !Matches(current_, current_hash_)
            || !Matches(original_, original_hash_)) return false;
        // A late cancellation must retire C-only native owners before undo.
        // Relinquish their live registry backing again, preserving private B.
        current_.data = current_.flags = nullptr;
        current_charge_ = 0; settled_ = false;
        return true;
    }
    bool Undo() noexcept {
        if (!published_) return false;
        if ((executing_ && !settled_) || !Matches(original_, original_hash_)) return false;
        if (!undo_started_ && write_complete_) {
            Image live{};
            if (publication_write_ ? (!Read(root_, live) || !SameAllocations(live, current_))
                : (!Same(root_, current_) || !Matches(current_, current_hash_))) return false;
        }
        undo_started_ = true;
        if (!Copy(root_, &original_, sizeof(original_)) || !Same(root_, original_) || !Matches(original_, original_hash_)) return false;
        Free(current_); // B is independently verified and native-owned again.
        Reset(); return true;
    }
    bool Commit() noexcept {
        if (!published_ || undo_started_ || publication_write_ || (executing_ && !settled_) || !write_complete_
            || !Same(root_, current_) || !Matches(current_, current_hash_) || !Matches(original_, original_hash_)) return false;
        Free(original_); // The caller has made its enclosing success decision.
        Reset(); return true;
    }
private:
    struct Image {
        void* data{}; std::int32_t count{}, capacity{};
        std::array<std::uint32_t, 4> inline_flags{};
        void* flags{}; std::int32_t bits{}, max_bits{}, head{}, free_count{};
    };
    static_assert(sizeof(Image) == 0x38);
    Heap heap_{}; void* root_{};
    Image original_{}, current_{};
    std::size_t original_charge_{}, current_charge_{}, execution_ceiling_{};
    std::uint64_t original_hash_{}, current_hash_{};
    bool published_{}, write_complete_{}, executing_{}, settled_{}, undo_started_{}, publication_write_{};
    static bool Copy(void* destination, const void* source, std::size_t size) noexcept {
        __try { if (size) std::memcpy(destination, source, size); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    static bool Read(const void* root, Image& image) noexcept { return Copy(&image, root, sizeof(image)); }
    static bool Same(const void* root, const Image& image) noexcept {
        Image live{}; return Read(root, live) && !std::memcmp(&live, &image, sizeof(image));
    }
    static bool SameAllocations(const Image& a, const Image& b) noexcept {
        return a.data == b.data && a.capacity == b.capacity && a.flags == b.flags && a.max_bits == b.max_bits;
    }
    static std::size_t Extent(const Image& image, unsigned index) noexcept {
        return index ? (image.flags ? (std::size_t(image.max_bits) + 31) / 32 * 4 : 0) : std::size_t(image.capacity) * 8;
    }
    static bool Overlap(const void* a, std::size_t n, const void* b, std::size_t m) noexcept {
        const auto x = reinterpret_cast<std::uintptr_t>(a), y = reinterpret_cast<std::uintptr_t>(b);
        return n && m && (n > UINTPTR_MAX - x || m > UINTPTR_MAX - y || (x < y + m && y < x + n));
    }
    static bool Valid(const void* root, const Image& image) noexcept {
        if (image.count < 0 || image.capacity < image.count || image.capacity > 65536
            || image.bits != image.count || image.max_bits < image.bits || image.max_bits > 65536
            || image.free_count < 0 || image.free_count > image.count
            || bool(image.data) != bool(image.capacity) || (!image.flags && image.max_bits > 128)
            || (image.flags && !image.max_bits)
            || Overlap(image.data, Extent(image, 0), root, sizeof(Image))
            || Overlap(image.flags, Extent(image, 1), root, sizeof(Image))
            || Overlap(image.data, Extent(image, 0), image.flags, Extent(image, 1))) return false;
        __try {
            const auto* slots = static_cast<const std::uint64_t*>(image.data);
            const auto* flags = image.flags ? static_cast<const std::uint32_t*>(image.flags) : image.inline_flags.data();
            int vacant{};
            for (int i = 0; i < image.count; ++i) {
                const bool occupied = (flags[i / 32] & (1u << (i % 32))) != 0;
                if (occupied && !slots[i]) return false;
                vacant += !occupied;
            }
            if (vacant != image.free_count) return false;
            int cursor = image.head, previous = -1;
            for (int i = 0; i < image.free_count; ++i) {
                if (cursor < 0 || cursor >= image.count || (flags[cursor / 32] & (1u << (cursor % 32)))
                    || static_cast<std::int32_t>(slots[cursor]) != previous) return false;
                previous = cursor; cursor = static_cast<std::int32_t>(slots[cursor] >> 32);
            }
            return cursor == -1;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    static bool Disjoint(const void* root, const Image& current, const Image& original) noexcept {
        if (!Valid(root, current)) return false;
        for (unsigned i = 0; i < 2; ++i) for (unsigned j = 0; j < 2; ++j)
            if (Overlap(i ? current.flags : current.data, Extent(current, i),
                j ? original.flags : original.data, Extent(original, j))) return false;
        return true;
    }
    static std::size_t Charge(Heap heap, const Image& image) noexcept {
        std::size_t sum{};
        for (unsigned i = 0; i < 2; ++i) {
            const auto bytes = Extent(image, i), charge = bytes ? heap.charge(heap.context, bytes) : 0;
            if (charge < bytes || charge > SIZE_MAX - sum) return SIZE_MAX;
            sum += charge;
        }
        return sum;
    }
    static bool Fingerprint(const Image& image, std::uint64_t& hash) noexcept {
        hash = 14695981039346656037ull;
        __try {
            for (unsigned i = 0; i < 2; ++i) {
                const auto* bytes = static_cast<const unsigned char*>(i ? image.flags : image.data);
                for (std::size_t j = 0; j < Extent(image, i); ++j) { hash ^= bytes[j]; hash *= 1099511628211ull; }
            }
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    static bool Matches(const Image& image, std::uint64_t expected) noexcept {
        std::uint64_t actual{}; return Fingerprint(image, actual) && actual == expected;
    }
    void Free(Image& image) noexcept {
        if (image.flags) heap_.release(heap_.context, image.flags);
        if (image.data) heap_.release(heap_.context, image.data);
        image.data = image.flags = nullptr;
    }
    void Reset() noexcept {
        root_ = nullptr; original_ = {}; current_ = {};
        original_charge_ = current_charge_ = execution_ceiling_ = 0;
        original_hash_ = current_hash_ = 0;
        published_ = write_complete_ = executing_ = settled_ = undo_started_ = publication_write_ = false;
    }
};
#endif
}
