#include "deterministic/OnlineCoordinator.hpp"
#include "deterministic/SteamP2PTransport.hpp"

#include <algorithm>
#include <cstring>
#include <deque>
#include <iostream>
#include <vector>

using namespace Horse::Deterministic;

namespace {
int failures{};

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

struct Datagram {
  std::uint64_t source{};
  std::uint64_t destination{};
  int send_type{};
  int channel{};
  std::vector<std::byte> bytes;
};

struct Hub {
  std::deque<Datagram> packets;
};

class FakeSteamApi final : public ISteamNetworkingLegacyApi {
public:
  FakeSteamApi(Hub &hub, std::uint64_t local) : hub_(hub), local_(local) {}

  bool Initialize() noexcept override { return initialize_succeeds; }
  std::uint64_t LocalSteamId() const noexcept override { return local_; }
  bool Send(std::uint64_t peer, const void *data, std::uint32_t size,
            int send_type, int channel) noexcept override {
    if (!send_succeeds)
      return false;
    Datagram packet{local_, peer, send_type, channel};
    const auto *begin = static_cast<const std::byte *>(data);
    packet.bytes.assign(begin, begin + size);
    hub_.packets.push_back(std::move(packet));
    return true;
  }
  bool PacketAvailable(std::uint32_t &size, int channel) noexcept override {
    const auto found = std::find_if(
        hub_.packets.begin(), hub_.packets.end(), [&](const Datagram &packet) {
          return packet.destination == local_ && packet.channel == channel;
        });
    size = found == hub_.packets.end()
               ? 0u
               : static_cast<std::uint32_t>(found->bytes.size());
    return found != hub_.packets.end();
  }
  bool Read(void *destination, std::uint32_t capacity, std::uint32_t &size,
            std::uint64_t &sender, int channel) noexcept override {
    const auto found = std::find_if(
        hub_.packets.begin(), hub_.packets.end(), [&](const Datagram &packet) {
          return packet.destination == local_ && packet.channel == channel;
        });
    if (found == hub_.packets.end() || found->bytes.size() > capacity)
      return false;
    size = static_cast<std::uint32_t>(found->bytes.size());
    sender = found->source;
    std::memcpy(destination, found->bytes.data(), size);
    hub_.packets.erase(found);
    return true;
  }
  bool CloseChannel(std::uint64_t peer, int channel) noexcept override {
    closed_peer = peer;
    closed_channel = channel;
    ++close_count;
    return close_succeeds;
  }

