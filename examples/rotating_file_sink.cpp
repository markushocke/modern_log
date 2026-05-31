import modern.log;

#include <cstdint>
#include <filesystem>
#include <memory>

int main() {
	const auto path = std::filesystem::current_path() / "service.log";
	auto sink = std::make_shared<modern::log::rotating_file_sink>(
		path,
		std::uintmax_t{4096},
		3
	);

	auto logger = modern::log::logger::builder()
		.sink(sink)
		.build();

	logger.category("http").info("listener ready");
	logger.event("request.completed")
		.field("status_code", std::uint64_t{200})
		.field("bytes_written", std::uint64_t{512})
		.submit();

	logger.warn("response body spiked beyond normal baseline");
	return 0;
}