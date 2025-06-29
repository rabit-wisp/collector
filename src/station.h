#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <net/if.h>
#include <sys/time.h>
#include <time.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
//#include <linux/nl80211.h>
#include "nl80211.h"
#include <vector>
#include <string>
#include <map>
#include <optional>

using MacAddress = std::array<uint8_t,8>;

struct BitrateInfo {
    int rate_mbps_x10 = 0;  // Rate * 10 (e.g., 65 for 6.5 Mbps)
    std::optional<int> mcs;
    std::optional<int> vht_mcs;
    std::optional<int> vht_nss;
    std::optional<int> he_mcs;
    std::optional<int> he_nss;
    std::optional<int> he_gi;
    std::optional<int> he_dcm;
    std::optional<int> he_ru_alloc;
    std::optional<int> eht_mcs;
    std::optional<int> eht_nss;
    std::optional<int> eht_gi;
    std::optional<int> eht_ru_alloc;
    bool short_gi = false;
    bool width_40mhz = false;
    bool width_80mhz = false;
    bool width_80p80mhz = false;
    bool width_160mhz = false;
    bool width_320mhz = false;
    bool width_1mhz = false;
    bool width_2mhz = false;
    bool width_4mhz = false;
    bool width_8mhz = false;
    bool width_16mhz = false;
};

struct TxqStats {
    std::optional<uint32_t> backlog_bytes;
    std::optional<uint32_t> backlog_packets;
    std::optional<uint32_t> flows;
    std::optional<uint32_t> drops;
    std::optional<uint32_t> ecn_marks;
    std::optional<uint32_t> overlimit;
    std::optional<uint32_t> collisions;
    std::optional<uint32_t> tx_bytes;
    std::optional<uint32_t> tx_packets;
};

struct TidStats {
    int tid;
    std::optional<uint64_t> rx_msdu;
    std::optional<uint64_t> tx_msdu;
    std::optional<uint64_t> tx_msdu_retries;
    std::optional<uint64_t> tx_msdu_failed;
    std::optional<TxqStats> txq_stats;
};

struct BssParam {
    std::optional<uint8_t> dtim_period;
    std::optional<uint16_t> beacon_interval;
    std::optional<bool> cts_protection;
    std::optional<bool> short_preamble;
    std::optional<bool> short_slot_time;
};

struct StationFlags {
    std::optional<bool> authorized;
    std::optional<bool> authenticated;
    std::optional<bool> associated;
    std::optional<bool> short_preamble;
    std::optional<bool> wmm_wme;
    std::optional<bool> mfp;
    std::optional<bool> tdls_peer;
};

struct StationInfo {
    std::string mac_address;
    std::string interface;

    std::optional<uint32_t> inactive_time_ms;
    std::optional<uint64_t> rx_bytes;
    std::optional<uint32_t> rx_packets;
    std::optional<uint64_t> tx_bytes;
    std::optional<uint32_t> tx_packets;
    std::optional<uint32_t> tx_retries;
    std::optional<uint32_t> tx_failed;
    std::optional<uint32_t> beacon_loss;
    std::optional<uint64_t> beacon_rx;
    std::optional<uint64_t> rx_drop_misc;
    std::optional<int8_t> signal_dbm;
    std::optional<int8_t> signal_avg_dbm;
    std::optional<int8_t> beacon_signal_avg_dbm;
    std::optional<uint64_t> t_offset_us;
    std::optional<BitrateInfo> tx_bitrate;
    std::optional<uint64_t> tx_duration_us;
    std::optional<BitrateInfo> rx_bitrate;
    std::optional<uint64_t> rx_duration_us;
    std::optional<int8_t> last_ack_signal_dbm;
    std::optional<int8_t> avg_ack_signal_dbm;
    std::optional<uint16_t> airtime_weight;
    std::optional<uint32_t> expected_throughput_kbps;

    // Mesh-specific fields
    std::optional<uint16_t> mesh_llid;
    std::optional<uint16_t> mesh_plid;
    std::optional<std::string> mesh_plink_state;
    std::optional<uint32_t> mesh_airtime_link_metric;
    std::optional<bool> mesh_connected_to_gate;
    std::optional<bool> mesh_connected_to_as;
    std::optional<std::string> mesh_local_ps_mode;
    std::optional<std::string> mesh_peer_ps_mode;
    std::optional<std::string> mesh_nonpeer_ps_mode;

    std::optional<StationFlags> flags;
    std::vector<TidStats> tid_stats;
    std::optional<BssParam> bss_param;
    std::optional<uint32_t> connected_time_sec;
    std::optional<uint64_t> assoc_at_boottime_us;
    std::optional<uint64_t> assoc_at_ms;

    std::vector<int8_t> chain_signal;
    std::vector<int8_t> chain_signal_avg;

    uint64_t current_time_ms;
};

const StationInfo wifi_station_dump(const std::string& interface);
const std::string wifi_stations_dump_json(const std::string& iface);
