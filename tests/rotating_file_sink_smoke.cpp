import modern.log;

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

[[nodiscard]] std::filesystem::path archive_path(const std::filesystem::path& path, std::size_t index) {
	auto archive = path;
	archive += "." + std::to_string(index);
	return archive;
}

[[nodiscard]] std::string read_all(const std::filesystem::path& path) {
	std::ifstream input(path);
	return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void remove_if_exists(const std::filesystem::path& path) {
	std::error_code error{};
	std::filesystem::remove(path, error);
}

} // namespace

int main() {
	using namespace modern::log;

	const auto path = std::filesystem::temp_directory_path() / "modern_log_rotating_file_sink_smoke.log";
	const auto first_archive = archive_path(path, 1);
	const auto second_archive = archive_path(path, 2);

	remove_if_exists(path);
	remove_if_exists(first_archive);
	remove_if_exists(second_archive);

	bool ok = true;

	{
		auto sink = std::make_shared<rotating_file_sink>(path, std::uintmax_t{1}, 2);
		auto logger = logger::builder()
			.sink(sink)
			.build();

		logger.info("first");
		logger.info("second");
		logger.info("third");
		sink->flush();
	}

	const auto current = read_all(path);
	const auto archived_one = read_all(first_archive);
	const auto archived_two = read_all(second_archive);

	ok = ok && current.find("message=\"third\"") != std::string::npos;
	ok = ok && archived_one.find("message=\"second\"") != std::string::npos;
	ok = ok && archived_two.find("message=\"first\"") != std::string::npos;

	bool threw = false;
	try {
		[[maybe_unused]] rotating_file_sink invalid_sink{path, std::uintmax_t{0}};
	} catch (const std::invalid_argument&) {
		threw = true;
	}

	remove_if_exists(path);
	remove_if_exists(first_archive);
	remove_if_exists(second_archive);

	return ok && threw ? 0 : 1;
}