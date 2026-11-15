#ifndef slic3r_WebcamUrl_hpp_
#define slic3r_WebcamUrl_hpp_

#include <cctype>
#include <string>

namespace Slic3r { namespace Webcam {

// Host part of a printer address such as "192.168.2.72", "http://192.168.2.72:7125/"
// or "[fe80::1]:80". Returns an empty string when nothing usable is found.
inline std::string host_only(std::string value)
{
    const auto scheme = value.find("://");
    if (scheme != std::string::npos)
        value = value.substr(scheme + 3);
    value = value.substr(0, value.find_first_of("/?#"));
    const auto at = value.rfind('@');
    if (at != std::string::npos)
        value = value.substr(at + 1);
    if (!value.empty() && value.front() == '[') {
        const auto close = value.find(']');
        return close == std::string::npos ? std::string() : value.substr(0, close + 1);
    }
    const auto colon = value.find(':');
    if (colon != std::string::npos)
        value.resize(colon);
    for (unsigned char c : value)
        if (!(std::isalnum(c) || c == '.' || c == '-' || c == '_'))
            return {};
    return value;
}

// The 30 fps player served by the extended U1 firmware, e.g. http://192.168.2.72/webcam/player
inline std::string player_url(const std::string& printer_host)
{
    const auto host = host_only(printer_host);
    return host.empty() ? std::string() : "http://" + host + "/webcam/player";
}

}} // namespace Slic3r::Webcam
#endif
