#include "ppp.h"
#include <filesystem>
#include <string.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <experimental/iterator>

namespace fs = std::filesystem;

std::vector<std::string> getEndpoints()
{
    std::vector<std::string> endpoints;
    for (const auto& e : fs::directory_iterator("/sys/class/net"))
        if (e.is_symlink() && e.path().filename().string().rfind("ppp", 0) == 0)
            endpoints.push_back(e.path().filename().string());
    return endpoints;
}

inline bool is_number(std::string_view s) {
    char* e = nullptr;
    std::strtod(s.data(), &e);
    return e == s.data() + s.size();
}

std::string pppoe_dump_endpoint_json(const std::string& endpoint)
{
    std::vector<std::string> values;
    values.push_back("\"interface\": \"" + endpoint  + "\"");
    std::ostringstream output;
    output << "{";

    {
        std::ifstream in("/var/run/pppd/" + endpoint + ".mac");
        std::string val;

        if (!in.is_open()) {

            values.push_back("\"remote-mac\": null");

        } else {
            std::getline(in, val);
            val.erase(std::remove(val.begin(), val.end(), '"'), val.end());
            values.push_back("\"remote-mac\": \"" + val + "\"");
        }
    }


    for (const auto& f : fs::directory_iterator("/sys/class/net/" + endpoint + "/statistics"))
    {
        if (!f.is_regular_file())
            continue;

        std::ifstream in(f.path());
        std::string val;
        std::getline(in, val);

        if( !is_number(val) )
            continue; // optionally: values.push_back("\"" + f.path().filename().string() + "\": \"" + val + "\"");

        if( val == "" )
            val = "null";

        values.push_back("\"" + f.path().filename().string() + "\": " + val);
    }

    std::copy(values.begin(), values.end(),
              std::experimental::make_ostream_joiner(output, ", "));

    output << "}";
    return output.str();
}

const std::string pppoe_dump_json()
{
    std::vector<std::string> endpoints = getEndpoints();
    std::ostringstream output;

    output << "[";

    std::transform(endpoints.begin(), endpoints.end(),
                   std::experimental::make_ostream_joiner(output, ", "),
                   [](const auto& e){
                       return pppoe_dump_endpoint_json(e);
                   });
    output << "]";
    return std::string(output.str());
}
