// cppgram_livetest: live-test runner for the CppGram library.
//
// Categories (each test belongs to exactly one):
//   offline              no Telegram or proxy contact. Local logic (session file, secret parsing) and loopback-only
//                        failure injection (scripted peers on 127.0.0.1)
//   environment          local failure paths (closed ports, unroutable address); no Telegram or proxy contact
//   live_no_credentials  real Telegram DC: TCP, MTProto handshake, encrypted ping. No account is used
//   live_credentials     real Telegram account actions. Credentials are read only after the network gate allows the test
//   owner_approval       needs owner infrastructure or approval (dd proxy, SOCKS5, message loop). Never runs without
//                        CGLT_OWNER_APPROVED=1, and never answers a chat other than CGLT_TEST_CHAT_ID
//
// Statuses: PASS, FAIL, BLOCKED, NOT RUN, PENDING USER APPROVAL.
//
// Network gate (src/netgate.h). Every test in a network category goes through it first:
//   - the interception probe must be Clean (TCP to 10.255.255.1 times out). CGLT_NETWORK_UNRESTRICTED=1 cannot override it.
//   - CGLT_NETWORK_UNRESTRICTED=1 must be set.
//   - owner_approval tests also need CGLT_OWNER_APPROVED=1.
// The test body, and so every credential read and every socket, is reached only after the gate allows it.
//
// Every result records the last stage reached, the error class, whether Telegram was contacted, and a redacted
// detail. Secrets (API hash, bot token, proxy secret, session bytes) are never printed.
//
//   cppgram_livetest [--list] [--category NAME] [--test ID] [--json FILE] [--workdir DIR]
//
// Exit codes: 0 no test FAILed (BLOCKED, NOT RUN and PENDING are not failures); 1 at least one FAIL;
//             2 bad command line, unknown category or test, no test selected, or --json cannot be written.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <mutex>
#include <atomic>
#include <thread>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "cppgram/client.h"
#include "cppgram/mtproto/auth.h"
#include "cppgram/mtproto/connection.h"
#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/obfuscated.h"
#include "cppgram/mtproto/proxy_secret.h"
#include "cppgram/mtproto/session.h"
#include "cppgram/mtproto/transport.h"
#include "netgate.h"

#ifndef CGLT_CPPGRAM_VERSION
#define CGLT_CPPGRAM_VERSION "unknown"
#endif
#ifndef CGLT_TREE_SHA256
#define CGLT_TREE_SHA256 "unavailable"
#endif

namespace mt = cppgram::mtproto;
namespace fs = std::filesystem;

namespace {

// ---------- status and result ----------

enum class Status { Pass, Fail, Blocked, NotRun, PendingUserApproval };

const char* status_text(Status s) {
    switch (s) {
        case Status::Pass: return "PASS";
        case Status::Fail: return "FAIL";
        case Status::Blocked: return "BLOCKED";
        case Status::NotRun: return "NOT RUN";
        case Status::PendingUserApproval: return "PENDING USER APPROVAL";
    }
    return "?";
}

struct Ctx {
    std::string stage = "start";
    bool contacted = false;  // true once any packet was sent to a Telegram or proxy address
};

struct Outcome {
    Status status = Status::NotRun;
    std::string detail;
    std::string error_class;
};

struct Result {
    std::string id;
    std::string category;
    std::string stage;
    bool contacted_telegram = false;
    Status status = Status::NotRun;
    std::string detail;
    std::string error_class;
    long long duration_ms = 0;
};

Outcome pass(std::string detail) { return {Status::Pass, std::move(detail), ""}; }
Outcome blocked(std::string detail) { return {Status::Blocked, std::move(detail), ""}; }
Outcome pending(std::string detail) { return {Status::PendingUserApproval, std::move(detail), ""}; }
Outcome not_run(std::string detail) { return {Status::NotRun, std::move(detail), ""}; }

// ---------- environment ----------

std::optional<std::string> env(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) return std::nullopt;
    return std::string(v);
}

// Declarations only. They are read before any test runs, and neither one can override the interception probe.
bool owner_approved() {
    auto v = env("CGLT_OWNER_APPROVED");
    return v && *v == "1";
}

bool unrestricted_declared() {
    auto v = env("CGLT_NETWORK_UNRESTRICTED");
    return v && *v == "1";
}

std::string g_work_dir = "cglt-work";

fs::path work_dir() {
    fs::path p = g_work_dir;
    std::error_code ec;
    fs::create_directories(p, ec);
    fs::permissions(p, fs::perms::owner_all, fs::perm_options::replace, ec);
    return p;
}

// ---------- Telegram DC addresses (public, from the library's built-in list) ----------

const char* dc_ip(int dc) {
    switch (dc) {
        case 1: return "149.154.175.53";
        case 2: return "149.154.167.51";
        case 3: return "149.154.175.100";
        case 4: return "149.154.167.91";
        case 5: return "91.108.56.130";
        default: return nullptr;
    }
}

// ---------- plain channel for the handshake (same contract as the library's own adapter) ----------

// The handshake already wraps and unwraps packets (auth_key_id 0, message_id, length), so this passes them through.
class PlainConnection : public mt::PlainChannel {
public:
    explicit PlainConnection(mt::Connection& c) : c_(c) {}
    void send_plain(const mt::Bytes& packet) override { c_.send_packet(packet); }
    mt::Bytes receive_plain() override { return c_.receive_packet(); }

private:
    mt::Connection& c_;
};

// Connects to a DC and sets the receive timeout. The timeout needs a connected socket, so it cannot come first.
void connect_dc(mt::Connection& conn, int dc) {
    conn.connect(dc_ip(dc), 443);
    conn.set_receive_timeout(15000);
}

// ---------- TL bytes for ping / pong (raw, so no generated-code dependency in the runner) ----------

constexpr std::uint32_t kPingCtor = 0x7abe77ecu;  // ping#7abe77ec ping_id:long
constexpr std::uint32_t kPongCtor = 0x347773c5u;  // pong#347773c5 msg_id:long ping_id:long

