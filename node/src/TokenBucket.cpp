#include <lux/communication/TokenBucket.hpp>

namespace lux::communication
{
    TokenBucket::TokenBucket(size_t rate, size_t burst)
        : rate_(rate), burst_(burst ? burst : rate), tokens_(static_cast<double>(burst_ ? burst_ : rate_)), last_refill_(std::chrono::steady_clock::now())
    {
    }

    bool TokenBucket::tryConsume(size_t bytes)
    {
        std::lock_guard lock(mutex_);
        refill();
        if (tokens_ >= static_cast<double>(bytes))
        {
            tokens_ -= static_cast<double>(bytes);
            return true;
        }
        return false;
    }

    void TokenBucket::waitAndConsume(size_t bytes)
    {
        for (;;)
        {
            {
                std::lock_guard lock(mutex_);
                refill();
                if (tokens_ >= static_cast<double>(bytes))
                {
                    tokens_ -= static_cast<double>(bytes);
                    return;
                }
                // Compute sleep time outside the lock.
                double deficit = static_cast<double>(bytes) - tokens_;
                auto wait_us = static_cast<int64_t>(deficit / static_cast<double>(rate_) * 1e6);
                if (wait_us < 10)
                    wait_us = 10;
                // Unlock before sleeping.
                // (fall through to sleep below)
                std::this_thread::sleep_for(std::chrono::microseconds(wait_us));
            }
        }
    }

    void TokenBucket::refill()
    {
        auto now = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - last_refill_).count();
        last_refill_ = now;
        tokens_ = std::min(
            static_cast<double>(burst_), tokens_ + elapsed_sec * static_cast<double>(rate_)
        );
    }

} // namespace lux::communication
