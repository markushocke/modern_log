module;

#include <cstddef>
#include <string>
#include <string_view>

#include "../detail/format_buffer.hpp"
#include "../detail/timestamp_cache.hpp"

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

[[nodiscard]] inline std::size_t estimate_field_value_size(const field_value& value) {
	switch (value.type) {
	case field_type::signed_integer:
	case field_type::unsigned_integer:
		return 20;
	case field_type::floating_point:
		return 32;
	case field_type::boolean:
		return 5;
	case field_type::string:
		return value.storage.string.size() + 2;
	case field_type::bytes:
		return 24;
	}

	return 0;
}

[[nodiscard]] inline std::size_t estimate_record_size(const record& entry) {
	std::size_t size = 96
		+ entry.category.size()
		+ entry.event_name.size()
		+ entry.trace_id.size()
		+ entry.span_id.size()
		+ entry.traceparent.size()
		+ entry.message_template.size();

	for (const auto& current_field : entry.fields) {
		size += current_field.name.size() + 2 + estimate_field_value_size(current_field.value);
	}

	return size;
}

[[nodiscard]] inline std::size_t estimate_batch_size(batch_view batch) {
	std::size_t size{};

	for (std::size_t index = 0; index < batch.size; ++index) {
		size += estimate_record_size(batch.data[index]) + 1;
	}

	return size;
}

inline void append_escaped_string(reusable_format_buffer& buffer, std::string_view value) {
	for (const char current : value) {
		switch (current) {
		case '\\':
			buffer.append("\\\\");
			break;
		case '"':
			buffer.append("\\\"");
			break;
		case '\n':
			buffer.append("\\n");
			break;
		case '\r':
			buffer.append("\\r");
			break;
		case '\t':
			buffer.append("\\t");
			break;
		default:
			buffer.push_back(current);
			break;
		}
	}
}

inline void append_quoted_string(reusable_format_buffer& buffer, std::string_view value) {
	buffer.push_back('"');
	append_escaped_string(buffer, value);
	buffer.push_back('"');
}

inline void append_field_value(reusable_format_buffer& buffer, const field_value& value) {
	switch (value.type) {
	case field_type::signed_integer:
		buffer.append_int64(value.storage.signed_integer);
		break;
	case field_type::unsigned_integer:
		buffer.append_uint64(value.storage.unsigned_integer);
		break;
	case field_type::floating_point:
		buffer.append_double(value.storage.floating_point);
		break;
	case field_type::boolean:
		buffer.append(value.storage.boolean ? "true" : "false");
		break;
	case field_type::string:
		append_quoted_string(buffer, value.storage.string);
		break;
	case field_type::bytes:
		buffer.append("<bytes:");
		buffer.append_uint64(value.storage.bytes.size);
		buffer.push_back('>');
		break;
	}
}

inline void append_json_string(reusable_format_buffer& buffer, std::string_view value) {
	append_quoted_string(buffer, value);
}

inline void append_json_member_name(
	reusable_format_buffer& buffer,
	std::string_view name,
	bool& first_member
) {
	if (!first_member) {
		buffer.push_back(',');
	}

	first_member = false;
	append_json_string(buffer, name);
	buffer.push_back(':');
}

inline void append_json_field_value(reusable_format_buffer& buffer, const field_value& value) {
	switch (value.type) {
	case field_type::signed_integer:
		buffer.append_int64(value.storage.signed_integer);
		break;
	case field_type::unsigned_integer:
		buffer.append_uint64(value.storage.unsigned_integer);
		break;
	case field_type::floating_point:
		buffer.append_double(value.storage.floating_point);
		break;
	case field_type::boolean:
		buffer.append(value.storage.boolean ? "true" : "false");
		break;
	case field_type::string:
		append_json_string(buffer, value.storage.string);
		break;
	case field_type::bytes:
		buffer.push_back('"');
		buffer.append("<bytes:");
		buffer.append_uint64(value.storage.bytes.size);
		buffer.push_back('>');
		buffer.push_back('"');
		break;
	}
}

inline void append_text_record(
	reusable_format_buffer& buffer,
	timestamp_cache& timestamp_cache,
	const record& entry
) {
	const auto timestamp = timestamp_cache.format_utc(entry.timestamp_ns);
	if (!timestamp.empty()) {
		buffer.append("timestamp=");
		buffer.append(timestamp);
		buffer.push_back(' ');
	}

	buffer.append("ts=");
	buffer.append_uint64(entry.timestamp_ns);
	buffer.append(" level=");
	buffer.append(level_name(entry.log_level));

	if (!entry.category.empty()) {
		buffer.append(" category=");
		buffer.append(entry.category);
	}

	if (!entry.event_name.empty()) {
		buffer.append(" event=");
		buffer.append(entry.event_name);
	}

	if (entry.thread_id != 0) {
		buffer.append(" thread_id=");
		buffer.append_uint64(entry.thread_id);
	}

	if (entry.task_id != 0) {
		buffer.append(" task_id=");
		buffer.append_uint64(entry.task_id);
	}

	if (!entry.trace_id.empty()) {
		buffer.append(" trace_id=");
		buffer.append(entry.trace_id);
	}

	if (!entry.span_id.empty()) {
		buffer.append(" span_id=");
		buffer.append(entry.span_id);
	}

	if (!entry.traceparent.empty()) {
		buffer.append(" traceparent=");
		buffer.append(entry.traceparent);
	}

	if (!entry.message_template.empty()) {
		buffer.append(" message=");
		append_quoted_string(buffer, entry.message_template);
	}

	for (const auto& current_field : entry.fields) {
		buffer.push_back(' ');
		buffer.append(current_field.name);
		buffer.push_back('=');
		append_field_value(buffer, current_field.value);
	}

	if (entry.dropped_count != 0) {
		buffer.append(" dropped_count=");
		buffer.append_uint64(entry.dropped_count);
	}
}

