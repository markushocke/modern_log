import modern.log;

#include <atomic>
#include <cstddef>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

class overlap_detecting_sink final : public modern::log::sink {
public:
    void write(modern::log::batch_view batch) override {
        enter();
        records.fetch_add(batch.size, std::memory_order_relaxed);
        leave();
    }

    void flush() override {
        enter();
        flushes.fetch_add(1, std::memory_order_relaxed);
        leave();
    }

    std::atomic<std::size_t> records{0};
    std::atomic<std::size_t> flushes{0};
    std::atomic<bool> overlap{false};

private:
    void enter() {
        if (active_.fetch_add(1, std::memory_order_acq_rel) != 0) {
            overlap.store(true, std::memory_order_relaxed);
        }
        std::this_thread::yield();
    }

    void leave() {
        active_.fetch_sub(1, std::memory_order_acq_rel);
    }

    std::atomic<int> active_{0};
};

} // namespace

int main() {
    using namespace modern::log;

    bool rejected_null = false;
    try {
        [[maybe_unused]] synchronized_sink invalid{std::shared_ptr<sink>{}};
    } catch (const std::invalid_argument&) {
        rejected_null = true;
    }
    if (!rejected_null) {
        return 1;
    }

    std::ostringstream output;
    auto console = std::make_shared<synchronized_sink>(
        std::make_shared<console_sink>(output)
    );
    auto probe = std::make_shared<overlap_detecting_sink>();
    auto synchronized_probe = std::make_shared<synchronized_sink>(probe);

    auto log = logger::builder()
        .sink(console)
        .sink(synchronized_probe)
        .build();

    constexpr int thread_count = 8;
    constexpr int messages_per_thread = 200;
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (int index = 0; index < thread_count; ++index) {
        threads.emplace_back([log] {
            auto worker_log = log.category("workers");
            for (int message = 0; message < messages_per_thread; ++message) {
                worker_log.info("parallel message");
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    constexpr auto expected = static_cast<std::size_t>(thread_count * messages_per_thread);
    if (probe->overlap.load() ||
        probe->records.load() != expected ||
        probe->flushes.load() != expected) {
        return 2;
    }

    std::istringstream input{output.str()};
    std::string line;
    std::size_t line_count = 0;
    while (std::getline(input, line)) {
        if (line.find("category=workers") == std::string::npos ||
            line.find("message=\"parallel message\"") == std::string::npos) {
            return 3;
        }
        ++line_count;
    }

    return line_count == expected ? 0 : 4;
}
