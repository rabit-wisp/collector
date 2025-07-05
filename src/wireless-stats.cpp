#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <thread>
#include <chrono>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <map>
#include <vector>
#include <numeric>
#include <string>
#include <iostream>
#include <iomanip>
#include <ranges>

#include <docopt.h>

#include "output.h"
#include "station.h"
#include "survey.h"
#include "ppp.h"
#include "ping.h"

static const char USAGE[] =  R"(wireless-stats
Usage:
  wireless-stats <interface> --tcp --dest-ip=<ip> --dest-port=<port> [options]
  wireless-stats <interface> --udp --dest-ip=<ip> --dest-port=<port> [options]
  wireless-stats <interface> --zmq --endpoint=<endpoint> [--mode=(connect|bind)] [--hostname-tag] [options]
  wireless-stats <interface> --stdout [options]
  wireless-stats <interface> --stderr [options]

Options:
  --stdout                      output to stdout
  --stderr                      output to stdout
  --udp                         send over udp
  --tcp                         send over tcp
  --zmq                         send over zmq
  --dest-ip=<ip>                Destination IPv4 address.
  --dest-port=<port>            Destination UDP port.
  --endpoint=<endpoint>         zmq endpoint (e.g. tcp://*:8000)
  --mode=<mode>                 zmq connection mode [default: connect]
  --interval=<msec>             milliseconds between samples [default: 1000].
  --ping-hosts=<hosts>,...      list of hosts to get ping statistics on (leave empty to not ping)
  --ping-frequency=<msec>       frequency of ping packets (in ms) [default: 1000]
  --count=<count>               only do count number of polls [default: 0]
  --no-compress                 don't gzip content
)";


int main(int argc, const char* argv[]) {

    const auto args = docopt::docopt(USAGE, {argv + 1, argv + argc}, true, "wireless-stats 0.1");
    std::string ifname = args.at("<interface>").asString();
    int interval       = std::stoi(args.at("--interval").asString());
    int count          = std::stoi(args.at("--count").asString());
    bool compress      = !args.at("--no-compress").asBool();
    int ping_frequency = std::stoi(args.at("--ping-frequency").asString());
    std::vector<std::string> hosts;
    if (args.at("--ping-hosts")) {
        auto ping_hosts = args.at("--ping-hosts").asString();
        for (auto&& part : std::views::split(ping_hosts, ',')) {
            hosts.emplace_back(part.begin(), part.end());
        }

    }

    char buf[256]{};
    std::string hostname = ::gethostname(buf, sizeof(buf)) == 0 ? std::string(buf) : std::string{};

    // Configure transport based on command line args
    std::unique_ptr<DataWriter> writer;

    if (args.at("--stdout").asBool()) {
        writer = DataWriter::create_stdout();
    }
    else if (args.at("--stderr").asBool()) {
        writer = DataWriter::create_stderr();
    }
    else if (args.at("--udp").asBool()) {
        std::string dest_ip = args.at("--dest-ip").asString();
        int dest_port = std::stoi(args.at("--dest-port").asString());
        writer = DataWriter::create_udp(dest_ip, dest_port);
    }
    else if (args.at("--tcp").asBool()) {
        std::string dest_ip = args.at("--dest-ip").asString();
        int dest_port = std::stoi(args.at("--dest-port").asString());
        writer = DataWriter::create_tcp(dest_ip, dest_port);
    }
    else if (args.at("--zmq").asBool()) {
        std::string endpoint = args.at("--endpoint").asString();
        std::string mode = args.at("--mode").asString();
        bool hostname_tag = args.at("--hostname-tag").asBool();
        bool should_bind = (mode == "bind");
        writer = DataWriter::create_zmq(endpoint, should_bind, hostname_tag? hostname : "");
    }

    ping::start_ping_monitoring(hosts, ping_frequency);

    writer->set_compression(compress);

    auto dump_and_send = [&]() {
        double now = std::chrono::system_clock::now().time_since_epoch().count() / 1e9;

        auto pppoe = pppoe_dump_json();
        auto survey = wifi_survey_dump_json(ifname);
        auto stations = wifi_stations_dump_json(ifname);
        auto ping_stats = ping::ping_stats_dump_json();

        // Build JSON message
        std::ostringstream o;
        o << "{\"hostname\": \"" << hostname << "\""
          << ", \"sent\": " << std::fixed << std::setprecision(3) << now
          << ", \"pppoe\": " << pppoe
          << ", \"wireless\": " << survey
          << ", \"stations\": " << stations
          << ", \"ping\": " << ping_stats
          << "}" << std::endl;

        // Send via configured transport
        auto result = writer->write(o.str());
        if (!result) {
            std::cerr << "Failed to send data: " << result.error() << std::endl;
        }
    };

    do {
        dump_and_send();
        if (interval > 0 && count != 1)
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));

    } while( count == 0 || --count > 0);

    ping::stop_ping_monitoring();

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // wait a bit so network buffers get a chance to flush out

    return 0;
}
