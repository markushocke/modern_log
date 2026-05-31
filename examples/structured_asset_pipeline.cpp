import modern.log;

#include <cstdint>
#include <memory>

int main() {
	auto logger = modern::log::logger::builder()
		.sink(std::make_shared<modern::log::json_sink>())
		.build();

	logger.category("assets")
		.event("asset.loaded")
		.field("path", "ship.mesh")
		.field("bytes", std::uint64_t{65536})
		.field("duration_ms", 3.5)
		.field("cached", true)
		.submit();

	return 0;
}