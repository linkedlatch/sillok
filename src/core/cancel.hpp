#ifndef SILLOK_CORE_CANCEL_HPP_
#define SILLOK_CORE_CANCEL_HPP_

#include <atomic>
#include <memory>
#include <stdexcept>

namespace sillok {

// Thrown when work stops because cancellation was requested.
class Cancelled : public std::runtime_error {
 public:
  Cancelled() : std::runtime_error("cancelled") {}
};

// The part of std::stop_token and std::stop_source that sillok needs. Apple's
// libc++ (Xcode 16) offers those only as an experimental library.
class StopToken {
 public:
  StopToken() = default;  // never stops

  bool StopRequested() const noexcept {
    return state_ != nullptr && state_->load(std::memory_order_acquire);
  }

 private:
  friend class StopSource;
  explicit StopToken(std::shared_ptr<const std::atomic<bool>> state)
      : state_(std::move(state)) {}

  std::shared_ptr<const std::atomic<bool>> state_;
};

// Requests a stop from any thread; every token from Token() sees it.
class StopSource {
 public:
  StopSource() : state_(std::make_shared<std::atomic<bool>>(false)) {}

  void RequestStop() noexcept { state_->store(true, std::memory_order_release); }
  bool StopRequested() const noexcept {
    return state_->load(std::memory_order_acquire);
  }
  StopToken Token() const { return StopToken(state_); }

 private:
  std::shared_ptr<std::atomic<bool>> state_;
};

inline void ThrowIfStopped(const StopToken& stop) {
  if (stop.StopRequested()) throw Cancelled();
}

}  // namespace sillok

#endif  // SILLOK_CORE_CANCEL_HPP_
