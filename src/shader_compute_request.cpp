#include "shader_compute_request.h"
#include <charconv>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace fs = std::filesystem;
namespace {
using Bytes = std::string;
template <class T> T read(const Bytes &data, size_t offset) {
    if (offset > data.size() || sizeof(T) > data.size() - offset)
        throw std::runtime_error("truncated compute request");
    T value;
    std::memcpy(&value, data.data() + offset, sizeof(value));
    return value;
}
Bytes part(fs::path path, const char *suffix, size_t limit) {
    path.replace_extension(suffix);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    auto size = file.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(limit))
        throw std::runtime_error(std::string("missing or oversized ") + suffix);
    Bytes bytes(static_cast<size_t>(size), 0);
    file.seekg(0);
    if (size && !file.read(bytes.data(), size))
        throw std::runtime_error("short read");
    return bytes;
}
void hash_bytes(uint64_t &key, const void *data, size_t size) {
    const auto *bytes = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < size; ++i)
        key = (key ^ bytes[i]) * 1099511628211ull;
}
} // namespace

x2w::GraphicsRequest x2w::read_graphics_request(const fs::path &marker) {
    auto stem = marker.stem().string();
    if (marker.extension() != ".a" || stem.size() != 20 || stem.compare(0, 4, "req_"))
        throw std::runtime_error("expected req_<16 hex digits>.a");
    uint64_t expected = 0;
    const auto end = stem.data() + stem.size();
    const auto parsed = std::from_chars(stem.data() + 4, end, expected, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != end)
        throw std::runtime_error("invalid graphics request key");
    GraphicsRequest request{part(marker, ".a", 64 << 20), part(marker, ".b", 64 << 20),
                            part(marker, ".root", 16 << 20), {}};
    if (fs::exists(fs::path(marker).replace_extension(".ia")))
        request.input_layout = part(marker, ".ia", 1 << 20);
    if (fs::exists(fs::path(marker).replace_extension(".psin")))
        request.pixel_entry = part(marker, ".psin", 12);
    constexpr char revision[] = "x2w-startup-shader-28";
    uint64_t key = 1469598103934665603ull;
    hash_bytes(key, revision, sizeof(revision) - 1);
    for (const auto *p : {&request.a, &request.b, &request.root, &request.input_layout, &request.pixel_entry})
        hash_bytes(key, p->data(), p->size());
    if (request.root.empty() || key != expected)
        throw std::runtime_error("graphics request key mismatch or missing root");
    return request;
}

x2w::ComputeRequest x2w::read_compute_request(const fs::path &marker) {
    auto name = marker.stem().string();
    if (marker.extension() != ".c67" || name.size() != 20 || name.compare(0, 4, "req_"))
        throw std::runtime_error("expected req_<16 hex digits>.c67");
    uint64_t expected = 0;
    auto parsed = std::from_chars(name.data() + 4, name.data() + 20, expected, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != name.data() + 20)
        throw std::runtime_error("invalid request key");
    auto code = part(marker, ".c67", 1 << 20), data = part(marker, ".data", 1 << 20),
         root = part(marker, ".root", 16 << 20);
    auto desc = part(marker, ".desc", 96), sa = part(marker, ".sa", 4096), sb = part(marker, ".sb", 4096);
    if (desc.size() != 96 || sa.size() != 28 || code.empty() || data.empty() || code.size() % 4 ||
        read<uint64_t>(desc, 48) != code.size() || read<uint64_t>(desc, 64) != data.size() ||
        read<uint64_t>(desc, 16) != sa.size() || read<uint64_t>(desc, 32) != sb.size())
        throw std::runtime_error("inconsistent compute request");
    uint64_t key = 1469598103934665603ull;
    constexpr char revision[] = "x2w-startup-shader-28cs";
    hash_bytes(key, revision, sizeof(revision) - 1);
    for (const auto *p : {&code, &data, &root, &desc, &sa, &sb}) {
        uint64_t size = p->size();
        hash_bytes(key, &size, sizeof(size));
        hash_bytes(key, p->data(), p->size());
    }
    if (key != expected)
        throw std::runtime_error("compute request key mismatch");
    return {std::move(code), std::move(data), std::move(root), std::move(desc), std::move(sa), std::move(sb), key};
}

std::string x2w::read_shader_container_bytes(const fs::path &file) {
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    auto length = stream.tellg();
    if (length < 32 || length > 64 * 1024 * 1024)
        throw std::runtime_error("missing or oversized shader container");
    std::string bytes(static_cast<size_t>(length), 0);
    stream.seekg(0);
    if (!stream.read(bytes.data(), length)) throw std::runtime_error("truncated shader container");
    return bytes;
}
x2w::ComputeContainer x2w::read_compute_container(const fs::path &file) {
    auto bytes=read_shader_container_bytes(file);
    const auto stem=file.stem().string();
    if (stem.compare(0,4,"req_") != 0) return {std::move(bytes), {}};
    uint64_t expected=0;
    if (stem.size()!=20 || file.extension()!=".cdx")
        throw std::runtime_error("invalid container request filename");
    auto parsed=std::from_chars(stem.data()+4,stem.data()+stem.size(),expected,16);
    if(parsed.ec!=std::errc{} || parsed.ptr!=stem.data()+stem.size())
        throw std::runtime_error("invalid container request key");
    auto root=part(file,".root",16<<20);
    uint64_t key=1469598103934665603ull, size=root.size();
    constexpr char revision[]="native-container-1";
    hash_bytes(key,revision,sizeof(revision)-1);
    hash_bytes(key,&size,sizeof(size));
    hash_bytes(key,root.data(),root.size());
    hash_bytes(key,bytes.data(),bytes.size());
    if (root.empty() || key!=expected) throw std::runtime_error("container request key mismatch");
    return {std::move(bytes),std::move(root)};
}
