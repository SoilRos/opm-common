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

#include <opm/runflow/launch/Launcher.hpp>

#include <opm/runflow/launch/LaunchInfo.hpp>
#include <opm/runflow/process/Process.hpp>
#include <opm/runflow/versions/Candidates.hpp>

#include <chrono>
#include <exception>
#include <regex>
#include <system_error>

namespace {

namespace fs = std::filesystem;

std::string replaceAll(std::string text, const std::string& from, const std::string& to)
{
    for (auto pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos + to.size()))
        text.replace(pos, from.size(), to);
    return text;
}

// `program` as an absolute path without symbolic links resolved: the path
// users see (e.g. /usr/lib64/openmpi/bin/mpirun), made absolute.
std::optional<fs::path> executable(const fs::path& program)
{
    if (!Opm::RunFlow::isExecutableFile(program))
        return std::nullopt;
    std::error_code ec;
    auto absolute = fs::absolute(program, ec);
    if (ec)
        return std::nullopt;
    return absolute.lexically_normal();
}

// "5.0.7" -> "5.0": the part of a version that must match.
std::string majorMinor(const std::string& version)
{
    const auto first = version.find('.');
    const auto second = first == std::string::npos ? first : version.find('.', first + 1);
    return version.substr(0, second);
}

std::string describe(const Opm::RunFlow::MpiIdentity& mpi)
{
    return mpi.implementation + " " + mpi.version;
}

} // namespace

namespace Opm::RunFlow {

std::vector<std::string> Launcher::command(int np, int slots) const
{
    std::vector<std::string> words{program.string()};
    for (const auto& argument : arguments)
        words.push_back(replaceAll(replaceAll(argument, "{np}", std::to_string(np)),
                                   "{slots}", std::to_string(slots)));
    return words;
}

std::optional<Launcher> findLauncher(const Installation& flow, const std::string& program,
                                     const std::vector<std::string>& options,
                                     const std::vector<fs::path>& pathDirs)
{
    Launcher launcher;
    launcher.arguments = options;
    const auto& wanted = flow.launcher ? *flow.launcher : program;
    const std::string mine = flow.launcher ? "the version's launcher" : "";

    // A path: as it is.
    if (wanted.find('/') != std::string::npos) {
        const auto found = executable(wanted);
        if (!found)
            return std::nullopt;
        launcher.program = *found;
        launcher.how = mine.empty() ? "as configured" : mine;
        return launcher;
    }

    // A name: as the shell would after `module load`: next to flow first, then PATH.
    auto flowDirs = std::vector<fs::path>{};
    for (const auto& path : flow.paths)
        flowDirs.push_back(path.parent_path());
    flowDirs.push_back(flow.flow.parent_path());
    for (const auto& dir : flowDirs) {
        if (const auto found = executable(dir / wanted)) {
            launcher.program = *found;
            launcher.how = mine.empty() ? "next to flow" : mine + ", next to flow";
            return launcher;
        }
    }
    for (const auto& dir : pathDirs) {
        if (const auto found = executable(dir / wanted)) {
            launcher.program = *found;
            launcher.how = mine.empty() ? "PATH" : mine + ", in PATH";
            return launcher;
        }
    }
    return std::nullopt;
}

std::optional<MpiIdentity> identifyMpi(const std::string& text)
{
    MpiIdentity mpi;
    if (text.find("Open MPI") != std::string::npos)
        mpi.implementation = "Open MPI";
    else if (text.find("Intel(R) MPI") != std::string::npos || text.find("Intel MPI") != std::string::npos)
        mpi.implementation = "Intel MPI";
    else if (text.find("MPICH") != std::string::npos || text.find("HYDRA") != std::string::npos)
        mpi.implementation = "MPICH"; // MPICH's launcher, mpiexec.hydra, says HYDRA
    else
        return std::nullopt;

    // The first dotted number: "v5.0.7", "Version: 4.1.2", "Library 2021.10".
    static const std::regex version(R"((\d+\.\d+(\.\d+)*))");
    std::smatch match;
    if (!std::regex_search(text, match, version))
        return std::nullopt;
    mpi.version = match[1].str();
    return mpi;
}

LauncherCheck checkLauncher(const Installation& flow, const Launcher& launcher)
{
    if (flow.build.mpi == "none")
        throw LaunchError(flow.flow.string() + " was built without MPI: it runs on one process only");
    const auto flowMpi = flow.build.mpi ? identifyMpi(*flow.build.mpi) : std::nullopt;
    if (!flowMpi)
        return {LauncherCheck::Result::FlowUnknown, "not checked: this Flow does not say which MPI it uses"};

    std::optional<MpiIdentity> launcherMpi;
    try {
        const auto result = captureOutput({launcher.program.string(), "--version"},
                                          std::chrono::seconds(30));
        launcherMpi = identifyMpi(result.output + result.errors);
    } catch (const std::exception&) {
        // cannot be started: not understood
    }
    if (!launcherMpi)
        return {LauncherCheck::Result::LauncherUnknown,
                "not checked: " + launcher.program.string() + " --version is not understood"};

    if (launcherMpi->implementation != flowMpi->implementation
        || majorMinor(launcherMpi->version) != majorMinor(flowMpi->version)) {
        throw LaunchError("the MPI launcher " + launcher.program.string() + " (" + launcher.how
                          + ") is " + describe(*launcherMpi) + ", but " + flow.flow.string()
                          + " was built with " + describe(*flowMpi)
                          + ": set this version's \"launcher\" in runflow.json");
    }
    return {LauncherCheck::Result::Match, describe(*launcherMpi) + ", as Flow's"};
}

} // namespace Opm::RunFlow
