#include "survey.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <map>
#include <string>
#include <net/if.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
#include <linux/nl80211.h>

struct survey_data {
    int noise = -999;           // dBm, -999 indicates no data
    uint64_t active = 0;        // ms
    uint64_t busy = 0;          // ms
    uint64_t ext_busy = 0;      // ms
    uint64_t rx_time = 0;       // ms
    uint64_t tx_time = 0;       // ms
    bool in_use = false;
};

struct survey_dump_ctx {
    const std::string ifname;
    int ifindex;
    std::map<uint32_t, survey_data> *survey_map;
};

static int survey_response_handler(struct nl_msg *msg, void *arg)
{
    struct survey_dump_ctx *ctx = (struct survey_dump_ctx *)arg;
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct genlmsghdr *gnlh = (struct genlmsghdr *)nlmsg_data(nlmsg_hdr(msg));
    struct nlattr *sinfo[NL80211_SURVEY_INFO_MAX + 1];
    
    static struct nla_policy survey_policy[NL80211_SURVEY_INFO_MAX + 1];
    static bool initialized = false;
    
    if (!initialized) {
        memset(survey_policy, 0, sizeof(survey_policy));
        survey_policy[NL80211_SURVEY_INFO_FREQUENCY].type = NLA_U32;
        survey_policy[NL80211_SURVEY_INFO_NOISE].type = NLA_U8;
        survey_policy[NL80211_SURVEY_INFO_CHANNEL_TIME].type = NLA_U64;
        survey_policy[NL80211_SURVEY_INFO_CHANNEL_TIME_BUSY].type = NLA_U64;
        survey_policy[NL80211_SURVEY_INFO_CHANNEL_TIME_EXT_BUSY].type = NLA_U64;
        survey_policy[NL80211_SURVEY_INFO_CHANNEL_TIME_RX].type = NLA_U64;
        survey_policy[NL80211_SURVEY_INFO_CHANNEL_TIME_TX].type = NLA_U64;
        survey_policy[NL80211_SURVEY_INFO_IN_USE].type = NLA_FLAG;
        initialized = true;
    }
    
    // Parse the netlink attributes
    if (nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
                  genlmsg_attrlen(gnlh, 0), NULL) < 0) {
        return NL_SKIP;
    }
    
    // Check if survey info is present
    if (!tb[NL80211_ATTR_SURVEY_INFO]) {
        return NL_SKIP;
    }
    
    // Parse nested survey attributes
    if (nla_parse_nested(sinfo, NL80211_SURVEY_INFO_MAX,
                         tb[NL80211_ATTR_SURVEY_INFO],
                         survey_policy) < 0) {
        return NL_SKIP;
    }
    
    // Extract frequency - this is our key
    if (!sinfo[NL80211_SURVEY_INFO_FREQUENCY]) {
        return NL_SKIP;  // Skip entries without frequency
    }
    
    uint32_t frequency = nla_get_u32(sinfo[NL80211_SURVEY_INFO_FREQUENCY]);
    survey_data data;
    
    // Extract survey data
    if (sinfo[NL80211_SURVEY_INFO_NOISE]) {
        data.noise = (int8_t)nla_get_u8(sinfo[NL80211_SURVEY_INFO_NOISE]);
    }
    
    if (sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME]) {
        data.active = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME]);
    }
    
    if (sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_BUSY]) {
        data.busy = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_BUSY]);
    }
    
    if (sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_EXT_BUSY]) {
        data.ext_busy = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_EXT_BUSY]);
    }
    
    if (sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_RX]) {
        data.rx_time = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_RX]);
    }
    
    if (sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_TX]) {
        data.tx_time = nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_TX]);
    }
    
    if (sinfo[NL80211_SURVEY_INFO_IN_USE]) {
        data.in_use = true;
    }
    
    // Store in our map
    (*ctx->survey_map)[frequency] = data;
    
    return NL_SKIP;
}

static int error_handler(struct sockaddr_nl *nla, struct nlmsgerr *err, void *arg)
{
    int *ret = (int *)arg;
    *ret = err->error;
    return NL_STOP;
}

