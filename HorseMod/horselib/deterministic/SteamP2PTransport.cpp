#include "SteamP2PTransport.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace Horse::Deterministic {
namespace {
constexpr std::uint32_t bootstrap_magic = 0x42534848u;
constexpr std::uint32_t wire_magic = 0x57534848u;
constexpr std::uint16_t transport_version = 1;
constexpr int steam_send_unreliable = 0;
constexpr int steam_send_reliable = 2;
constexpr auto bootstrap_timeout = std::chrono::seconds(10);
constexpr auto bootstrap_resend = std::chrono::milliseconds(250);
constexpr std::size_t authentication_tag_size = 16;

enum class BootstrapKind : std::uint8_t { Hello = 1, Confirm = 2 };

#pragma pack(push, 1)
struct BootstrapPacket {
  std::uint32_t magic{};
  std::uint16_t version{};
  BootstrapKind kind{};
  std::uint8_t reserved{};
  std::uint64_t source{};
  std::uint64_t destination{};
  std::array<std::byte, 32> nonce{};
  std::array<std::byte, 72> public_key{};
  std::array<std::byte, 32> peer_nonce{};
  std::array<std::byte, authentication_tag_size> tag{};
};

struct BootstrapTranscript {
  std::uint32_t magic{};
  std::uint16_t version{};
  std::uint16_t size{};
  std::uint64_t lower_id{};
  std::uint64_t upper_id{};
  std::array<std::byte, 32> lower_nonce{};
  std::array<std::byte, 32> upper_nonce{};
  std::array<std::byte, 72> lower_public{};
  std::array<std::byte, 72> upper_public{};
};

struct WireHeader {
  std::uint32_t magic{};
  std::uint16_t version{};
  std::uint8_t kind{};
  std::uint8_t reliability{};
  std::uint64_t session_id{};
  std::uint64_t sequence{};
  std::uint16_t payload_size{};
  std::uint16_t reserved{};
  std::array<std::byte, authentication_tag_size> tag{};
};
#pragma pack(pop)

static_assert(sizeof(BootstrapPacket) <= maximum_steam_wire_packet);
static_assert(sizeof(WireHeader) + Schema::maximum_transport_payload <=
              maximum_steam_wire_packet);

bool constant_time_equal(const std::byte *first, const std::byte *second,
                         std::size_t size) noexcept {
  std::uint8_t difference{};
  for (std::size_t index = 0; index < size; ++index)
    difference |= std::to_integer<std::uint8_t>(first[index] ^ second[index]);
  return difference == 0;
}

bool hmac_sha256(const std::array<std::byte, 32> &key, const void *bytes,
                 std::size_t size, std::array<std::byte, 32> &output) noexcept {
  BCRYPT_ALG_HANDLE algorithm{};
  BCRYPT_HASH_HANDLE hash{};
  std::array<std::byte, 1024> object{};
  DWORD object_size{};
  DWORD result_size{};
  bool ok = false;
  if (size > std::numeric_limits<ULONG>::max() ||
      !BCRYPT_SUCCESS(
          BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                      nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)) ||
      !BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                        reinterpret_cast<PUCHAR>(&object_size),
                                        sizeof(object_size), &result_size,
                                        0)) ||
      object_size > object.size() ||
      !BCRYPT_SUCCESS(BCryptCreateHash(
          algorithm, &hash, reinterpret_cast<PUCHAR>(object.data()),
          object_size,
          const_cast<PUCHAR>(reinterpret_cast<const UCHAR *>(key.data())),
          static_cast<ULONG>(key.size()), 0)) ||
      !BCRYPT_SUCCESS(BCryptHashData(
          hash, const_cast<PUCHAR>(reinterpret_cast<const UCHAR *>(bytes)),
          static_cast<ULONG>(size), 0)) ||
      !BCRYPT_SUCCESS(BCryptFinishHash(hash,
                                       reinterpret_cast<PUCHAR>(output.data()),
                                       static_cast<ULONG>(output.size()), 0))) {
    output.fill({});
  } else {
    ok = true;
  }
  if (hash)
    BCryptDestroyHash(hash);
  if (algorithm)
    BCryptCloseAlgorithmProvider(algorithm, 0);
  SecureZeroMemory(object.data(), object.size());
  return ok;
}

