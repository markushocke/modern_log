module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

export module modern.log.sinks;

export import modern.log.core;
export import modern.log.format;

import modern_io;

namespace modern::log::detail {

class file_writer {
public:
	virtual ~file_writer() = default;

	virtual void write(std::string_view value) = 0;
	virtual void flush() = 0;
	[[nodiscard]] virtual bool is_open() const noexcept = 0;
};

class modern_io_file_writer final : public file_writer {
public:
	explicit modern_io_file_writer(const std::filesystem::path& path)
		: stream_(path, std::ios::out | std::ios::app),
		  writer_(stream_) {
		if (!stream_.is_open()) {
			throw std::runtime_error("failed to open log file sink");
		}
	}

	void write(std::string_view value) override {
		writer_.write(value.data(), value.size());
		if (!stream_) {
			throw std::runtime_error("failed to write log file sink");
		}
	}

	void flush() override {
		writer_.flush();
		if (!stream_) {
			throw std::runtime_error("failed to flush log file sink");
		}
	}

	[[nodiscard]] bool is_open() const noexcept override {
		return stream_.is_open();
	}

private:
	std::ofstream stream_{};
	modern_io::OstreamOutputStream writer_;
};

[[nodiscard]] inline std::unique_ptr<file_writer> make_file_writer(const std::filesystem::path& path) {
	return std::make_unique<modern_io_file_writer>(path);
}

} // namespace modern::log::detail

export namespace modern::log {

class sink {
public:
	virtual ~sink() = default;

	virtual void write(batch_view batch) = 0;

	virtual void flush() {}
};

enum class remote_payload_format : std::uint8_t {
	json,
	text,
};

class remote_writer {
public:
	virtual ~remote_writer() = default;

	virtual void send(std::string payload) = 0;
	virtual void flush() {}
};

enum class otel_severity_number : std::uint8_t {
	trace = 1,
	debug = 5,
	info = 9,
	warn = 13,
	error = 17,
	fatal = 21,
};

struct otel_log_record_view final {
	std::uint64_t timestamp_ns{};
	otel_severity_number severity_number{otel_severity_number::info};
	std::string_view severity_text{};
	std::string_view body{};
	std::string_view logger_name{};
	std::string_view event_name{};
	std::uint64_t thread_id{};
	std::uint64_t task_id{};
	trace_context_handle trace_context{};
	std::string_view trace_id{};
	std::string_view span_id{};
	std::string_view traceparent{};
	std::span<const field> attributes{};
	std::uint32_t dropped_count{};
};

class otel_log_exporter {
public:
	virtual ~otel_log_exporter() = default;

	virtual void export_logs(std::span<const otel_log_record_view> records) = 0;
	virtual void flush() {}
};

struct async_sink_write final {
	batch_view batch{};
	bool flush_after_write{};
};

inline void dispatch_async_write(sink& target, async_sink_write request) {
	if (request.batch.empty()) {
		return;
	}

	target.write(request.batch);
	if (request.flush_after_write) {
		target.flush();
	}
}

namespace detail {

[[nodiscard]] constexpr otel_severity_number otel_severity(level log_level) noexcept {
	switch (log_level) {
	case level::trace:
		return otel_severity_number::trace;
	case level::debug:
		return otel_severity_number::debug;
	case level::info:
		return otel_severity_number::info;
	case level::warn:
		return otel_severity_number::warn;
	case level::error:
		return otel_severity_number::error;
	case level::fatal:
		return otel_severity_number::fatal;
	}

	return otel_severity_number::info;
}

[[nodiscard]] constexpr std::string_view otel_severity_text(level log_level) noexcept {
	switch (log_level) {
	case level::trace:
		return "TRACE";
	case level::debug:
		return "DEBUG";
	case level::info:
		return "INFO";
	case level::warn:
		return "WARN";
	case level::error:
		return "ERROR";
	case level::fatal:
		return "FATAL";
	}

	return "INFO";
}

} // namespace detail

class console_sink final : public sink {
public:
	console_sink() noexcept
	    : stream_(&std::cerr) {}

	explicit console_sink(std::ostream& stream) noexcept
	    : stream_(&stream) {}

