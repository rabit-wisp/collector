#include <string>
#include <vector>


namespace ping {

    void start_ping_monitoring(const std::vector<std::string>& hosts, std::chrono::milliseconds frequency);
    void stop_ping_monitoring();
    const std::string ping_stats_dump_json();

}
