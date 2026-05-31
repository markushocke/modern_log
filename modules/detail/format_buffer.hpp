#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

namespace modern::log::detail {

class reusable_format_buffer final {
public:
	void reset(std::size_t reserve_hint = 0) {
		storage_.clear();
		if (storage_.capacity() < reserve_hint) {
			storage_.reserve(reserve_hint);
		}
	}

	void append(std::string_view value) {
		storage_.append(value);
	}

	void push_back(char value) {
		storage_.push_back(value);
	}

	void append_uint64(std::uint64_t value) {
		append_integral(value);
	}

	void append_int64(std::int64_t value) {
		append_integral(value);
	}

	void append_double(double value) {
		char buffer[128]{};
		auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
		if (error == std::errc{}) {
			storage_.append(buffer, end);
			return;
		}

		std::ostringstream fallback;
		fallback << value;
		storage_ += fallback.str();
	}

	[[nodiscard]] std::string str() const {
		return storage_;
	}

	[[nodiscard]] std::string_view view() const noexcept {
		return storage_;
	}

private:
	template <typename Integer>
	void append_integral(Integer value) {
		char buffer[64]{};
		auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
		if (error == std::errc{}) {
			storage_.append(buffer, end);
		}
	}

	std::string storage_{};
};

} // namespace modern::log::detail