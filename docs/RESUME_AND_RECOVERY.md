# Resume And Recovery

Implemented now:

- Outbound frames are recorded in `ReplayWindow` with epoch and frame sequence.
- ACK ranges retire exact encoded frames.
- Retry returns the original encoded bytes.
- Epoch mismatch rejects replay operations.

Remaining full-version work:

- RESUME frame validation against prior session ID and transcript hash.
- Retained range proof for reconnect.
- Keepalive and retransmission timers.
- Canonical trace writer and replay tool for API events, timers, transport completions, and plugin results.

Recovery must reject incomplete retained state rather than inventing delivery summaries.
