#include "log.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>

namespace
{
std::mutex g_mutex;
std::ofstream g_stream;
} // namespace

namespace cs4x3ui::log
{
bool initialize(const std::filesystem::path& module_path)
{
    const auto log_path = module_path.parent_path() / L"cs4x3ui.log";
    std::scoped_lock lock(g_mutex);
    g_stream.open(log_path, std::ios::out | std::ios::trunc);
    return g_stream.is_open();
}

void write(const std::string_view message)
{
    std::scoped_lock lock(g_mutex);
    if (!g_stream.is_open())
        return;

    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &time);

    g_stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << " " << message << '\n';
    g_stream.flush();
}
} // namespace cs4x3ui::log
