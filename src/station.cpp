#include "station.h"
#include <dirent.h>
#include <algorithm>
#include <numeric>
#include <filesystem>

#include <iostream>

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/ranges.h>

namespace fs = std::filesystem;

#define BIT(x) (1ULL<<(x))

static std::string mac_addr_n2a(const unsigned char *mac) {
    return fmt::format("{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
                       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static std::string mac_addr_n2a(const std::array<uint8_t, 8>& mac) {
    return mac_addr_n2a(mac.data());
}

static std::string power_mode_to_string(uint32_t pm) {
    switch (pm) {
        case NL80211_MESH_POWER_ACTIVE: return "ACTIVE";
        case NL80211_MESH_POWER_LIGHT_SLEEP: return "LIGHT SLEEP";
        case NL80211_MESH_POWER_DEEP_SLEEP: return "DEEP SLEEP";
        default: return "UNKNOWN";
    }
}

static std::string plink_state_to_string(uint8_t state) {
    const char* states[] = {"LISTEN", "OPN_SNT", "OPN_RCVD", "CNF_RCVD", "ESTAB", "HOLDING", "BLOCKED"};
    if (state < sizeof(states)/sizeof(states[0])) {
        return std::string(states[state]);
    }
    return "UNKNOWN";
}

static BitrateInfo parse_bitrate(struct nlattr *bitrate_attr) {
    BitrateInfo info;
    struct nlattr *rinfo[NL80211_RATE_INFO_MAX + 1];
    static struct nla_policy rate_policy[NL80211_RATE_INFO_MAX + 1];

    // Initialize policy array
    memset(rate_policy, 0, sizeof(rate_policy));
    rate_policy[NL80211_RATE_INFO_BITRATE].type = NLA_U16;
    rate_policy[NL80211_RATE_INFO_BITRATE32].type = NLA_U32;
    rate_policy[NL80211_RATE_INFO_MCS].type = NLA_U8;
    rate_policy[NL80211_RATE_INFO_40_MHZ_WIDTH].type = NLA_FLAG;
    rate_policy[NL80211_RATE_INFO_SHORT_GI].type = NLA_FLAG;
    rate_policy[NL80211_RATE_INFO_VHT_MCS].type = NLA_U8;
    rate_policy[NL80211_RATE_INFO_VHT_NSS].type = NLA_U8;
    rate_policy[NL80211_RATE_INFO_80_MHZ_WIDTH].type = NLA_FLAG;
    rate_policy[NL80211_RATE_INFO_80P80_MHZ_WIDTH].type = NLA_FLAG;
    rate_policy[NL80211_RATE_INFO_160_MHZ_WIDTH].type = NLA_FLAG;
    rate_policy[NL80211_RATE_INFO_320_MHZ_WIDTH].type = NLA_FLAG;
    rate_policy[NL80211_RATE_INFO_HE_MCS].type = NLA_U8;
    rate_policy[NL80211_RATE_INFO_HE_NSS].type = NLA_U8;
    rate_policy[NL80211_RATE_INFO_HE_GI].type = NLA_U8;
    rate_policy[NL80211_RATE_INFO_HE_DCM].type = NLA_U8;
    rate_policy[NL80211_RATE_INFO_HE_RU_ALLOC].type = NLA_U8;
    rate_policy[NL80211_RATE_INFO_EHT_MCS].type = NLA_U8;
    rate_policy[NL80211_RATE_INFO_EHT_NSS].type = NLA_U8;
    rate_policy[NL80211_RATE_INFO_EHT_GI].type = NLA_U8;
    rate_policy[NL80211_RATE_INFO_EHT_RU_ALLOC].type = NLA_U8;
    rate_policy[NL80211_RATE_INFO_1_MHZ_WIDTH].type = NLA_FLAG;
    rate_policy[NL80211_RATE_INFO_2_MHZ_WIDTH].type = NLA_FLAG;
    rate_policy[NL80211_RATE_INFO_4_MHZ_WIDTH].type = NLA_FLAG;
    rate_policy[NL80211_RATE_INFO_8_MHZ_WIDTH].type = NLA_FLAG;
    rate_policy[NL80211_RATE_INFO_16_MHZ_WIDTH].type = NLA_FLAG;

    if (nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX, bitrate_attr, rate_policy)) {
        return info;
    }

    if (rinfo[NL80211_RATE_INFO_BITRATE32]) {
        info.rate_mbps_x10 = nla_get_u32(rinfo[NL80211_RATE_INFO_BITRATE32]);
    } else if (rinfo[NL80211_RATE_INFO_BITRATE]) {
        info.rate_mbps_x10 = nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE]);
    }

    if (rinfo[NL80211_RATE_INFO_MCS]) {
        info.mcs = nla_get_u8(rinfo[NL80211_RATE_INFO_MCS]);
    }
    if (rinfo[NL80211_RATE_INFO_VHT_MCS]) {
        info.vht_mcs = nla_get_u8(rinfo[NL80211_RATE_INFO_VHT_MCS]);
    }
    if (rinfo[NL80211_RATE_INFO_VHT_NSS]) {
        info.vht_nss = nla_get_u8(rinfo[NL80211_RATE_INFO_VHT_NSS]);
    }
    if (rinfo[NL80211_RATE_INFO_HE_MCS]) {
        info.he_mcs = nla_get_u8(rinfo[NL80211_RATE_INFO_HE_MCS]);
    }
    if (rinfo[NL80211_RATE_INFO_HE_NSS]) {
        info.he_nss = nla_get_u8(rinfo[NL80211_RATE_INFO_HE_NSS]);
    }
    if (rinfo[NL80211_RATE_INFO_HE_GI]) {
        info.he_gi = nla_get_u8(rinfo[NL80211_RATE_INFO_HE_GI]);
    }
    if (rinfo[NL80211_RATE_INFO_HE_DCM]) {
        info.he_dcm = nla_get_u8(rinfo[NL80211_RATE_INFO_HE_DCM]);
    }
    if (rinfo[NL80211_RATE_INFO_HE_RU_ALLOC]) {
        info.he_ru_alloc = nla_get_u8(rinfo[NL80211_RATE_INFO_HE_RU_ALLOC]);
    }
    if (rinfo[NL80211_RATE_INFO_EHT_MCS]) {
        info.eht_mcs = nla_get_u8(rinfo[NL80211_RATE_INFO_EHT_MCS]);
    }
    if (rinfo[NL80211_RATE_INFO_EHT_NSS]) {
        info.eht_nss = nla_get_u8(rinfo[NL80211_RATE_INFO_EHT_NSS]);
    }
    if (rinfo[NL80211_RATE_INFO_EHT_GI]) {
        info.eht_gi = nla_get_u8(rinfo[NL80211_RATE_INFO_EHT_GI]);
    }
    if (rinfo[NL80211_RATE_INFO_EHT_RU_ALLOC]) {
        info.eht_ru_alloc = nla_get_u8(rinfo[NL80211_RATE_INFO_EHT_RU_ALLOC]);
    }

    info.short_gi = !!rinfo[NL80211_RATE_INFO_SHORT_GI];
    info.width_40mhz = !!rinfo[NL80211_RATE_INFO_40_MHZ_WIDTH];
    info.width_80mhz = !!rinfo[NL80211_RATE_INFO_80_MHZ_WIDTH];
    info.width_80p80mhz = !!rinfo[NL80211_RATE_INFO_80P80_MHZ_WIDTH];
    info.width_160mhz = !!rinfo[NL80211_RATE_INFO_160_MHZ_WIDTH];
    info.width_320mhz = !!rinfo[NL80211_RATE_INFO_320_MHZ_WIDTH];
    info.width_1mhz = !!rinfo[NL80211_RATE_INFO_1_MHZ_WIDTH];
    info.width_2mhz = !!rinfo[NL80211_RATE_INFO_2_MHZ_WIDTH];
    info.width_4mhz = !!rinfo[NL80211_RATE_INFO_4_MHZ_WIDTH];
    info.width_8mhz = !!rinfo[NL80211_RATE_INFO_8_MHZ_WIDTH];
    info.width_16mhz = !!rinfo[NL80211_RATE_INFO_16_MHZ_WIDTH];

    return info;
}