void put_u32(mt::Bytes& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
void put_u64(mt::Bytes& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
std::uint32_t get_u32(const mt::Bytes& in, std::size_t at) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(in.at(at + i)) << (8 * i);
    return v;
}
std::uint64_t get_u64(const mt::Bytes& in, std::size_t at) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(in.at(at + i)) << (8 * i);
    return v;
}

// ---------- exception classification ----------

std::string classify(const std::exception& e) {
    if (dynamic_cast<const mt::ConnectionTimeout*>(&e)) return "ConnectionTimeout";
    if (dynamic_cast<const mt::ProxyError*>(&e)) return "ProxyError";
    if (dynamic_cast<const mt::ConnectionError*>(&e)) return "ConnectionError";
    if (dynamic_cast<const mt::AuthError*>(&e)) return "AuthError";
    if (dynamic_cast<const mt::CryptoError*>(&e)) return "CryptoError";
    if (dynamic_cast<const mt::SessionCorruptError*>(&e)) return "SessionCorruptError";
    if (dynamic_cast<const mt::FloodWaitError*>(&e)) return "FloodWaitError";
    if (dynamic_cast<const mt::RpcError*>(&e)) return "RpcError";
    if (dynamic_cast<const std::invalid_argument*>(&e)) return "InvalidArgument";
    return "std::exception";
}

std::string clip(std::string s) {
    if (s.size() > 300) s.resize(300);
    return s;
}

// ---------- shared live steps ----------

// Opens a direct connection to DC `dc`, runs the real MTProto handshake and returns the new session.
mt::SessionData live_handshake(mt::TransportKind kind, int dc, Ctx& ctx, std::string& detail) {
    const char* ip = dc_ip(dc);
    if (!ip) throw std::invalid_argument("unknown DC id");
    mt::Connection conn(kind);
    conn.set_connect_timeout(10000);
    ctx.stage = "tcp_connect";
    ctx.contacted = true;
    connect_dc(conn, dc);
    ctx.stage = "mtproto_handshake";
    PlainConnection plain(conn);
    mt::HandshakeOptions opts;
    opts.dc_id = dc;
    mt::AuthKeyResult key = mt::create_auth_key(plain, opts);
    if (key.auth_key.size() != 256) throw mt::AuthError("auth key is not 256 bytes");
    mt::SessionData data;
    data.dc_id = dc;
    data.auth_key = key.auth_key;
    data.server_salt = key.server_salt;
    data.time_offset = key.time_offset;
    detail = "dc " + std::to_string(dc) + ", 256-byte auth key, time offset " + std::to_string(key.time_offset) + " s";
    conn.close();
    return data;
}

// Encrypted round trip: ping over MtSession, verify the pong carries the same ping_id.
void encrypted_ping(mt::PacketChannel& channel, const mt::SessionData& data, Ctx& ctx) {
    ctx.stage = "encrypted_ping";
    std::random_device rd;
    const std::uint64_t ping_id = (static_cast<std::uint64_t>(rd()) << 32) | rd();
    mt::Bytes body;
    put_u32(body, kPingCtor);
    put_u64(body, ping_id);
    mt::MtSession session(channel, data);
    session.set_request_timeout(std::chrono::seconds(20));
    mt::Bytes reply = session.invoke(body);
    if (reply.size() < 20 || get_u32(reply, 0) != kPongCtor) throw std::runtime_error("reply is not pong");
    if (get_u64(reply, 12) != ping_id) throw std::runtime_error("pong ping_id does not match the ping");
}

// ---------- test table ----------

struct TestDef {
    std::string id;
    std::string category;
    std::string summary;
    std::function<Outcome(Ctx&)> body;
};

// ---------- offline ----------

Outcome test_session_roundtrip(Ctx&) {
    const fs::path file = work_dir() / "offline_roundtrip.session";
    mt::SessionData in;
    in.dc_id = 2;
    in.auth_key.assign(256, 0x5a);
    in.server_salt = 0x1122334455667788ULL;
    in.time_offset = -3;
    mt::save_session_file(file.string(), in);

    mt::SessionData out;
    if (!mt::load_session_file(file.string(), out)) throw std::runtime_error("saved session file not found");
    if (out.dc_id != in.dc_id || out.auth_key != in.auth_key || out.server_salt != in.server_salt ||
        out.time_offset != in.time_offset)
        throw std::runtime_error("loaded session differs from saved session");

    // Flip one byte inside the body; the CRC-32 must reject the file rather than accept it.
    {
        std::fstream f(file, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(20);
        char c = 0x7f;
        f.write(&c, 1);
    }
    bool detected = false;
    try {
        mt::SessionData bad;
        mt::load_session_file(file.string(), bad);
    } catch (const mt::SessionCorruptError&) {
        detected = true;
    }
    std::error_code ec;
    fs::remove(file, ec);
    if (!detected) throw std::runtime_error("corrupted session file was accepted");
    return pass("round trip equal; corrupted byte rejected by SessionCorruptError");
}

Outcome test_fake_tls_refused(Ctx& ctx) {
    ctx.stage = "parse_secret";
    const std::string text = "ee" + std::string(32, 'a') + "7777772e676f6f676c652e636f6d";  // ee + key + hex domain
    mt::ProxySecret secret = mt::parse_proxy_secret(text);
    if (secret.kind != mt::ProxySecretKind::FakeTls) throw std::runtime_error("secret was not parsed as fake-TLS");
    ctx.stage = "connect_refused_before_network";
    mt::Connection conn;
    try {
        conn.connect_obfuscated("127.0.0.1", 1, secret, 2, std::chrono::milliseconds(500));
    } catch (const std::invalid_argument&) {
        return pass("fake-TLS secret refused with std::invalid_argument before any network use");
    }
    return {Status::Fail, "fake-TLS secret was not refused", ""};
}

// ---------- environment ----------

Outcome test_connect_refused(Ctx& ctx) {
    ctx.stage = "tcp_connect";
    mt::Connection conn;
    conn.set_connect_timeout(2000);
    try {
        conn.connect("127.0.0.1", 1);
    } catch (const mt::ConnectionError& e) {
        return pass(std::string("refused: ") + clip(e.what()));
    }
    return {Status::Fail, "connect to a closed local port succeeded", ""};
}

// Connects to the same address the interception probe uses. Not a Telegram or proxy address.
Outcome test_connect_unroutable(Ctx& ctx) {
    ctx.stage = "tcp_connect";
    mt::Connection conn;
    conn.set_connect_timeout(1500);
    const auto t0 = std::chrono::steady_clock::now();
    try {
        conn.connect("10.255.255.1", 443);
    } catch (const mt::ConnectionTimeout& e) {
        return pass(std::string("timeout reported: ") + clip(e.what()));
    } catch (const mt::ConnectionError& e) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        return pass("reported ConnectionError after " + std::to_string(ms) + " ms: " + clip(e.what()));
    }
    return {Status::Blocked, "this network accepted a connection to 10.255.255.1 (egress interception); unroutable-address behaviour cannot be tested here", ""};
}

