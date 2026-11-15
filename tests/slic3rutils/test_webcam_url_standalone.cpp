#include "slic3r/Utils/WebcamUrl.hpp"
#include <cstdio>

static int failures = 0;
static void expect(const std::string& in, const std::string& want)
{
    const auto got = Slic3r::Webcam::player_url(in);
    if (got != want) {
        std::printf("FAIL: '%s' -> '%s' (want '%s')\n", in.c_str(), got.c_str(), want.c_str());
        ++failures;
    }
}

int main()
{
    expect("192.168.2.72", "http://192.168.2.72/webcam/player");
    expect("http://192.168.2.72", "http://192.168.2.72/webcam/player");
    expect("http://192.168.2.72:7125/", "http://192.168.2.72/webcam/player");
    expect("192.168.2.72:7125", "http://192.168.2.72/webcam/player");
    expect("https://u1.local/printer?x=1", "http://u1.local/webcam/player");
    expect("[fe80::1]:80", "http://[fe80::1]/webcam/player");
    expect("", "");
    expect("bad host", "");
    std::printf(failures ? "webcam url tests failed\n" : "webcam url tests passed\n");
    return failures ? 1 : 0;
}