inline void append_json_record(
	reusable_format_buffer& buffer,
	timestamp_cache& timestamp_cache,
	const record& entry
) {
	bool first_member = true;
	const auto timestamp = timestamp_cache.format_utc(entry.timestamp_ns);

	buffer.push_back('{');

	if (!timestamp.empty()) {
		append_json_member_name(buffer, "timestamp", first_member);
		append_json_string(buffer, timestamp);
	}

	append_json_member_name(buffer, "timestamp_ns", first_member);
	buffer.append_uint64(entry.timestamp_ns);

	append_json_member_name(buffer, "level", first_member);
	append_json_string(buffer, level_name(entry.log_level));

	if (!entry.category.empty()) {
		append_json_member_name(buffer, "category", first_member);
		append_json_string(buffer, entry.category);
	}

	if (!entry.event_name.empty()) {
		append_json_member_name(buffer, "event", first_member);
		append_json_string(buffer, entry.event_name);
	}

	if (entry.thread_id != 0) {
		append_json_member_name(buffer, "thread_id", first_member);
		buffer.append_uint64(entry.thread_id);
	}

	if (entry.task_id != 0) {
		append_json_member_name(buffer, "task_id", first_member);
		buffer.append_uint64(entry.task_id);
	}

	if (!entry.trace_id.empty()) {
		append_json_member_name(buffer, "trace_id", first_member);
		append_json_string(buffer, entry.trace_id);
	}

	if (!entry.span_id.empty()) {
		append_json_member_name(buffer, "span_id", first_member);
		append_json_string(buffer, entry.span_id);
	}

	if (!entry.traceparent.empty()) {
		append_json_member_name(buffer, "traceparent", first_member);
		append_json_string(buffer, entry.traceparent);
	}

	if (!entry.message_template.empty()) {
		append_json_member_name(buffer, "message", first_member);
		append_json_string(buffer, entry.message_template);
	}

	for (const auto& current_field : entry.fields) {
		append_json_member_name(buffer, current_field.name, first_member);
		append_json_field_value(buffer, current_field.value);
	}

	if (entry.dropped_count != 0) {
		append_json_member_name(buffer, "dropped_count", first_member);
		buffer.append_uint64(entry.dropped_count);
	}

	buffer.push_back('}');
}

} // namespace modern::log::detail

export namespace modern::log {

class text_formatter {
public:
	[[nodiscard]] std::string format(const record& entry) const {
		buffer_.reset(detail::estimate_record_size(entry));
		detail::append_text_record(buffer_, timestamp_cache_, entry);
		return buffer_.str();
	}

	[[nodiscard]] std::string format(batch_view batch) const {
		buffer_.reset(detail::estimate_batch_size(batch));

		for (std::size_t index = 0; index < batch.size; ++index) {
			if (index != 0) {
				buffer_.push_back('\n');
			}

			detail::append_text_record(buffer_, timestamp_cache_, batch.data[index]);
		}

		if (!batch.empty()) {
			buffer_.push_back('\n');
		}

		return buffer_.str();
	}

private:
	mutable detail::timestamp_cache timestamp_cache_{};
	mutable detail::reusable_format_buffer buffer_{};
};

class json_formatter {
public:
	[[nodiscard]] std::string format(const record& entry) const {
		buffer_.reset(detail::estimate_record_size(entry));
		detail::append_json_record(buffer_, timestamp_cache_, entry);
		return buffer_.str();
	}

	[[nodiscard]] std::string format(batch_view batch) const {
		buffer_.reset(detail::estimate_batch_size(batch));

		for (std::size_t index = 0; index < batch.size; ++index) {
			if (index != 0) {
				buffer_.push_back('\n');
			}

			detail::append_json_record(buffer_, timestamp_cache_, batch.data[index]);
		}

		if (!batch.empty()) {
			buffer_.push_back('\n');
		}

		return buffer_.str();
	}

private:
	mutable detail::timestamp_cache timestamp_cache_{};
	mutable detail::reusable_format_buffer buffer_{};
};

} // namespace modern::log