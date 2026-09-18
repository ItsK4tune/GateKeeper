#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gatekeeper::protocol
{

class JsonReader
{
public:
    explicit JsonReader(std::string_view input);

    void Skip();
    [[nodiscard]] char Peek();
    bool Take(char c);
    void Expect(char c);
    void End();

    std::string String();
    std::uint64_t UnsignedNumber();
    std::int64_t SignedNumber();
    bool Boolean();
    std::vector<std::string> StringArray();

private:
    std::string_view input_;
    std::size_t pos_{0};

    [[noreturn]] void Fail(const std::string& message);
    unsigned Hex();
    static void AppendUtf8(std::string& out, unsigned code_point);
};

}
