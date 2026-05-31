module;

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

export module modern.log.format;

export import modern.log.core;

namespace modern::log::detail {

[[nodiscard]] inline std::string escape_string(std::string_view value) {
	std::string escaped;
	escaped.reserve(value.size());

	for (const char current : value) {
		switch (current) {
		case '\\':
			escaped += "\\\\";
			break;
		case '"':
			escaped += "\\\"";
			break;
		case '\n':
			escaped += "\\n";
			break;
		case '\r':
			escaped += "\\r";
			break;
		case '\t':
			escaped += "\\t";
			break;
		default:
			escaped.push_back(current);
			break;
		}
	}

	return escaped;
}

[[nodiscard]] inline std::string_view level_name(level value) {
	switch (value) {
	case level::trace:
		return "trace";
	case level::debug:
		return "debug";
	case level::info:
		return "info";
	case level::warn:
		return "warn";
	case level::error:
		return "error";
	case level::fatal:
		return "fatal";
	}

	return "unknown";
}

[[nodiscard]] inline std::string format_timestamp_utc(std::uint64_t timestamp_ns) {
	if (timestamp_ns > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
		return {};
	}

	using namespace std::chrono;

	const auto timestamp = sys_time<nanoseconds>{nanoseconds{static_cast<std::int64_t>(timestamp_ns)}};
	const auto day_point = floor<days>(timestamp);
	const auto calendar_date = year_month_day{day_point};
	const auto time_of_day = hh_mm_ss<nanoseconds>{timestamp - day_point};

	std::ostringstream stream;
	stream << std::setfill('0')
		<< std::setw(4) << static_cast<int>(calendar_date.year())
		<< '-'
		<< std::setw(2) << static_cast<unsigned>(calendar_date.month())
		<< '-'
		<< std::setw(2) << static_cast<unsigned>(calendar_date.day())
		<< 'T'
		<< std::setw(2) << time_of_day.hours().count()
		<< ':'
		<< std::setw(2) << time_of_day.minutes().count()
		<< ':'
		<< std::setw(2) << time_of_day.seconds().count()
		<< '.'
		<< std::setw(9) << time_of_day.subseconds().count()
		<< 'Z';

	return stream.str();
}

inline void append_field_value(std::ostringstream& stream, const field_value& value) {
	switch (value.type) {
	case field_type::signed_integer:
		stream << value.storage.signed_integer;
		break;
	case field_type::unsigned_integer:
		stream << value.storage.unsigned_integer;
		break;
	case field_type::floating_point:
		stream << value.storage.floating_point;
		break;
	case field_type::boolean:
		stream << (value.storage.boolean ? "true" : "false");
		break;
	case field_type::string:
		stream << '"' << escape_string(value.storage.string) << '"';
		break;
	case field_type::bytes:
		stream << "<bytes:" << value.storage.bytes.size << '>';
		break;
	}
}

inline void append_json_string(std::ostringstream& stream, std::string_view value) {
	stream << '"' << escape_string(value) << '"';
}

inline void append_json_member_name(
	std::ostringstream& stream,
	std::string_view name,
	bool& first_member
) {
	if (!first_member) {
		stream << ',';
	}

	first_member = false;
	append_json_string(stream, name);
	stream << ':';
}

inline void append_json_field_value(std::ostringstream& stream, const field_value& value) {
	switch (value.type) {
	case field_type::signed_integer:
		stream << value.storage.signed_integer;
		break;
	case field_type::unsigned_integer:
		stream << value.storage.unsigned_integer;
		break;
	case field_type::floating_point:
		stream << value.storage.floating_point;
		break;
	case field_type::boolean:
		stream << (value.storage.boolean ? "true" : "false");
		break;
	case field_type::string:
		append_json_string(stream, value.storage.string);
		break;
	case field_type::bytes:
		stream << '"' << "<bytes:" << value.storage.bytes.size << ">";
		break;
	}
}

} // namespace modern::log::detail