static int finish_handler(struct nl_msg *msg, void *arg)
{
    int *ret = (int *)arg;
    *ret = 0;
    return NL_SKIP;
}

static int ack_handler(struct nl_msg *msg, void *arg)
{
    int *ret = (int *)arg;
    *ret = 0;
    return NL_STOP;
}

std::string make_map_object(const survey_data &data) {

    std::ostringstream output;
    output << "{\"active\":"
           << (data.in_use ? "true" : "false")
           << ", \"noise\": "
           << (data.noise == -999? "null": std::to_string(data.noise))
           << ", \"time\": {";

    output << "\"total\":" + std::to_string(data.active);
    output << ", \"busy\":" + std::to_string(data.busy);
    output << ", \"ext_busy\":" + std::to_string(data.ext_busy);
    output << ", \"rx\":" + std::to_string(data.rx_time);
    output << ", \"tx\":" + std::to_string(data.tx_time);
    output << "}}";

    return output.str();
}

std::string json_survey_dump(const std::map<uint32_t, survey_data> &survey_map)
{
    std::ostringstream output;
    output << "{";
    output << std::accumulate(std::next(survey_map.begin()), survey_map.end(),
                              "\"" + std::to_string(survey_map.begin()->first) + "\": " + make_map_object(survey_map.begin()->second),
                              [](auto acc, const auto& next){
                                  return std::move(acc) + ", \"" + std::to_string(next.first) + "\": " + make_map_object(next.second);
                              });
    output << "}";
    return output.str();
}


const std::string wifi_survey_dump_json(const std::string& ifname)
{
    struct nl_sock *sock = NULL;
    struct nl_msg *msg = NULL;
    struct nl_cb *cb = NULL;
    int nl80211_family_id;
    int ifindex;
    int ret = -1;
    int err;
    std::string result;
    // Set the survey response handler
    struct survey_dump_ctx ctx = {
        .ifname = ifname,
        .ifindex = ifindex,
        .survey_map = nullptr
    };
    std::map<uint32_t, survey_data> survey_map;

    // Get interface index
    ifindex = if_nametoindex(ifname.c_str());
    if (ifindex == 0) {
        //return -ENODEV;
        return std::string("null");
    }
    
    // Create netlink socket
    sock = nl_socket_alloc();
    if (!sock) {
        //return -ENOMEM;
        return std::string("null");
    }
    
    // Connect to generic netlink
    if (genl_connect(sock) < 0) {
        goto cleanup;
    }
    
    // Resolve nl80211 family
    nl80211_family_id = genl_ctrl_resolve(sock, "nl80211");
    if (nl80211_family_id < 0) {
        goto cleanup;
    }
    
    // Allocate message
    msg = nlmsg_alloc();
    if (!msg) {
        goto cleanup;
    }
    
    // Setup message header
    if (!genlmsg_put(msg, 0, 0, nl80211_family_id, 0, NLM_F_DUMP,
                     NL80211_CMD_GET_SURVEY, 0)) {
        goto cleanup;
    }
    
    // Add interface index attribute
    if (nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) < 0) {
        goto cleanup;
    }
    
    // Add radio stats flag if requested
    if (nla_put_flag(msg, NL80211_ATTR_SURVEY_RADIO_STATS) < 0) {
        goto cleanup;
    }

    // Setup callback
    cb = nl_cb_alloc(NL_CB_DEFAULT);
    if (!cb) {
        goto cleanup;
    }
    
    err = 1;
    nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &err);
    nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);
    nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &err);

    ctx.survey_map = &survey_map;
    nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, survey_response_handler, &ctx);
    
    // Send message
    if (nl_send_auto_complete(sock, msg) < 0) {
        goto cleanup;
    }
    
    // Process response
    while (err > 0) {
        int res = nl_recvmsgs(sock, cb);
        if (res < 0) {
            break;
        }
    }
    
    if (err == 0) {
        result = json_survey_dump(survey_map);
        ret = 0;  // Success
    } else if (err < 0) {
        ret = err;
    }

cleanup:
    if (cb)
        nl_cb_put(cb);
    if (msg)
        nlmsg_free(msg);
    if (sock)
        nl_socket_free(sock);

    if (err)
        return std::string("null");
    else
        return result;
}
