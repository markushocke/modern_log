import modern.log.sinks;

#include <cstddef>
#include <type_traits>

namespace {

class null_sink final : public modern::log::sink {
public:
    void write(modern::log::batch_view batch) override {
        last_size = batch.size;
    }

    std::size_t last_size{};
};

} // namespace

int main() {
    using namespace modern::log;

    static_assert(std::has_virtual_destructor_v<sink>);
    static_assert(std::is_standard_layout_v<async_sink_write>);

    record records[1]{};
    batch_view batch{records, 1};

    null_sink target{};
    target.write(batch);

    async_sink_write request{batch, true};

    return target.last_size == 1 && request.flush_after_write ? 0 : 1;
}