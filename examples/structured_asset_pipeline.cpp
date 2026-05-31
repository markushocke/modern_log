import modern.log;

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

namespace {

struct asset_event final {
	std::string_view path;
	std::uint64_t bytes;
	double duration_ms;
	bool cached;
};

} // namespace

int main() {
	constexpr std::array assets{
		asset_event{"ship.mesh", 65536, 3.5, true},
		asset_event{"pilot.anim", 12288, 1.4, false},
		asset_event{"terrain.bin", 524288, 8.9, true},
	};

	auto logger = modern::log::logger::builder()
		.sink(std::make_shared<modern::log::json_sink>())
		.build();

	for (const auto& asset : assets) {
		logger.category("assets")
			.event("asset.loaded")
			.field("path", asset.path)
			.field("bytes", asset.bytes)
			.field("duration_ms", asset.duration_ms)
			.field("cached", asset.cached)
			.submit();
	}

	return 0;
}