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


#include <docopt.h>
#include "station.h"
#include "compress.h"
#include "survey.h"
#include "ppp.h"


static const char USAGE[] =  R"(wireless-stats
Usage:
  wireless-stats <interface> --dest-ip=<ip> --dest-port=<port> [--interval=<sec>] [--no-compress] [--count=<count>]
  wireless-stats <interface> --stdout [--interval=<sec>] [--count=<count>]

Options:
  --dest-ip=<ip>      Destination IPv4 address.
  --dest-port=<port>  Destination UDP port.
  --interval=<sec>    milliseconds between samples [default: 1000].
  --no-compress       don't gzip content
  --count=<count>     only do count number of polls [default: 0]
  --stdout            output stats directly to stdout
)";


int main(int argc, const char* argv[]) {
    const auto args = docopt::docopt(USAGE, {argv + 1, argv + argc}, true, "wireless-stats 0.1");
    std::string ifname = args.at("<interface>").asString();
    int interval       = std::stoi(args.at("--interval").asString());
    int count          = std::stoi(args.at("--count").asString());
    bool stdout = args.at("--stdout").asBool();
    bool compress = !stdout && !args.at("--no-compress").asBool();

    std::string dest_ip  = args.at("--dest-ip").isString()? args.at("--dest-ip").asString() : "";
    int dest_port        = args.at("--dest-port").isString()? std::stoi(args.at("--dest-port").asString()) : 0;

    char buf[256]{};
    std::string hostname = ::gethostname(buf, sizeof(buf)) == 0 ? std::string(buf) : std::string{};

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(dest_port);
    inet_pton(AF_INET, dest_ip.c_str(), &dst.sin_addr);

    auto dump_and_send = [&]{
        double now = std::chrono::system_clock::now().time_since_epoch().count() / 1e9;
        std::ostringstream o;

        auto pppoe = pppoe_dump_json();
        auto survey = wifi_survey_dump_json(ifname);
        auto stations = wifi_stations_dump_json(ifname);

        o << "{\"hostname\": \"" << hostname << "\""
          << ", \"sent\": " << std::fixed <<  std::setprecision(3)  << now
          << ", \"pppoe\": " << pppoe
          << ", \"wireless\": " << survey
          << ", \"stations\": " << stations
          << "}";

        const std::string& s = o.str();

        if (stdout) {

            std::cout << o.str() << std::endl;

        } else if (compress) {

            auto gz = gzip(s);
            sendto(sock, gz.data(), gz.size(), 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));

        } else {

            sendto(sock, s.c_str(), s.length(), 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));

        }
    };


    dump_and_send();
    for (int i = 1; count == 0 || i < count; ++i) {

        if (interval > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));

        dump_and_send();
    };

    close(sock);
    return 0;
}

