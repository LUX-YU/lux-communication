#pragma once

#include <cstdint>
#include <memory>
#include <functional>

namespace lux::communication {

    /**
     * @brief Execution entry for reorder buffer.
     *        Uses function pointer trampoline instead of std::function for O(1) performance.
     */
    struct ExecEntry {
        uint64_t seq;
        void* obj;  // Subscriber pointer (type-erased)
        void (*invoke)(void* obj, const std::shared_ptr<void>& msg);
        std::shared_ptr<void> msg;

        ExecEntry() noexcept
            : seq(0), obj(nullptr), invoke(nullptr), msg(nullptr) {}

        ExecEntry(uint64_t s, void* o, 
                  void (*inv)(void*, const std::shared_ptr<void>&),
                  std::shared_ptr<void> m) noexcept
            : seq(s), obj(o), invoke(inv), msg(std::move(m)) {}

        // Move only
        ExecEntry(ExecEntry&&) noexcept = default;
        ExecEntry& operator=(ExecEntry&&) noexcept = default;
        ExecEntry(const ExecEntry&) = delete;
        ExecEntry& operator=(const ExecEntry&) = delete;

        void execute() const noexcept {
            if (invoke) {
                invoke(obj, msg);
            }
        }

        explicit operator bool() const noexcept {
            return invoke != nullptr;
        }
    };

} // namespace lux::communication
