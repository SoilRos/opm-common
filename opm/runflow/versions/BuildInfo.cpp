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

#include <opm/runflow/versions/BuildInfo.hpp>

#include <opm/runflow/process/Process.hpp>

#include <chrono>
#include <exception>
#include <sstream>

namespace {

// Flow answers both options without starting MPI; older versions that do not
// know --build-info may start it before refusing the option.
constexpr auto askTimeout = std::chrono::seconds(30);

std::optional<std::string> ask(const std::filesystem::path& flow, const std::string& option)
{
    try {
        const auto result = Opm::RunFlow::captureOutput({flow.string(), option}, askTimeout);
        if (result.exitCode == 0)
            return result.output;
    } catch (const std::exception&) {
        // cannot be started: nothing is known
    }
    return std::nullopt;
}

} // namespace

namespace Opm::RunFlow {

std::optional<BuildInfo> parseBuildInfo(const std::string& output)
{
    BuildInfo info;
    std::istringstream lines(output);
    for (std::string line; std::getline(lines, line);) {
        const auto equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const auto key = line.substr(0, equals);
        auto value = line.substr(equals + 1);
        if (value.empty())
            continue; // not known to Flow either
        if (key == "version")
            info.opmVersion = value;
        else if (key == "hash")
            info.gitHash = value;
        else if (key == "build_time")
            info.buildTime = value;
        else if (key == "build_type")
            info.buildType = value;
        else if (key == "mpi")
            info.mpi = value;
    }
    if (!info.opmVersion)
        return std::nullopt;
    return info;
}

std::optional<std::string> parseFlowVersion(const std::string& output)
{
    std::istringstream lines(output);
    std::string line;
    std::getline(lines, line);
    std::istringstream words(line);
    std::string program, version;
    if (words >> program >> version)
        return version;
    return std::nullopt;
}

BuildInfo describeFlow(const std::filesystem::path& flow)
{
    if (const auto output = ask(flow, "--build-info")) {
        if (auto info = parseBuildInfo(*output))
            return *info;
    }
    BuildInfo info;
    if (const auto output = ask(flow, "--version"))
        info.opmVersion = parseFlowVersion(*output);
    return info;
}

} // namespace Opm::RunFlow
