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

// runflow: runs OPM Flow on one deck, here (-i) or as an LSF job, with the
// command line of the legacy runflow script. This file only combines the
// parts in opm/runflow (see docs/runflow/ROADMAP.md):
//
//   command line -> Flow version -> what the deck needs -> launch plan
//   -> job -> run here, or bsub through system(), or print it (--dry-run)
//   -> the run log next to the deck

#include <opm/runflow/cli/CommandLine.hpp>
#include <opm/runflow/cli/RunflowConfig.hpp>
#include <opm/runflow/config/ConfigFiles.hpp>
#include <opm/runflow/versions/VersionRegistry.hpp>
#include <opm/runflow/launch/LaunchInfo.hpp>
#include <opm/runflow/launch/LaunchPlan.hpp>
#include <opm/runflow/launch/Launcher.hpp>
#include <opm/runflow/versions/Candidates.hpp>
#include <opm/runflow/submit/JobSpec.hpp>
#include <opm/runflow/submit/Local.hpp>
#include <opm/runflow/submit/Lsf.hpp>
#include <opm/runflow/submit/RunLog.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// opm-common's version, written at build time (UseVersion.cmake).
#if __has_include("project-version.h")
#include "project-version.h"
#endif
#ifndef PROJECT_VERSION
#define PROJECT_VERSION "(version unknown)"
#endif

