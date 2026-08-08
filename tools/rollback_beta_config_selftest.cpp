#include "../HorseMod/horselib/RollbackBetaConfig.hpp"

#include <cstdio>
#include <cstring>
#include <string>

int main()
{
    const std::string steam =
        "config_version=3\n"
        "enabled=true\n"
        "transport=steam-p2p\n"
        "rollback_window=12\n"
        "input_delay=1\n"
        "save_policy=confirmed-speculative\n"
        "lead_pacing=true\n"
        "lead_pacing_enter=1.5\n"
        "lead_pacing_exit=0.5\n"
        "lead_pacing_max_holds=2\n"
        "trace=false\n";
    Horse::RollbackBetaConfig steam_config {};
    const bool steam_valid =
        Horse::ParseRollbackBetaConfig(steam, steam_config)
        && steam_config.valid()
        && steam_config.transport
            == Horse::RollbackBetaTransport::SteamP2P
        && steam_config.role == Horse::RollbackBetaRole::Invalid
        && steam_config.peer_address.empty()
        && steam_config.secret.empty();

    const std::string shared =
        "config_version=1\n"
        "enabled=true\n"
        "role=host\n"
        "bind_address=0.0.0.0\n"
        "bind_port=47170\n"
        "peer_address=peer.example.net\n"
        "peer_port=47171\n"
        "secret=000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f\n"
        "rollback_window=12\n"
        "input_delay=1\n"
        "trace=false\n";
    Horse::RollbackBetaConfig host {};
    const bool host_valid = Horse::ParseRollbackBetaConfig(shared, host)
        && host.valid()
        && host.role == Horse::RollbackBetaRole::Host
        && host.local_player_slot() == 1
        && host.local_peer() == 1
        && host.remote_peer() == 2
        && host.peer_address == "peer.example.net"
        && host.peer_port == 47171;

    std::string guest_text = shared;
    guest_text.replace(
        guest_text.find("role=host"),
        std::strlen("role=host"),
        "role=guest");
    Horse::RollbackBetaConfig guest {};
    const bool guest_valid =
        Horse::ParseRollbackBetaConfig(guest_text, guest)
        && guest.valid()
        && guest.role == Horse::RollbackBetaRole::Guest
        && guest.local_player_slot() == 0
        && guest.local_peer() == 2
        && guest.remote_peer() == 1;

    Horse::RollbackBetaConfig missing {};
    const bool missing_rejected = !Horse::ParseRollbackBetaConfig(
            "config_version=3\nenabled=true\n",
            missing)
        && std::strcmp(
            missing.failure,
            "beta-config-required-key-missing") == 0;

    Horse::RollbackBetaConfig duplicate {};
    const bool duplicate_rejected = !Horse::ParseRollbackBetaConfig(
            shared + "role=guest\n", duplicate)
        && std::strcmp(
            duplicate.failure, "beta-config-duplicate-key") == 0;

    Horse::RollbackBetaConfig unknown {};
    const bool unknown_rejected = !Horse::ParseRollbackBetaConfig(
            shared + "typo_peer_port=1\n", unknown)
        && std::strcmp(
            unknown.failure, "beta-config-unknown-key") == 0;

    std::string short_secret = shared;
    const size_t secret_begin =
        short_secret.find("secret=") + std::strlen("secret=");
    const size_t secret_end = short_secret.find('\n', secret_begin);
    short_secret.replace(
        secret_begin, secret_end - secret_begin, "too-short");
    Horse::RollbackBetaConfig malformed {};
    const bool malformed_secret_rejected =
        !Horse::ParseRollbackBetaConfig(short_secret, malformed)
        && std::strcmp(
            malformed.failure, "beta-config-secret-invalid") == 0;

    std::string repeated_secret = shared;
    const size_t repeated_begin =
        repeated_secret.find("secret=") + std::strlen("secret=");
    const size_t repeated_end =
        repeated_secret.find('\n', repeated_begin);
    repeated_secret.replace(
        repeated_begin, repeated_end - repeated_begin,
        std::string(64, '0'));
    Horse::RollbackBetaConfig repeated {};
    const bool repeated_secret_rejected =
        !Horse::ParseRollbackBetaConfig(repeated_secret, repeated)
        && std::strcmp(
            repeated.failure, "beta-config-secret-invalid") == 0;

    Horse::RollbackBetaConfig invalid_host {};
    std::string invalid_host_text = shared;
    invalid_host_text.replace(
        invalid_host_text.find("peer.example.net"),
        std::strlen("peer.example.net"),
        "https://peer.example.net");
    const bool invalid_host_rejected =
        !Horse::ParseRollbackBetaConfig(
            invalid_host_text, invalid_host)
        && std::strcmp(
            invalid_host.failure,
            "beta-config-peer-address-invalid") == 0;

    Horse::RollbackBetaConfig unsupported {};
    std::string unsupported_text = shared;
    unsupported_text.replace(
        unsupported_text.find("config_version=1"),
        std::strlen("config_version=1"),
        "config_version=4");
    const bool unsupported_rejected =
        !Horse::ParseRollbackBetaConfig(
            unsupported_text, unsupported)
        && std::strcmp(
            unsupported.failure,
            "beta-config-version-unsupported") == 0;

    std::string direct_v2 = shared;
    direct_v2.replace(
        direct_v2.find("config_version=1"),
        std::strlen("config_version=1"),
        "config_version=2\ntransport=direct-udp");
    Horse::RollbackBetaConfig direct_v2_config {};
    const bool direct_v2_rejected =
        !Horse::ParseRollbackBetaConfig(direct_v2, direct_v2_config)
        && std::strcmp(
            direct_v2_config.failure,
            "beta-config-upgrade-required") == 0;

    const bool ok = steam_valid && host_valid && guest_valid
        && missing_rejected && duplicate_rejected
        && unknown_rejected && malformed_secret_rejected
        && repeated_secret_rejected
        && invalid_host_rejected && unsupported_rejected
        && direct_v2_rejected;
    std::printf(
        "rollback beta config self-test %s steam=%d host=%d guest=%d "
        "missing=%d duplicate=%d unknown=%d malformed_secret=%d "
        "repeated_secret=%d "
        "invalid_host=%d unsupported=%d direct_v2_rejected=%d\n",
        ok ? "passed" : "failed",
        steam_valid ? 1 : 0,
        host_valid ? 1 : 0,
        guest_valid ? 1 : 0,
        missing_rejected ? 1 : 0,
        duplicate_rejected ? 1 : 0,
        unknown_rejected ? 1 : 0,
        malformed_secret_rejected ? 1 : 0,
        repeated_secret_rejected ? 1 : 0,
        invalid_host_rejected ? 1 : 0,
        unsupported_rejected ? 1 : 0,
        direct_v2_rejected ? 1 : 0);
    return ok ? 0 : 1;
}
