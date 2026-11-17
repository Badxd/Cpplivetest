// cppgram_livetest: live-test runner for the CppGram library.
//
// Categories (each test belongs to exactly one):
//   offline              no network; pure local logic (session file, proxy secret parsing)
//   environment          local failure paths (closed ports, unroutable address); no Telegram or proxy contact
//   live_no_credentials  real Telegram DC: TCP, MTProto handshake, encrypted ping. No account is used
//   live_credentials     real Telegram account actions. Credentials are read only after the network gate allows the test
//   owner_approval       needs owner infrastructure or approval. Never runs without CGLT_OWNER_APPROVED=1
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
#include <iostream>
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

// ---------- table ----------

std::vector<TestDef> build_tests() {
    return {
        {"offline.session_roundtrip", "offline", "save and load a session; a corrupted file is rejected",
         [](Ctx& c) { return test_session_roundtrip(c); }},
        {"offline.fake_tls_refused", "offline", "fake-TLS proxy secret is refused before any network use", test_fake_tls_refused},
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
        {"owner.send_message", "owner_approval", "post one message to a test chat", test_message_send},
        {"owner.flood_wait_controlled", "owner_approval", "observe and handle a real FLOOD_WAIT", test_flood_wait_controlled},
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