BootstrapTranscript
make_transcript(std::uint64_t local_id, std::uint64_t remote_id,
                const std::array<std::byte, 32> &local_nonce,
                const std::array<std::byte, 32> &remote_nonce,
                const std::array<std::byte, 72> &local_public,
                const std::array<std::byte, 72> &remote_public) noexcept {
  BootstrapTranscript value{};
  value.magic = bootstrap_magic;
  value.version = transport_version;
  value.size = sizeof(value);
  value.lower_id = std::min(local_id, remote_id);
  value.upper_id = std::max(local_id, remote_id);
  if (local_id == value.lower_id) {
    value.lower_nonce = local_nonce;
    value.upper_nonce = remote_nonce;
    value.lower_public = local_public;
    value.upper_public = remote_public;
  } else {
    value.lower_nonce = remote_nonce;
    value.upper_nonce = local_nonce;
    value.lower_public = remote_public;
    value.upper_public = local_public;
  }
  return value;
}

bool make_confirmation(
    const std::array<std::byte, 32> &session_key,
    const BootstrapTranscript &transcript, std::uint64_t sender,
    std::array<std::byte, authentication_tag_size> &output) noexcept {
#pragma pack(push, 1)
  struct Confirmation {
    BootstrapTranscript transcript{};
    std::uint64_t sender{};
    std::array<char, 31> domain{};
  };
#pragma pack(pop)
  Confirmation input{};
  input.transcript = transcript;
  input.sender = sender;
  constexpr char domain[] = "HorseMod-SteamP2P-confirm-v1";
  static_assert(sizeof(domain) <= input.domain.size());
  std::memcpy(input.domain.data(), domain, sizeof(domain));
  std::array<std::byte, 32> full{};
  const bool ok = hmac_sha256(session_key, &input, sizeof(input), full);
  std::copy_n(full.begin(), output.size(), output.begin());
  SecureZeroMemory(full.data(), full.size());
  return ok;
}

std::size_t reliability_index(TransportReliability reliability) noexcept {
  return reliability == TransportReliability::Reliable ? 1u : 0u;
}
} // namespace

bool SteamNetworkingLegacyApi::Initialize() noexcept {
  const HMODULE module = GetModuleHandleW(L"steam_api64.dll");
  if (!module)
    return false;
  if (!symbols_resolved_) {
    steam_client_ = Resolve<SteamClientFn>(module, "SteamClient");
    get_user_handle_ = Resolve<GetHandleFn>(module, "SteamAPI_GetHSteamUser");
    get_pipe_handle_ = Resolve<GetHandleFn>(module, "SteamAPI_GetHSteamPipe");
    get_user_interface_ =
        Resolve<GetInterfaceFn>(module, "SteamAPI_ISteamClient_GetISteamUser");
    get_networking_ = Resolve<GetInterfaceFn>(
        module, "SteamAPI_ISteamClient_GetISteamNetworking");
    get_steam_id_ =
        Resolve<GetSteamIdFn>(module, "SteamAPI_ISteamUser_GetSteamID");
    send_ = Resolve<SendFn>(module, "SteamAPI_ISteamNetworking_SendP2PPacket");
    available_ = Resolve<AvailableFn>(
        module, "SteamAPI_ISteamNetworking_IsP2PPacketAvailable");
    read_ = Resolve<ReadFn>(module, "SteamAPI_ISteamNetworking_ReadP2PPacket");
    close_channel_ = Resolve<CloseChannelFn>(
        module, "SteamAPI_ISteamNetworking_CloseP2PChannelWithUser");
    symbols_resolved_ = steam_client_ && get_user_handle_ && get_pipe_handle_ &&
                        get_user_interface_ && get_networking_ &&
                        get_steam_id_ && send_ && available_ && read_ &&
                        close_channel_;
  }
  if (!symbols_resolved_)
    return false;

  void *client = steam_client_();
  const int user = get_user_handle_();
  const int pipe = get_pipe_handle_();
  if (!client || user == 0 || pipe == 0)
    return false;
  if (client != client_ || user != user_handle_ || pipe != pipe_handle_ ||
      !user_interface_ || !networking_) {
    client_ = client;
    user_handle_ = user;
    pipe_handle_ = pipe;
    user_interface_ = get_user_interface_(client, user, pipe, "SteamUser019");
    networking_ = get_networking_(client, user, pipe, "SteamNetworking005");
  }
  if (!user_interface_ || !networking_)
    return false;
  local_steam_id_ = get_steam_id_(user_interface_);
  return local_steam_id_ != 0;
}

