// Small standalone test: c++ -std=c++17 -Isrc this-file.cpp -o test && ./test
#include "slic3r/Utils/MakerWorldUrl.hpp"
#include <cassert>
#include <iostream>

int main()
{
    using namespace Slic3r::MakerWorld;
    assert(is_page("https://makerworld.com/en/studio/webview"));
    assert(!is_page("https://makerworld.com.attacker.test/"));
    assert(!is_download("https://makerworld.com@evil.test/file.3mf"));
    assert(!is_download("http://public-cdn.bblmw.com/file.3mf"));
    assert(!is_download("https://127.0.0.1/file.3mf"));
    assert(!is_download("https://public-cdn.bblmw.com:8080/file.3mf"));
    assert(!is_download("https://public-cdn.bblmw.com\\@evil.test/file.3mf"));
    assert(!parse_download("file:///etc/passwd"));
    assert(!parse_download("https://public-cdn.bblmw.com/f.3mf&name=program.exe"));
    const auto plain = parse_download("https://public-cdn.bblmw.com/a.3mf?signature=A%2FB%2BC&name=My%20model.3mf");
    assert(plain && plain.name == "My model.3mf");
    assert(plain.url == "https://public-cdn.bblmw.com/a.3mf?signature=A%2FB%2BC");
    const auto bridge = parse_download("https%3A%2F%2Fpublic-cdn.bblmw.com%2Fa.3mf%3Ftoken%3Da%252Fb%26name%3DModel.3mf");
    assert(bridge && bridge.url == "https://public-cdn.bblmw.com/a.3mf?token=a%2Fb");
    assert(bridge.name == "Model.3mf");
    for (const auto& prefix : {"bambustudio://open?file=", "bambustudio://open/?file=", "bambustudioopen://"}) {
        const auto link = parse_download(std::string(prefix) + "https%3A%2F%2Fpublic-cdn.bblmw.com%2Fa.3mf%26name%3DPart.3mf");
        assert(link && link.name == "Part.3mf" && link.url == "https://public-cdn.bblmw.com/a.3mf");
    }
    const auto traversal = parse_download("https://public-cdn.bblmw.com/a.3mf&name=..%2F..%2Fpart.3mf");
    assert(traversal && traversal.name == "part.3mf");
    assert(!parse_download("https://public-cdn.bblmw.com/a.3mf&name=.hidden.3mf"));
    assert(!parse_download("https%3A%2F%2Fpublic-cdn.bblmw.com%00.evil.test%2Fa.3mf"));
    assert(parse_download("https://public-cdn.bblmw.com/a.stl").name == "a.stl");
    assert(parse_download("https://makerworld.com/api/file?id=42").name == "MakerWorld-model.3mf");
    assert(!parse_download("bambustudio://other?file=https://public-cdn.bblmw.com/a.3mf"));
    std::cout << "MakerWorld URL and filename checks passed.\n";
}
