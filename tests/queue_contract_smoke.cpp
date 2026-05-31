#include <type_traits>

#include "modules/detail/queue_policy.hpp"

int main() {
    using namespace modern::log::detail;

    static_assert(std::is_enum_v<backpressure_policy>);
    static_assert(std::is_enum_v<flush_trigger>);
    static_assert(std::is_standard_layout_v<queue_contract>);
    static_assert(std::is_standard_layout_v<lifecycle_contract>);

    constexpr queue_contract queue{};
    constexpr lifecycle_contract lifecycle{};

    if (!valid(queue)) {
        return 1;
    }

    return lifecycle.shutdown_drains_pending ? 0 : 1;
}