#pragma once
#include <array>
#include <cstdint>

namespace Horse::Deterministic {
// Owned allocation identity and immutable comparison bytes. Contains no
// native services so the deterministic core does not depend on UE4SS hooks.
struct ReplayVfxFinishBinding {
    struct Header {
        std::uintptr_t data{};std::int32_t count{},capacity{};
        friend bool operator==(const Header&,const Header&)=default;
    };
    struct Row {
        std::array<std::int32_t,2> weak{};std::uint64_t name{};
        friend bool operator==(const Row&,const Row&)=default;
    };
    std::uintptr_t base{},manager{};
    std::array<std::int32_t,2> manager_weak{};
    std::uint64_t epoch{};
    Header header{}; // PreparedManager's allocation, never a live census.
    std::array<Row,2> rows{}; // Captured order and both FName words.
};
static_assert(sizeof(ReplayVfxFinishBinding::Header)==16 && sizeof(ReplayVfxFinishBinding::Row)==16);
}