std::uint64_t SteamNetworkingLegacyApi::LocalSteamId() const noexcept {
  return local_steam_id_;
}

bool SteamNetworkingLegacyApi::Send(std::uint64_t peer, const void *data,
                                    std::uint32_t size, int send_type,
                                    int channel) noexcept {
  return networking_ && peer && data && size &&
         send_(networking_, peer, data, size, send_type, channel);
}

bool SteamNetworkingLegacyApi::PacketAvailable(std::uint32_t &size,
                                               int channel) noexcept {
  size = 0;
  return networking_ && available_(networking_, &size, channel);
}

bool SteamNetworkingLegacyApi::Read(void *destination, std::uint32_t capacity,
                                    std::uint32_t &size, std::uint64_t &sender,
                                    int channel) noexcept {
  size = 0;
  sender = 0;
  return networking_ && destination && capacity &&
         read_(networking_, destination, capacity, &size, &sender, channel);
}

bool SteamNetworkingLegacyApi::CloseChannel(std::uint64_t peer,
                                            int channel) noexcept {
  return networking_ && peer && close_channel_(networking_, peer, channel);
}

SteamP2PTransport::EphemeralKey::~EphemeralKey() noexcept { Clear(); }

bool SteamP2PTransport::EphemeralKey::Generate() noexcept {
  Clear();
  ULONG bytes{};
  if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
          &algorithm_, BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0)) ||
      !BCRYPT_SUCCESS(BCryptGenerateKeyPair(algorithm_, &key_, 256, 0)) ||
      !BCRYPT_SUCCESS(BCryptFinalizeKeyPair(key_, 0)) ||
      !BCRYPT_SUCCESS(
          BCryptExportKey(key_, nullptr, BCRYPT_ECCPUBLIC_BLOB,
                          reinterpret_cast<PUCHAR>(public_key_.data()),
                          static_cast<ULONG>(public_key_.size()), &bytes, 0)) ||
      bytes != public_key_.size()) {
    Clear();
    return false;
  }
  return true;
}

bool SteamP2PTransport::EphemeralKey::Derive(
    const std::array<std::byte, 72> &peer_public, const void *transcript,
    std::size_t transcript_size, std::array<std::byte, 32> &output) noexcept {
  if (!algorithm_ || !key_ ||
      transcript_size > std::numeric_limits<ULONG>::max())
    return false;
  BCRYPT_KEY_HANDLE peer{};
  BCRYPT_SECRET_HANDLE secret{};
  ULONG bytes{};
  BCryptBuffer buffers[3]{};
  BCryptBufferDesc descriptor{};
  const wchar_t hash_name[] = BCRYPT_SHA256_ALGORITHM;
  constexpr char domain[] = "HorseMod-SteamP2P-key-v1";
  buffers[0] = {sizeof(hash_name), KDF_HASH_ALGORITHM,
                const_cast<wchar_t *>(hash_name)};
  buffers[1] = {static_cast<ULONG>(transcript_size), KDF_SECRET_PREPEND,
                const_cast<void *>(transcript)};
  buffers[2] = {sizeof(domain) - 1, KDF_SECRET_APPEND,
                const_cast<char *>(domain)};
  descriptor = {BCRYPTBUFFER_VERSION, 3, buffers};
  const bool ok =
      BCRYPT_SUCCESS(BCryptImportKeyPair(
          algorithm_, nullptr, BCRYPT_ECCPUBLIC_BLOB, &peer,
          const_cast<PUCHAR>(
              reinterpret_cast<const UCHAR *>(peer_public.data())),
          static_cast<ULONG>(peer_public.size()), 0)) &&
      BCRYPT_SUCCESS(BCryptSecretAgreement(key_, peer, &secret, 0)) &&
      BCRYPT_SUCCESS(BCryptDeriveKey(secret, BCRYPT_KDF_HASH, &descriptor,
                                     reinterpret_cast<PUCHAR>(output.data()),
                                     static_cast<ULONG>(output.size()), &bytes,
                                     0)) &&
      bytes == output.size();
  if (secret)
    BCryptDestroySecret(secret);
  if (peer)
    BCryptDestroyKey(peer);
  if (!ok)
    output.fill({});
  return ok;
}

void SteamP2PTransport::EphemeralKey::Clear() noexcept {
  if (key_)
    BCryptDestroyKey(key_);
  if (algorithm_)
    BCryptCloseAlgorithmProvider(algorithm_, 0);
  key_ = nullptr;
  algorithm_ = nullptr;
  SecureZeroMemory(public_key_.data(), public_key_.size());
}