	void write(batch_view batch) override {
		if (stream_ == nullptr || batch.empty()) {
			return;
		}

		(*stream_) << formatter_.format(batch);
	}

	void flush() override {
		if (stream_ != nullptr) {
			stream_->flush();
		}
	}

private:
	std::ostream* stream_{};
	text_formatter formatter_{};
};

class stderr_sink final : public sink {
public:
	stderr_sink() noexcept
	    : stream_(&std::cerr) {}

	explicit stderr_sink(std::ostream& stream) noexcept
	    : stream_(&stream) {}

	void write(batch_view batch) override {
		if (stream_ == nullptr || batch.empty()) {
			return;
		}

		(*stream_) << formatter_.format(batch);
	}

	void flush() override {
		if (stream_ != nullptr) {
			stream_->flush();
		}
	}

private:
	std::ostream* stream_{};
	text_formatter formatter_{};
};

class json_sink final : public sink {
public:
	json_sink() noexcept
	    : stream_(&std::cout) {}

	explicit json_sink(std::ostream& stream) noexcept
	    : stream_(&stream) {}

	void write(batch_view batch) override {
		if (stream_ == nullptr || batch.empty()) {
			return;
		}

		(*stream_) << formatter_.format(batch);
	}

	void flush() override {
		if (stream_ != nullptr) {
			stream_->flush();
		}
	}

private:
	std::ostream* stream_{};
	json_formatter formatter_{};
};

class remote_sink final : public sink {
public:
	explicit remote_sink(
		std::shared_ptr<remote_writer> writer,
		remote_payload_format format = remote_payload_format::json
	)
		: writer_(std::move(writer)),
		  format_(format) {
		if (!writer_) {
			throw std::invalid_argument("remote sink requires a remote_writer");
		}
	}

	void write(batch_view batch) override {
		if (batch.empty()) {
			return;
		}

		switch (format_) {
		case remote_payload_format::json:
			writer_->send(json_formatter_.format(batch));
			break;
		case remote_payload_format::text:
			writer_->send(text_formatter_.format(batch));
			break;
		}
	}

	void flush() override {
		writer_->flush();
	}

	[[nodiscard]] remote_payload_format format() const noexcept {
		return format_;
	}

private:
	std::shared_ptr<remote_writer> writer_{};
	remote_payload_format format_{remote_payload_format::json};
	text_formatter text_formatter_{};
	json_formatter json_formatter_{};
};

class otel_sink final : public sink {
public:
	explicit otel_sink(std::shared_ptr<otel_log_exporter> exporter)
	    : exporter_(std::move(exporter)) {
		if (!exporter_) {
			throw std::invalid_argument("otel sink requires an otel_log_exporter");
		}
	}

	void write(batch_view batch) override {
		if (batch.empty()) {
			return;
		}

		staged_records_.clear();
		staged_records_.reserve(batch.size);
		for (std::size_t index = 0; index < batch.size; ++index) {
			const auto& source = batch.data[index];
			staged_records_.push_back(otel_log_record_view{
				.timestamp_ns = source.timestamp_ns,
				.severity_number = detail::otel_severity(source.log_level),
				.severity_text = detail::otel_severity_text(source.log_level),
				.body = source.message_template,
				.logger_name = source.category,
				.event_name = source.event_name,
				.thread_id = source.thread_id,
				.task_id = source.task_id,
				.trace_context = source.trace_context,
				.trace_id = source.trace_id,
				.span_id = source.span_id,
				.traceparent = source.traceparent,
				.attributes = source.fields,
				.dropped_count = source.dropped_count,
			});
		}

		exporter_->export_logs(std::span<const otel_log_record_view>(staged_records_.data(), staged_records_.size()));
	}

	void flush() override {
		exporter_->flush();
	}

private:
	std::shared_ptr<otel_log_exporter> exporter_{};
	std::vector<otel_log_record_view> staged_records_{};
};

class file_sink final : public sink {
public:
	explicit file_sink(std::filesystem::path path)
	    : path_(std::move(path)) {
		open();
	}

	void write(batch_view batch) override {
		if (batch.empty()) {
			return;
		}

		if (!writer_ || !writer_->is_open()) {
			open();
		}

		const auto formatted = formatter_.format(batch);
		writer_->write(formatted);
	}