namespace {

using namespace Opm::RunFlow;

// Exit codes, as the legacy script's.
constexpr int exitOk = 0;
constexpr int exitError = 1;   // version not found, and every other failure
constexpr int exitUsage = 2;   // wrong options
constexpr int exitNoDeck = 4;  // not exactly one deck

void usage(std::ostream& out, const std::string& defaultQueue, const std::string& defaultVersion)
{
    out << "\nusage: runflow [-q queue] [-i] [-l] [-v version] [-n processes] [-o options] "
           "[--dry-run] input_deck\n\n"
           "   queue       :  name of LSF queue, default is " << defaultQueue << "\n"
           "   version     :  flow version, default is " << defaultVersion
        << ", list available with '-l' option\n"
           "   processes   :  number of processes (of the master, for reservoir coupling)\n"
           "   options     :  example runflow -o '--max-time-step-in-days=1.0 --enable-esmry=true' TEST1.DATA\n"
           "   -i          :  Local run without queue\n"
           "   -l (--list) :  List available versions\n"
           "   --dry-run   :  Show what would run or be submitted, and do nothing\n\n"
           "Flow runs in the deck's directory; its output goes to DECK.LOG and DECK.ERR.\n"
           "See runflow(1).\n";
}

// The name a user selects an installation by: a configured name, else the file name.
std::string selectableName(const Installation& inst)
{
    return inst.names.empty() ? inst.executableNames.front() : inst.names.front();
}

void list(VersionRegistry& registry)
{
    std::cout << "Available versions (for '-v' option without absolute path)\n"
                 "----------------------------------------------------------\n";
    registry.describeAll(); // the list shows each version's OPM version
    for (const auto& inst : registry.installations()) {
        auto name = selectableName(inst);
        name.resize(std::max<std::size_t>(name.size(), 16), ' ');
        std::cout << name << "  " << inst.build.opmVersion.value_or("-")
                  << (registry.isDefault(inst) ? "  (default)  " : "  ") << inst.flow.string() << '\n';
    }
    std::cout << "----------------------------------------------------------\n";
}

struct Warnings
{
    std::vector<std::string> list;
    ~Warnings()
    {
        for (const auto& warning : list)
            std::cerr << "runflow: warning: " << warning << '\n';
    }
};

int run(const std::vector<std::string>& args)
{
    const auto line = parseCommandLine(args); // throws UsageError

    Warnings warnings;
    const auto config = RunflowConfig::load(configFiles(), warnings.list);
    auto registry = VersionRegistry::standard(config.versions);
    const auto addRegistryWarnings = [&] {
        warnings.list.insert(warnings.list.end(), registry.warnings().begin(),
                             registry.warnings().end());
    };

    if (line.help) {
        std::string defaultVersion = "none (see runflow -l)";
        try {
            defaultVersion = selectableName(registry.defaultInstallation());
        } catch (const VersionError&) {
        }
        usage(std::cout, config.lsf.defaultQueue.empty() ? "LSF's default" : config.lsf.defaultQueue,
              defaultVersion);
        return exitOk;
    }
    if (line.list) {
        list(registry);
        addRegistryWarnings();
        return exitOk;
    }
    if (line.decks.size() != 1) {
        std::cerr << "runflow: A valid data deck is required.\n";
        return exitNoDeck;
    }

    // The version, the deck, and what the deck needs.
    const auto location = locateDeck(line.decks.front());
    const auto deckPath = location.workdir / location.deck;
    if (!std::filesystem::is_regular_file(deckPath)) {
        std::cerr << "runflow: the deck " << deckPath.string() << " does not exist\n";
        return exitNoDeck;
    }
    const Installation* flow = nullptr;
    try {
        flow = line.version ? &registry.resolve(*line.version) : &registry.defaultInstallation();
    } catch (const VersionError& e) {
        addRegistryWarnings();
        std::cerr << "\n!Error, Flow version can not be found.\nrunflow: " << e.what() << '\n';
        return exitError;
    }
    addRegistryWarnings();
    const auto resources = deckResources(*flow, deckPath);

    // Where it runs selects the MPI launcher template: local.launcher for -i,
    // lsf.launcher for a job. Its program is the version's mpirun, else the
    // one next to flow, else PATH's; it must belong to Flow's MPI.
    const bool here = line.interactive;
    const LaunchRequest request{location.deck, line.processes, splitFlowOptions(line.flowOptions)};
    std::optional<Launcher> launcher;
    std::string launcherNote;
    if (needsMpi(request, resources.info)) {
        launcher = findLauncher(*flow, here ? config.local.launcher : config.lsf.launcher,
                                here ? config.local.launcherOptions : config.lsf.launcherOptions,
                                pathFromEnvironment());
        if (launcher) {
            const auto check = checkLauncher(*flow, *launcher); // throws on a mismatch
            if (check.result == LauncherCheck::Result::LauncherUnknown)
                warnings.list.push_back(check.text);
            launcherNote = "runflow: MPI launcher " + launcher->program.string() + " ("
                + launcher->how + "; " + check.text + ")";
        }
    }
    const auto plan = planLaunch(*flow, request, resources.info, launcher);

    JobSpec job;
    job.command = plan.command;
    job.workdir = location.workdir;
    job.environment = plan.environment;
    job.name = location.root;
    job.queue = line.queue.value_or("");
    job.slots = plan.slots;
    job.singleHost = config.lsf.singleHost; // only jobs have hosts to keep together
    job.stdoutFile = location.root + ".LOG";
    job.stderrFile = location.root + ".ERR";
    job.banner = plan.banner;
    if (!launcherNote.empty())
        job.banner.push_back(launcherNote);
    job.banner.push_back("runflow: launch information from " + resources.source);
    job.scriptFile = location.root + ".job";
    job.report = location.root + ".{id}.job.done";

    RunRecord record;
    record.runflowVersion = PROJECT_VERSION;
    record.flowVersion = flow->build.opmVersion.value_or("(version unknown)")
        + (flow->build.buildType ? " " + *flow->build.buildType : "");
    record.flow = flow->flow;
    const auto runLog = location.workdir / (location.root + ".runflow.log");

    if (here) {
        if (line.dryRun) {
            checkLocalSlots(job);
            std::cout << "# dry run: would run here\n" << describeLocal(job);
            return exitOk;
        }
        record.where = "here";
        record.command = shellQuote(job.command);
        record.exitStatus = runLocal(job);
        appendRunLog(runLog, record);
        return record.exitStatus;
    }

    const auto command = lsfCommand(job, config.lsf); // refuses what LSF could not run
    if (line.dryRun) {
        std::cout << "# dry run: would run\n" << command << "\n"
                  << "# with " << job.scriptFile << ":\n" << lsfJobScript(job);
        return exitOk;
    }
    record.where = "LSF";
    record.command = command;
    record.exitStatus = submitToLsf(job, config.lsf); // bsub answers on the terminal
    appendRunLog(runLog, record);
    if (record.exitStatus == 127)
        std::cerr << "runflow: bsub not found: load LSF's environment, or set "
                     "\"lsf\": {\"bsub\": PATH} in runflow.json\n";
    return record.exitStatus; // bsub's, as the legacy script's
}

} // namespace

int main(int argc, char** argv)
{
    const std::vector<std::string> args(argv, argv + argc);
    try {
        return run(args);
    } catch (const UsageError& e) {
        std::cerr << "runflow: " << e.what() << "\nTry 'runflow --help'.\n";
        return exitUsage;
    } catch (const std::exception& e) {
        // Submission, launch and configuration errors: their messages say what to do.
        std::cerr << "runflow: " << e.what() << '\n';
        return exitError;
    }
}
