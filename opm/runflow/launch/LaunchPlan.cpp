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

#include <opm/runflow/launch/LaunchPlan.hpp>

#include <opm/runflow/versions/Candidates.hpp>
#include <opm/runflow/process/Process.hpp>
#include <opm/runflow/submit/JobSpec.hpp>

#include <chrono>
#include <stdexcept>

namespace {

namespace fs = std::filesystem;

// Reading a large deck may take a while; a hung reader must not hang runflow.
constexpr auto launchInfoTimeout = std::chrono::minutes(30);

std::string trimmed(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
        text.pop_back();
    return text;
}

} // namespace

namespace Opm::RunFlow {

bool needsMpi(const LaunchRequest& request, const LaunchInfo& info)
{
    return request.ranks > 1 || !info.slaves.empty();
}

LaunchPlan planLaunch(const Installation& flow, const LaunchRequest& request,
                      const LaunchInfo& info, const std::optional<Launcher>& launcher)
{
    if (request.ranks < 1)
        throw LaunchError("the number of processes must be at least 1, not "
                          + std::to_string(request.ranks));

    LaunchPlan plan;
    plan.ranks = request.ranks;
    plan.slots = request.ranks + info.slaveProcesses();

    std::vector<std::string> flowCommand{flow.flow.string(), request.deck};
    flowCommand.insert(flowCommand.end(), request.flowOptions.begin(), request.flowOptions.end());

    const bool coupled = !info.slaves.empty();
    if (needsMpi(request, info)) {
        if (!launcher)
            throw LaunchError("no MPI launcher found for " + flow.flow.string()
                              + ": none next to it or in PATH; set the version's \"launcher\""
                              " in runflow.json");
        plan.command = launcher->command(plan.ranks, plan.slots);
    }
    plan.command.insert(plan.command.end(), flowCommand.begin(), flowCommand.end());

    if (flow.pythonPath)
        plan.environment.push_back({"PYTHONPATH", *flow.pythonPath, EnvSetting::Mode::Prepend});

    plan.banner.push_back("runflow: Flow " + flow.build.opmVersion.value_or("(version unknown)")
                          + (flow.build.buildType ? " " + *flow.build.buildType : "") + ", "
                          + flow.flow.string());
    std::string resources = "runflow: " + std::to_string(plan.ranks)
        + (plan.ranks == 1 ? " rank" : " ranks");
    if (coupled) {
        resources += " + " + std::to_string(info.slaveProcesses()) + " for the slaves (";
        for (std::size_t i = 0; i < info.slaves.size(); ++i)
            resources += (i ? ", " : "") + info.slaves[i].name + ": "
                + std::to_string(info.slaves[i].processes);
        resources += ") = " + std::to_string(plan.slots) + " slots";
    }
    plan.banner.push_back(resources);
    plan.banner.push_back("runflow: " + shellQuote(plan.command));
    return plan;
}

std::optional<fs::path> findLaunchInfo(const Installation& flow)
{
    if (flow.launchInfo) { // configured: used, or an error when it cannot run
        if (const auto found = findProgram(*flow.launchInfo, pathFromEnvironment()))
            return found;
        return fs::path(*flow.launchInfo);
    }
    auto paths = flow.paths;
    paths.push_back(flow.flow);
    for (const auto& path : paths) {
        const auto candidate = path.parent_path() / "opmlaunchinfo";
        if (isExecutableFile(candidate))
            return candidate;
    }
    return std::nullopt;
}

DeckResources deckResources(const Installation& flow, const fs::path& deck)
{
    if (const auto launchInfo = findLaunchInfo(flow)) {
        const auto program = launchInfo->string();
        CapturedOutput result;
        try {
            result = captureOutput({program, deck.string()}, launchInfoTimeout);
        } catch (const std::exception& e) {
            throw LaunchError("cannot run " + program + ": " + e.what());
        }
        if (result.exitCode != 0) {
            throw LaunchError(program + " could not read " + deck.string() + ": "
                              + trimmed(result.errors.empty() ? result.output : result.errors));
        }
        return {LaunchInfo::fromJson(result.output), "opmlaunchinfo " + program};
    }
    return {readLaunchInfo(deck), "runflow's parser"};
}

} // namespace Opm::RunFlow
