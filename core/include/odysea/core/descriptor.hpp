// OdySea core: RAII wrapper for a POSIX file descriptor.
//
// Toolkit-agnostic. Ownership is unique and move-only, so a descriptor is
// closed exactly once on every path out of a scope.
#pragma once

#include <unistd.h>
#include <utility>

namespace odysea::core {

class Descriptor {
  public:
    static constexpr int invalid_value = -1;

    Descriptor() noexcept = default;
    explicit Descriptor(int descriptor) noexcept : descriptor_(descriptor) {}

    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;

    Descriptor(Descriptor&& other) noexcept
        : descriptor_(std::exchange(other.descriptor_, invalid_value)) {}

    Descriptor& operator=(Descriptor&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.descriptor_, invalid_value));
        }
        return *this;
    }

    ~Descriptor() { reset(); }

    [[nodiscard]] bool valid() const noexcept { return descriptor_ >= 0; }
    [[nodiscard]] int get() const noexcept { return descriptor_; }

    [[nodiscard]] int release() noexcept { return std::exchange(descriptor_, invalid_value); }

    void reset(int descriptor = invalid_value) noexcept {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
        descriptor_ = descriptor;
    }

  private:
    int descriptor_ = invalid_value;
};

} // namespace odysea::core
