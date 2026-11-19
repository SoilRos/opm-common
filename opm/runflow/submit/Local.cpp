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

#include <opm/runflow/submit/Local.hpp>

#include <opm/json/JsonObject.hpp>
#include <opm/runflow/config/ConfigFiles.hpp>
#include <opm/runflow/process/Process.hpp>

#include <algorithm>
#include <array>
#include <ctime>
#include <stdexcept>
#include <thread>

#include <unistd.h>

namespace {

std::string hostName()
{
    std::array<char, 256> name{};
    if (::gethostname(name.data(), name.size() - 1) != 0)
        return "this host";
    return name.data();
}

std::string now()
{
    const std::time_t t = std::time(nullptr);
    std::array<char, 64> text{};
    std::strftime(text.data(), text.size(), "%a %b %e %H:%M:%S %Z %Y", std::localtime(&t));
    return text.data();
}

} // namespace

namespace Opm::RunFlow {

void LocalSettings::merge(const Json::JsonObject& section, const std::filesystem::path& origin,
                          std::vector<std::string>& warnings)
{
    if (!section.is_object())
        throw std::runtime_error(origin.string() + ": \"local\" must be an object");
    for (const auto& key : section.keys()) {
        const auto value = section.get_item(key);
        const auto setting = "local." + key;
        if (key == "launcher")
            launcher = resolveProgram(readString(value, origin, setting), origin.parent_path());
        else if (key == "launcher_options")
            launcherOptions = readLauncherOptions(value, launcherOptions, origin, setting, warnings);
        else
            warnings.push_back(origin.string() + ": unknown setting \"" + setting + "\"");
    }
}

void checkLocalSlots(const JobSpec& job, unsigned cores)
{
    if (cores == 0)
        cores = std::max(1U, std::thread::hardware_concurrency());
    if (job.slots < 1 || static_cast<unsigned>(job.slots) > cores) {
        throw SubmitError("the run needs " + std::to_string(job.slots)
                          + " processor slots, but this machine has " + std::to_string(cores));
    }
}

std::string describeLocal(const JobSpec& job)
{
    std::string text = "cd " + shellQuote(job.workdir.string()) + "\n";
    for (const auto& env : job.environment) {
        text += "export " + env.name + "=" + shellQuote(env.value);
        if (env.mode == EnvSetting::Mode::Prepend)
            text += "\"${" + env.name + ":+:$" + env.name + "}\"";
        text += "\n";
    }
    text += shellQuote(job.command);
    if (!job.stdoutFile.empty())
        text += "    # output also to " + job.stdoutFile
            + (job.stderrFile.empty() ? "" : " and " + job.stderrFile);
    return text + "\n";
}

int runLocal(const JobSpec& job, unsigned cores, int terminalOut, int terminalErr)
{
    checkLocalSlots(job, cores);

    ForegroundCommand command;
    command.argv = job.command;
    command.workdir = job.workdir;
    command.environment = applyEnvironment(currentEnvironment(), job.environment);
    command.banner = "local run on " + hostName() + ", started " + now() + "\n";
    for (const auto& line : job.banner)
        command.banner += line + "\n";
    if (!job.stdoutFile.empty()) {
        command.stdoutFile = job.workdir / job.stdoutFile;
        // No separate file: errors go with the output, as in a job.
        command.stderrFile = job.workdir / (job.stderrFile.empty() ? job.stdoutFile : job.stderrFile);
    }
    command.terminalOut = terminalOut;
    command.terminalErr = terminalErr;
    return runForeground(command);
}

} // namespace Opm::RunFlow
