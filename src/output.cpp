#include "output.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <zlib.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#include <zmq.h>

std::string to_string(Error error) {
    switch (error) {
        case Error::NetworkError: return "Network error";
        case Error::ConnectionFailed: return "Connection failed";
        case Error::SendFailed: return "Send failed";
        case Error::InvalidArgument: return "Invalid argument";
        case Error::CompressionFailed: return "Compression failed";
        default: return "Unknown error";
    }
}

std::ostream& operator<< (std::ostream& os, const Error& err)
{
    os << to_string(err);
    return os;
}

// ============================================================================
// Transport Implementations
// ============================================================================

class StdoutTransport : public DataTransport {
public:
    std::expected<void, Error> send(std::span<const std::byte> data) override {
        std::cout.write(reinterpret_cast<const char*>(data.data()), data.size());
        std::cout << std::endl;
        return {};
    }
};

class StderrTransport : public DataTransport {
public:
    std::expected<void, Error> send(std::span<const std::byte> data) override {
        std::cerr.write(reinterpret_cast<const char*>(data.data()), data.size());
        std::cerr << std::endl;
        return {};
    }
};

class UdpTransport : public DataTransport {
private:
    int socket_;
    sockaddr_in dest_addr_;
    std::string host_;
    int port_;

public:
    UdpTransport(const std::string& host, int port)
        : socket_(-1), host_(host), port_(port) {
        memset(&dest_addr_, 0, sizeof(dest_addr_));
    }

    ~UdpTransport() {
        disconnect();
    }

    std::expected<void, Error> connect() override {
        socket_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_ < 0) {
            return std::unexpected(Error::ConnectionFailed);
        }

        dest_addr_.sin_family = AF_INET;
        dest_addr_.sin_port = htons(port_);

        if (inet_pton(AF_INET, host_.c_str(), &dest_addr_.sin_addr) <= 0) {
            close(socket_);
            socket_ = -1;
            return std::unexpected(Error::InvalidArgument);
        }

        return {};
    }

    void disconnect() override {
        if (socket_ >= 0) {
            close(socket_);
            socket_ = -1;
        }
    }

    std::expected<void, Error> send(std::span<const std::byte> data) override {
        if (socket_ < 0) {
            auto result = connect();
            if (!result) return result;
        }

        ssize_t sent = sendto(socket_, data.data(), data.size(), 0,
                             reinterpret_cast<sockaddr*>(&dest_addr_), sizeof(dest_addr_));

        if (sent < 0 || static_cast<size_t>(sent) != data.size()) {
            return std::unexpected(Error::SendFailed);
        }

        return {};
    }
};

class TcpTransport : public DataTransport {
private:
    int socket_;
    std::string host_;
    int port_;
    bool connected_;

public:
    TcpTransport(const std::string& host, int port) 
        : socket_(-1), host_(host), port_(port), connected_(false) {}

    ~TcpTransport() {
        disconnect();
    }

    std::expected<void, Error> connect() override {
        if (connected_) return {};

        socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_ < 0) {
            return std::unexpected(Error::ConnectionFailed);
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port_);

        if (inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr) <= 0) {
            close(socket_);
            socket_ = -1;
            return std::unexpected(Error::InvalidArgument);
        }

        if (::connect(socket_, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
            close(socket_);
            socket_ = -1;
            return std::unexpected(Error::ConnectionFailed);
        }

        connected_ = true;
        return {};
    }

    void disconnect() override {
        if (socket_ >= 0) {
            close(socket_);
            socket_ = -1;
            connected_ = false;
        }
    }

    std::expected<void, Error> send(std::span<const std::byte> data) override {
        if (!connected_) {
            auto result = connect();
            if (!result) return result;
        }

        ssize_t sent = ::send(socket_, data.data(), data.size(), 0);
        if (sent < 0 || static_cast<size_t>(sent) != data.size()) {
            connected_ = false;
            return std::unexpected(Error::SendFailed);
        }

        return {};
    }
};

class ZmqTransport : public DataTransport {
private:
    void* context_;
    void* socket_;
    std::string endpoint_;
    std::string tag_; // whether or not to include hostname as multi-part tag
    bool bind_mode_;

public:
    ZmqTransport(const std::string& endpoint, bool bind_mode, const std::string& tag)
        : context_(nullptr), socket_(nullptr), endpoint_(endpoint), bind_mode_(bind_mode), tag_(tag) {}

