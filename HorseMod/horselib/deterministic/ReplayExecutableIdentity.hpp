#pragma once
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <filesystem>

namespace Horse::Deterministic {
// The in-process session entry is limited to the audited retail executable.
// Existing native function/binding checks still apply after this file check.
inline bool VerifiedReplayExecutable() noexcept
{
    static const bool verified=[]() noexcept {
        try {
            std::array<wchar_t,1024> name{};
            const auto length=GetModuleFileNameW(nullptr,name.data(),static_cast<DWORD>(name.size()));
            if(!length || length>=name.size()) return false;
            std::ifstream source(std::filesystem::path(name.data()),std::ios::binary);
            if(!source) return false;
            struct Hash {BCRYPT_HASH_HANDLE value{};~Hash(){if(value)BCryptDestroyHash(value);}} hash;
            if(!BCRYPT_SUCCESS(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,&hash.value,nullptr,0,nullptr,0,0))) return false;
            std::array<unsigned char,16384> buffer{};
            while(source) {
                source.read(reinterpret_cast<char*>(buffer.data()),buffer.size());
                const auto count=source.gcount();
                if(count && !BCRYPT_SUCCESS(BCryptHashData(hash.value,buffer.data(),static_cast<ULONG>(count),0))) return false;
            }
            if(!source.eof() || source.bad()) return false;
            std::array<unsigned char,32> digest{};
            if(!BCRYPT_SUCCESS(BCryptFinishHash(hash.value,digest.data(),digest.size(),0))) return false;
            constexpr std::array<unsigned char,32> expected{
                0xf8,0x90,0x4e,0x4b,0x04,0xbc,0xa3,0xb4,0x7b,0xc5,0x2a,0x68,0x3f,0x61,0x90,0x36,
                0x5d,0x2e,0xb8,0x9e,0xe8,0xf4,0x4f,0x80,0x72,0x75,0x9e,0x9c,0x5e,0x04,0xa5,0x53};
            return digest==expected;
        } catch(...) {return false;}
    }();
    return verified;
}
}
