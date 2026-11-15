#ifndef slic3r_MakerWorldUrl_hpp_
#define slic3r_MakerWorldUrl_hpp_

#include <algorithm>
#include <cctype>
#include <string>

namespace Slic3r { namespace MakerWorld {

inline std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

inline std::string decode(const std::string& value)
{
    std::string result;
    auto        hex = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%') {
            if (i + 2 >= value.size() || hex(value[i + 1]) < 0 || hex(value[i + 2]) < 0)
                return {};
            result += char(hex(value[i + 1]) * 16 + hex(value[i + 2]));
            i += 2;
        } else {
            result += value[i]; // '+' is a literal in signed URLs, not form encoding.
        }
    }
    return result;
}

inline std::string https_host(const std::string& url)
{
    if (lower(url.substr(0, 8)) != "https://")
        return {};
    if (std::any_of(url.begin(), url.end(), [](unsigned char c) { return c <= 32 || c == 127 || c == '\\'; }))
        return {};
    auto host = lower(url.substr(8, url.find_first_of("/?#", 8) - 8));
    if (host.size() > 4 && host.substr(host.size() - 4) == ":443")
        host.resize(host.size() - 4);
    if (host.empty() || host.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-") != std::string::npos)
        return {};
    return host;
}

inline bool is_page(const std::string& url)
{
    const auto host = https_host(url);
    return host == "makerworld.com" || host == "www.makerworld.com" || host == "makerworld.com.cn";
}

inline bool is_download(const std::string& url)
{
    const auto host = https_host(url);
    return is_page(url) || host == "public-cdn.bblmw.com" || host == "makerworld.bblmw.com" || host == "makerworld.bblmw.cn";
}

inline bool is_open_link(const std::string& url)
{
    const auto value = lower(url);
    return value.rfind("bambustudio://", 0) == 0 || value.rfind("bambustudioopen://", 0) == 0;
}

inline bool supported_file(const std::string& name)
{
    auto pos = name.find_last_of('.');
    if (pos == std::string::npos)
        return false;
    const auto ext = lower(name.substr(pos));
    return ext == ".3mf" || ext == ".stl" || ext == ".obj" || ext == ".step" || ext == ".stp" || ext == ".amf" || ext == ".zip";
}

struct Download
{
    std::string url;
    std::string name;
    explicit    operator bool() const { return !url.empty(); }
};

inline Download parse_download(std::string value)
{
    if (value.size() > 32768)
        return {};
    const auto prefix = lower(value);
    if (prefix.rfind("bambustudioopen://", 0) == 0) {
        value = value.substr(std::string("bambustudioopen://").size());
    } else if (prefix.rfind("bambustudio://open/?file=", 0) == 0) {
        value = value.substr(std::string("bambustudio://open/?file=").size());
    } else if (prefix.rfind("bambustudio://open?file=", 0) == 0) {
        value = value.substr(std::string("bambustudio://open?file=").size());
    } else if (is_open_link(value)) {
        return {};
    }
    // Decode an encoded envelope once, preserving escapes in an already-plain
    // signed HTTPS URL. This matches MakerWorld's model.url bridge payload.
    if (lower(value.substr(0, 8)) != "https://")
        value = decode(value);
    std::string name;
    const auto  name_pos = value.rfind("&name=");
    if (name_pos != std::string::npos) {
        name = decode(value.substr(name_pos + 6));
        value.resize(name_pos);
    }
    if (!is_download(value))
        return {};
    if (name.empty()) {
        auto path = value.substr(0, value.find_first_of("?#"));
        name      = decode(path.substr(path.find_last_of('/') + 1));
        if (!supported_file(name))
            name = "MakerWorld-model.3mf";
    }
    // A web page may supply a display name, never a path on the local machine.
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos)
        name = name.substr(slash + 1);
    for (auto& c : name) {
        if (static_cast<unsigned char>(c) < 32 || c == 127 || c == ':' || c == '?' || c == '*')
            c = '_';
    }
    if (name.empty() || name.size() > 180 || name.front() == '.' || !supported_file(name))
        return {};
    return {value, name};
}

}} // namespace Slic3r::MakerWorld
#endif
