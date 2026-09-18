#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string_view>

namespace gatekeeper::log
{

class Sink
{
public:
    virtual ~Sink() = default;
    virtual void Write(std::string_view message) = 0;
    virtual void Flush() = 0;
};

class NullSink final : public Sink
{
public:
    void Write(std::string_view) override {}
    void Flush() override {}
};

class ConsoleSink final : public Sink
{
public:
    void Write(std::string_view message) override
    {
        std::clog << message;
    }

    void Flush() override
    {
        std::clog.flush();
    }
};

class FileSink final : public Sink
{
public:
    explicit FileSink(const std::filesystem::path& file_path);
    void Write(std::string_view message) override;
    void Flush() override;

private:
    std::ofstream stream_;
};

}