Outcome test_proxy_unreachable(Ctx& ctx) {
    ctx.stage = "proxy_connect";
    mt::ProxySecret secret = mt::parse_proxy_secret(std::string(32, 'b'));
    mt::Connection conn;
    try {
        conn.connect_obfuscated("127.0.0.1", 1, secret, 2, std::chrono::milliseconds(1500));
    } catch (const mt::ProxyError& e) {
        if (!e.transient()) return {Status::Fail, "unreachable proxy not marked transient", classify(e)};
        return pass(std::string("transient proxy error: ") + clip(e.what()));
    }
    return {Status::Fail, "connect to a closed proxy port succeeded", ""};
}

// ---------- live, no credentials ----------

Outcome test_handshake(mt::TransportKind kind, int dc, Ctx& ctx) {
    std::string detail;
    live_handshake(kind, dc, ctx, detail);
    return pass(detail);
}

Outcome test_encrypted_ping(Ctx& ctx) {
    std::string detail;
    mt::SessionData data = live_handshake(mt::TransportKind::Intermediate, 2, ctx, detail);
    mt::Connection conn(mt::TransportKind::Intermediate);
    conn.set_connect_timeout(10000);
    ctx.stage = "tcp_connect_for_ping";
    connect_dc(conn, 2);
    encrypted_ping(conn, data, ctx);
    return pass("encrypted ping answered by pong with matching ping_id");
}

Outcome test_reconnect_restored_key(Ctx& ctx) {
    std::string detail;
    mt::SessionData data = live_handshake(mt::TransportKind::Intermediate, 2, ctx, detail);
    // First connection: one encrypted ping, then the socket is closed (simulated network drop).
    {
        mt::Connection first(mt::TransportKind::Intermediate);
        first.set_connect_timeout(10000);
        connect_dc(first, 2);
        encrypted_ping(first, data, ctx);
        first.close();
    }
    // Second connection: reuse the same key with no new handshake.
    mt::Connection second(mt::TransportKind::Intermediate);
    second.set_connect_timeout(10000);
    ctx.stage = "reconnect_tcp";
    connect_dc(second, 2);
    encrypted_ping(second, data, ctx);
    return pass("dropped and reconnected with the saved key; no new handshake");
}

Outcome test_lifecycle_repeat(Ctx& ctx) {
    std::string detail;
    mt::SessionData data = live_handshake(mt::TransportKind::Intermediate, 2, ctx, detail);
    for (int i = 1; i <= 3; ++i) {
        ctx.stage = "cycle_" + std::to_string(i) + "_connect";
        mt::Connection conn(mt::TransportKind::Intermediate);
        conn.set_connect_timeout(10000);
        connect_dc(conn, 2);
        encrypted_ping(conn, data, ctx);
        conn.close();
    }
    return pass("3 connect / encrypted ping / close cycles over one saved key");
}

// ---------- live, credentials ----------
// Each body reads its credentials here, and only after run_one() has let the test through the network gate.

cppgram::ClientOptions client_options(const std::string& api_id, const std::string& api_hash, const fs::path& session) {
    cppgram::ClientOptions o;
    o.api_id = std::stoi(api_id);
    o.api_hash = api_hash;
    o.session_path = session.string();
    return o;
}

Outcome test_login_bot_and_get_me(Ctx& ctx) {
    auto api_id = env("TG_API_ID"), api_hash = env("TG_API_HASH"), token = env("TG_BOT_TOKEN");
    if (!api_id || !api_hash || !token) return blocked("needs TG_API_ID, TG_API_HASH and TG_BOT_TOKEN in the environment");
    ctx.stage = "client_connect";
    ctx.contacted = true;
    cppgram::Client client(client_options(*api_id, *api_hash, work_dir() / "bot.session"));
    client.connect();
    ctx.stage = "login_bot";
    client.login_bot(*token);
    ctx.stage = "get_me";
    client.get_me();
    client.disconnect();
    return pass("bot login and get_me succeeded");
}

Outcome test_session_restore(Ctx& ctx) {
    auto api_id = env("TG_API_ID"), api_hash = env("TG_API_HASH");
    if (!api_id || !api_hash) return blocked("needs TG_API_ID and TG_API_HASH");
    const fs::path session = work_dir() / "bot.session";
    if (!fs::exists(session)) return blocked("no saved session; run live.login_bot_and_get_me first");
    ctx.stage = "client_restore";
    ctx.contacted = true;
    cppgram::Client client(client_options(*api_id, *api_hash, session));
    client.connect();
    ctx.stage = "get_me";
    client.get_me();
    client.disconnect();
    return pass("saved session restored and get_me succeeded without a new login");
}

// ---------- owner approval ----------

