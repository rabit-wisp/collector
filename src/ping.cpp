#include "ppp.h"
#include <filesystem>
#include <string.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <optional>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <algorithm>
#include <experimental/iterator>

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/time.h>

using namespace std::chrono;

namespace ping {

    typedef std::chrono::high_resolution_clock mainclock;
    typedef std::chrono::time_point<mainclock> timestamp;

    std::atomic<bool> running;
    std::thread send_thread;
    std::thread recv_thread;

    struct ICMPPacket {
        struct icmphdr header;
        char data[56]; // Standard ping data size
    };

    struct sock_wrap {
        int sock_fd = -1;
        milliseconds timeout;

        sock_wrap(milliseconds timeout_) : timeout(timeout_) {
            reconnect();
        }

        ~sock_wrap() { close(sock_fd); }

        void reconnect() {

            if(sock_fd >= 0)
                close(sock_fd);

            sock_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
            if (sock_fd < 0) {
                throw std::runtime_error(fmt::format("failed to create socket ({}: {}).",
                                                     errno, std::strerror(errno)));
            }

            // Set socket timeout to the ping frequency to essentially stop waiting for a ping reply
            // if we've already (roughly) sent the next ping request
            struct timeval timeout_;
            timeout_.tv_sec = int(timeout.count() / 1000);
            timeout_.tv_usec = 0;
            setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout_, sizeof(timeout_));
        }

        struct failed_connection : std::exception {
            std::string msg;
            failed_connection() {
                msg = fmt::format("recvfrom failed ({} {})", errno, std::strerror(errno));
            }