bool SteamP2PTransport::ReplayWindow::Accept(std::uint64_t sequence) noexcept {
  if (sequence == 0)
    return false;
  if (highest == 0) {
    highest = sequence;
    seen = 1;
    return true;
  }
  if (sequence > highest) {
    const std::uint64_t shift = sequence - highest;
    seen = shift >= 64 ? 1 : (seen << shift) | 1;
    highest = sequence;
    return true;
  }
  const std::uint64_t distance = highest - sequence;
  if (distance >= 64 || (seen & (std::uint64_t{1} << distance)))
    return false;
  seen |= std::uint64_t{1} << distance;
  return true;
}

SteamP2PTransport::SteamP2PTransport() noexcept : api_(&owned_api_) {}

SteamP2PTransport::SteamP2PTransport(ISteamNetworkingLegacyApi &api) noexcept
    : api_(&api) {}

SteamP2PTransport::~SteamP2PTransport() { Stop(); }

Status SteamP2PTransport::Start(std::uint64_t validated_steam_peer) noexcept {
  if (started_ || validated_steam_peer == 0)
    return Status::failure(FailureCode::IllegalTransition);
  Stop();
  if (!api_ || !api_->Initialize())
    return Fail(FailureCode::TransportFailed);
  local_steam_id_ = api_->LocalSteamId();
  if (local_steam_id_ == 0 || local_steam_id_ == validated_steam_peer)
    return Fail(FailureCode::IdentityMismatch);
  peer_steam_id_ = validated_steam_peer;
  if (!ephemeral_.Generate() ||
      !BCRYPT_SUCCESS(BCryptGenRandom(
          nullptr, reinterpret_cast<PUCHAR>(local_nonce_.data()),
          static_cast<ULONG>(local_nonce_.size()),
          BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
    return Fail(FailureCode::AuthenticationFailed);
  }
  started_ = true;
  started_at_ = std::chrono::steady_clock::now();
  next_bootstrap_send_ = started_at_;
  if (!SendBootstrap(false))
    return Fail(FailureCode::TransportFailed);
  next_bootstrap_send_ = started_at_ + bootstrap_resend;
  return Status::success();
}

Status SteamP2PTransport::Send(const TransportMessage &message,
                               TransportReliability reliability) noexcept {
  if (!started_)
    return Status::failure(FailureCode::IllegalTransition);
  if (failure_ != FailureCode::None)
    return Status::failure(failure_);
  if (message.payload_size > message.payload.size())
    return Fail(FailureCode::CapacityExceeded);
  if (authenticated_)
    return SendWire(message, reliability) ? Status::success()
                                          : Fail(FailureCode::TransportFailed);
  if (queue_size_ == queue_.size())
    return Fail(FailureCode::CapacityExceeded);
  const std::size_t tail = (queue_head_ + queue_size_) % queue_.size();
  queue_[tail] = QueuedMessage{message, reliability};
  ++queue_size_;
  return Status::success();
}

bool SteamP2PTransport::SendBootstrap(bool confirmation) noexcept {
  BootstrapPacket packet{};
  packet.magic = bootstrap_magic;
  packet.version = transport_version;
  packet.kind = confirmation ? BootstrapKind::Confirm : BootstrapKind::Hello;
  packet.source = local_steam_id_;
  packet.destination = peer_steam_id_;
  packet.nonce = local_nonce_;
  packet.public_key = ephemeral_.Public();
  if (confirmation) {
    packet.peer_nonce = remote_nonce_;
    const auto transcript =
        make_transcript(local_steam_id_, peer_steam_id_, local_nonce_,
                        remote_nonce_, ephemeral_.Public(), remote_public_);
    if (!make_confirmation(session_key_, transcript, local_steam_id_,
                           packet.tag))
      return false;
    sent_confirmation_ = true;
  }
  return api_->Send(peer_steam_id_, &packet, sizeof(packet),
                    steam_send_reliable, steam_p2p_channel);
}

bool SteamP2PTransport::DeriveSessionKey() noexcept {
  const auto transcript =
      make_transcript(local_steam_id_, peer_steam_id_, local_nonce_,
                      remote_nonce_, ephemeral_.Public(), remote_public_);
  return ephemeral_.Derive(remote_public_, &transcript, sizeof(transcript),
                           session_key_);
}

bool SteamP2PTransport::HandleBootstrap(const std::byte *bytes,
                                        std::size_t size,
                                        std::uint64_t sender) noexcept {
  if (size != sizeof(BootstrapPacket) || sender != peer_steam_id_)
    return false;
  BootstrapPacket packet{};
  std::memcpy(&packet, bytes, sizeof(packet));
  if (packet.magic != bootstrap_magic || packet.version != transport_version ||
      packet.reserved != 0 || packet.source != peer_steam_id_ ||
      packet.destination != local_steam_id_ ||
      (packet.kind != BootstrapKind::Hello &&
       packet.kind != BootstrapKind::Confirm))
    return false;
  // A reliable bootstrap datagram can remain queued after both peers have
  // confirmed. The established wire key is already authoritative, and the
  // ephemeral private key has deliberately been destroyed.
  if (authenticated_)
    return true;
  remote_nonce_ = packet.nonce;
  remote_public_ = packet.public_key;
  remote_hello_ = true;
  if (!DeriveSessionKey())
    return false;
  if (packet.kind == BootstrapKind::Confirm) {
    if (packet.peer_nonce != local_nonce_)
      return false;
    const auto transcript =
        make_transcript(local_steam_id_, peer_steam_id_, local_nonce_,
                        remote_nonce_, ephemeral_.Public(), remote_public_);
    std::array<std::byte, authentication_tag_size> expected{};
    if (!make_confirmation(session_key_, transcript, peer_steam_id_,
                           expected) ||
        !constant_time_equal(expected.data(), packet.tag.data(),
                             expected.size()))
      return false;
    if (!sent_confirmation_ && !SendBootstrap(true))
      return false;
    authenticated_ = true;
    ephemeral_.Clear();
    return FlushQueue();
  }
  return SendBootstrap(true);
}

bool SteamP2PTransport::SendWire(const TransportMessage &message,
                                 TransportReliability reliability) noexcept {
  std::array<std::byte, maximum_steam_wire_packet> bytes{};
  WireHeader header{};
  header.magic = wire_magic;
  header.version = transport_version;
  header.kind = static_cast<std::uint8_t>(message.kind);
  header.reliability = static_cast<std::uint8_t>(reliability);
  header.session_id = message.session_id;
  const std::size_t index = reliability_index(reliability);
  header.sequence = ++send_sequence_[index];
  header.payload_size = message.payload_size;
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + sizeof(header), message.payload.data(),
              message.payload_size);
  const std::size_t total = sizeof(header) + message.payload_size;
  std::array<std::byte, 32> tag{};
  if (!hmac_sha256(session_key_, bytes.data(), total, tag))
    return false;
  std::copy_n(tag.begin(), header.tag.size(), header.tag.begin());
  std::memcpy(bytes.data(), &header, sizeof(header));
  const int send_type = reliability == TransportReliability::Reliable
                            ? steam_send_reliable
                            : steam_send_unreliable;
  return api_->Send(peer_steam_id_, bytes.data(),
                    static_cast<std::uint32_t>(total), send_type,
                    steam_p2p_channel);
}

std::optional<TransportMessage>
SteamP2PTransport::HandleWire(const std::byte *bytes, std::size_t size,
                              std::uint64_t sender) noexcept {
  if (!authenticated_ || sender != peer_steam_id_ || size < sizeof(WireHeader))
    return std::nullopt;
  WireHeader header{};
  std::memcpy(&header, bytes, sizeof(header));
  if (header.magic != wire_magic || header.version != transport_version ||
      header.reserved != 0 ||
      header.kind >
          static_cast<std::uint8_t>(TransportMessageKind::Disconnect) ||
      header.reliability >
          static_cast<std::uint8_t>(TransportReliability::Unreliable) ||
      header.payload_size > Schema::maximum_transport_payload ||
      size != sizeof(header) + header.payload_size) {
    Fail(FailureCode::ProtocolMismatch);
    return std::nullopt;
  }
  const auto received_tag = header.tag;
  header.tag.fill({});
  std::array<std::byte, maximum_steam_wire_packet> authenticated_bytes{};
  std::memcpy(authenticated_bytes.data(), &header, sizeof(header));
  std::memcpy(authenticated_bytes.data() + sizeof(header),
              bytes + sizeof(header), header.payload_size);
  std::array<std::byte, 32> expected{};
  if (!hmac_sha256(session_key_, authenticated_bytes.data(), size, expected) ||
      !constant_time_equal(received_tag.data(), expected.data(),
                           received_tag.size())) {
    Fail(FailureCode::AuthenticationFailed);
    return std::nullopt;
  }
  const auto reliability =
      static_cast<TransportReliability>(header.reliability);
  if (!receive_windows_[reliability_index(reliability)].Accept(header.sequence))
    return std::nullopt;
  TransportMessage message{};
  message.kind = static_cast<TransportMessageKind>(header.kind);
  message.session_id = header.session_id;
  message.payload_size = header.payload_size;
  std::memcpy(message.payload.data(), bytes + sizeof(header),
              header.payload_size);
  return message;
}

bool SteamP2PTransport::FlushQueue() noexcept {
  while (queue_size_ != 0) {
    auto &entry = queue_[queue_head_];
    if (!entry || !SendWire(entry->message, entry->reliability))
      return false;
    entry.reset();
    queue_head_ = (queue_head_ + 1) % queue_.size();
    --queue_size_;
  }
  return true;
}

std::optional<TransportMessage> SteamP2PTransport::Poll() noexcept {
  if (!started_ || failure_ != FailureCode::None)
    return std::nullopt;
  const auto now = std::chrono::steady_clock::now();
  if (!authenticated_ && now - started_at_ >= bootstrap_timeout) {
    Fail(FailureCode::PeerDisconnected);
    return std::nullopt;
  }
  if (!authenticated_ && now >= next_bootstrap_send_) {
    if (!SendBootstrap(remote_hello_)) {
      Fail(FailureCode::TransportFailed);
      return std::nullopt;
    }
    next_bootstrap_send_ = now + bootstrap_resend;
  }
  for (std::size_t count = 0; count < maximum_packets_per_poll; ++count) {
    std::uint32_t available{};
    if (!api_->PacketAvailable(available, steam_p2p_channel))
      break;
    std::array<std::byte, maximum_steam_wire_packet> bytes{};
    std::uint32_t read{};
    std::uint64_t sender{};
    if (available == 0 || available > bytes.size() ||
        !api_->Read(bytes.data(), static_cast<std::uint32_t>(bytes.size()),
                    read, sender, steam_p2p_channel)) {
      Fail(FailureCode::TransportFailed);
      return std::nullopt;
    }
    if (sender != peer_steam_id_)
      continue;
    if (read >= sizeof(std::uint32_t)) {
      std::uint32_t magic{};
      std::memcpy(&magic, bytes.data(), sizeof(magic));
      if (magic == bootstrap_magic) {
        if (!HandleBootstrap(bytes.data(), read, sender))
          Fail(FailureCode::AuthenticationFailed);
        if (failure_ != FailureCode::None)
          return std::nullopt;
        continue;
      }
      if (magic == wire_magic) {
        auto message = HandleWire(bytes.data(), read, sender);
        if (message || failure_ != FailureCode::None)
          return message;
        continue;
      }
    }
    Fail(authenticated_ ? FailureCode::ProtocolMismatch
                        : FailureCode::AuthenticationFailed);
    return std::nullopt;
  }
  return std::nullopt;
}

FailureCode SteamP2PTransport::TerminalFailure() const noexcept {
  return failure_;
}

Status SteamP2PTransport::Fail(FailureCode code) noexcept {
  failure_ = code;
  return Status::failure(code);
}

void SteamP2PTransport::ClearSecrets() noexcept {
  ephemeral_.Clear();
  SecureZeroMemory(local_nonce_.data(), local_nonce_.size());
  SecureZeroMemory(remote_nonce_.data(), remote_nonce_.size());
  SecureZeroMemory(remote_public_.data(), remote_public_.size());
  SecureZeroMemory(session_key_.data(), session_key_.size());
}

void SteamP2PTransport::Stop() noexcept {
  if (started_ && api_ && peer_steam_id_)
    (void)api_->CloseChannel(peer_steam_id_, steam_p2p_channel);
  for (auto &entry : queue_)
    entry.reset();
  queue_head_ = 0;
  queue_size_ = 0;
  send_sequence_ = {};
  receive_windows_ = {};
  started_ = false;
  remote_hello_ = false;
  sent_confirmation_ = false;
  authenticated_ = false;
  local_steam_id_ = 0;
  peer_steam_id_ = 0;
  failure_ = FailureCode::None;
  ClearSecrets();
}
} // namespace Horse::Deterministic
