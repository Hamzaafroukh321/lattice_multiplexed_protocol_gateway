# Resume And Recovery

Implemented now:

- Outbound frames are recorded in `ReplayWindow` with epoch and frame sequence.
- ACK ranges retire exact encoded frames.
- Retry returns the original encoded bytes.
- Epoch mismatch rejects replay operations.
- ACK payloads have a canonical epoch/count/range encoding and reject overlapping ranges.
- `TimerWheel` schedules, cancels, and expires generation-tagged retry/idle/drain timers deterministically.
- `ResumeProof` rejects requests outside the retained replay window.
- Valid RESUME requests return the exact retained encoded frame suffix from `first_required_seq`.
- `ReplayWindow` serializes and restores canonical `LTXREPLAY/1` retained-state snapshots.
- Corrupt, unordered, or malformed replay snapshots reject with `ResumeRejected`.
- `TraceLog` serializes, parses, and verifies canonical deterministic `LTXTRACE/1` replay summaries.
- `ConnectionEngine` handles ACK, PING/PONG, missed-PONG liveness timeout, malformed RESUME rejection, handshake timeout, retry timer, idle ping, and bounded drain timer paths.

Remaining full-version work:

- Process-level integration for loading `LTXREPLAY/1` snapshots during startup.
- Full replay tool that re-injects API events, timers, transport completions, and plugin results beyond the current canonical summary verification.

Recovery must reject incomplete retained state rather than inventing delivery summaries.
