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

#include <opm/runflow/submit/Lsf.hpp>

#include <opm/json/JsonObject.hpp>
#include <opm/runflow/config/ConfigFiles.hpp>

#include <cstdlib>
#include <fstream>
#include <stdexcept>

#include <sys/wait.h>

namespace {

namespace fs = std::filesystem;

[[noreturn]] void typeError(const fs::path& origin, const std::string& setting,
                            const std::string& expected)
{
    throw std::runtime_error(origin.string() + ": \"lsf." + setting + "\" must be " + expected);
}

std::string replaceAll(std::string text, const std::string& from, const std::string& to)
{
    for (auto pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos + to.size()))
        text.replace(pos, from.size(), to);
    return text;
}

} // namespace

namespace Opm::RunFlow {

void LsfSettings::merge(const Json::JsonObject& section, const fs::path& origin,
                        std::vector<std::string>& warnings)
{
    if (!section.is_object())
        typeError(origin, "", "an object");
    for (const auto& key : section.keys()) {
        const auto value = section.get_item(key);
        const auto setting = "lsf." + key;
        if (key == "bsub") {
            bsub = resolveProgram(readString(value, origin, setting), origin.parent_path());
        } else if (key == "bsub_options") {
            bsubOptions = listWithInherited(readStringList(value, origin, setting), bsubOptions,
                                            [](const std::string& option) { return option; },
                                            setting, origin, warnings);
        } else if (key == "default_queue") {
            defaultQueue = readString(value, origin, setting);
        } else if (key == "single_host") {
            if (!value.is_bool())
                typeError(origin, key, "true or false");
            singleHost = value.as_bool();
        } else if (key == "max_slots_per_host") {
            if (!value.is_number() || value.as_int() < 1)
                typeError(origin, key, "a positive number");
            maxSlotsPerHost = value.as_int();
        } else if (key == "launcher") {
            launcher = resolveProgram(readString(value, origin, setting), origin.parent_path());
        } else if (key == "launcher_options") {
            launcherOptions = readLauncherOptions(value, launcherOptions, origin, setting, warnings);
        } else {
            warnings.push_back(origin.string() + ": unknown setting \"" + setting + "\"");
        }
    }
}

std::string lsfCommand(const JobSpec& job, const LsfSettings& settings)
{
    if (job.scriptFile.empty())
        throw SubmitError("an LSF job needs a script file");
    if (job.singleHost && settings.maxSlotsPerHost && job.slots > *settings.maxSlotsPerHost) {
        throw SubmitError("the job needs " + std::to_string(job.slots)
                          + " slots on one host, but a host has "
                          + std::to_string(*settings.maxSlotsPerHost)
                          + " (lsf.max_slots_per_host)");
    }

    std::vector<std::string> bsub{settings.bsub, "-n", std::to_string(job.slots)};
    const auto& queue = job.queue.empty() ? settings.defaultQueue : job.queue;
    if (!queue.empty())
        bsub.insert(bsub.end(), {"-q", queue});
    if (!job.name.empty())
        bsub.insert(bsub.end(), {"-J", job.name});
    if (job.singleHost)
        bsub.insert(bsub.end(), {"-R", "span[hosts=1]"});
    if (!job.report.empty())
        bsub.insert(bsub.end(), {"-o", replaceAll(job.report, "{id}", "%J")});
    bsub.insert(bsub.end(), settings.bsubOptions.begin(), settings.bsubOptions.end());

    return "cd " + shellQuote(job.workdir.string()) + " && " + shellQuote(bsub)
        + " < " + shellQuote(job.scriptFile);
}

std::string lsfJobScript(const JobSpec& job)
{
    return jobScript(job, "LSF", "LSB_JOBID");
}

int submitToLsf(const JobSpec& job, const LsfSettings& settings)
{
    const auto command = lsfCommand(job, settings); // checks before anything is written
    const auto script = job.workdir / job.scriptFile;
    std::ofstream(script) << lsfJobScript(job);
    fs::permissions(script, fs::perms::owner_exec | fs::perms::group_exec, fs::perm_options::add);

    const int status = std::system(command.c_str());
    if (status == -1)
        throw SubmitError("cannot run the shell for bsub");
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

} // namespace Opm::RunFlow
