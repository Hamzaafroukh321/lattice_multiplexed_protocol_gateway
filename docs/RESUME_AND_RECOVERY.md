# Resume And Recovery

Implemented now:

- Outbound frames are recorded in `ReplayWindow` with epoch and frame sequence.
- ACK ranges retire exact encoded frames.
- Retry returns the original encoded bytes.
- Epoch mismatch rejects replay operations.
- ACK payloads have a canonical epoch/count/range encoding and reject overlapping ranges.
- `TimerWheel` schedules, cancels, and expires generation-tagged retry/idle/drain timers deterministically.
- `ResumeProof` rejects requests outside the retained replay window.
- `TraceLog` serializes and parses deterministic `LTXTRACE/1` text traces.

Remaining full-version work:

- RESUME frame validation against prior session ID and transcript hash inside `ConnectionEngine`.
- Engine integration for keepalive and retransmission timers.
- Durable retained state across process restart.
- Full replay tool that re-injects API events, timers, transport completions, and plugin results.

Recovery must reject incomplete retained state rather than inventing delivery summaries.
