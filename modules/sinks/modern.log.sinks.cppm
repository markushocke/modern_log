module;

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

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

} // namespace modern::log