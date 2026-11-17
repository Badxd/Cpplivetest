# Diagnostics: why live results from the development sandbox are not valid

Recorded 2026-11-15 during development. These observations justify the BLOCKED status of every live check in this
release. They are not library test results, and no library behaviour is concluded from them.

## Observations

1. **TCP to Telegram DC 2 (149.154.167.51:443) connects.** The `Connection::connect` call returned, so TCP itself
   worked from the sandbox.
2. **The MTProto exchange does not complete.** On a direct intermediate or full connection, the first request
   (`req_pq_multi`) got no reply, and the 15 s receive timeout fired (`ConnectionTimeout`, stage `mtproto_handshake`).
   On an abridged connection the peer closed the socket after about 1 s (`connection closed by server`).
3. **The reply to an encrypted-session request began with ASCII `HTTP`.** The first four reply bytes were
   `48 54 54 50`, which reads as the little-endian word `0x50545448` in the intermediate framing. The library reported
   `intermediate frame too large (length word 0x50545448)`. An MTProto server does not send HTTP text, so the path
   answered with an HTTP responder.
4. **TCP to an unroutable address succeeds.** A connect to `10.255.255.1:443` was accepted. A private address that
   nothing should accept is accepted, so the network intercepts TCP connections in general.

## Conclusion

The sandbox intercepts connections and does not relay MTProto to Telegram. Live results obtained from it cannot show
how CppGram behaves against Telegram. The runner therefore:

- probes for interception first (step 4), and treats any live check as BLOCKED when it finds interception, even if
  `CGLT_NETWORK_UNRESTRICTED=1` is set (verified in `TEST_RESULTS.md`, section 3);
- does not contact Telegram at all in that case (`telegram_contact=no`).

## What would resolve it

Run `cppgram_livetest` on a machine with unrestricted outbound access to Telegram's DC addresses, such as a home
computer or a VPS you control, with `CGLT_NETWORK_UNRESTRICTED=1`. The probe must report that the unroutable address is
refused or times out. Record the output with `LIVE_RESULT_TEMPLATE.md`.