  bool initialize_succeeds{true};
  bool send_succeeds{true};
  bool close_succeeds{true};
  std::uint64_t closed_peer{};
  int closed_channel{};
  int close_count{};

private:
  Hub &hub_;
  std::uint64_t local_{};
};

void authenticate(SteamP2PTransport &first, SteamP2PTransport &second,
                  std::optional<TransportMessage> *first_received = nullptr,
                  std::optional<TransportMessage> *second_received = nullptr) {
  for (int attempt = 0;
       attempt < 16 && (!first.Authenticated() || !second.Authenticated());
       ++attempt) {
    auto first_message = first.Poll();
    auto second_message = second.Poll();
    if (first_message && first_received)
      *first_received = first_message;
    if (second_message && second_received)
      *second_received = second_message;
  }
}

TransportMessage message(std::uint64_t session, std::uint8_t payload) {
  TransportMessage value{};
  value.kind = TransportMessageKind::Input;
  value.session_id = session;
  value.payload_size = 1;
  value.payload[0] = static_cast<std::byte>(payload);
  return value;
}

class QualifiedAllowlist final : public IOnlineContentAllowlist {
public:
  bool IsQualified(const OnlineContentContract &) const noexcept override {
    return true;
  }
};

OnlinePeerContract contract(std::uint8_t slot) {
  OnlinePeerContract value{};
  value.session_id = 0x1234;
  value.lobby_id = 0x5678;
  value.steam_ids = {4001, 4002};
  value.local_player_slot = slot;
  value.lobby_member_count = 2;
  value.casual_player_match = true;
  value.executable_id[0] = std::byte{1};
  value.build_id[0] = std::byte{2};
  std::memcpy(value.content.fighter_codes[0].data(), "ger", 4);
  std::memcpy(value.content.fighter_codes[1].data(), "tir", 4);
  std::memcpy(value.content.stage_code.data(), "017", 4);
  value.content.map_identity[0] = std::byte{0x17};
  constexpr char map_name[] = "Snow-Capped Showdown";
  std::memcpy(value.content.map_name.data(), map_name, sizeof(map_name));
  value.input_delay = 1;
  value.rollback_window = 12;
  return value;
}

CanonicalHash hash(std::byte value) {
  CanonicalHash output{};
  output.fill(value);
  return output;
}

void pump_pair(OnlineCoordinator &first, OnlineCoordinator &second,
               OnlineState target) {
  for (int attempt = 0;
       attempt < 64 && (first.state() != target || second.state() != target);
       ++attempt) {
    expect(first.Pump().ok(), "first production coordinator pump succeeds");
    expect(second.Pump().ok(), "second production coordinator pump succeeds");
  }
}

void test_real_coordinator_over_authenticated_pair() {
  Hub hub;
  FakeSteamApi first_api{hub, 4001};
  FakeSteamApi second_api{hub, 4002};
  SteamP2PTransport first_transport{first_api};
  SteamP2PTransport second_transport{second_api};
  QualifiedAllowlist allowlist;
  OnlineCoordinator first{first_transport, allowlist};
  OnlineCoordinator second{second_transport, allowlist};
  expect(first.Enable().ok() && second.Enable().ok(),
         "real coordinators enable");
  expect(first.ObserveLobby(contract(0)).ok() &&
             second.ObserveLobby(contract(1)).ok(),
         "real coordinators enter authenticated Steam handshake");
  pump_pair(first, second, OnlineState::AwaitingBattle);
  expect(first.state() == OnlineState::AwaitingBattle &&
             second.state() == OnlineState::AwaitingBattle,
         "authenticated pair reaches bilateral pre-ownership agreement");
  const CanonicalHash baseline = hash(std::byte{0x31});
  expect(first.ReadyBaseline({1, 0}).ok() &&
             second.ReadyBaseline({1, 0}).ok(),
         "real coordinators publish baseline readiness");
  pump_pair(first, second, OnlineState::AwaitingBaselineTarget);
  expect(first.FreezeBaseline({1, 120}, baseline, hash(std::byte{9})).ok() &&
             second.FreezeBaseline({1, 120}, baseline, hash(std::byte{9})).ok(),
         "real coordinators freeze identical canonical baseline");
  pump_pair(first, second, OnlineState::Active);
  expect(
      first.state() == OnlineState::Active &&
          second.state() == OnlineState::Active,
      "real coordinators activate through production authenticated transport");

  PlayerInput input{0x41, 0x02};
  expect(first.SendInput({1, 125}, input).ok(),
         "active production coordinator sends gameplay input");
  expect(second.Pump().ok(),
         "remote production coordinator pumps authenticated gameplay");
  const auto event = second.PopGameplay();
  expect(event && std::holds_alternative<OnlineInputPacket>(*event) &&
             std::get<OnlineInputPacket>(*event).input == input,
         "real coordinator decodes authenticated gameplay input");
  first.Disable();
  second.Disable();
}

void test_authenticated_delivery_and_replay_rejection() {
  Hub hub;
  FakeSteamApi first_api{hub, 1001};
  FakeSteamApi second_api{hub, 1002};
  SteamP2PTransport first{first_api};
  SteamP2PTransport second{second_api};
  expect(first.Start(1002).ok(),
         "first peer starts on validated Steam identity");
  expect(second.Start(1001).ok(),
         "second peer starts on validated Steam identity");
  expect(first.Send(message(77, 4), TransportReliability::Reliable).ok(),
         "pre-authentication message enters bounded queue");
  std::optional<TransportMessage> received;
  authenticate(first, second, nullptr, &received);
  expect(first.Authenticated() && second.Authenticated(),
         "bilateral ephemeral ECDH confirmation authenticates both peers");
  if (!received)
    received = second.Poll();
  expect(received && received->session_id == 77 &&
             received->payload[0] == std::byte{4},
         "queued reliable payload is released only after authentication");

  expect(first.Send(message(77, 9), TransportReliability::Unreliable).ok(),
         "authenticated gameplay datagram sends");
  expect(!hub.packets.empty(), "gameplay datagram reaches fake Steam channel");
  if (!hub.packets.empty())
    hub.packets.push_back(hub.packets.back());
  received = second.Poll();
  expect(received && received->payload[0] == std::byte{9},
         "first authenticated gameplay datagram is accepted");
  expect(!second.Poll(), "duplicate authenticated sequence is discarded");
  expect(second.TerminalFailure() == FailureCode::None,
         "duplicate datagram is non-terminal");

  first_api.close_succeeds = false;
  first.Stop();
  expect(!first.IsClearForStock(),
         "failed native channel close withholds stock clearance");
  first_api.close_succeeds = true;
  first.Stop();
  second.Stop();
  expect(first.IsClearForStock() && second.IsClearForStock(),
         "transport teardown clears authentication, queues, identities, and secrets");
  expect(first_api.closed_peer == 1002 &&
             first_api.closed_channel == steam_p2p_channel,
         "teardown closes only the Horse-owned peer channel");
}

void test_tamper_and_wrong_sender() {
  Hub hub;
  FakeSteamApi first_api{hub, 2001};
  FakeSteamApi second_api{hub, 2002};
  SteamP2PTransport first{first_api};
  SteamP2PTransport second{second_api};
  expect(first.Start(2002).ok() && second.Start(2001).ok(),
         "tamper fixture starts");
  authenticate(first, second);
  hub.packets.push_front(Datagram{9999, 2002, 0, steam_p2p_channel,
                                  std::vector<std::byte>(8, std::byte{0x55})});
  expect(!second.Poll(), "packet from a non-validated Steam peer is ignored");
  expect(second.TerminalFailure() == FailureCode::None,
         "foreign channel injection cannot terminate the validated match");
  expect(first.Send(message(88, 3), TransportReliability::Unreliable).ok(),
         "tamper fixture sends authenticated data");
  if (!hub.packets.empty())
    hub.packets.back().bytes.back() ^= std::byte{1};
  expect(!second.Poll(), "tampered authenticated datagram is rejected");
  expect(second.TerminalFailure() == FailureCode::AuthenticationFailed,
         "tamper is a terminal authentication failure");
}

void test_bounded_pre_authentication_queue() {
  Hub hub;
  FakeSteamApi api{hub, 3001};
  SteamP2PTransport transport{api};
  expect(transport.Start(3002).ok(), "bounded queue fixture starts");
  for (std::uint8_t index = 0; index < 64; ++index)
    expect(
        transport.Send(message(99, index), TransportReliability::Reliable).ok(),
        "bounded queue accepts capacity");
  expect(transport.QueuedMessages() == 64,
         "pre-authentication queue reaches fixed capacity exactly");
  expect(transport.Send(message(99, 65), TransportReliability::Reliable).code ==
             FailureCode::CapacityExceeded,
         "pre-authentication queue fails closed at fixed capacity");
}
} // namespace

int main() {
  test_real_coordinator_over_authenticated_pair();
  test_authenticated_delivery_and_replay_rejection();
  test_tamper_and_wrong_sender();
  test_bounded_pre_authentication_queue();
  if (failures == 0)
    std::cout << "SteamP2PTransportSelfTest passed\n";
  return failures == 0 ? 0 : 1;
}
