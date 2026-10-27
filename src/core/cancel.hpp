#ifndef SILLOK_CORE_CANCEL_HPP_
#define SILLOK_CORE_CANCEL_HPP_

#include <stdexcept>
#include <stop_token>

namespace sillok {

// Thrown when work stops because cancellation was requested.
class Cancelled : public std::runtime_error {
 public:
  Cancelled() : std::runtime_error("cancelled") {}
};

inline void ThrowIfStopped(const std::stop_token& stop) {
  if (stop.stop_requested()) throw Cancelled();
}

}  // namespace sillok

#endif  // SILLOK_CORE_CANCEL_HPP_
