// ============================================================================
// Horse::RollbackBetaConfig
//
// Strict persistent configuration for the public rollback beta. Version 3
// defaults to SC6's Steam P2P session, so users do not exchange IP addresses,
// forward ports, or provision a shared gameplay secret. Version 1 remains a
// read-only compatibility format for explicit direct-UDP deployments;
// enabled version-2 Steam profiles fail with an upgrade-required error.
// ============================================================================

#pragma once

#include "RollbackRuntimePolicy.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace Horse
{
    static constexpr uint32_t kRollbackBetaLegacyConfigVersion = 1;
    static constexpr uint32_t kRollbackBetaObsoleteSteamConfigVersion = 2;
    static constexpr uint32_t kRollbackBetaConfigVersion = 3;
    static constexpr uint16_t kRollbackBetaDefaultPort = 47170;

    enum class RollbackBetaRole : uint8_t
    {
        Invalid,
        Host,
        Guest,
    };

    enum class RollbackBetaTransport : uint8_t
    {
        Invalid,
        SteamP2P,
        DirectUdp,
    };

    static constexpr const char* RollbackBetaTransportName(
        RollbackBetaTransport transport) noexcept
    {
        switch (transport)
        {
        case RollbackBetaTransport::SteamP2P: return "steam-p2p";
        case RollbackBetaTransport::DirectUdp: return "direct-udp";
        case RollbackBetaTransport::Invalid: break;
        }
        return "invalid";
    }

    static constexpr const char* RollbackBetaRoleName(
        RollbackBetaRole role) noexcept
    {
        switch (role)
        {
        case RollbackBetaRole::Host: return "host";
        case RollbackBetaRole::Guest: return "guest";
        case RollbackBetaRole::Invalid: break;
        }
        return "invalid";
    }

    struct RollbackBetaConfig
    {
        uint32_t config_version {0};
        bool enabled {false};
        bool trace {false};
        RollbackBetaTransport transport {RollbackBetaTransport::Invalid};
        RollbackBetaRole role {RollbackBetaRole::Invalid};
        std::string bind_address {"0.0.0.0"};
        uint16_t bind_port {kRollbackBetaDefaultPort};
        std::string peer_address;
        uint16_t peer_port {kRollbackBetaDefaultPort};
        std::string secret;
        uint16_t rollback_window {12};
        uint16_t input_delay {1};
        RollbackSavePolicy save_policy {
            RollbackSavePolicy::ConfirmedSpeculative};
        RollbackLeadPacingConfig lead_pacing {};
        const char* failure {"ok"};

        bool valid() const noexcept
        {
            const bool common = enabled
                && rollback_window != 0
                && rollback_window <= 60
                && input_delay != 0
                && input_delay <= rollback_window;
            if (!common) return false;
            if (config_version == kRollbackBetaConfigVersion
                && (save_policy
                        != RollbackSavePolicy::ConfirmedSpeculative
                    || !lead_pacing.enabled
                    || !lead_pacing.valid()))
                return false;
            if (config_version == kRollbackBetaConfigVersion
                && transport == RollbackBetaTransport::SteamP2P)
            {
                return save_policy
                        == RollbackSavePolicy::ConfirmedSpeculative
                    && lead_pacing.enabled
                    && lead_pacing.valid();
            }
            const bool direct =
                (config_version == kRollbackBetaLegacyConfigVersion
                    && transport == RollbackBetaTransport::Invalid)
                || (config_version == kRollbackBetaConfigVersion
                    && transport == RollbackBetaTransport::DirectUdp);
            return direct
                && (role == RollbackBetaRole::Host
                    || role == RollbackBetaRole::Guest)
                && !bind_address.empty()
                && bind_port != 0
                && valid_peer_name(peer_address)
                && peer_port != 0
                && valid_secret(secret);
        }

        uint8_t local_player_slot() const noexcept
        {
            // Verified stock private-room ownership: the invited player is
            // native/active slot 0 and the room creator is passive slot 1.
            // Lobby ownership must not be mistaken for fighter-slot order.
            return role == RollbackBetaRole::Guest ? 0u : 1u;
        }

        uint8_t local_peer() const noexcept
        {
            return role == RollbackBetaRole::Guest ? 2u : 1u;
        }

        uint8_t remote_peer() const noexcept
        {
            return role == RollbackBetaRole::Guest ? 1u : 2u;
        }

        static bool valid_peer_name(const std::string& value) noexcept
        {
            if (value.empty() || value.size() > 253) return false;
            for (const unsigned char c : value)
            {
                if ((c >= 'a' && c <= 'z')
                    || (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9')
                    || c == '.' || c == '-')
                    continue;
                return false;
            }
            return true;
        }

        static bool valid_secret(const std::string& value) noexcept
        {
            // Canonical 256-bit HMAC key. The beta profile tool generates
            // all bytes with the OS CSPRNG; the diversity floor also rejects
            // obvious repeated or low-entropy operator-provided values.
            if (value.size() != 64) return false;
            bool seen[256] {};
            size_t distinct = 0;
            for (size_t i = 0; i < value.size(); i += 2)
            {
                auto nibble = [](char c) noexcept -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                const int high = nibble(value[i]);
                const int low = nibble(value[i + 1]);
                if (high < 0 || low < 0) return false;
                const uint8_t byte =
                    static_cast<uint8_t>((high << 4) | low);
                if (!seen[byte])
                {
                    seen[byte] = true;
                    ++distinct;
                }
            }
            return distinct >= 16;
        }
    };

    static inline std::string RollbackBetaTrimAscii(std::string value)
    {
        auto space = [](unsigned char c) noexcept {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        };
        while (!value.empty()
            && space(static_cast<unsigned char>(value.back())))
            value.pop_back();
        size_t first = 0;
        while (first < value.size()
            && space(static_cast<unsigned char>(value[first])))
            ++first;
        if (first != 0) value.erase(0, first);
        return value;
    }

    static inline std::string RollbackBetaLowerAscii(std::string value)
    {
        for (char& c : value)
        {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + ('a' - 'A'));
        }
        return value;
    }

    static inline bool RollbackBetaParseBool(
        const std::string& value,
        bool& out) noexcept
    {
        const std::string normalized =
            RollbackBetaLowerAscii(RollbackBetaTrimAscii(value));
        if (normalized == "1" || normalized == "true"
            || normalized == "yes" || normalized == "on")
        {
            out = true;
            return true;
        }
        if (normalized == "0" || normalized == "false"
            || normalized == "no" || normalized == "off")
        {
            out = false;
            return true;
        }
        return false;
    }

    static inline bool RollbackBetaParseU16(
        const std::string& value,
        uint16_t& out) noexcept
    {
        if (value.empty()) return false;
        char* end = nullptr;
        const unsigned long parsed =
            std::strtoul(value.c_str(), &end, 10);
        if (end == value.c_str() || *end != '\0'
            || parsed == 0 || parsed > UINT16_MAX)
            return false;
        out = static_cast<uint16_t>(parsed);
        return true;
    }

    static inline bool RollbackBetaParseFloat(
        const std::string& value, float& out) noexcept
    {
        if (value.empty()) return false;
        char* end = nullptr;
        const float parsed = std::strtof(value.c_str(), &end);
        if (end == value.c_str() || *end != '\0'
            || !std::isfinite(parsed))
            return false;
        out = parsed;
        return true;
    }

    static inline bool ParseRollbackBetaConfig(
        const std::string& text,
        RollbackBetaConfig& out) noexcept
    {
        out = {};
        try
        {
            uint32_t seen = 0;
            size_t line_start = 0;
            while (line_start <= text.size())
            {
                size_t line_end = text.find('\n', line_start);
                if (line_end == std::string::npos)
                    line_end = text.size();
                std::string line = RollbackBetaTrimAscii(
                    text.substr(line_start, line_end - line_start));
                line_start = line_end + 1;
                if (line.empty() || line[0] == '#') continue;
                const size_t equals = line.find('=');
                if (equals == std::string::npos)
                {
                    out.failure = "beta-config-line-missing-equals";
                    return false;
                }
                const std::string key = RollbackBetaLowerAscii(
                    RollbackBetaTrimAscii(line.substr(0, equals)));
                const std::string value = RollbackBetaTrimAscii(
                    line.substr(equals + 1));
                uint32_t bit = 0;
                if (key == "config_version") bit = 1u << 0;
                else if (key == "enabled") bit = 1u << 1;
                else if (key == "role") bit = 1u << 2;
                else if (key == "bind_address") bit = 1u << 3;
                else if (key == "bind_port") bit = 1u << 4;
                else if (key == "peer_address") bit = 1u << 5;
                else if (key == "peer_port") bit = 1u << 6;
                else if (key == "secret") bit = 1u << 7;
                else if (key == "rollback_window") bit = 1u << 8;
                else if (key == "input_delay") bit = 1u << 9;
                else if (key == "trace") bit = 1u << 10;
                else if (key == "transport") bit = 1u << 11;
                else if (key == "save_policy") bit = 1u << 12;
                else if (key == "lead_pacing") bit = 1u << 13;
                else if (key == "lead_pacing_enter") bit = 1u << 14;
                else if (key == "lead_pacing_exit") bit = 1u << 15;
                else if (key == "lead_pacing_max_holds") bit = 1u << 16;
                else
                {
                    out.failure = "beta-config-unknown-key";
                    return false;
                }
                if ((seen & bit) != 0)
                {
                    out.failure = "beta-config-duplicate-key";
                    return false;
                }
                seen |= bit;

                if (key == "config_version")
                {
                    uint16_t parsed = 0;
                    if (!RollbackBetaParseU16(value, parsed))
                    {
                        out.failure = "beta-config-version-invalid";
                        return false;
                    }
                    out.config_version = parsed;
                }
                else if (key == "enabled")
                {
                    if (!RollbackBetaParseBool(value, out.enabled))
                    {
                        out.failure = "beta-config-enabled-invalid";
                        return false;
                    }
                }
                else if (key == "trace")
                {
                    if (!RollbackBetaParseBool(value, out.trace))
                    {
                        out.failure = "beta-config-trace-invalid";
                        return false;
                    }
                }
                else if (key == "transport")
                {
                    const std::string transport =
                        RollbackBetaLowerAscii(value);
                    out.transport =
                        transport == "steam-p2p"
                            || transport == "steam"
                        ? RollbackBetaTransport::SteamP2P
                        : transport == "direct-udp"
                            || transport == "winsock"
                            ? RollbackBetaTransport::DirectUdp
                            : RollbackBetaTransport::Invalid;
                    if (out.transport == RollbackBetaTransport::Invalid)
                    {
                        out.failure = "beta-config-transport-invalid";
                        return false;
                    }
                }
                else if (key == "save_policy")
                {
                    const std::string policy =
                        RollbackBetaLowerAscii(value);
                    if (policy != "confirmed-speculative")
                    {
                        out.failure = "beta-config-save-policy-invalid";
                        return false;
                    }
                    out.save_policy =
                        RollbackSavePolicy::ConfirmedSpeculative;
                }
                else if (key == "lead_pacing")
                {
                    if (!RollbackBetaParseBool(
                            value, out.lead_pacing.enabled))
                    {
                        out.failure = "beta-config-lead-pacing-invalid";
                        return false;
                    }
                }
                else if (key == "lead_pacing_enter")
                {
                    if (!RollbackBetaParseFloat(
                            value, out.lead_pacing.enter_frames))
                    {
                        out.failure =
                            "beta-config-lead-pacing-enter-invalid";
                        return false;
                    }
                }
                else if (key == "lead_pacing_exit")
                {
                    if (!RollbackBetaParseFloat(
                            value, out.lead_pacing.exit_frames))
                    {
                        out.failure =
                            "beta-config-lead-pacing-exit-invalid";
                        return false;
                    }
                }
                else if (key == "lead_pacing_max_holds")
                {
                    uint16_t parsed = 0;
                    if (!RollbackBetaParseU16(value, parsed)
                        || parsed > 8)
                    {
                        out.failure =
                            "beta-config-lead-pacing-holds-invalid";
                        return false;
                    }
                    out.lead_pacing.maximum_consecutive_holds =
                        static_cast<uint8_t>(parsed);
                }
                else if (key == "role")
                {
                    const std::string role =
                        RollbackBetaLowerAscii(value);
                    out.role = role == "host"
                        ? RollbackBetaRole::Host
                        : role == "guest"
                            ? RollbackBetaRole::Guest
                            : RollbackBetaRole::Invalid;
                    if (out.role == RollbackBetaRole::Invalid)
                    {
                        out.failure = "beta-config-role-invalid";
                        return false;
                    }
                }
                else if (key == "bind_address")
                    out.bind_address = value;
                else if (key == "bind_port")
                {
                    if (!RollbackBetaParseU16(value, out.bind_port))
                    {
                        out.failure = "beta-config-bind-port-invalid";
                        return false;
                    }
                }
                else if (key == "peer_address")
                    out.peer_address = value;
                else if (key == "peer_port")
                {
                    if (!RollbackBetaParseU16(value, out.peer_port))
                    {
                        out.failure = "beta-config-peer-port-invalid";
                        return false;
                    }
                }
                else if (key == "secret")
                    out.secret = value;
                else if (key == "rollback_window")
                {
                    if (!RollbackBetaParseU16(
                            value, out.rollback_window))
                    {
                        out.failure = "beta-config-window-invalid";
                        return false;
                    }
                }
                else if (key == "input_delay")
                {
                    if (!RollbackBetaParseU16(value, out.input_delay))
                    {
                        out.failure = "beta-config-input-delay-invalid";
                        return false;
                    }
                }
            }
            const uint32_t common_required =
                (1u << 0) | (1u << 1);
            if ((seen & common_required) != common_required)
            {
                out.failure = "beta-config-required-key-missing";
                return false;
            }
            const bool legacy =
                out.config_version == kRollbackBetaLegacyConfigVersion;
            const bool current =
                out.config_version == kRollbackBetaConfigVersion;
            if (out.config_version
                == kRollbackBetaObsoleteSteamConfigVersion)
            {
                out.failure = "beta-config-upgrade-required";
                return false;
            }
            if (!legacy && !current)
            {
                out.failure = "beta-config-version-unsupported";
                return false;
            }
            const uint32_t current_required =
                (1u << 11) | (1u << 12) | (1u << 13)
                | (1u << 14) | (1u << 15) | (1u << 16);
            if (current && (seen & current_required) != current_required)
            {
                out.failure = "beta-config-required-key-missing";
                return false;
            }
            const bool direct = legacy
                || out.transport == RollbackBetaTransport::DirectUdp;
            const uint32_t direct_required =
                (1u << 2) | (1u << 5) | (1u << 6) | (1u << 7);
            if (direct
                && (seen & direct_required) != direct_required)
            {
                out.failure = "beta-config-required-key-missing";
                return false;
            }
            if (!out.valid())
            {
                out.failure = !out.enabled
                        ? "beta-config-disabled"
                        : direct
                            && !RollbackBetaConfig::valid_peer_name(
                            out.peer_address)
                            ? "beta-config-peer-address-invalid"
                            : direct
                                && !RollbackBetaConfig::valid_secret(out.secret)
                                ? "beta-config-secret-invalid"
                                : "beta-config-invalid";
                return false;
            }
            out.failure = "ok";
            return true;
        }
        catch (...)
        {
            out = {};
            out.failure = "beta-config-allocation-failed";
            return false;
        }
    }
}