export namespace modern::log {

class text_formatter {
public:
	[[nodiscard]] std::string format(const record& entry) const {
		std::ostringstream stream;
		const auto timestamp = detail::format_timestamp_utc(entry.timestamp_ns);
		if (!timestamp.empty()) {
			stream << "timestamp=" << timestamp << ' ';
		}

		stream << "ts=" << entry.timestamp_ns;
		stream << " level=" << detail::level_name(entry.log_level);

		if (!entry.category.empty()) {
			stream << " category=" << entry.category;
		}

		if (!entry.event_name.empty()) {
			stream << " event=" << entry.event_name;
		}

		if (entry.thread_id != 0) {
			stream << " thread_id=" << entry.thread_id;
		}

		if (entry.task_id != 0) {
			stream << " task_id=" << entry.task_id;
		}

		if (!entry.trace_id.empty()) {
			stream << " trace_id=" << entry.trace_id;
		}

		if (!entry.span_id.empty()) {
			stream << " span_id=" << entry.span_id;
		}

		if (!entry.traceparent.empty()) {
			stream << " traceparent=" << entry.traceparent;
		}

		if (!entry.message_template.empty()) {
			stream << " message=\"" << detail::escape_string(entry.message_template) << '"';
		}

		for (const auto& current_field : entry.fields) {
			stream << ' ' << current_field.name << '=';
			detail::append_field_value(stream, current_field.value);
		}

		if (entry.dropped_count != 0) {
			stream << " dropped_count=" << entry.dropped_count;
		}

		return stream.str();
	}

	[[nodiscard]] std::string format(batch_view batch) const {
		std::ostringstream stream;

		for (std::size_t index = 0; index < batch.size; ++index) {
			if (index != 0) {
				stream << '\n';
			}

			stream << format(batch.data[index]);
		}

		if (!batch.empty()) {
			stream << '\n';
		}

		return stream.str();
	}
};

class json_formatter {
public:
	[[nodiscard]] std::string format(const record& entry) const {
		std::ostringstream stream;
		bool first_member = true;
		const auto timestamp = detail::format_timestamp_utc(entry.timestamp_ns);

		stream << '{';

		if (!timestamp.empty()) {
			detail::append_json_member_name(stream, "timestamp", first_member);
			detail::append_json_string(stream, timestamp);
		}

		detail::append_json_member_name(stream, "timestamp_ns", first_member);
		stream << entry.timestamp_ns;

		detail::append_json_member_name(stream, "level", first_member);
		detail::append_json_string(stream, detail::level_name(entry.log_level));

		if (!entry.category.empty()) {
			detail::append_json_member_name(stream, "category", first_member);
			detail::append_json_string(stream, entry.category);
		}

		if (!entry.event_name.empty()) {
			detail::append_json_member_name(stream, "event", first_member);
			detail::append_json_string(stream, entry.event_name);
		}

		if (entry.thread_id != 0) {
			detail::append_json_member_name(stream, "thread_id", first_member);
			stream << entry.thread_id;
		}

		if (entry.task_id != 0) {
			detail::append_json_member_name(stream, "task_id", first_member);
			stream << entry.task_id;
		}

		if (!entry.trace_id.empty()) {
			detail::append_json_member_name(stream, "trace_id", first_member);
			detail::append_json_string(stream, entry.trace_id);
		}

		if (!entry.span_id.empty()) {
			detail::append_json_member_name(stream, "span_id", first_member);
			detail::append_json_string(stream, entry.span_id);
		}

		if (!entry.traceparent.empty()) {
			detail::append_json_member_name(stream, "traceparent", first_member);
			detail::append_json_string(stream, entry.traceparent);
		}

		if (!entry.message_template.empty()) {
			detail::append_json_member_name(stream, "message", first_member);
			detail::append_json_string(stream, entry.message_template);
		}

		for (const auto& current_field : entry.fields) {
			detail::append_json_member_name(stream, current_field.name, first_member);
			detail::append_json_field_value(stream, current_field.value);
		}

		if (entry.dropped_count != 0) {
			detail::append_json_member_name(stream, "dropped_count", first_member);
			stream << entry.dropped_count;
		}

		stream << '}';
		return stream.str();
	}

	[[nodiscard]] std::string format(batch_view batch) const {
		std::ostringstream stream;

		for (std::size_t index = 0; index < batch.size; ++index) {
			if (index != 0) {
				stream << '\n';
			}

			stream << format(batch.data[index]);
		}

		if (!batch.empty()) {
			stream << '\n';
		}

		return stream.str();
	}
};

} // namespace modern::log