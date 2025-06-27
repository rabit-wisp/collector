#include "compress.h"
#include <zlib.h>
#include <stdexcept>

std::vector<uint8_t> gzip(const std::string& data) {
    std::vector<uint8_t> out(deflateBound(nullptr, data.size()));
    z_stream zs{};                                   // zero-init
    if (deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED,
                     15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        throw std::runtime_error("deflateInit2");

    zs.next_in   = (Bytef*)data.data();
    zs.avail_in  = data.size();
    zs.next_out  = out.data();
    zs.avail_out = out.size();

    if (deflate(&zs, Z_FINISH) != Z_STREAM_END)
        throw std::runtime_error("deflate");
    out.resize(out.size() - zs.avail_out);
    deflateEnd(&zs);
    return out;
}

