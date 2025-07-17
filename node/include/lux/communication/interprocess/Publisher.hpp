#pragma once

#include <memory>
#include <string>

#include <lux/communication/visibility.h>

namespace lux::communication::interprocess {

    class PublisherImplBase
    {
    public:
        virtual ~PublisherImplBase() = default;
        virtual void publish(const void* data, size_t size) = 0;
    };

    std::unique_ptr<PublisherImplBase> makePublisherImpl(
        const std::string& topic, const std::string& endpoint);
    
    template<typename T>
    class Publisher
    {
    public:
        explicit Publisher(const std::string& topic, std::string endpoint = "")
            : impl_(makePublisherImpl(topic, std::move(endpoint)))
        {
        }

        void publish(const T& msg)
        {
            impl_->publish(&msg, sizeof(T));
        }

    private:
        std::unique_ptr<PublisherImplBase> impl_;
    };

} // namespace lux::communication::interprocess

