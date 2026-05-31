module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

export module modern.log.core;

export namespace modern::log {

enum class level : std::uint8_t {
	trace,
	debug,
	info,
	warn,
	error,
	fatal,
};

enum class field_type : std::uint8_t {
	signed_integer,
	unsigned_integer,
	floating_point,
	boolean,
	string,
	bytes,
};

struct byte_view final {
	const std::byte* data{};
	std::size_t size{};
};

struct field_value final {
	field_type type{field_type::string};

	union storage_t {
		std::int64_t signed_integer;
		std::uint64_t unsigned_integer;
		double floating_point;
		bool boolean;
		std::string_view string;
		byte_view bytes;

		constexpr storage_t() noexcept : string{} {}
	} storage{};
};

struct field final {
	std::string_view name{};
	field_value value{};
};

struct trace_context_handle final {
	const void* native_context{};

	[[nodiscard]] constexpr explicit operator bool() const noexcept {
		return native_context != nullptr;
	}
};

struct record final {
	std::uint64_t timestamp_ns{};
	level log_level{level::info};
	std::string_view category{};
	std::string_view event_name{};
	std::uint64_t thread_id{};
	std::uint64_t task_id{};
	trace_context_handle trace_context{};
	std::string_view trace_id{};
	std::string_view span_id{};
	std::string_view traceparent{};
	std::string_view message_template{};
	std::span<const field> fields{};
	std::uint32_t dropped_count{};
};

struct batch_view final {
	const record* data{};
	std::size_t size{};

	[[nodiscard]] constexpr bool empty() const noexcept {
		return size == 0;
	}
};

} // namespace modern::log