static TxqStats parse_txq_stats(struct nlattr *tid_stats_attr) {
    TxqStats stats;
    struct nlattr *txqstats_info[NL80211_TXQ_STATS_MAX + 1];
    static struct nla_policy txqstats_policy[NL80211_TXQ_STATS_MAX + 1];

    // Initialize policy array
    memset(txqstats_policy, 0, sizeof(txqstats_policy));
    txqstats_policy[NL80211_TXQ_STATS_BACKLOG_BYTES].type = NLA_U32;
    txqstats_policy[NL80211_TXQ_STATS_BACKLOG_PACKETS].type = NLA_U32;
    txqstats_policy[NL80211_TXQ_STATS_FLOWS].type = NLA_U32;
    txqstats_policy[NL80211_TXQ_STATS_DROPS].type = NLA_U32;
    txqstats_policy[NL80211_TXQ_STATS_ECN_MARKS].type = NLA_U32;
    txqstats_policy[NL80211_TXQ_STATS_OVERLIMIT].type = NLA_U32;
    txqstats_policy[NL80211_TXQ_STATS_COLLISIONS].type = NLA_U32;
    txqstats_policy[NL80211_TXQ_STATS_TX_BYTES].type = NLA_U32;
    txqstats_policy[NL80211_TXQ_STATS_TX_PACKETS].type = NLA_U32;

    if (nla_parse_nested(txqstats_info, NL80211_TXQ_STATS_MAX, tid_stats_attr, txqstats_policy)) {
        return stats;
    }

    if (txqstats_info[NL80211_TXQ_STATS_BACKLOG_BYTES]) {
        stats.backlog_bytes = nla_get_u32(txqstats_info[NL80211_TXQ_STATS_BACKLOG_BYTES]);
    }
    if (txqstats_info[NL80211_TXQ_STATS_BACKLOG_PACKETS]) {
        stats.backlog_packets = nla_get_u32(txqstats_info[NL80211_TXQ_STATS_BACKLOG_PACKETS]);
    }
    if (txqstats_info[NL80211_TXQ_STATS_FLOWS]) {
        stats.flows = nla_get_u32(txqstats_info[NL80211_TXQ_STATS_FLOWS]);
    }
    if (txqstats_info[NL80211_TXQ_STATS_DROPS]) {
        stats.drops = nla_get_u32(txqstats_info[NL80211_TXQ_STATS_DROPS]);
    }
    if (txqstats_info[NL80211_TXQ_STATS_ECN_MARKS]) {
        stats.ecn_marks = nla_get_u32(txqstats_info[NL80211_TXQ_STATS_ECN_MARKS]);
    }
    if (txqstats_info[NL80211_TXQ_STATS_OVERLIMIT]) {
        stats.overlimit = nla_get_u32(txqstats_info[NL80211_TXQ_STATS_OVERLIMIT]);
    }
    if (txqstats_info[NL80211_TXQ_STATS_COLLISIONS]) {
        stats.collisions = nla_get_u32(txqstats_info[NL80211_TXQ_STATS_COLLISIONS]);
    }
    if (txqstats_info[NL80211_TXQ_STATS_TX_BYTES]) {
        stats.tx_bytes = nla_get_u32(txqstats_info[NL80211_TXQ_STATS_TX_BYTES]);
    }
    if (txqstats_info[NL80211_TXQ_STATS_TX_PACKETS]) {
        stats.tx_packets = nla_get_u32(txqstats_info[NL80211_TXQ_STATS_TX_PACKETS]);
    }

    return stats;
}