Outcome test_proxy_dd(Ctx& ctx) {
    // The gate has already required owner approval; it is checked again here so this body cannot run without it.
    if (!owner_approved()) return pending("owner approval (CGLT_OWNER_APPROVED=1) not given");
    auto host = env("CGLT_PROXY_HOST"), port = env("CGLT_PROXY_PORT"), secret_text = env("CGLT_PROXY_SECRET");
    if (!host || !port || !secret_text) return pending("needs CGLT_PROXY_HOST, CGLT_PROXY_PORT and CGLT_PROXY_SECRET");
    mt::ProxySecret secret = mt::parse_proxy_secret(*secret_text);
    if (secret.kind != mt::ProxySecretKind::PaddedIntermediate) return blocked("CGLT_PROXY_SECRET must be a dd (padded intermediate) secret");
    ctx.stage = "proxy_connect";
    ctx.contacted = true;
    mt::Connection conn;
    conn.set_connect_timeout(10000);
    conn.connect_obfuscated(*host, static_cast<std::uint16_t>(std::stoi(*port)), secret, 2,
                            std::chrono::milliseconds(10000), mt::ObfuscatedFraming::PaddedIntermediate);
    ctx.stage = "mtproto_handshake";
    PlainConnection plain(conn);
    mt::HandshakeOptions opts;
    opts.dc_id = 2;
    mt::AuthKeyResult key = mt::create_auth_key(plain, opts);
    mt::SessionData data;
    data.dc_id = 2;
    data.auth_key = key.auth_key;
    data.server_salt = key.server_salt;
    data.time_offset = key.time_offset;
    encrypted_ping(conn, data, ctx);
    return pass("dd proxy handshake and encrypted ping succeeded (padded intermediate)");
}

Outcome test_message_send(Ctx&) {
    return not_run("send-message live test is not implemented in this build (needs TG_TEST_CHAT_ID when it is)");
}

Outcome test_flood_wait_controlled(Ctx&) {
    return not_run("no safe controlled trigger exists; FLOOD_WAIT handling is covered by offline fake-DC tests");
}

// ---------- local failure injection (offline: loopback only, never Telegram or a real proxy) ----------

int open_fd_count() {
    int n = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/proc/self/fd", ec)) {
        (void)e;
        ++n;
    }
    return n;
}

// A loopback listener that runs one scripted behaviour on the first accepted connection, then closes it.
class LocalServer {
public:
    using Behavior = std::function<void(int)>;

    explicit LocalServer(Behavior behavior) : behavior_(std::move(behavior)) {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) throw std::runtime_error("cannot create a loopback listener");
        int one = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(listen_fd_, 4) != 0) {
            ::close(listen_fd_);
            throw std::runtime_error("cannot bind a loopback listener");
        }
        socklen_t len = sizeof addr;
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this] {
            const int c = ::accept(listen_fd_, nullptr, nullptr);
            if (c < 0) return;
            timeval tv{10, 0};  // a scripted peer never waits longer than this
            ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            behavior_(c);
            ::close(c);
        });
    }
    ~LocalServer() {
        ::shutdown(listen_fd_, SHUT_RDWR);  // wakes accept() when no client came
        ::close(listen_fd_);
        if (thread_.joinable()) thread_.join();
    }
    LocalServer(const LocalServer&) = delete;
    LocalServer& operator=(const LocalServer&) = delete;

    std::uint16_t port() const { return port_; }

private:
    Behavior behavior_;
    int listen_fd_ = -1;
    std::uint16_t port_ = 0;
    std::thread thread_;
};

