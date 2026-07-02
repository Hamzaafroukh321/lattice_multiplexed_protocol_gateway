# Architecture Decisions

## ADR-0001: Compact Single-Loop MVP Core

Context: The specification requires actor-confined mutable protocol state with future loop sharding.

Decision: Implement the current `ConnectionEngine` as a deterministic single-loop owner. It processes decoded frames synchronously and records replay bytes before exposing outbound frames.

Alternatives considered: introduce worker threads immediately, or make channel objects independently mutable. Both would increase race risk before the protocol invariants are covered by tests.

Consequences: The MVP path is deterministic and testable. Stable connection-to-shard routing is implemented; threaded loop execution and TSan stress remain full-version work.

Validation: Unit and integration test sources cover negotiation, generated channels, fragment delivery, stale generations, and credit conservation.

## ADR-0002: Static Plugin Registry First

Context: The spec requires plugin family/schema negotiation and eventual quiescent unload.

Decision: Use static C++ factories and a built-in echo plugin. Registry entries are immutable descriptors plus factories; dynamic code loading is excluded.

Alternatives considered: shared-library loading or opaque payload forwarding. Those conflict with the non-goals and would complicate safe unload before schema negotiation is stable.

Consequences: Plugin dispatch is real and schema-checked. Quiescent unregister is implemented for active and queued dispatch leases; dynamic shared-library unloading remains outside the current static plugin model.

Validation: `SchemaHashMismatchRejectsOpen` and gateway policy reject schema mismatch.

## ADR-0003: Portable Memory Transport As The Verified Path

Context: The MVP permits memory or Unix transports, while this environment is Windows for verification and POSIX Unix-domain sockets are not available at runtime.

Decision: Implement deterministic memory transport and POSIX Unix-domain transport/listener adapters. Windows builds expose the same API but return stable transport errors for Unix socket operations. The CLI exposes `probe --socket`, one-connection `serve --socket --plugin echo`, and `bridge --left --right --policy` negotiation/route-validation paths over those adapters.

Alternatives considered: Windows named pipes or a stub that reports success. Reporting success would be misleading; named pipes are outside the spec's main platform.

Consequences: Protocol correctness can be tested without OS socket timing, and POSIX hosts can exercise `UnixTransport::pair_for_test`, `connect_path`, `UnixListener::bind_path`/`accept_one`, and the socket CLI negotiation surface. Full continuous bridge data-plane forwarding still needs channel-pump lifecycle work.

Validation: `TwoMemoryTransportsCompleteHello` covers the memory path. `UnixTransportPairRoundTripOrPortableError` and `UnixListenerBindOrPortableError` cover POSIX primitives or Windows unsupported behavior. CTest covers memory CLI commands and Windows unsupported Unix CLI commands.