static std::vector<TidStats> parse_tid_stats(struct nlattr *tid_stats_attr) {
    std::vector<TidStats> tid_stats;
    struct nlattr *tidattr;
    int rem, tid = 0;

    static struct nla_policy stats_policy[NL80211_TID_STATS_MAX + 1];

    // Initialize policy array
    memset(stats_policy, 0, sizeof(stats_policy));
    stats_policy[NL80211_TID_STATS_RX_MSDU].type = NLA_U64;
    stats_policy[NL80211_TID_STATS_TX_MSDU].type = NLA_U64;
    stats_policy[NL80211_TID_STATS_TX_MSDU_RETRIES].type = NLA_U64;
    stats_policy[NL80211_TID_STATS_TX_MSDU_FAILED].type = NLA_U64;
    stats_policy[NL80211_TID_STATS_TXQ_STATS].type = NLA_NESTED;

    nla_for_each_nested(tidattr, tid_stats_attr, rem) {
        struct nlattr *stats_info[NL80211_TID_STATS_MAX + 1];
        TidStats ts;
        ts.tid = tid++;

        if (nla_parse_nested(stats_info, NL80211_TID_STATS_MAX, tidattr, stats_policy)) {
            continue;
        }

        if (stats_info[NL80211_TID_STATS_RX_MSDU]) {
            ts.rx_msdu = nla_get_u64(stats_info[NL80211_TID_STATS_RX_MSDU]);
        }
        if (stats_info[NL80211_TID_STATS_TX_MSDU]) {
            ts.tx_msdu = nla_get_u64(stats_info[NL80211_TID_STATS_TX_MSDU]);
        }
        if (stats_info[NL80211_TID_STATS_TX_MSDU_RETRIES]) {
            ts.tx_msdu_retries = nla_get_u64(stats_info[NL80211_TID_STATS_TX_MSDU_RETRIES]);
        }
        if (stats_info[NL80211_TID_STATS_TX_MSDU_FAILED]) {
            ts.tx_msdu_failed = nla_get_u64(stats_info[NL80211_TID_STATS_TX_MSDU_FAILED]);
        }
        if (stats_info[NL80211_TID_STATS_TXQ_STATS]) {
            ts.txq_stats = parse_txq_stats(stats_info[NL80211_TID_STATS_TXQ_STATS]);
        }

        tid_stats.push_back(ts);
    }

    return tid_stats;
}

static BssParam parse_bss_param(struct nlattr *bss_param_attr) {
    BssParam param;
    struct nlattr *bss_param_info[NL80211_STA_BSS_PARAM_MAX + 1];
    static struct nla_policy bss_policy[NL80211_STA_BSS_PARAM_MAX + 1];

    // Initialize policy array
    memset(bss_policy, 0, sizeof(bss_policy));
    bss_policy[NL80211_STA_BSS_PARAM_CTS_PROT].type = NLA_FLAG;
    bss_policy[NL80211_STA_BSS_PARAM_SHORT_PREAMBLE].type = NLA_FLAG;
    bss_policy[NL80211_STA_BSS_PARAM_SHORT_SLOT_TIME].type = NLA_FLAG;
    bss_policy[NL80211_STA_BSS_PARAM_DTIM_PERIOD].type = NLA_U8;
    bss_policy[NL80211_STA_BSS_PARAM_BEACON_INTERVAL].type = NLA_U16;

    if (nla_parse_nested(bss_param_info, NL80211_STA_BSS_PARAM_MAX, bss_param_attr, bss_policy)) {
        return param;
    }

    if (bss_param_info[NL80211_STA_BSS_PARAM_DTIM_PERIOD]) {
        param.dtim_period = nla_get_u8(bss_param_info[NL80211_STA_BSS_PARAM_DTIM_PERIOD]);
    }
    if (bss_param_info[NL80211_STA_BSS_PARAM_BEACON_INTERVAL]) {
        param.beacon_interval = nla_get_u16(bss_param_info[NL80211_STA_BSS_PARAM_BEACON_INTERVAL]);
    }
    if (bss_param_info[NL80211_STA_BSS_PARAM_CTS_PROT]) {
        param.cts_protection = true;
    }
    if (bss_param_info[NL80211_STA_BSS_PARAM_SHORT_PREAMBLE]) {
        param.short_preamble = true;
    }
    if (bss_param_info[NL80211_STA_BSS_PARAM_SHORT_SLOT_TIME]) {
        param.short_slot_time = true;
    }

    return param;
}

static std::vector<int8_t> get_chain_signal(struct nlattr *attr_list) {
    std::vector<int8_t> chain_signals;
    if (!attr_list) return chain_signals;

    struct nlattr *attr;
    int rem;
    nla_for_each_nested(attr, attr_list, rem) {
        chain_signals.push_back((int8_t)nla_get_u8(attr));
    }
    return chain_signals;
}