    ~ZmqTransport() {
        disconnect();
    }

    std::expected<void, Error> connect() override {
        context_ = zmq_ctx_new();
        if (!context_) {
            return std::unexpected(Error::ConnectionFailed);
        }

        socket_ = zmq_socket(context_, ZMQ_PUB);
        if (!socket_) {
            zmq_ctx_destroy(context_);
            context_ = nullptr;
            return std::unexpected(Error::ConnectionFailed);
        }

        int result;
        if (bind_mode_) {
            result = zmq_bind(socket_, endpoint_.c_str());
        } else {
            result = zmq_connect(socket_, endpoint_.c_str());
        }

        if (result != 0) {
            zmq_close(socket_);
            zmq_ctx_destroy(context_);
            socket_ = nullptr;
            context_ = nullptr;
            return std::unexpected(Error::ConnectionFailed);
        }

        return {};
    }

    void disconnect() override {
        if (socket_) {
            zmq_close(socket_);
            socket_ = nullptr;
        }
        if (context_) {
            zmq_ctx_destroy(context_);
            context_ = nullptr;
        }
    }

    std::expected<void, Error> send(std::span<const std::byte> data) override {
        if (!socket_) {
            auto result = connect();
            if (!result) return result;
        }

        if (!tag_.empty() && zmq_send(socket_, tag_.c_str(), tag_.size(), ZMQ_SNDMORE) < 0) {
            return std::unexpected(Error::SendFailed);
        }

        if (zmq_send(socket_, data.data(), data.size(), 0) < 0) {
            return std::unexpected(Error::SendFailed);
        }

        return {};
    }
};

// ============================================================================
// DataWriter Implementation
// ============================================================================

DataWriter::DataWriter(std::unique_ptr<DataTransport> transport)
    : transport_(std::move(transport)) {}

std::expected<void, Error> DataWriter::write(const std::string& data) {
    auto bytes = std::as_bytes(std::span(data));
    return write(bytes);
}

std::expected<void, Error> DataWriter::write(std::span<const std::byte> data) {
    std::span<const std::byte> to_send = data;
    std::vector<std::byte> compressed_data;

    if (compression_enabled_) {
        auto compressed = compress_data(data);
        if (!compressed) {
            return std::unexpected(compressed.error());
        }
        compressed_data = std::move(*compressed);
        to_send = compressed_data;
    }

    return transport_->send(to_send);
}

std::expected<std::vector<std::byte>, Error> DataWriter::compress_data(std::span<const std::byte> data) {
    z_stream stream{};
    if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED,
                     15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return std::unexpected(Error::CompressionFailed);
    }

    std::vector<std::byte> compressed;
    compressed.resize(data.size() + 32); // Initial guess

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(data.data()));
    stream.avail_in = data.size();
    stream.next_out = reinterpret_cast<Bytef*>(compressed.data());
    stream.avail_out = compressed.size();

    int result = deflate(&stream, Z_FINISH);

    if (result == Z_STREAM_END) {
        compressed.resize(stream.total_out);
        deflateEnd(&stream);
        return compressed;
    } else {
        deflateEnd(&stream);
        return std::unexpected(Error::CompressionFailed);
    }
}

// Factory methods
std::unique_ptr<DataWriter> DataWriter::create_stdout() {
    auto transport = std::make_unique<StdoutTransport>();
    return std::make_unique<DataWriter>(std::move(transport));
}

std::unique_ptr<DataWriter> DataWriter::create_stderr() {
    auto transport = std::make_unique<StderrTransport>();
    return std::make_unique<DataWriter>(std::move(transport));
}

std::unique_ptr<DataWriter> DataWriter::create_udp(const std::string& host, int port) {
    auto transport = std::make_unique<UdpTransport>(host, port);
    return std::make_unique<DataWriter>(std::move(transport));
}

std::unique_ptr<DataWriter> DataWriter::create_tcp(const std::string& host, int port) {
    auto transport = std::make_unique<TcpTransport>(host, port);
    return std::make_unique<DataWriter>(std::move(transport));
}

std::unique_ptr<DataWriter> DataWriter::create_zmq(const std::string& endpoint, bool bind, const std::string& tag) {
    auto transport = std::make_unique<ZmqTransport>(endpoint, bind, tag);
    return std::make_unique<DataWriter>(std::move(transport));
}

