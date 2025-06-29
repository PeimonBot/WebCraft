#ifdef __linux__

#include <webcraft/async/runtime/provider.hpp>

class io_uring_runtime_provider : public webcraft::async::runtime::detail::runtime_provider
{

    
};

static auto provider = std::make_shared<io_uring_runtime_provider>();

static std::shared_ptr<webcraft::async::runtime::detail::runtime_provider> get_runtime_provider()
{
    return provider;
}
#endif