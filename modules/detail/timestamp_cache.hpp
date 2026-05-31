#pragma once

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace modern::log::detail {

class timestamp_cache final {
public:
	[[nodiscard]] std::string_view format_utc(std::uint64_t timestamp_ns) {
		if (timestamp_ns > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
			formatted_.clear();
			return {};
		}

		constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ull;
		const auto unix_second = timestamp_ns / nanoseconds_per_second;
		const auto subseconds = timestamp_ns % nanoseconds_per_second;

		if (cached_second_ != unix_second) {
			rebuild_prefix(unix_second);
		}

		formatted_.clear();
		formatted_.reserve(cached_prefix_.size() + 11);
		formatted_ += cached_prefix_;
		formatted_.push_back('.');
		append_zero_padded(formatted_, subseconds, 9);
		formatted_.push_back('Z');
		return formatted_;
	}

private:
	static void append_zero_padded(std::string& target, std::uint64_t value, std::size_t width) {
		char buffer[32]{};
		auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
		if (error != std::errc{}) {
			return;
		}

		const auto digits = static_cast<std::size_t>(end - buffer);
		if (digits < width) {
			target.append(width - digits, '0');
		}

		target.append(buffer, end);
	}

	void rebuild_prefix(std::uint64_t unix_second) {
		using namespace std::chrono;

		const auto timestamp = sys_seconds{seconds{static_cast<std::int64_t>(unix_second)}};
		const auto day_point = floor<days>(timestamp);
		const auto calendar_date = year_month_day{day_point};
		const auto time_of_day = hh_mm_ss<seconds>{timestamp - day_point};

		cached_prefix_.clear();
		cached_prefix_.reserve(19);

		append_zero_padded(cached_prefix_, static_cast<unsigned>(static_cast<int>(calendar_date.year())), 4);
		cached_prefix_.push_back('-');
		append_zero_padded(cached_prefix_, static_cast<unsigned>(calendar_date.month()), 2);
		cached_prefix_.push_back('-');
		append_zero_padded(cached_prefix_, static_cast<unsigned>(calendar_date.day()), 2);
		cached_prefix_.push_back('T');
		append_zero_padded(cached_prefix_, static_cast<unsigned>(time_of_day.hours().count()), 2);
		cached_prefix_.push_back(':');
		append_zero_padded(cached_prefix_, static_cast<unsigned>(time_of_day.minutes().count()), 2);
		cached_prefix_.push_back(':');
		append_zero_padded(cached_prefix_, static_cast<unsigned>(time_of_day.seconds().count()), 2);

		cached_second_ = unix_second;
	}

	std::uint64_t cached_second_{std::numeric_limits<std::uint64_t>::max()};
	std::string cached_prefix_{};
	std::string formatted_{};
};

} // namespace modern::log::detail