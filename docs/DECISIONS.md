# Architecture Decisions

## ADR-0001: Compact Single-Loop MVP Core

Context: The specification requires actor-confined mutable protocol state with future loop sharding.

Decision: Implement the current `ConnectionEngine` as a deterministic single-loop owner. It processes decoded frames synchronously and records replay bytes before exposing outbound frames.

Alternatives considered: introduce worker threads immediately, or make channel objects independently mutable. Both would increase race risk before the protocol invariants are covered by tests.

Consequences: The MVP path is deterministic and testable. Full multi-connection sharding, TSan stress, and bounded executor queues remain full-version work.

Validation: Unit and integration test sources cover negotiation, generated channels, fragment delivery, stale generations, and credit conservation.

## ADR-0002: Static Plugin Registry First

Context: The spec requires plugin family/schema negotiation and eventual quiescent unload.

Decision: Use static C++ factories and a built-in echo plugin. Registry entries are immutable descriptors plus factories; dynamic code loading is excluded.

Alternatives considered: shared-library loading or opaque payload forwarding. Those conflict with the non-goals and would complicate safe unload before schema negotiation is stable.

Consequences: Plugin dispatch is real and schema-checked, but quiescent dynamic unload remains to be implemented.

Validation: `SchemaHashMismatchRejectsOpen` and gateway policy reject schema mismatch.

## ADR-0003: Portable Memory Transport As The Verified Path

Context: The MVP permits memory or Unix transports, while this environment is Windows for verification and POSIX Unix-domain sockets are not available at runtime.

Decision: Implement deterministic memory transport and a POSIX Unix-domain adapter. Windows builds expose the same API but return stable transport errors for Unix socket operations. Unix socket serve/bridge CLI lifecycle remains a later endpoint-integration task.

Alternatives considered: Windows named pipes or a stub that reports success. Reporting success would be misleading; named pipes are outside the spec's main platform.

Consequences: Protocol correctness can be tested without OS socket timing, and POSIX hosts can exercise `UnixTransport::pair_for_test` and `connect_path`. Full CLI bridge behavior still needs endpoint lifecycle tests.

Validation: `TwoMemoryTransportsCompleteHello` covers the memory path. `UnixTransportPairRoundTripOrPortableError` covers POSIX round trip or Windows unsupported behavior.