static int station_dump_handler(struct nl_msg *msg, void *arg) {
    // Get current time
    struct timeval now;
    gettimeofday(&now, NULL);
    uint64_t current_time_ms = now.tv_sec * 1000ULL + (now.tv_usec / 1000);

    StationInfo *ctx = static_cast<StationInfo*>(arg);
    StationInfo& station = *ctx;
    station.current_time_ms = current_time_ms;


    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct genlmsghdr *gnlh = (struct genlmsghdr*)nlmsg_data(nlmsg_hdr(msg));
    struct nlattr *sinfo[NL80211_STA_INFO_MAX + 1];

    static struct nla_policy stats_policy[NL80211_STA_INFO_MAX + 1];
    static bool policy_initialized = false;

    if (!policy_initialized) {
        memset(stats_policy, 0, sizeof(stats_policy));
        stats_policy[NL80211_STA_INFO_INACTIVE_TIME].type = NLA_U32;
        stats_policy[NL80211_STA_INFO_RX_BYTES].type = NLA_U32;
        stats_policy[NL80211_STA_INFO_TX_BYTES].type = NLA_U32;
        stats_policy[NL80211_STA_INFO_RX_BYTES64].type = NLA_U64;
        stats_policy[NL80211_STA_INFO_TX_BYTES64].type = NLA_U64;
        stats_policy[NL80211_STA_INFO_RX_PACKETS].type = NLA_U32;
        stats_policy[NL80211_STA_INFO_TX_PACKETS].type = NLA_U32;
        stats_policy[NL80211_STA_INFO_BEACON_RX].type = NLA_U64;
        stats_policy[NL80211_STA_INFO_SIGNAL].type = NLA_U8;
        stats_policy[NL80211_STA_INFO_SIGNAL_AVG].type = NLA_U8;
        stats_policy[NL80211_STA_INFO_BEACON_SIGNAL_AVG].type = NLA_U8;
        stats_policy[NL80211_STA_INFO_T_OFFSET].type = NLA_U64;
        stats_policy[NL80211_STA_INFO_TX_BITRATE].type = NLA_NESTED;
        stats_policy[NL80211_STA_INFO_RX_BITRATE].type = NLA_NESTED;
        stats_policy[NL80211_STA_INFO_LLID].type = NLA_U16;
        stats_policy[NL80211_STA_INFO_PLID].type = NLA_U16;
        stats_policy[NL80211_STA_INFO_PLINK_STATE].type = NLA_U8;
        stats_policy[NL80211_STA_INFO_TX_RETRIES].type = NLA_U32;
        stats_policy[NL80211_STA_INFO_TX_FAILED].type = NLA_U32;
        stats_policy[NL80211_STA_INFO_BEACON_LOSS].type = NLA_U32;
        stats_policy[NL80211_STA_INFO_RX_DROP_MISC].type = NLA_U64;
        stats_policy[NL80211_STA_INFO_STA_FLAGS].minlen = sizeof(struct nl80211_sta_flag_update);
        stats_policy[NL80211_STA_INFO_LOCAL_PM].type = NLA_U32;
        stats_policy[NL80211_STA_INFO_PEER_PM].type = NLA_U32;
        stats_policy[NL80211_STA_INFO_NONPEER_PM].type = NLA_U32;
        stats_policy[NL80211_STA_INFO_CHAIN_SIGNAL].type = NLA_NESTED;
        stats_policy[NL80211_STA_INFO_CHAIN_SIGNAL_AVG].type = NLA_NESTED;
        stats_policy[NL80211_STA_INFO_TID_STATS].type = NLA_NESTED;
        stats_policy[NL80211_STA_INFO_BSS_PARAM].type = NLA_NESTED;
        stats_policy[NL80211_STA_INFO_RX_DURATION].type = NLA_U64;
        stats_policy[NL80211_STA_INFO_TX_DURATION].type = NLA_U64;
        stats_policy[NL80211_STA_INFO_ACK_SIGNAL].type = NLA_U8;
        stats_policy[NL80211_STA_INFO_ACK_SIGNAL_AVG].type = NLA_U8;
        stats_policy[NL80211_STA_INFO_AIRTIME_LINK_METRIC].type = NLA_U32;
        stats_policy[NL80211_STA_INFO_CONNECTED_TO_AS].type = NLA_U8;
        stats_policy[NL80211_STA_INFO_CONNECTED_TO_GATE].type = NLA_U8;
        stats_policy[NL80211_STA_INFO_AIRTIME_WEIGHT].type = NLA_U16;
        stats_policy[NL80211_STA_INFO_EXPECTED_THROUGHPUT].type = NLA_U32;
        stats_policy[NL80211_STA_INFO_CONNECTED_TIME].type = NLA_U32;
        stats_policy[NL80211_STA_INFO_ASSOC_AT_BOOTTIME].type = NLA_U64;
        policy_initialized = true;
    }

    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);

    if (!tb[NL80211_ATTR_STA_INFO]) {
        return NL_SKIP;
    }

    if (nla_parse_nested(sinfo, NL80211_STA_INFO_MAX, tb[NL80211_ATTR_STA_INFO], stats_policy)) {
        return NL_SKIP;
    }


    // Get MAC address and interface
    if (tb[NL80211_ATTR_MAC]) {
        // TODO: this is a bug in unreviewed code
        //station.mac_address = mac_addr_n2a((unsigned char*)nla_data(tb[NL80211_ATTR_MAC]));
    }
    
    if (tb[NL80211_ATTR_IFINDEX]) {
        char dev[IFNAMSIZ];
        if_indextoname(nla_get_u32(tb[NL80211_ATTR_IFINDEX]), dev);
        station.interface = std::string(dev);
    }

    // Basic statistics
    if (sinfo[NL80211_STA_INFO_INACTIVE_TIME]) {
        station.inactive_time_ms = nla_get_u32(sinfo[NL80211_STA_INFO_INACTIVE_TIME]);
    }
    
    if (sinfo[NL80211_STA_INFO_RX_BYTES64]) {
        station.rx_bytes = nla_get_u64(sinfo[NL80211_STA_INFO_RX_BYTES64]);
    } else if (sinfo[NL80211_STA_INFO_RX_BYTES]) {
        station.rx_bytes = nla_get_u32(sinfo[NL80211_STA_INFO_RX_BYTES]);
    }
    
    if (sinfo[NL80211_STA_INFO_TX_BYTES64]) {
        station.tx_bytes = nla_get_u64(sinfo[NL80211_STA_INFO_TX_BYTES64]);
    } else if (sinfo[NL80211_STA_INFO_TX_BYTES]) {
        station.tx_bytes = nla_get_u32(sinfo[NL80211_STA_INFO_TX_BYTES]);
    }
    
    if (sinfo[NL80211_STA_INFO_RX_PACKETS]) {
        station.rx_packets = nla_get_u32(sinfo[NL80211_STA_INFO_RX_PACKETS]);
    }
    
    if (sinfo[NL80211_STA_INFO_TX_PACKETS]) {
        station.tx_packets = nla_get_u32(sinfo[NL80211_STA_INFO_TX_PACKETS]);
    }
    
    if (sinfo[NL80211_STA_INFO_TX_RETRIES]) {
        station.tx_retries = nla_get_u32(sinfo[NL80211_STA_INFO_TX_RETRIES]);
    }
    
    if (sinfo[NL80211_STA_INFO_TX_FAILED]) {
        station.tx_failed = nla_get_u32(sinfo[NL80211_STA_INFO_TX_FAILED]);
    }
    
    if (sinfo[NL80211_STA_INFO_BEACON_LOSS]) {
        station.beacon_loss = nla_get_u32(sinfo[NL80211_STA_INFO_BEACON_LOSS]);
    }
    
    if (sinfo[NL80211_STA_INFO_BEACON_RX]) {
        station.beacon_rx = nla_get_u64(sinfo[NL80211_STA_INFO_BEACON_RX]);
    }
    
    if (sinfo[NL80211_STA_INFO_RX_DROP_MISC]) {
        station.rx_drop_misc = nla_get_u64(sinfo[NL80211_STA_INFO_RX_DROP_MISC]);
    }

    // Signal information
    if (sinfo[NL80211_STA_INFO_SIGNAL]) {
        station.signal_dbm = (int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL]);
    }
    
    if (sinfo[NL80211_STA_INFO_SIGNAL_AVG]) {
        station.signal_avg_dbm = (int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL_AVG]);
    }
    
    if (sinfo[NL80211_STA_INFO_BEACON_SIGNAL_AVG]) {
        station.beacon_signal_avg_dbm = (int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_BEACON_SIGNAL_AVG]);
    }
    
    if (sinfo[NL80211_STA_INFO_ACK_SIGNAL]) {
        station.last_ack_signal_dbm = (int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_ACK_SIGNAL]);
    }
    
    if (sinfo[NL80211_STA_INFO_ACK_SIGNAL_AVG]) {
        station.avg_ack_signal_dbm = (int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_ACK_SIGNAL_AVG]);
    }

    // Chain signal information
    station.chain_signal = get_chain_signal(sinfo[NL80211_STA_INFO_CHAIN_SIGNAL]);
    station.chain_signal_avg = get_chain_signal(sinfo[NL80211_STA_INFO_CHAIN_SIGNAL_AVG]);

    // Timing information
    if (sinfo[NL80211_STA_INFO_T_OFFSET]) {
        station.t_offset_us = nla_get_u64(sinfo[NL80211_STA_INFO_T_OFFSET]);
    }
    
    if (sinfo[NL80211_STA_INFO_TX_DURATION]) {
        station.tx_duration_us = nla_get_u64(sinfo[NL80211_STA_INFO_TX_DURATION]);
    }
    
    if (sinfo[NL80211_STA_INFO_RX_DURATION]) {
        station.rx_duration_us = nla_get_u64(sinfo[NL80211_STA_INFO_RX_DURATION]);
    }
    
    if (sinfo[NL80211_STA_INFO_CONNECTED_TIME]) {
        station.connected_time_sec = nla_get_u32(sinfo[NL80211_STA_INFO_CONNECTED_TIME]);
    }
    
    if (sinfo[NL80211_STA_INFO_ASSOC_AT_BOOTTIME]) {
        station.assoc_at_boottime_us = nla_get_u64(sinfo[NL80211_STA_INFO_ASSOC_AT_BOOTTIME]);
        
        // Calculate association time in wall clock time
        struct timespec now_ts;
        clock_gettime(CLOCK_BOOTTIME, &now_ts);
        uint64_t boot_ns = now_ts.tv_sec * 1000000000ULL + now_ts.tv_nsec;
        uint64_t bt = station.assoc_at_boottime_us.value() * 1000; // Convert to ns
        station.assoc_at_ms = station.current_time_ms - ((boot_ns - bt) / 1000000);
    }

    // Bitrate information
    if (sinfo[NL80211_STA_INFO_TX_BITRATE]) {
        station.tx_bitrate = parse_bitrate(sinfo[NL80211_STA_INFO_TX_BITRATE]);
    }
    
    if (sinfo[NL80211_STA_INFO_RX_BITRATE]) {
        station.rx_bitrate = parse_bitrate(sinfo[NL80211_STA_INFO_RX_BITRATE]);
    }

    // Quality metrics
    if (sinfo[NL80211_STA_INFO_AIRTIME_WEIGHT]) {
        station.airtime_weight = nla_get_u16(sinfo[NL80211_STA_INFO_AIRTIME_WEIGHT]);
    }
    
    if (sinfo[NL80211_STA_INFO_EXPECTED_THROUGHPUT]) {
        uint32_t thr = nla_get_u32(sinfo[NL80211_STA_INFO_EXPECTED_THROUGHPUT]);
        station.expected_throughput_kbps = thr; // Already in kbps from kernel
    }

    // Mesh-specific information
    if (sinfo[NL80211_STA_INFO_LLID]) {
        station.mesh_llid = nla_get_u16(sinfo[NL80211_STA_INFO_LLID]);
    }
    
    if (sinfo[NL80211_STA_INFO_PLID]) {
        station.mesh_plid = nla_get_u16(sinfo[NL80211_STA_INFO_PLID]);
    }
    
    if (sinfo[NL80211_STA_INFO_PLINK_STATE]) {
        station.mesh_plink_state = plink_state_to_string(nla_get_u8(sinfo[NL80211_STA_INFO_PLINK_STATE]));
    }
    
    if (sinfo[NL80211_STA_INFO_AIRTIME_LINK_METRIC]) {
        station.mesh_airtime_link_metric = nla_get_u32(sinfo[NL80211_STA_INFO_AIRTIME_LINK_METRIC]);
    }
    
    if (sinfo[NL80211_STA_INFO_CONNECTED_TO_GATE]) {
        station.mesh_connected_to_gate = !!nla_get_u8(sinfo[NL80211_STA_INFO_CONNECTED_TO_GATE]);
    }
    
    if (sinfo[NL80211_STA_INFO_CONNECTED_TO_AS]) {
        station.mesh_connected_to_as = !!nla_get_u8(sinfo[NL80211_STA_INFO_CONNECTED_TO_AS]);
    }
    
    if (sinfo[NL80211_STA_INFO_LOCAL_PM]) {
        station.mesh_local_ps_mode = power_mode_to_string(nla_get_u32(sinfo[NL80211_STA_INFO_LOCAL_PM]));
    }
    
    if (sinfo[NL80211_STA_INFO_PEER_PM]) {
        station.mesh_peer_ps_mode = power_mode_to_string(nla_get_u32(sinfo[NL80211_STA_INFO_PEER_PM]));
    }
    
    if (sinfo[NL80211_STA_INFO_NONPEER_PM]) {
        station.mesh_nonpeer_ps_mode = power_mode_to_string(nla_get_u32(sinfo[NL80211_STA_INFO_NONPEER_PM]));
    }

    // Station flags
    if (sinfo[NL80211_STA_INFO_STA_FLAGS]) {
        StationFlags flags;
        struct nl80211_sta_flag_update *sta_flags = 
            (struct nl80211_sta_flag_update *)nla_data(sinfo[NL80211_STA_INFO_STA_FLAGS]);

        if (sta_flags->mask & BIT(NL80211_STA_FLAG_AUTHORIZED)) {
            flags.authorized = !!(sta_flags->set & BIT(NL80211_STA_FLAG_AUTHORIZED));
        }
        if (sta_flags->mask & BIT(NL80211_STA_FLAG_AUTHENTICATED)) {
            flags.authenticated = !!(sta_flags->set & BIT(NL80211_STA_FLAG_AUTHENTICATED));
        }
        if (sta_flags->mask & BIT(NL80211_STA_FLAG_ASSOCIATED)) {
            flags.associated = !!(sta_flags->set & BIT(NL80211_STA_FLAG_ASSOCIATED));
        }
        if (sta_flags->mask & BIT(NL80211_STA_FLAG_SHORT_PREAMBLE)) {
            flags.short_preamble = !!(sta_flags->set & BIT(NL80211_STA_FLAG_SHORT_PREAMBLE));
        }
        if (sta_flags->mask & BIT(NL80211_STA_FLAG_WME)) {
            flags.wmm_wme = !!(sta_flags->set & BIT(NL80211_STA_FLAG_WME));
        }
        if (sta_flags->mask & BIT(NL80211_STA_FLAG_MFP)) {
            flags.mfp = !!(sta_flags->set & BIT(NL80211_STA_FLAG_MFP));
        }
        if (sta_flags->mask & BIT(NL80211_STA_FLAG_TDLS_PEER)) {
            flags.tdls_peer = !!(sta_flags->set & BIT(NL80211_STA_FLAG_TDLS_PEER));
        }
        
        station.flags = flags;
    }

    // TID statistics (verbose mode)
    if (sinfo[NL80211_STA_INFO_TID_STATS] ) {
        station.tid_stats = parse_tid_stats(sinfo[NL80211_STA_INFO_TID_STATS]);
    }

    // BSS parameters
    if (sinfo[NL80211_STA_INFO_BSS_PARAM]) {
        station.bss_param = parse_bss_param(sinfo[NL80211_STA_INFO_BSS_PARAM]);
    }

    return NL_SKIP;
}

