/*
  Copyright 2026 OPM-OP AS

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <opm/runflow/config/ConfigFiles.hpp>

#include <opm/json/JsonObject.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <system_error>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

// Set by the build system: the path from the installation's bin directory to
// its sysconfdir, e.g. "../etc".
#ifndef OPM_RUNFLOW_BIN_TO_SYSCONF
#define OPM_RUNFLOW_BIN_TO_SYSCONF "../etc"
#endif

namespace {

std::string getEnv(const char* name)
{
    const char* value = std::getenv(name);
    return value ? value : "";
}

} // namespace

namespace Opm::RunFlow {

std::filesystem::path executablePath()
{
    std::error_code ec;
#if defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        return {};
    auto path = std::filesystem::canonical(buffer.data(), ec);
#else
    auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
#endif
    return ec ? std::filesystem::path{} : path;
}

std::filesystem::path installationConfigFile()
{
    const auto program = executablePath();
    if (program.empty())
        return {};
    return (program.parent_path() / OPM_RUNFLOW_BIN_TO_SYSCONF / "opm" / "runflow.json")
        .lexically_normal();
}

std::vector<ConfigFile> configFiles()
{
    std::vector<ConfigFile> files;
    if (const auto installation = installationConfigFile(); !installation.empty())
        files.push_back({installation, false});
    if (const auto home = getEnv("HOME"); !home.empty())
        files.push_back({std::filesystem::path(home) / ".runflow.json", false});
    std::error_code ec;
    if (const auto here = std::filesystem::current_path(ec); !ec)
        files.push_back({here / "runflow.json", false});
    // Absolute, so that the relative paths in it are relative to the file's
    // own directory (resolveConfigPath()), as in every other file.
    if (const auto named = getEnv("RUNFLOW_CONFIG"); !named.empty())
        files.push_back({std::filesystem::absolute(named, ec).lexically_normal(), true});
    return files;
}

std::filesystem::path resolveConfigPath(const std::string& value,
                                        const std::filesystem::path& base)
{
    std::filesystem::path path = value;
    if (value == "~" || value.starts_with("~/"))
        path = getEnv("HOME") + value.substr(1);
    if (path.is_relative())
        path = base / path;
    return path.lexically_normal();
}

std::string readString(const Json::JsonObject& value, const std::filesystem::path& origin,
                       const std::string& setting)
{
    if (!value.is_string())
        throw std::runtime_error(origin.string() + ": \"" + setting + "\" must be a string");
    return value.as_string();
}

std::vector<std::string> readStringList(const Json::JsonObject& value,
                                        const std::filesystem::path& origin,
                                        const std::string& setting)
{
    const auto error = [&] {
        throw std::runtime_error(origin.string() + ": \"" + setting + "\" must be a list of strings");
    };
    if (!value.is_array())
        error();
    std::vector<std::string> list;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto item = value.get_array_item(i);
        if (!item.is_string())
            error();
        list.push_back(item.as_string());
    }
    return list;
}

std::string resolveProgram(const std::string& value, const std::filesystem::path& base)
{
    if (value.find('/') == std::string::npos && value != "~")
        return value; // a name: looked up when used
    return resolveConfigPath(value, base).string();
}

std::vector<std::string> readLauncherOptions(const Json::JsonObject& value,
                                             const std::vector<std::string>& earlier,
                                             const std::filesystem::path& origin,
                                             const std::string& setting,
                                             std::vector<std::string>& warnings)
{
    auto options = listWithInherited(readStringList(value, origin, setting), earlier,
                                     [](const std::string& option) { return option; },
                                     setting, origin, warnings);
    const bool hasRanks = std::ranges::any_of(options, [](const std::string& option) {
        return option.find("{np}") != std::string::npos;
    });
    if (!hasRanks)
        throw std::runtime_error(origin.string() + ": \"" + setting
                                 + "\" must contain {np}, the number of processes");
    return options;
}

std::string resolveConfigPathList(const std::string& value,
                                  const std::filesystem::path& base)
{
    std::string result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = std::min(value.find(':', start), value.size());
        if (end > start) // empty entries are skipped
            result += (result.empty() ? "" : ":")
                + resolveConfigPath(value.substr(start, end - start), base).string();
        start = end + 1;
    }
    return result;
}

} // namespace Opm::RunFlow
