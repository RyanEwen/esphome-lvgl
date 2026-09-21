// Helpers for features/idle/idle.yaml.
#pragma once

#include <cstdint>

// An idle timeout in milliseconds from a minutes setting; 0 (or less) means
// never, which on_idle reads as a threshold the inactivity time cannot pass.
inline uint32_t idle_ms(float minutes) { return minutes > 0 ? (uint32_t) (minutes * 60000.0f) : UINT32_MAX; }