static int error_handler(struct sockaddr_nl *nla, struct nlmsgerr *err, void *arg) {
    int *ret = static_cast<int*>(arg);
    *ret = err->error;
    return NL_STOP;
}

static int finish_handler(struct nl_msg *msg, void *arg) {
    int *ret = static_cast<int*>(arg);
    *ret = 0;
    return NL_SKIP;
}

static int ack_handler(struct nl_msg *msg, void *arg) {
    int *ret = static_cast<int*>(arg);
    *ret = 0;
    return NL_STOP;
}

const StationInfo wifi_station_dump(const std::string& interface, MacAddress  mac_bytes) {
    struct nl_sock *sock = nullptr;
    struct nl_msg *msg = nullptr;
    struct nl_cb *cb = nullptr;
    int ifindex = -1;
    int ret = -1;
    int family_id;
    StationInfo station;

    station.mac_address = mac_addr_n2a(mac_bytes);

    // Create netlink socket
    sock = nl_socket_alloc();
    if (!sock) {
        return std::move(station);
    }

    if (genl_connect(sock)) {
        goto cleanup;
    }

    // Get nl80211 family ID
    family_id = genl_ctrl_resolve(sock, "nl80211");
    if (family_id < 0) {
        goto cleanup;
    }

    // Create message
    msg = nlmsg_alloc();
    if (!msg) {
        goto cleanup;
    }

    // Setup the message
    genlmsg_put(msg, 0, 0, family_id, 0, NLM_F_DUMP, NL80211_CMD_GET_STATION, 0);

    // Add interface index
    ifindex = if_nametoindex(interface.c_str());
    if (ifindex == 0) {
        goto cleanup;
    }

    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex);
    nla_put(msg, NL80211_ATTR_MAC, 6, mac_bytes.data());

    // Set up callbacks
    cb = nl_cb_alloc(NL_CB_DEFAULT);
    if (!cb) {
        goto cleanup;
    }

    nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, station_dump_handler, &station);
    nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &ret);
    nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &ret);
    nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &ret);

    // Send message
    ret = nl_send_auto_complete(sock, msg);
    if (ret < 0) {
        nl_cb_put(cb);
        goto cleanup;
    }

    ret = 1;
    while (ret > 0) {
        nl_recvmsgs(sock, cb);
    }

    nl_cb_put(cb);