// Scripted peers.
void behave_silent(int c) {  // accepts, never answers, returns when the client closes
    char buf[256];
    while (::recv(c, buf, sizeof buf, 0) > 0) {
    }
}
void behave_close(int) {}  // accepts and closes at once
void behave_socks_wrong_version(int c) {
    char buf[64];
    (void)::recv(c, buf, sizeof buf, 0);
    const char reply[2] = {0x04, 0x00};
    (void)::send(c, reply, sizeof reply, MSG_NOSIGNAL);
}
void behave_socks_refuse_connect(int c) {  // no-auth accepted, CONNECT answered with REP 0x05 (connection refused)
    char buf[64];
    (void)::recv(c, buf, sizeof buf, 0);
    const char method_ok[2] = {0x05, 0x00};
    (void)::send(c, method_ok, sizeof method_ok, MSG_NOSIGNAL);
    (void)::recv(c, buf, sizeof buf, 0);
    const char rep[10] = {0x05, 0x05, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
    (void)::send(c, rep, sizeof rep, MSG_NOSIGNAL);
}

// A loopback port with nothing listening on it (bound, then released).
std::uint16_t closed_loopback_port() {
    LocalServer probe(behave_close);
    return probe.port();
}

struct FailureObservation {
    bool threw = false;
    std::string type;  // ProxyError, Socks5Error, ConnectionError, ConnectionTimeout or other
    bool transient = false;
    bool has_transient = false;
    long long ms = 0;
    std::string message;
};

template <class Attempt>
FailureObservation observe_failure(Attempt&& attempt) {
    FailureObservation o;
    const auto t0 = std::chrono::steady_clock::now();
    try {
        attempt();
    } catch (const mt::ProxyError& e) {
        o.type = "ProxyError";
        o.transient = e.transient();
        o.has_transient = true;
        o.message = e.what();
    } catch (const mt::Socks5Error& e) {
        o.type = "Socks5Error";
        o.transient = e.transient();
        o.has_transient = true;
        o.message = e.what();
    } catch (const mt::ConnectionTimeout& e) {
        o.type = "ConnectionTimeout";
        o.message = e.what();
    } catch (const mt::ConnectionError& e) {
        o.type = "ConnectionError";
        o.message = e.what();
    } catch (const std::exception& e) {
        o.type = "other";
        o.message = e.what();
    }
    o.threw = !o.type.empty();
    o.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    o.message = clip(o.message);
    return o;
}

std::string describe(const FailureObservation& o) {
    std::string s = o.type + (o.has_transient ? (o.transient ? " transient" : " final") : "") + " after " +
                    std::to_string(o.ms) + " ms: " + o.message;
    return s;
}

// Client configured to reach Telegram through a local MTProto proxy. The DC is never reached in these tests.
cppgram::ClientOptions local_mtproxy_options(std::uint16_t port, int connect_timeout_ms, const fs::path& session) {
    cppgram::ClientOptions o;
    o.api_id = 1;
    o.api_hash = "0123456789abcdef0123456789abcdef";
    o.session_path = session.string();
    o.dc_ip = "149.154.167.51";
    o.dc_port = 443;
    cppgram::MtprotoProxyOptions p;
    p.host = "127.0.0.1";
    p.port = port;
    p.secret = std::string(32, 'c');  // plain secret, dummy value: the peer never reads it
    p.connect_timeout_ms = connect_timeout_ms;
    o.mtproto_proxy = p;
    o.connect_timeout_ms = connect_timeout_ms;
    return o;
}

// Shared checks for a failure test: the attempt must throw, within the expected time window, with an expected class.
Outcome check_failure(const FailureObservation& o, long long min_ms, long long max_ms,
                      std::initializer_list<const char*> expected_types) {
    if (!o.threw) return {Status::Fail, "the attempt succeeded where a failure was expected", ""};
    bool type_ok = false;
    for (const char* t : expected_types) type_ok = type_ok || o.type == t;
    if (!type_ok) return {Status::Fail, "unexpected error class: " + describe(o), o.type};
    if (o.ms < min_ms || o.ms > max_ms)
        return {Status::Fail, "failure reported outside the expected window (" + std::to_string(min_ms) + " to " +
                                  std::to_string(max_ms) + " ms): " + describe(o), o.type};
    return pass(describe(o));
}

Outcome test_failure_mtproxy_refused(Ctx& ctx) {
    const int fds_before = open_fd_count();
    const fs::path session = work_dir() / "failure_refused.session";
    ctx.stage = "mtproxy_connect_refused";
    FailureObservation o = observe_failure([&] {
        cppgram::Client client(local_mtproxy_options(closed_loopback_port(), 2000, session));
        client.connect();
    });
    std::error_code ec;
    fs::remove(session, ec);
    if (open_fd_count() != fds_before) return {Status::Fail, "file descriptors leaked after the failure", ""};
    return check_failure(o, 0, 5000, {"ProxyError", "ConnectionError"});
}

Outcome test_failure_mtproxy_silent_timeout(Ctx& ctx) {
    const int fds_before = open_fd_count();
    const fs::path session = work_dir() / "failure_silent.session";
    FailureObservation o;
    {
        LocalServer silent(behave_silent);  // stays alive for the attempt, and is closed before the descriptor check
        ctx.stage = "mtproxy_silent_peer";
        o = observe_failure([&] {
            cppgram::Client client(local_mtproxy_options(silent.port(), 1000, session));
            client.connect();
        });
    }
    std::error_code ec;
    fs::remove(session, ec);
    if (open_fd_count() != fds_before) return {Status::Fail, "file descriptors leaked after the timeout", ""};
    return check_failure(o, 800, 8000, {"ProxyError", "ConnectionTimeout"});
}

Outcome test_failure_mtproxy_closes_early(Ctx& ctx) {
    const int fds_before = open_fd_count();
    const fs::path session = work_dir() / "failure_close.session";
    FailureObservation o;
    {
        LocalServer closer(behave_close);
        ctx.stage = "mtproxy_peer_closes";
        o = observe_failure([&] {
            cppgram::Client client(local_mtproxy_options(closer.port(), 2000, session));
            client.connect();
        });
    }
    std::error_code ec;
    fs::remove(session, ec);
    if (open_fd_count() != fds_before) return {Status::Fail, "file descriptors leaked after the failure", ""};
    return check_failure(o, 0, 8000, {"ProxyError", "ConnectionError"});
}

// After failed attempts, a fresh connection to a healthy listener must still work (no global state is left broken).
Outcome test_failure_recovery(Ctx& ctx) {
    const std::uint16_t dead = closed_loopback_port();
    FailureObservation first = observe_failure([&] {
        cppgram::Client client(local_mtproxy_options(dead, 1000, work_dir() / "failure_recover.session"));
        client.connect();
    });
    if (!first.threw) return {Status::Fail, "the attempt against a closed port succeeded", ""};
    LocalServer healthy(behave_silent);
    ctx.stage = "recovery_connect";
    mt::Connection conn;
    conn.set_connect_timeout(2000);
    conn.connect("127.0.0.1", healthy.port());
    conn.close();
    std::error_code ec;
    fs::remove(work_dir() / "failure_recover.session", ec);
    return pass("after " + first.type + ", a new connection to a healthy loopback listener succeeded");
}

Outcome test_failure_socks5_refused(Ctx& ctx) {
    ctx.stage = "socks5_connect_refused";
    mt::Socks5Options opts;
    opts.proxy_host = "127.0.0.1";
    opts.proxy_port = closed_loopback_port();
    opts.timeout = std::chrono::milliseconds(2000);
    FailureObservation o = observe_failure([&] { mt::socks5_connect(opts, "149.154.167.51", 443); });
    if (!o.threw || o.type != "Socks5Error" || !o.transient) return {Status::Fail, "expected a transient Socks5Error: " + describe(o), o.type};
    return pass(describe(o));
}

Outcome test_failure_socks5_silent_timeout(Ctx& ctx) {
    LocalServer silent(behave_silent);
    ctx.stage = "socks5_silent_peer";
    mt::Socks5Options opts;
    opts.proxy_host = "127.0.0.1";
    opts.proxy_port = silent.port();
    opts.timeout = std::chrono::milliseconds(1000);
    FailureObservation o = observe_failure([&] { mt::socks5_connect(opts, "149.154.167.51", 443); });
    if (!o.threw || o.type != "Socks5Error" || !o.transient) return {Status::Fail, "expected a transient Socks5Error: " + describe(o), o.type};
    if (o.ms < 800 || o.ms > 8000) return {Status::Fail, "timeout outside the expected window: " + describe(o), o.type};
    return pass(describe(o));
}

Outcome test_failure_socks5_wrong_version(Ctx& ctx) {
    LocalServer bad(behave_socks_wrong_version);
    ctx.stage = "socks5_wrong_version";
    mt::Socks5Options opts;
    opts.proxy_host = "127.0.0.1";
    opts.proxy_port = bad.port();
    opts.timeout = std::chrono::milliseconds(2000);
    FailureObservation o = observe_failure([&] { mt::socks5_connect(opts, "149.154.167.51", 443); });
    if (!o.threw || o.type != "Socks5Error") return {Status::Fail, "expected a Socks5Error: " + describe(o), o.type};
    if (o.transient) return {Status::Fail, "a protocol violation was marked transient", o.type};
    return pass(describe(o));
}

Outcome test_failure_socks5_refused_reply(Ctx& ctx) {
    LocalServer refuser(behave_socks_refuse_connect);
    ctx.stage = "socks5_connect_reply";
    mt::Socks5Options opts;
    opts.proxy_host = "127.0.0.1";
    opts.proxy_port = refuser.port();
    opts.timeout = std::chrono::milliseconds(2000);
    bool code_ok = false;
    FailureObservation o;
    try {
        mt::socks5_connect(opts, "149.154.167.51", 443);
    } catch (const mt::Socks5Error& e) {
        code_ok = e.reply_code() == 0x05;
        o.threw = true;
        o.type = "Socks5Error";
        o.transient = e.transient();
        o.has_transient = true;
        o.message = clip(e.what());
    }
    if (!o.threw) return {Status::Fail, "CONNECT with REP 0x05 succeeded", ""};
    if (!code_ok) return {Status::Fail, "reply code was not kept (expected 0x05): " + o.message, o.type};
    if (o.transient) return {Status::Fail, "REP 0x05 was marked transient", o.type};
    return pass("REP 0x05 kept as reply_code 5, final (not retried): " + o.message);
}

// ---------- owner approval: SOCKS5 and message loop (live; never run without owner approval) ----------

struct OwnerEnv {
    std::string api_id, api_hash, token;
};

// Reads the owner's credentials. Called only after the gate has allowed the test.
std::optional<OwnerEnv> owner_bot_env() {
    auto id = env("TG_API_ID"), hash = env("TG_API_HASH"), token = env("TG_BOT_TOKEN");
    if (!id || !hash || !token) return std::nullopt;
    return OwnerEnv{*id, *hash, *token};
}

Outcome test_owner_socks5(Ctx& ctx) {
    if (!owner_approved()) return pending("owner approval (CGLT_OWNER_APPROVED=1) not given");
    auto host = env("CGLT_SOCKS5_HOST"), port = env("CGLT_SOCKS5_PORT");
    if (!host || !port) return blocked("needs CGLT_SOCKS5_HOST and CGLT_SOCKS5_PORT (owner-provided SOCKS5 endpoint)");
    auto user = env("CGLT_SOCKS5_USER"), pass_text = env("CGLT_SOCKS5_PASS");
    if (static_cast<bool>(user) != static_cast<bool>(pass_text)) return blocked("set CGLT_SOCKS5_USER and CGLT_SOCKS5_PASS together, or neither");
    auto creds = owner_bot_env();
    if (!creds) return blocked("needs TG_API_ID, TG_API_HASH and TG_BOT_TOKEN in the environment");

    cppgram::ClientOptions o;
    o.api_id = std::stoi(creds->api_id);
    o.api_hash = creds->api_hash;
    o.session_path = (work_dir() / "owner_socks5.session").string();
    mt::Socks5Options s;
    s.proxy_host = *host;
    s.proxy_port = static_cast<std::uint16_t>(std::stoi(*port));
    if (user) s.auth = mt::Socks5Auth{*user, *pass_text};
    o.socks5 = s;

    ctx.stage = "socks5_client_connect";
    ctx.contacted = true;
    const auto t0 = std::chrono::steady_clock::now();
    cppgram::Client client(o);
    client.connect();
    ctx.stage = "login_bot";
    client.login_bot(creds->token);
    ctx.stage = "get_me";
    const auto me = client.get_me();
    ctx.stage = "clean_disconnect";
    client.disconnect();
    ctx.stage = "reconnect";
    client.connect();
    client.get_me();
    client.disconnect();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::error_code ec;
    fs::remove(work_dir() / "owner_socks5.session", ec);
    return pass("SOCKS5 connect, bot login, get_me (user id " + std::to_string(me.id) + "), reconnect and clean disconnect in " +
                std::to_string(ms) + " ms");
}

Outcome test_owner_message_loop(Ctx& ctx) {
    if (!owner_approved()) return pending("owner approval (CGLT_OWNER_APPROVED=1) not given");
    auto chat_text = env("CGLT_TEST_CHAT_ID");
    if (!chat_text) return blocked("needs CGLT_TEST_CHAT_ID: the one authorised test chat id. No other chat is ever answered");
    std::int64_t allowed_chat = 0;
    try {
        allowed_chat = std::stoll(*chat_text);
    } catch (const std::exception&) {
        return blocked("CGLT_TEST_CHAT_ID must be a number");
    }
    int wait_s = 120;
    if (auto w = env("CGLT_MESSAGE_WAIT_SECONDS")) {
        try {
            wait_s = std::stoi(*w);
        } catch (const std::exception&) {
            return blocked("CGLT_MESSAGE_WAIT_SECONDS must be a number");
        }
        if (wait_s < 10 || wait_s > 600) return blocked("CGLT_MESSAGE_WAIT_SECONDS must be between 10 and 600");
    }
    auto creds = owner_bot_env();
    if (!creds) return blocked("needs TG_API_ID, TG_API_HASH and TG_BOT_TOKEN in the environment");

    cppgram::ClientOptions o;
    o.api_id = std::stoi(creds->api_id);
    o.api_hash = creds->api_hash;
    o.session_path = (work_dir() / "owner_loop.session").string();
    ctx.stage = "client_connect";
    ctx.contacted = true;
    cppgram::Client client(o);

    std::atomic<bool> answered{false};
    std::atomic<bool> stop_requested{false};
    std::atomic<int> errors{0};
    std::atomic<bool> flood_wait_seen{false};
    std::string error_text;
    std::mutex error_mutex;
    std::int32_t reply_id = 0;

    client.on_error([&](cppgram::Client&, const std::string& stage, const std::string& message) {
        ++errors;
        if (message.find("FLOOD_WAIT") != std::string::npos) flood_wait_seen = true;
        std::lock_guard<std::mutex> lock(error_mutex);
        if (error_text.size() < 200) error_text += stage + ": " + clip(message) + "; ";
    });
    // Only the authorised chat is matched, and only the exact command. Any other message is ignored.
    client.on_message(cppgram::Filter([allowed_chat](const cppgram::Message& m) {
                          return m.chat.id == allowed_chat && !m.outgoing && m.text == "/test";
                      }),
                      [&](cppgram::Client& c, const cppgram::Message& m) {
                          if (answered.exchange(true)) return;
                          reply_id = c.send_text(m.chat, "cppgram live test: update received").id;
                          c.stop();
                      });

    ctx.stage = "login_bot";
    client.connect();
    client.login_bot(creds->token);
    ctx.stage = "update_loop";
    std::thread watchdog([&] {
        for (int i = 0; i < wait_s * 10 && !answered && !stop_requested; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!answered) client.stop();
    });
    const auto t0 = std::chrono::steady_clock::now();
    client.run();
    const auto shutdown_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    stop_requested = true;
    watchdog.join();
    ctx.stage = "clean_disconnect";
    client.disconnect();
    std::error_code ec;
    fs::remove(work_dir() / "owner_loop.session", ec);

    std::string errs;
    {
        std::lock_guard<std::mutex> lock(error_mutex);
        errs = error_text;
    }
    const std::string flood = flood_wait_seen ? " FLOOD_WAIT observed during the run (handled by the client)." : "";
    if (!answered) {
        return not_run("no /test message from the authorised chat within " + std::to_string(wait_s) +
                       " s; run returned after stop in " + std::to_string(shutdown_ms) + " ms; errors: " + (errs.empty() ? "none" : errs) + flood);
    }
    return pass("update received from the authorised chat, reply sent (message id " + std::to_string(reply_id) +
                "), run returned after stop in " + std::to_string(shutdown_ms) + " ms; handler errors: " +
                std::to_string(errors.load()) + flood);
}

// ---------- table ----------

std::vector<TestDef> build_tests() {
    return {
        {"offline.session_roundtrip", "offline", "save and load a session; a corrupted file is rejected",
         [](Ctx& c) { return test_session_roundtrip(c); }},
        {"offline.fake_tls_refused", "offline", "fake-TLS proxy secret is refused before any network use", test_fake_tls_refused},
        {"offline.failure_mtproxy_refused", "offline", "MTProxy to a closed loopback port: transient error, no leaked descriptors", test_failure_mtproxy_refused},
        {"offline.failure_mtproxy_silent_timeout", "offline", "MTProxy peer that never answers: timeout within the configured window", test_failure_mtproxy_silent_timeout},
        {"offline.failure_mtproxy_closes_early", "offline", "MTProxy peer that closes at once: error reported, no leak", test_failure_mtproxy_closes_early},
        {"offline.failure_recovery", "offline", "after failed attempts, a new connection to a healthy listener works", test_failure_recovery},
        {"offline.failure_socks5_refused", "offline", "SOCKS5 to a closed loopback port: transient Socks5Error", test_failure_socks5_refused},
        {"offline.failure_socks5_silent_timeout", "offline", "SOCKS5 peer that never answers: transient timeout", test_failure_socks5_silent_timeout},
        {"offline.failure_socks5_wrong_version", "offline", "SOCKS5 peer with wrong version: final (not retried) error", test_failure_socks5_wrong_version},
        {"offline.failure_socks5_refused_reply", "offline", "SOCKS5 CONNECT answered REP 0x05: reply code kept, final", test_failure_socks5_refused_reply},
        {"environment.connect_refused", "environment", "closed local port reports ConnectionError", test_connect_refused},
        {"environment.connect_unroutable", "environment", "unroutable address fails with a timeout or ConnectionError", test_connect_unroutable},
        {"environment.proxy_unreachable", "environment", "unreachable MTProto proxy reports a transient ProxyError", test_proxy_unreachable},
        {"live.handshake_intermediate_dc2", "live_no_credentials", "real handshake, intermediate transport, DC 2",
         [](Ctx& c) { return test_handshake(mt::TransportKind::Intermediate, 2, c); }},
        {"live.handshake_abridged_dc2", "live_no_credentials", "real handshake, abridged transport, DC 2",
         [](Ctx& c) { return test_handshake(mt::TransportKind::Abridged, 2, c); }},
        {"live.handshake_full_dc2", "live_no_credentials", "real handshake, full transport, DC 2",
         [](Ctx& c) { return test_handshake(mt::TransportKind::Full, 2, c); }},
        {"live.encrypted_ping_dc2", "live_no_credentials", "handshake then encrypted ping/pong against the real server", test_encrypted_ping},
        {"live.reconnect_restored_key_dc2", "live_no_credentials", "drop and reconnect, reusing the saved key", test_reconnect_restored_key},
        {"live.lifecycle_repeat_dc2", "live_no_credentials", "three connect/ping/close cycles over one key", test_lifecycle_repeat},
        {"live.login_bot_and_get_me", "live_credentials", "bot login then get_me", test_login_bot_and_get_me},
        {"live.session_restore", "live_credentials", "restore saved session then get_me without login", test_session_restore},
        {"owner.proxy_dd", "owner_approval", "dd proxy (padded intermediate) against an owner-provided proxy", test_proxy_dd},
        {"owner.send_message", "owner_approval", "single send: performed inside owner.message_loop; no standalone send test", test_message_send},
        {"owner.flood_wait_controlled", "owner_approval", "FLOOD_WAIT is not provoked; a natural one is recorded by owner.message_loop", test_flood_wait_controlled},
        {"owner.socks5", "owner_approval", "SOCKS5 proxy (owner endpoint): connect, bot login, get_me, reconnect, clean disconnect", test_owner_socks5},
        {"owner.message_loop", "owner_approval", "client.run() receives /test from the authorised chat, replies, shuts down cleanly", test_owner_message_loop},
    };
}

// ---------- runner ----------

// Runs one test through the network gate. The body (and so every credential read and socket) runs only on Allow.
Result run_one(const TestDef& t, const livegate::GateInputs& in) {
    Ctx ctx;
    Outcome out;
    const auto started = std::chrono::steady_clock::now();
    const livegate::TestGate gate = livegate::gate_for_category(t.category);
    const livegate::Decision d = livegate::run_if_allowed(gate, in, [&]() {
        try {
            out = t.body(ctx);
        } catch (const std::exception& e) {
            out.status = Status::Fail;
            out.error_class = classify(e);
            out.detail = clip(e.what());
        } catch (...) {
            out.status = Status::Fail;
            out.error_class = "unknown";
            out.detail = "non-standard exception";
        }
    });
    if (d.verdict == livegate::Verdict::Blocked) out = blocked(d.reason);
    if (d.verdict == livegate::Verdict::Pending) out = pending(d.reason);

    Result r;
    r.id = t.id;
    r.category = t.category;
    r.stage = ctx.stage;
    r.contacted_telegram = ctx.contacted;
    r.status = out.status;
    r.detail = clip(out.detail);
    r.error_class = out.error_class;
    r.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    return r;
}

std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
        else o += c;
    }
    return o;
}

