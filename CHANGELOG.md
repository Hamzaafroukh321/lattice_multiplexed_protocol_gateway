# Changelog

## 0.1.0

- Added initial C++20 Lattice core.
- Added LTX/1 frame codec with CRC32C, ULEB128, TLVs, and streaming decode.
- Added HELLO negotiation, generated channels, flow accounting, reassembly, replay retention, echo plugin, gateway policy, CLI, tests, fuzz smoke targets, and documentation.
- Added ACK payload helpers, resume-window checks, generation-tagged timer wheel, bounded outbound scheduler, and deterministic trace parser.
- Integrated engine ACK/PING/PONG/RESUME timeout paths, scheduler-backed emission, plugin completion tokens, and quiescent plugin leases.
