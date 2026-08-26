#pragma once

#include "Interfaces.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Horse::Deterministic {
inline constexpr int steam_p2p_channel = 0x484f;
inline constexpr std::size_t maximum_steam_wire_packet = 1400;

class ISteamNetworkingLegacyApi {
public:
  virtual ~ISteamNetworkingLegacyApi() = default;
  virtual bool Initialize() noexcept = 0;
  [[nodiscard]] virtual std::uint64_t LocalSteamId() const noexcept = 0;
  virtual bool Send(std::uint64_t peer, const void *data, std::uint32_t size,
                    int send_type, int channel) noexcept = 0;
  virtual bool PacketAvailable(std::uint32_t &size, int channel) noexcept = 0;
  virtual bool Read(void *destination, std::uint32_t capacity,
                    std::uint32_t &size, std::uint64_t &sender,
                    int channel) noexcept = 0;
  virtual bool CloseChannel(std::uint64_t peer, int channel) noexcept = 0;
};

class SteamNetworkingLegacyApi final : public ISteamNetworkingLegacyApi {
public:
  bool Initialize() noexcept override;
  [[nodiscard]] std::uint64_t LocalSteamId() const noexcept override;
  bool Send(std::uint64_t peer, const void *data, std::uint32_t size,
            int send_type, int channel) noexcept override;
  bool PacketAvailable(std::uint32_t &size, int channel) noexcept override;
  bool Read(void *destination, std::uint32_t capacity, std::uint32_t &size,
            std::uint64_t &sender, int channel) noexcept override;
  bool CloseChannel(std::uint64_t peer, int channel) noexcept override;

private:
  template <typename Function>
  static Function Resolve(HMODULE module, const char *name) noexcept {
    return reinterpret_cast<Function>(GetProcAddress(module, name));
  }

  using SteamClientFn = void *(__cdecl *)();
  using GetHandleFn = int(__cdecl *)();
  using GetInterfaceFn = void *(__cdecl *)(void *, int, int, const char *);
  using GetSteamIdFn = std::uint64_t(__cdecl *)(void *);
  using SendFn = bool(__cdecl *)(void *, std::uint64_t, const void *,
                                 std::uint32_t, int, int);
  using AvailableFn = bool(__cdecl *)(void *, std::uint32_t *, int);
  using ReadFn = bool(__cdecl *)(void *, void *, std::uint32_t, std::uint32_t *,
                                 std::uint64_t *, int);
  using CloseChannelFn = bool(__cdecl *)(void *, std::uint64_t, int);

  bool symbols_resolved_{};
  void *client_{};
  void *user_interface_{};
  void *networking_{};
  int user_handle_{};
  int pipe_handle_{};
  std::uint64_t local_steam_id_{};
  SteamClientFn steam_client_{};
  GetHandleFn get_user_handle_{};
  GetHandleFn get_pipe_handle_{};
  GetInterfaceFn get_user_interface_{};
  GetInterfaceFn get_networking_{};
  GetSteamIdFn get_steam_id_{};
  SendFn send_{};
  AvailableFn available_{};
  ReadFn read_{};
  CloseChannelFn close_channel_{};
};

class SteamP2PTransport final : public IRollbackTransport {
public:
  SteamP2PTransport() noexcept;
  explicit SteamP2PTransport(ISteamNetworkingLegacyApi &api) noexcept;
  ~SteamP2PTransport() override;

  Status Start(std::uint64_t validated_steam_peer) noexcept override;
  Status Send(const TransportMessage &message,
              TransportReliability reliability) noexcept override;
  std::optional<TransportMessage> Poll() noexcept override;
  [[nodiscard]] FailureCode TerminalFailure() const noexcept override;
  void Stop() noexcept override;

  [[nodiscard]] bool Authenticated() const noexcept { return authenticated_; }
  [[nodiscard]] std::size_t QueuedMessages() const noexcept {
    return queue_size_;
  }

private:
  struct QueuedMessage {
    TransportMessage message{};
    TransportReliability reliability{};
  };

  class EphemeralKey {
  public:
    ~EphemeralKey() noexcept;
    bool Generate() noexcept;
    bool Derive(const std::array<std::byte, 72> &peer_public,
                const void *transcript, std::size_t transcript_size,
                std::array<std::byte, 32> &output) noexcept;
    void Clear() noexcept;
    [[nodiscard]] const std::array<std::byte, 72> &Public() const noexcept {
      return public_key_;
    }

  private:
    BCRYPT_ALG_HANDLE algorithm_{};
    BCRYPT_KEY_HANDLE key_{};
    std::array<std::byte, 72> public_key_{};
  };

  struct ReplayWindow {
    std::uint64_t highest{};
    std::uint64_t seen{};
    bool Accept(std::uint64_t sequence) noexcept;
  };

  Status Fail(FailureCode code) noexcept;
  bool SendBootstrap(bool confirmation) noexcept;
  bool HandleBootstrap(const std::byte *bytes, std::size_t size,
                       std::uint64_t sender) noexcept;
  bool DeriveSessionKey() noexcept;
  bool SendWire(const TransportMessage &message,
                TransportReliability reliability) noexcept;
  std::optional<TransportMessage> HandleWire(const std::byte *bytes,
                                             std::size_t size,
                                             std::uint64_t sender) noexcept;
  bool FlushQueue() noexcept;
  void ClearSecrets() noexcept;

  static constexpr std::size_t maximum_queued_messages = 64;
  static constexpr std::size_t maximum_packets_per_poll = 64;

  SteamNetworkingLegacyApi owned_api_{};
  ISteamNetworkingLegacyApi *api_{};
  std::array<std::optional<QueuedMessage>, maximum_queued_messages> queue_{};
  std::size_t queue_head_{};
  std::size_t queue_size_{};
  EphemeralKey ephemeral_{};
  std::array<std::byte, 32> local_nonce_{};
  std::array<std::byte, 32> remote_nonce_{};
  std::array<std::byte, 72> remote_public_{};
  std::array<std::byte, 32> session_key_{};
  std::array<std::uint64_t, 2> send_sequence_{};
  std::array<ReplayWindow, 2> receive_windows_{};
  std::chrono::steady_clock::time_point started_at_{};
  std::chrono::steady_clock::time_point next_bootstrap_send_{};
  std::uint64_t local_steam_id_{};
  std::uint64_t peer_steam_id_{};
  FailureCode failure_{FailureCode::None};
  bool started_{};
  bool remote_hello_{};
  bool sent_confirmation_{};
  bool authenticated_{};
};
} // namespace Horse::Deterministic