cleanup:
    if (msg) nlmsg_free(msg);
    if (sock) nl_socket_free(sock);

    return std::move(station);
}


// Helper template for optional values
template<typename T>
std::string format_optional(const char* name, const std::optional<T>& opt) {
    if (!opt.has_value()) return "";

    if constexpr (std::is_same_v<T, std::string>) {
        return fmt::format(R"("{}"  : "{}")", name, *opt);
    } else if constexpr (std::is_same_v<T, bool>) {
        return fmt::format(R"("{}" : {})", name, *opt ? "true" : "false");
    } else if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, uint8_t>) {
        return fmt::format(R"("{}" : {})", name, static_cast<int16_t>(*opt));
    } else {
        return fmt::format(R"("{}" : {})", name, *opt);
    }
}

// Helper for required values
template<typename T>
std::string format_value(const char* name, const T& val) {
    if constexpr (std::is_same_v<T, std::string>) {
        return fmt::format(R"("{}" : "{}")", name, val);
    } else if constexpr (std::is_same_v<T, bool>) {
        return fmt::format(R"("{}" : {})", name, val ? "true" : "false");
    } else if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, uint8_t>) {
        return fmt::format(R"("{}" : {})", name, static_cast<int16_t>(val));
    } else {
        return fmt::format(R"("{}" : {})", name, val);
    }
}

template<typename T>
void add_if_present(std::vector<std::string>& fields, const T& field, const char* name) {
    if (auto str = format_optional(name, field); !str.empty()) {
        fields.push_back(str);
    }
 };


// Helper template for optional values
template<typename T>
void add_optional(std::ostringstream& output, const char* name, const std::optional<T>& opt, bool& first) {
    if (opt.has_value()) {
        if (!first) output << ", ";
        output << "\"" << name << "\": ";
        if constexpr (std::is_same_v<T, std::string>) {
            output << "\"" << *opt << "\"";
        } else if constexpr (std::is_same_v<T, bool>) {
            output << (*opt ? "true" : "false");
        }  else if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, uint8_t>) {
            output << int16_t(*opt);
        } else {
            output << *opt;
        }
        first = false;
    }
}

