# Contributing

Keep changes small and requirement-linked. Update `docs/REQUIREMENTS_TRACEABILITY.md` and `docs/IMPLEMENTATION_STATUS.md` when behavior, verification, or blockers change.

Code style:

- C++20.
- RAII for ownership.
- `Result<T>` for recoverable protocol errors.
- Fixed-width integers for serialized values.
- Explicit endian conversion.
- No unchecked offset, length, or credit arithmetic.
- No network downloads during normal builds.

Before submitting changes, run the debug tests, relevant sanitizer build, and fuzz smoke targets where the local toolchain supports them.
