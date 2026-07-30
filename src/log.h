#pragma once

#include <filesystem>
#include <string_view>

namespace cs4x3ui::log
{
bool initialize(const std::filesystem::path& module_path);
void write(std::string_view message);
} // namespace cs4x3ui::log
