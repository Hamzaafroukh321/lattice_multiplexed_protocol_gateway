#include "lattice/frame.hpp"

namespace lattice {

// The current scheduler is intentionally represented by ConnectionEngine::emit:
// it records replay bytes before exposing them to transports, preserving per-channel
// FIFO order in the deterministic single-loop MVP.

}  // namespace lattice
