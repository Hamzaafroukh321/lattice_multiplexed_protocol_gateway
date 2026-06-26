# Security

Lattice treats all transport bytes, frame payloads, extension values, plugin messages, and trace inputs as untrusted.

Defensive rules:

- Check lengths, offsets, and arithmetic before allocation or slicing.
- Reject non-canonical ULEB128 and CRC mismatch.
- Do not resynchronize after corrupt magic or CRC failure.
- Dispatch plugins only after complete message reassembly.
- Reject schema-mismatched opaque forwarding.
- Use stable diagnostic codes and validated IDs/ranges only.

This project does not implement TLS, authentication, remote code loading, or general RPC security. It is intended as a local reliable-transport protocol core.

Report security issues through the repository owner until a formal disclosure channel is published.