const std::vector<std::string> kCategories = {"offline", "environment", "live_no_credentials", "live_credentials", "owner_approval"};

void usage_error(const char* msg) {
    std::cerr << msg << "\n";
    std::cerr << "usage: cppgram_livetest [--list] [--category NAME] [--test ID] [--json FILE] [--workdir DIR]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string only_category, only_test, json_path;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--list") list = true;
        else if (a == "--category" && i + 1 < argc) only_category = argv[++i];
        else if (a == "--test" && i + 1 < argc) only_test = argv[++i];
        else if (a == "--json" && i + 1 < argc) json_path = argv[++i];
        else if (a == "--workdir" && i + 1 < argc) g_work_dir = argv[++i];
        else {
            usage_error("unrecognised or incomplete option");
            return 2;
        }
    }

    const std::vector<TestDef> tests = build_tests();
    if (list) {
        for (const auto& t : tests) std::cout << t.category << "  " << t.id << "  " << t.summary << "\n";
        return 0;
    }

    // Selection is validated before anything runs. An unknown name must never produce a silent zero-test run.
    if (!only_category.empty() && std::find(kCategories.begin(), kCategories.end(), only_category) == kCategories.end()) {
        usage_error("unknown category (use one of offline, environment, live_no_credentials, live_credentials, owner_approval)");
        return 2;
    }
    if (!only_test.empty() && std::none_of(tests.begin(), tests.end(), [&](const TestDef& t) { return t.id == only_test; })) {
        usage_error("unknown test id (use --list)");
        return 2;
    }
    std::vector<const TestDef*> selected;
    for (const auto& t : tests) {
        if (!only_category.empty() && t.category != only_category) continue;
        if (!only_test.empty() && t.id != only_test) continue;
        selected.push_back(&t);
    }
    if (selected.empty()) {
        usage_error("no test matches the --category and --test selection");
        return 2;
    }

    // The JSON file is opened before any test runs, so a path that cannot be written is reported, not lost at the end.
    std::ofstream json_out;
    if (!json_path.empty()) {
        json_out.open(json_path);
        if (!json_out) {
            usage_error("cannot write the --json file");
            return 2;
        }
    }

    // The network gate runs once, before any credential is read. Only offline tests skip it.
    bool needs_network = false;
    for (const TestDef* t : selected) needs_network = needs_network || livegate::gate_for_category(t->category).network;
    livegate::GateInputs in;
    in.probe = needs_network ? livegate::probe_network() : livegate::Probe::NotRun;
    in.unrestricted_declared = unrestricted_declared();
    in.owner_approved = owner_approved();

    std::cout << "cppgram_livetest\n";
    std::cout << "  library version : " << CGLT_CPPGRAM_VERSION << "\n";
    std::cout << "  library tree    : " << CGLT_TREE_SHA256 << "\n";
    std::cout << "  network probe   : " << livegate::probe_text(in.probe) << "\n";
    std::cout << "  unrestricted    : " << (in.unrestricted_declared ? "declared" : "not declared") << "\n";
    std::cout << "  owner approval  : " << (in.owner_approved ? "given" : "not given") << "\n\n";

    std::vector<Result> results;
    int failures = 0;
    for (const TestDef* t : selected) {
        Result r = run_one(*t, in);
        if (r.status == Status::Fail) ++failures;
        std::cout << "[" << status_text(r.status) << "] " << r.id << "  (" << r.category << ", stage=" << r.stage
                  << ", telegram_contact=" << (r.contacted_telegram ? "yes" : "no") << ", " << r.duration_ms << " ms)\n";
        if (!r.detail.empty()) std::cout << "    " << r.detail << "\n";
        if (!r.error_class.empty()) std::cout << "    error: " << r.error_class << "\n";
        results.push_back(r);
    }

    if (json_out.is_open()) {
        json_out << "{\n  \"library_version\": \"" << CGLT_CPPGRAM_VERSION << "\",\n  \"library_tree_sha256\": \""
                 << CGLT_TREE_SHA256 << "\",\n  \"network_probe\": \"" << livegate::probe_text(in.probe)
                 << "\",\n  \"unrestricted_declared\": " << (in.unrestricted_declared ? "true" : "false")
                 << ",\n  \"owner_approved\": " << (in.owner_approved ? "true" : "false") << ",\n  \"results\": [\n";
        for (std::size_t i = 0; i < results.size(); ++i) {
            const Result& r = results[i];
            json_out << "    {\"id\": \"" << json_escape(r.id) << "\", \"category\": \"" << r.category << "\", \"status\": \""
                     << status_text(r.status) << "\", \"stage\": \"" << json_escape(r.stage) << "\", \"error_class\": \""
                     << json_escape(r.error_class) << "\", \"contacted_telegram\": " << (r.contacted_telegram ? "true" : "false")
                     << ", \"duration_ms\": " << r.duration_ms << ", \"detail\": \"" << json_escape(r.detail) << "\"}"
                     << (i + 1 < results.size() ? "," : "") << "\n";
        }
        json_out << "  ]\n}\n";
        json_out.flush();
        if (!json_out) {
            usage_error("failed while writing the --json file");
            return 2;
        }
    }
    return failures == 0 ? 0 : 1;
}