// Helper for required values
template<typename T>
void add_value(std::ostringstream& output, const char* name, const T& val, bool& first) {
    if (!first) output << ", ";
    output << "\"" << name << "\": ";
    if constexpr (std::is_same_v<T, std::string>) {
        output << "\"" << val << "\"";
    } else if constexpr (std::is_same_v<T, bool>) {
        output << (val ? "true" : "false");
    }  else if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, uint8_t>) {
        output << int16_t(val);
    } else {
        output << val;
    }
    first = false;
}

std::string serialize_bitrate(const BitrateInfo& bitrate) {

    std::vector<std::string> fields;

    fields.push_back(format_value("rate_mbps_x10", bitrate.rate_mbps_x10));

    add_if_present(fields, bitrate.mcs, "mcs");
    add_if_present(fields, bitrate.vht_mcs, "vht_mcs");
    add_if_present(fields, bitrate.vht_nss, "vht_nss");
    add_if_present(fields, bitrate.he_mcs, "he_mcs");
    add_if_present(fields, bitrate.he_nss, "he_nss");
    add_if_present(fields, bitrate.he_gi, "he_gi");
    add_if_present(fields, bitrate.he_dcm, "he_dcm");
    add_if_present(fields, bitrate.he_ru_alloc, "he_ru_alloc");
    add_if_present(fields, bitrate.eht_mcs, "eht_mcs");
    add_if_present(fields, bitrate.eht_nss, "eht_nss");
    add_if_present(fields, bitrate.eht_gi, "eht_gi");
    add_if_present(fields, bitrate.eht_ru_alloc, "eht_ru_alloc");

    fields.push_back(format_value("short_gi", bitrate.short_gi));
    fields.push_back(format_value("width_40mhz", bitrate.width_40mhz));
    fields.push_back(format_value("width_80mhz", bitrate.width_80mhz));
    fields.push_back(format_value("width_80p80mhz", bitrate.width_80p80mhz));
    fields.push_back(format_value("width_160mhz", bitrate.width_160mhz));
    fields.push_back(format_value("width_320mhz", bitrate.width_320mhz));
    fields.push_back(format_value("width_1mhz", bitrate.width_1mhz));
    fields.push_back(format_value("width_2mhz", bitrate.width_2mhz));
    fields.push_back(format_value("width_4mhz", bitrate.width_4mhz));
    fields.push_back(format_value("width_8mhz", bitrate.width_8mhz));
    fields.push_back(format_value("width_16mhz", bitrate.width_16mhz));

    return fmt::format("{{{}}}", fmt::join(fields, ", "));
}

std::string serialize_txq_stats(const TxqStats& txq) {
    std::vector<std::string> fields;

    add_if_present(fields, txq.backlog_bytes, "backlog_bytes");
    add_if_present(fields, txq.backlog_packets, "backlog_packets");
    add_if_present(fields, txq.flows, "flows");
    add_if_present(fields, txq.drops, "drops");
    add_if_present(fields, txq.ecn_marks, "ecn_marks");
    add_if_present(fields, txq.overlimit, "overlimit");
    add_if_present(fields, txq.collisions, "collisions");
    add_if_present(fields, txq.tx_bytes, "tx_bytes");
    add_if_present(fields, txq.tx_packets, "tx_packets");

    return fmt::format("{{{}}}", fmt::join(fields, ", "));
}

std::string serialize_tid_stats(const TidStats& tid) {
    std::vector<std::string> fields;
    fields.push_back(format_value("tid", tid.tid));

    add_if_present(fields, tid.rx_msdu, "rx_msdu");
    add_if_present(fields, tid.tx_msdu, "tx_msdu");
    add_if_present(fields, tid.tx_msdu_retries, "tx_msdu_retries");
    add_if_present(fields, tid.tx_msdu_failed, "tx_msdu_failed");

    if (tid.txq_stats.has_value()) {
        fields.push_back(fmt::format(R"("txq_stats" : {})", serialize_txq_stats(*tid.txq_stats)));
    }

    return fmt::format("{{{}}}", fmt::join(fields, ", "));
}

std::string serialize_bss_param(const BssParam& bss) {
    std::vector<std::string> fields;

    add_if_present(fields, bss.dtim_period, "dtim_period");
    add_if_present(fields, bss.beacon_interval, "beacon_interval");
    add_if_present(fields, bss.cts_protection, "cts_protection");
    add_if_present(fields, bss.short_preamble, "short_preamble");
    add_if_present(fields, bss.short_slot_time, "short_slot_time");

    return fmt::format("{{{}}}", fmt::join(fields, ", "));
}

std::string serialize_station_flags(const StationFlags& flags) {
    std::vector<std::string> fields;

    add_if_present(fields, flags.authorized, "authorized");
    add_if_present(fields, flags.authenticated, "authenticated");
    add_if_present(fields, flags.associated, "associated");
    add_if_present(fields, flags.short_preamble, "short_preamble");
    add_if_present(fields, flags.wmm_wme, "wmm_wme");
    add_if_present(fields, flags.mfp, "mfp");
    add_if_present(fields, flags.tdls_peer, "tdls_peer");

    return fmt::format("{{{}}}", fmt::join(fields, ", "));
}

