#include "ppp.h"
#include <filesystem>
#include <string.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <optional>
#include <expected>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <algorithm>
#include <experimental/iterator>

#include "ping.h"

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

    enum class IcmpResponse {
        ignore,      // otherwise valid ICMP packet that simply isn't our or is stale
        invalid,     // invalid payload (too small, too big, can't be safely interpreted)
        unreachable, // OS told us the target is unreachable
        timeout,     // the socket timed out
        socket_error // the socket failed
    };


    // forward declarations
    struct Target;
    std::string resolve_hostname(const std::string& hostname);

    typedef std::chrono::high_resolution_clock mainclock;
    typedef std::chrono::time_point<mainclock> timestamp;

    std::atomic<bool> running;
    std::thread send_thread;
    std::thread recv_thread;

    std::map<in_addr_t, std::unique_ptr<Target>> targets;

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

        ~sock_wrap() {
            if (sock_fd >= 0)
                close(sock_fd);
        }


        void reconnect() {

            if(sock_fd >= 0)
                close(sock_fd);

            sock_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
            if (sock_fd < 0) {
                throw std::runtime_error(fmt::format("failed to create socket ({}: {}).", errno, std::strerror(errno)));
            }

            // Set socket timeout to the ping frequency to essentially stop waiting for a ping reply
            // if we've already (roughly) sent the next ping request
            struct timeval timeout_;
            timeout_.tv_sec = int(timeout.count() / 1000);
            timeout_.tv_usec = (timeout.count() % 1000) * 1000;
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

    struct Target {
        std::string host;
        std::atomic<uint16_t> last_sent_seq;
        std::atomic<uint16_t> last_received_seq;
        std::atomic<timestamp> last_sent_time;
        std::atomic<int16_t> latency; // we could go milliseconds_d, but we're really looking for single digit precision only
        std::atomic<bool> reachable;

        struct sockaddr_in addr;

        Target(const std::string& host, const std::string& resolved) : host(host),
                                                                       last_sent_seq(0),
                                                                       last_received_seq(0),
                                                                       reachable(false)
        {
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            inet_pton(AF_INET, resolved.c_str(), &addr.sin_addr);
        }

        std::string dump_json()
        {
            if(reachable.load())
                return fmt::format("{:.1f}", float(latency.load()) / 10.0f);
            else
                return "null";
        }
    };

    // helper functions
    std::string resolve_hostname(const std::string& hostname)
    {
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

    uint16_t calculate_checksum(void* data, int length)
    {
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

    // ping sending functions

    std::expected<void, IcmpResponse> send_ping(int sock_fd, struct ICMPPacket& packet, struct sockaddr_in& dest_addr) noexcept
    {
        ssize_t len = sendto(sock_fd,
                             &packet,
                             sizeof(packet),
                             0,
                             (struct sockaddr*)&dest_addr,
                             sizeof(dest_addr));

        if (len < 0)
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
                return std::unexpected(IcmpResponse::timeout);

            std::cerr << "unable to send ping payload (err " << errno
                      << " - " << std::strerror(errno) << ")" << std::endl;
            return std::unexpected(IcmpResponse::socket_error);
        }
        else if (len < sizeof(packet))
        {
            std::cerr << "warning: unable to send ping payload (buffer too big)" << std::endl;
            return std::unexpected(IcmpResponse::invalid);
        }

        return {};
    }

    void send_worker(std::chrono::milliseconds frequency)
    {
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

        const auto timeout_threshold = frequency * 3;

        do
        {
            for (const auto& [k, target] : targets )
            {
                const auto now = std::chrono::system_clock::now();

                // only we write to last_sent, so there is no race here
                uint16_t last_sent = target->last_sent_seq.load(std::memory_order_relaxed);
                uint16_t last_received = target->last_received_seq.load(std::memory_order_relaxed);

                // Check if we're waiting for a response
                bool waiting_for_response = (last_sent != last_received);

                if (waiting_for_response)
                {
                    // Check if the outstanding ping has timed out
                    timestamp last_sent_time = target->last_sent_time.load(std::memory_order_relaxed);
                    auto elapsed = duration_cast<milliseconds>(now - last_sent_time);

                    // Still waiting, skip this target
                    if (elapsed < timeout_threshold)
                        continue;

                    target->reachable.store(false);
                    //target->latency.store(ping::TIMEOUT_LATENCY, std::memory_order_release);
                }

                // Send new ping
                uint16_t new_seq = last_sent + 1;

                packet.header.un.echo.sequence = htons(new_seq);
                packet.header.checksum = 0;
                packet.header.checksum = calculate_checksum(&packet, sizeof(packet));

                // Update state before sending
                target->last_sent_time.store(now, std::memory_order_relaxed);
                target->last_sent_seq.store(new_seq, std::memory_order_release);

                auto res = send_ping(socket.sock_fd, packet, target->addr);
                if(!res && res.error() == IcmpResponse::socket_error)
                {
                    std::cerr << "reconnecting send socket" << std::endl;
                    socket.reconnect();
                    std::this_thread::sleep_for(milliseconds(100));
                } else if(!res && res.error() == IcmpResponse::timeout)
                {
                    std::cerr << "send socket stalled (send buffer likely full)" << std::endl;
                    std::this_thread::sleep_for(milliseconds(100));
                }
            }

            std::this_thread::sleep_for(frequency);

        } while (running.load());
    }

    // ping receiver functions

    std::expected<std::tuple<in_addr_t, int16_t, timestamp>, IcmpResponse> ping_receive(int sock_fd) noexcept
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

        if (len < 0) // technically, recvfrom only responds with -1
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
                return std::unexpected(IcmpResponse::timeout);

            std::cerr << "recvfrom failed (" << errno << " " << std::strerror(errno) << ")" << std::endl;
            return std::unexpected(IcmpResponse::socket_error);
        }

        timestamp time = mainclock::now();

        // Validate minimum packet size
        if (len < sizeof(struct iphdr) + sizeof(struct icmphdr))
        {
            std::cerr << "received invalid echo packet (too big)" << std::endl;
            return std::unexpected(IcmpResponse::invalid);
        }

        struct iphdr* ip_header = (struct iphdr*)recv_buffer;
        int ip_header_len = ip_header->ihl * 4;

        if (ip_header->protocol != IPPROTO_ICMP)
            return std::unexpected(IcmpResponse::ignore);

        // Validate IP header length
        if (ip_header_len < sizeof(struct iphdr) ||
            ip_header_len > len - sizeof(struct icmphdr))
        {
            std::cerr << "received invalid echo packet (invalid IP header length)" << std::endl;
            return std::unexpected(IcmpResponse::invalid);
        }

        struct icmphdr* icmp_reply = (struct icmphdr*)(recv_buffer + ip_header_len);

        if (icmp_reply->un.echo.id != (getpid() & 0xFFFF)) // not ours
            return std::unexpected(IcmpResponse::ignore);

        if (icmp_reply->type == ICMP_ECHOREPLY)
            return std::make_tuple(recv_addr.sin_addr.s_addr, ntohs(icmp_reply->un.echo.sequence), time);
        // Check for ICMP Destination Unreachable
        else if (icmp_reply->type == ICMP_DEST_UNREACH)
        {
            // The ICMP error message contains the original IP header + first 8 bytes of original packet
            struct iphdr* orig_ip = (struct iphdr*)(recv_buffer + ip_header_len + sizeof(struct icmphdr));
            struct icmphdr* orig_icmp = (struct icmphdr*)((char*)orig_ip + (orig_ip->ihl * 4));

            in_addr_t target_addr = orig_ip->daddr;  // The destination we were trying to reach
            uint16_t sequence = ntohs(orig_icmp->un.echo.sequence);

            auto target_it = targets.find(target_addr);

            if (target_it == targets.end()) // not a ping we are sending out, simply ignore
                return std::unexpected(IcmpResponse::ignore);

            const char* error_msg = "";
            switch(icmp_reply->code)
            {
                case ICMP_NET_UNREACH:   error_msg = "Network unreachable"; break;
                case ICMP_HOST_UNREACH:  error_msg = "Host unreachable"; break;
                case ICMP_PROT_UNREACH:  error_msg = "Protocol unreachable"; break;
                case ICMP_PORT_UNREACH:  error_msg = "Port unreachable"; break;
                case ICMP_NET_ANO:       error_msg = "Network prohibited"; break;
                case ICMP_HOST_ANO:      error_msg = "Host prohibited"; break;
                default:                 error_msg = "Destination unreachable"; break;
            }

            std::cerr << "ICMP error for " << target_it->second->host << ": " << error_msg << std::endl;

            // Mark this ping as failed with special latency value
            uint16_t expected_seq = target_it->second->last_sent_seq.load(std::memory_order_acquire);
            if (expected_seq == sequence)
            {
                target_it->second->last_received_seq.store(sequence, std::memory_order_release);
                target_it->second->reachable.store(false);

            }

            return std::unexpected(IcmpResponse::unreachable);
        }

        return std::unexpected(IcmpResponse::ignore);
    }

    void receive_worker(milliseconds timeout)
    {
        sock_wrap socket(timeout);
        while(running.load())
        {
            auto res = ping_receive(socket.sock_fd);

            if(res) // if res is set, then it exists in targets
            {
                auto [addr, sequence, received] = *res;
                auto& target = targets[addr];

                // Check if this is the sequence we're waiting for
                uint16_t expected_seq = target->last_sent_seq.load(std::memory_order_acquire);

                if(expected_seq == sequence)
                {
                    // Calculate latency
                    const timestamp sent_time = target->last_sent_time.load(std::memory_order_relaxed);
                    const int16_t latency = duration_cast<microseconds>(received - sent_time).count() / 100;

                    target->last_received_seq.store(sequence, std::memory_order_release);
                    target->latency.store(latency, std::memory_order_relaxed);
                    target->reachable.store(true);
                }
                // else -> ignore this as sequence is stale
            }
            else if(res.error() == IcmpResponse::socket_error )
            {
                std::cerr << "reconnecting receive socket" << std::endl;
                socket.reconnect();
                std::this_thread::sleep_for(milliseconds(100));
            }
        }
    }

    // external interface facilities
    const std::string ping_stats_dump_json()
    {
        std::vector<std::string> entries;

        for (const auto& [k, target] : targets) {
            entries.push_back(fmt::format("\"{}\": {}", target->host, target->dump_json()));
        }
        return fmt::format("{{{}}}", fmt::join(entries, ", "));
    }

    void start_ping_monitoring(const std::vector<std::string>& hosts, std::chrono::milliseconds frequency)
    {
        if (hosts.empty())
            return;

        running.store(true);

        for(auto& host : hosts)
        {
            auto resolved = resolve_hostname(host);
            if (resolved.empty())
            {
                fmt::print("Can't resolve hostname '{}' - ignoring", host);
                continue;
            }
            auto target = std::make_unique<Target>(host, resolved);
            targets.insert({target->addr.sin_addr.s_addr, std::move(target)});
        }

        recv_thread = std::move(std::thread(receive_worker, frequency * 3));
        send_thread = std::move(std::thread(send_worker, frequency));
    }

    void stop_ping_monitoring() {
        running.store(false);

        recv_thread.join();
        send_thread.join();
    }
}
