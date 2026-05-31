import modern.log;

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

int main() {
    using namespace modern::log;

    std::ostringstream console_buffer;
    auto console = std::make_shared<console_sink>(console_buffer);

    auto console_logger = logger::builder()
        .sink(console)
        .build();

    console_logger.info("runtime started");

    if (console_buffer.str().find("message=\"runtime started\"") == std::string::npos) {
        return 1;
    }

    const auto path = std::filesystem::temp_directory_path() / "modern_log_file_sink_smoke.log";
    std::filesystem::remove(path);

    auto file = std::make_shared<file_sink>(path);
    auto file_logger = logger::builder()
        .sink(file)
        .build();

    file_logger.category("render").warn("shader fallback");

    std::ifstream input(path);
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::filesystem::remove(path);

    if (contents.find("category=render") == std::string::npos) {
        return 1;
    }

    if (contents.find("message=\"shader fallback\"") == std::string::npos) {
        return 1;
    }

    bool threw = false;
    try {
        [[maybe_unused]] file_sink invalid_sink{std::filesystem::temp_directory_path()};
    } catch (const std::runtime_error&) {
        threw = true;
    }

    return threw ? 0 : 1;
}