const std::string wifi_station_dump_json(const StationInfo& info) {
    std::vector<std::string> fields;

    // Required fields
    fields.push_back(format_value("mac_address", info.mac_address));
    fields.push_back(format_value("interface", info.interface));
    fields.push_back(format_value("current_time_ms", info.current_time_ms));

    add_if_present(fields, info.inactive_time_ms, "inactive_time_ms");
    add_if_present(fields, info.rx_bytes, "rx_bytes");
    add_if_present(fields, info.rx_packets, "rx_packets");
    add_if_present(fields, info.tx_bytes, "tx_bytes");
    add_if_present(fields, info.tx_packets, "tx_packets");
    add_if_present(fields, info.tx_retries, "tx_retries");
    add_if_present(fields, info.tx_failed, "tx_failed");
    add_if_present(fields, info.beacon_loss, "beacon_loss");
    add_if_present(fields, info.beacon_rx, "beacon_rx");
    add_if_present(fields, info.rx_drop_misc, "rx_drop_misc");
    add_if_present(fields, info.signal_dbm, "signal_dbm");
    add_if_present(fields, info.signal_avg_dbm, "signal_avg_dbm");
    add_if_present(fields, info.beacon_signal_avg_dbm, "beacon_signal_avg_dbm");
    add_if_present(fields, info.t_offset_us, "t_offset_us");
    add_if_present(fields, info.tx_duration_us, "tx_duration_us");
    add_if_present(fields, info.rx_duration_us, "rx_duration_us");
    add_if_present(fields, info.last_ack_signal_dbm, "last_ack_signal_dbm");
    add_if_present(fields, info.avg_ack_signal_dbm, "avg_ack_signal_dbm");
    add_if_present(fields, info.airtime_weight, "airtime_weight");
    add_if_present(fields, info.expected_throughput_kbps, "expected_throughput_kbps");

    // Mesh fields
    add_if_present(fields, info.mesh_llid, "mesh_llid");
    add_if_present(fields, info.mesh_plid, "mesh_plid");
    add_if_present(fields, info.mesh_plink_state, "mesh_plink_state");
    add_if_present(fields, info.mesh_airtime_link_metric, "mesh_airtime_link_metric");
    add_if_present(fields, info.mesh_connected_to_gate, "mesh_connected_to_gate");
    add_if_present(fields, info.mesh_connected_to_as, "mesh_connected_to_as");
    add_if_present(fields, info.mesh_local_ps_mode, "mesh_local_ps_mode");
    add_if_present(fields, info.mesh_peer_ps_mode, "mesh_peer_ps_mode");
    add_if_present(fields, info.mesh_nonpeer_ps_mode, "mesh_nonpeer_ps_mode");

    add_if_present(fields, info.connected_time_sec, "connected_time_sec");
    add_if_present(fields, info.assoc_at_boottime_us, "assoc_at_boottime_us");
    add_if_present(fields, info.assoc_at_ms, "assoc_at_ms");

    // Complex optional fields
    if (info.tx_bitrate.has_value()) {
        fields.push_back(fmt::format(R"("tx_bitrate" : {})", serialize_bitrate(*info.tx_bitrate)));
    }

    if (info.rx_bitrate.has_value()) {
        fields.push_back(fmt::format(R"("rx_bitrate" : {})", serialize_bitrate(*info.rx_bitrate)));
    }

    if (info.flags.has_value()) {
        fields.push_back(fmt::format(R"("flags" : {})", serialize_station_flags(*info.flags)));
    }

    if (info.bss_param.has_value()) {
        fields.push_back(fmt::format(R"("bss_param" : {})", serialize_bss_param(*info.bss_param)));
    }

    // Arrays
    if (!info.tid_stats.empty()) {
        std::vector<std::string> tid_json;
        tid_json.reserve(info.tid_stats.size());
        for (const auto& tid : info.tid_stats) {
            tid_json.push_back(serialize_tid_stats(tid));
        }
        fields.push_back(fmt::format(R"("tid_stats" : [{}])", fmt::join(tid_json, ", ")));
    }

    if (!info.chain_signal.empty()) {
        std::vector<int> chain_vals;
        chain_vals.reserve(info.chain_signal.size());
        for (const auto& signal : info.chain_signal) {
            chain_vals.push_back(static_cast<int>(signal));
        }
        fields.push_back(fmt::format(R"("chain_signal" : [{}])", fmt::join(chain_vals, ", ")));
    }

    if (!info.chain_signal_avg.empty()) {
        std::vector<int> chain_avg_vals;
        chain_avg_vals.reserve(info.chain_signal_avg.size());
        for (const auto& signal : info.chain_signal_avg) {
            chain_avg_vals.push_back(static_cast<int>(signal));
        }
        fields.push_back(fmt::format(R"("chain_signal_avg" : [{}])", fmt::join(chain_avg_vals, ", ")));
    }

    return fmt::format("{{{}}}", fmt::join(fields, ", "));
}

std::vector<MacAddress> getStations(const std::string& iface) {

    std::vector<MacAddress> stations;

    struct nl_sock* sock = nl_socket_alloc();
    if (!sock) return stations;

    if (genl_connect(sock) < 0) {
        nl_socket_free(sock);
        return stations;
    }

    int nl80211_id = genl_ctrl_resolve(sock, "nl80211");
    int ifidx = if_nametoindex(iface.c_str());
    if (!ifidx) {
        nl_socket_free(sock);
        return stations;
    }

    struct nl_msg* msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, nl80211_id, 0, NLM_F_DUMP,
                NL80211_CMD_GET_STATION, 0);
    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifidx);

    auto cb = nl_cb_alloc(NL_CB_DEFAULT);
    nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM,
              [](struct nl_msg* msg, void* arg) -> int {
                  std::vector<MacAddress>* out = static_cast<std::vector<MacAddress>*>(arg);
                  struct genlmsghdr* gnlh = static_cast<genlmsghdr*>(nlmsg_data(nlmsg_hdr(msg)));
                  struct nlattr* attrs[NL80211_ATTR_MAX + 1];

                  nla_parse(attrs, NL80211_ATTR_MAX,
                            genlmsg_attrdata(gnlh, 0),
                            genlmsg_attrlen(gnlh, 0), nullptr);

                  if (attrs[NL80211_ATTR_MAC]) {
                      MacAddress interface;
                      std::copy_n(static_cast<uint8_t*>(nla_data(attrs[NL80211_ATTR_MAC])),
                                  8,
                                  interface.begin());
                      out->push_back(interface);
                  }

                  return NL_OK;
              }, &stations);

    nl_send_auto(sock, msg);
    nl_recvmsgs(sock, cb);

    nl_cb_put(cb);
    nlmsg_free(msg);
    nl_socket_free(sock);

    return stations;
}

const std::string wifi_stations_dump_json(const std::string& ifname)
{
    const std::vector<MacAddress> stations = getStations(ifname);
    if (stations.size() == 0)
        return "[]";

    std::vector<std::string> json_stations;
    json_stations.reserve(stations.size());

    std::transform(stations.begin(), stations.end(),
                   std::back_inserter(json_stations),
                   [ifname](const auto& sta) {
                       return wifi_station_dump_json(wifi_station_dump(ifname, sta));
                   });

    return fmt::format("[{}]", fmt::join(json_stations, ", "));
}
