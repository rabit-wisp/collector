#pragma once
#include <memory>
#include <sstream>
#include <vector>
#include <string>
#include <expected>
#include <span>


enum class Error {
    NetworkError,
    ConnectionFailed,
    SendFailed,
    InvalidArgument,
    CompressionFailed
};

std::string to_string(Error error);

class DataTransport {
public:
    virtual ~DataTransport() = default;
    virtual std::expected<void, Error> send(std::span<const std::byte> data) = 0;
    virtual std::expected<void, Error> connect() { return {}; }
    virtual void disconnect() {}
};

class DataWriter {
private:
    std::unique_ptr<DataTransport> transport_;
    bool compression_enabled_ = false;

public:
    explicit DataWriter(std::unique_ptr<DataTransport> transport);
    ~DataWriter() = default;

    // Move-only
    DataWriter(const DataWriter&) = delete;
    DataWriter& operator=(const DataWriter&) = delete;
    DataWriter(DataWriter&&) = default;
    DataWriter& operator=(DataWriter&&) = default;

    // Configuration
    void set_compression(bool enabled) { compression_enabled_ = enabled; }
    bool compression_enabled() const { return compression_enabled_; }

    // Writing interface
    std::expected<void, Error> write(const std::string& data);
    std::expected<void, Error> write(std::span<const std::byte> data);

    // Stream operator for convenience
    template<typename T>
    friend DataWriter& operator<<(DataWriter& writer, const T& data);

    // Factory methods
    static std::unique_ptr<DataWriter> create_stdout();
    static std::unique_ptr<DataWriter> create_stderr();
    static std::unique_ptr<DataWriter> create_udp(const std::string& host, int port);
    static std::unique_ptr<DataWriter> create_tcp(const std::string& host, int port);
    static std::unique_ptr<DataWriter> create_zmq(const std::string& endpoint, bool bind = false, const std::string& tag = "");

private:
    std::expected<std::vector<std::byte>, Error> compress_data(std::span<const std::byte> data);
};

// Stream operator implementation
template<typename T>
DataWriter& operator<<(DataWriter& writer, const T& data) {
    std::ostringstream oss;
    oss << data;
    auto result = writer.write(oss.str());
    if (!result) {
        throw std::runtime_error("DataWriter stream operation failed: " + to_string(result.error()));
    }
    return writer;
}


std::ostream& operator<< (std::ostream& os, const Error& err);