	void flush() override {
		if (writer_) {
			writer_->flush();
		}
	}

	[[nodiscard]] const std::filesystem::path& path() const noexcept {
		return path_;
	}

	[[nodiscard]] bool is_open() const noexcept {
		return writer_ && writer_->is_open();
	}

private:
	void open() {
		writer_ = detail::make_file_writer(path_);
	}

	std::filesystem::path path_{};
	std::unique_ptr<detail::file_writer> writer_{};
	text_formatter formatter_{};
};

class rotating_file_sink final : public sink {
public:
	rotating_file_sink(
		std::filesystem::path path,
		std::uintmax_t max_bytes,
		std::size_t max_archives = 3
	)
	    : path_(std::move(path)),
	      max_bytes_(max_bytes),
	      max_archives_(max_archives) {
		if (path_.empty()) {
			throw std::invalid_argument("rotating file sink requires a target path");
		}

		if (max_bytes_ == 0) {
			throw std::invalid_argument("rotating file sink requires max_bytes > 0");
		}

		open();
	}

	void write(batch_view batch) override {
		if (batch.empty()) {
			return;
		}

		if (!writer_ || !writer_->is_open()) {
			open();
		}

		const auto formatted = formatter_.format(batch);
		if (should_rotate(formatted.size())) {
			rotate();
		}

		writer_->write(formatted);
		current_size_ += static_cast<std::uintmax_t>(formatted.size());
	}

	void flush() override {
		if (writer_) {
			writer_->flush();
		}
	}

	[[nodiscard]] const std::filesystem::path& path() const noexcept {
		return path_;
	}

	[[nodiscard]] std::uintmax_t max_bytes() const noexcept {
		return max_bytes_;
	}

	[[nodiscard]] std::size_t max_archives() const noexcept {
		return max_archives_;
	}

	[[nodiscard]] bool is_open() const noexcept {
		return writer_ && writer_->is_open();
	}

private:
	[[nodiscard]] bool should_rotate(std::size_t incoming_size) const noexcept {
		return current_size_ > 0
			&& current_size_ + static_cast<std::uintmax_t>(incoming_size) > max_bytes_;
	}

	void open() {
		writer_ = detail::make_file_writer(path_);
		current_size_ = current_file_size();
	}

	void rotate() {
		if (writer_) {
			writer_->flush();
			writer_.reset();
		}

		if (max_archives_ == 0) {
			remove_file(path_);
			open();
			return;
		}

		remove_file(archive_path(max_archives_));
		for (auto index = max_archives_; index > 1; --index) {
			rename_if_exists(archive_path(index - 1), archive_path(index));
		}
		rename_if_exists(path_, archive_path(1));
		open();
	}

	[[nodiscard]] std::uintmax_t current_file_size() const {
		std::error_code error{};
		const auto size = std::filesystem::file_size(path_, error);
		if (error) {
			return 0;
		}

		return size;
	}

	[[nodiscard]] std::filesystem::path archive_path(std::size_t index) const {
		auto archive = path_;
		archive += "." + std::to_string(index);
		return archive;
	}

	void remove_file(const std::filesystem::path& path) const {
		std::error_code error{};
		std::filesystem::remove(path, error);
		if (error) {
			throw std::runtime_error("failed to remove rotating log archive");
		}
	}

	void rename_if_exists(const std::filesystem::path& from, const std::filesystem::path& to) const {
		std::error_code exists_error{};
		const auto exists = std::filesystem::exists(from, exists_error);
		if (exists_error) {
			throw std::runtime_error("failed to inspect rotating log archive");
		}

		if (!exists) {
			return;
		}

		remove_file(to);

		std::error_code rename_error{};
		std::filesystem::rename(from, to, rename_error);
		if (rename_error) {
			throw std::runtime_error("failed to rotate log archive");
		}
	}

	std::filesystem::path path_{};
	std::uintmax_t max_bytes_{};
	std::size_t max_archives_{};
	std::uintmax_t current_size_{};
	std::unique_ptr<detail::file_writer> writer_{};
	text_formatter formatter_{};
};

} // namespace modern::log