            const char* what() const noexcept override { return msg.c_str(); };
        };
    };


    std::string resolve_hostname(const std::string& hostname) {

        struct addrinfo *result;
        int status = getaddrinfo(hostname.c_str(), nullptr, nullptr, &result);
        if (status != 0) {
            return "";
        }

        char ip_str[INET_ADDRSTRLEN];
        struct sockaddr_in* addr_in = (struct sockaddr_in*)result->ai_addr;
        inet_ntop(AF_INET, &(addr_in->sin_addr), ip_str, INET_ADDRSTRLEN);

        freeaddrinfo(result);
        return std::string(ip_str);
    }

    uint16_t calculate_checksum(void* data, int length) {

        uint16_t* ptr = static_cast<uint16_t*>(data);
        uint32_t sum = 0;

        while (length > 1) {
            sum += *ptr++;
            length -= 2;
        }

        if (length == 1) {
            sum += *(uint8_t*)ptr;
        }

        while (sum >> 16) {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }

        return ~sum;
    }


    struct Target {
        std::string host;
        std::mutex mutex;
        uint16_t sequence_number;
        int16_t last_ping;
        timestamp last_sent;
        struct sockaddr_in addr;

        Target(const std::string& host_) : host(host_), last_ping(-1), sequence_number(0)
        {
            auto resolved = resolve_hostname(host);
            if (resolved.empty())
                throw std::runtime_error(fmt::format("Can't resolve hostname '{}'", host));

            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            inet_pton(AF_INET, resolved.c_str(), &addr.sin_addr);
        }

        std::string dump_json()
        {
            return fmt::format("{}", last_ping);
        }
    };

    void send_ping(int sock_fd, struct ICMPPacket& packet, struct sockaddr_in& dest_addr) {

        ssize_t len = sendto(sock_fd,
                             &packet,
                             sizeof(packet),
                             0,
                             (struct sockaddr*)&dest_addr,
                             sizeof(dest_addr));

        if (len == -1)
            throw sock_wrap::failed_connection();
        else if (len < 0)
        {
            std::cerr << "unable to send ping payload (err " << errno
                      << " - " << std::strerror(errno) << ")" << std::endl;
        }
        else if (len < sizeof(packet))
            std::cerr << "unable to send ping payload (buffer too big)" << std::endl;
    }

    std::map<in_addr_t, std::unique_ptr<Target>> targets;

    void send_worker(milliseconds frequency) {

        struct ICMPPacket packet;
        // prepare the payload for general usage, we will be updating sequence and checksums only
        memset(&packet, 0, sizeof(packet));
        packet.header.type = ICMP_ECHO;
        packet.header.code = 0;
        packet.header.un.echo.id = getpid() & 0xFFFF;
        packet.header.un.echo.sequence = 0;
        packet.header.checksum = 0;

        // Fill data with pattern
        for (int i = 0; i < 56; i++) {
            packet.data[i] = i + 1;
        }

        // Create raw socket (requires root privileges)
        sock_wrap socket(frequency);

        do
        {
            try {

                for (const auto& [k, v] : targets )// | std::views::values ) {
                {
                    {
                        const std::lock_guard<std::mutex> lock(v->mutex);
                        packet.header.un.echo.sequence = ++(v->sequence_number);
                        packet.header.checksum = 0; // reset to 0 to not poison our own checksum!
                        packet.header.checksum = calculate_checksum(&packet, sizeof(packet));
                        v->last_sent = mainclock::now();
                    }
                    send_ping(socket.sock_fd, packet, v->addr);
                }

                std::this_thread::sleep_for(frequency);

            } catch (sock_wrap::failed_connection) {

                std::cerr << "reconnecting send socket" << std::endl;
                socket.reconnect();
                std::this_thread::sleep_for(milliseconds(1000));
            }
        } while (running.load());
    }


    std::optional<std::tuple<in_addr_t, int16_t, timestamp>> ping_receive(int sock_fd)
    {
        char recv_buffer[1500];
        struct sockaddr_in recv_addr;
        socklen_t addr_len = sizeof(recv_addr);

        ssize_t len = recvfrom(sock_fd,
                               recv_buffer,
                               sizeof(recv_buffer),
                               0,
                               (struct sockaddr*)&recv_addr,
                               &addr_len);

        if (len == -1)
            throw sock_wrap::failed_connection();
        else if (len < 0)
        {
            std::cerr << "recvfrom failed (" << errno << " " << std::strerror(errno) << ")" << std::endl;
            return std::nullopt;
        }

        timestamp time = mainclock::now();

        // Validate minimum packet size
        if (len < sizeof(struct iphdr) + sizeof(struct icmphdr)) {
            std::cerr << "received invalid echo packet (too big)" << std::endl;
            return std::nullopt;
        }

        struct iphdr* ip_header = (struct iphdr*)recv_buffer;
        int ip_header_len = ip_header->ihl * 4;

        // Validate IP header length
        if (ip_header_len < sizeof(struct iphdr) ||
            ip_header_len > len - sizeof(struct icmphdr)) {
            std::cerr << "received invalid echo packet (invalid IP header length)" << std::endl;
            return std::nullopt;
        }

        struct icmphdr* icmp_reply = (struct icmphdr*)(recv_buffer + ip_header_len);

        // Verify this is our echo reply
        if (icmp_reply->type == ICMP_ECHOREPLY &&
            icmp_reply->un.echo.id == (getpid() & 0xFFFF))
            return std::make_tuple(recv_addr.sin_addr.s_addr, icmp_reply->un.echo.sequence, time);
        else
        {
            std::cerr << "ignoring non-icmp echo reply" << std::endl;
            return std::nullopt;
        }
    }


    void receive_worker(milliseconds timeout){

        sock_wrap socket(timeout);
        while(running.load())
        {
            try {
                auto res = ping_receive(socket.sock_fd);

                if(res)
                {
                    auto [addr, sequence, received] = *res;

                    if( targets.contains(addr) )
                    {
                        auto& target = targets[addr];
                        const std::lock_guard<std::mutex> lock(target->mutex);

                        if(target->sequence_number == sequence){
                            target->last_ping = duration_cast<milliseconds>(received - target->last_sent).count();
                        }
                    }
                }
                else
                    std::this_thread::sleep_for(milliseconds(10)); // rate limit failures

            } catch (sock_wrap::failed_connection& e) {
                std::cerr << e.what() << std::endl;
                std::cerr << "reconnecting receive socket" << std::endl;
                socket.reconnect();
                std::this_thread::sleep_for(milliseconds(1000));
            }

        }
    }

    const std::string ping_stats_dump_json(){

        std::vector<std::string> entries;

        for (const auto& [k, v] : targets) {
            entries.push_back(fmt::format("\"{}\": {}", v->host, v->dump_json()));
        }
        return fmt::format("{{{}}}", fmt::join(entries, ", "));
    }

    void start_ping_monitoring(const std::vector<std::string>& hosts, int frequency) {

        if (hosts.empty())
            return;

        running.store(true);

        for(auto& host : hosts)
        {
            try {
                auto target = std::make_unique<Target>(host);
                targets.insert({target->addr.sin_addr.s_addr, std::move(target)});

            } catch (std::exception& e) {
                std::cerr << "unable to run ping on '" << host
                          << "' - error: " << e.what() << std::endl;
            }
        }

        recv_thread = std::move(std::thread(receive_worker, milliseconds(frequency)));
        send_thread = std::move(std::thread(send_worker, milliseconds(frequency)));
    }

    void stop_ping_monitoring() {
        running.store(false);

        recv_thread.join();
        send_thread.join();
    }
}
