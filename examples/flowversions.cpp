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

// flowversions: lists the Flow versions found, and which one a name selects.
// The finding is done by opm/runflow/versions, with runflow's runflow.json;
// this file only parses the command line and prints.

#include <opm/json/JsonObject.hpp>
#include <opm/runflow/cli/RunflowConfig.hpp>
#include <opm/runflow/config/ConfigFiles.hpp>
#include <opm/runflow/launch/LaunchPlan.hpp>
#include <opm/runflow/launch/Launcher.hpp>
#include <opm/runflow/versions/Candidates.hpp>
#include <opm/runflow/versions/VersionRegistry.hpp>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

using Opm::RunFlow::Installation;
using Opm::RunFlow::VersionRegistry;

// Exit codes.
constexpr int exitOk = 0;
constexpr int exitNotFound = 1;
constexpr int exitUsage = 2;

void usage(std::ostream& out)
{
    out << "usage: flowversions [list] [--json]\n"
           "       flowversions which [VERSION]\n"
           "       flowversions show [VERSION] [--json]\n"
           "\n"
           "  list     the Flow versions found; * marks the default\n"
           "  which    the executable VERSION selects (default: the default version)\n"
           "  show     everything known about VERSION\n"
           "  --json   machine-readable output\n"
           "\n"
           "VERSION is a path, a name from runflow.json, an executable's name\n"
           "(e.g. flowrc47) or an OPM version (e.g. 2026.04). See flowversions(1).\n";
}

std::string orDash(const std::optional<std::string>& value)
{
    return value ? *value : "-";
}

std::string join(const std::vector<std::string>& items, const std::string& separator)
{
    std::string result;
    for (const auto& item : items)
        result += (result.empty() ? "" : separator) + item;
    return result;
}

// The names a user knows the installation by: configured names, else file names.
std::string displayNames(const Installation& inst)
{
    return join(inst.names.empty() ? inst.executableNames : inst.names, ",");
}

void addJson(Json::JsonObject& object, const Installation& inst, bool isDefault)
{
    object.add_item("flow", inst.flow.string());
    auto names = object.add_array("names");
    for (const auto& name : inst.names)
        names.add(name);
    auto executableNames = object.add_array("executable_names");
    for (const auto& name : inst.executableNames)
        executableNames.add(name);
    const auto addOptional = [&](const std::string& key, const std::optional<std::string>& value) {
        if (value)
            object.add_item(key, *value);
    };
    addOptional("opm_version", inst.build.opmVersion);
    addOptional("git_hash", inst.build.gitHash);
    addOptional("build_time", inst.build.buildTime);
    addOptional("build_type", inst.build.buildType);
    addOptional("mpi", inst.build.mpi);
    addOptional("pythonpath", inst.pythonPath);
    if (const auto launchInfo = Opm::RunFlow::findLaunchInfo(inst))
        object.add_item("launchinfo", launchInfo->string());
    object.add_bool("default", isDefault);
    auto foundBy = object.add_array("found_by");
    for (const auto& how : inst.foundBy)
        foundBy.add(how);
}

void printList(VersionRegistry& registry, bool json)
{
    registry.describeAll(); // a listing shows every version's build
    const auto& all = registry.installations();
    if (json) {
        Json::JsonObject root;
        auto versions = root.add_array("versions");
        for (const auto& inst : all) {
            auto object = versions.add_object();
            addJson(object, inst, registry.isDefault(inst));
        }
        std::cout << root.dump() << '\n';
        return;
    }

    struct Row { std::string mark, names, version, build, flow; };
    std::vector<Row> rows{{" ", "NAMES", "OPM VERSION", "BUILD", "FLOW"}};
    for (const auto& inst : all) {
        rows.push_back({registry.isDefault(inst) ? "*" : " ", displayNames(inst),
                        orDash(inst.build.opmVersion), orDash(inst.build.buildType),
                        inst.flow.string()});
    }
    std::size_t names = 0, version = 0, build = 0;
    for (const auto& row : rows) {
        names = std::max(names, row.names.size());
        version = std::max(version, row.version.size());
        build = std::max(build, row.build.size());
    }
    const auto pad = [](const std::string& s, std::size_t width) {
        return s + std::string(width - s.size() + 2, ' ');
    };
    for (const auto& row : rows) {
        std::cout << row.mark << ' ' << pad(row.names, names) << pad(row.version, version)
                  << pad(row.build, build) << row.flow << '\n';
    }
    if (all.empty())
        std::cout << "(no Flow found: see flowversions(1), CONFIGURATION)\n";
}

// What runs of `inst` here would start it with (local.launcher), and why.
// The MPI launcher runs of `inst` here would use (local.launcher): its
// program, how it was found, and the check against Flow's MPI.
struct LauncherReport
{
    std::string program; // empty: none found
    std::string found;   // "as configured", "the version's launcher", "next to flow", "PATH"
    std::string check;   // e.g. "Open MPI 5.0.7, as Flow's", or why it does not fit
};

LauncherReport launcherReport(const Installation& inst,
                              const Opm::RunFlow::LocalSettings& local)
{
    const auto launcher = Opm::RunFlow::findLauncher(inst, local.launcher, local.launcherOptions,
                                                     Opm::RunFlow::pathFromEnvironment());
    if (!launcher)
        return {"", "", "none found: set the version's \"launcher\" in runflow.json"};
    try {
        return {launcher->program.string(), launcher->how,
                Opm::RunFlow::checkLauncher(inst, *launcher).text};
    } catch (const std::exception& e) {
        return {launcher->program.string(), launcher->how, e.what()};
    }
}

std::string launcherText(const LauncherReport& report)
{
    if (report.program.empty())
        return report.check;
    return report.program + " (" + report.found + "; " + report.check + ")";
}

void printShow(VersionRegistry& registry, const Installation& inst, bool json,
               const Opm::RunFlow::LocalSettings& local)
{
    const auto launchInfo = Opm::RunFlow::findLaunchInfo(inst);
    const auto launcher = launcherReport(inst, local);
    if (json) {
        Json::JsonObject root;
        addJson(root, inst, registry.isDefault(inst));
        auto launcherJson = root.add_object("launcher");
        if (!launcher.program.empty()) {
            launcherJson.add_item("program", launcher.program);
            launcherJson.add_item("found", launcher.found);
        }
        launcherJson.add_item("check", launcher.check);
        std::cout << root.dump() << '\n';
        return;
    }
    std::cout << "flow:             " << inst.flow.string() << '\n'
              << "names:            " << orDash(inst.names.empty() ? std::nullopt
                                               : std::optional(join(inst.names, ", "))) << '\n'
              << "executable names: " << join(inst.executableNames, ", ") << '\n'
              << "OPM version:      " << orDash(inst.build.opmVersion) << '\n'
              << "git hash:         " << orDash(inst.build.gitHash) << '\n'
              << "build time:       " << orDash(inst.build.buildTime) << '\n'
              << "build type:       " << orDash(inst.build.buildType) << '\n'
              << "MPI:              " << orDash(inst.build.mpi) << '\n'
              << "pythonpath:       " << orDash(inst.pythonPath) << '\n'
              << "launchinfo:       " << (launchInfo ? launchInfo->string() : "none: runflow's parser reads its decks") << '\n'
              << "launcher:         " << launcherText(launcher) << '\n'
              << "default:          " << (registry.isDefault(inst) ? "yes" : "no") << '\n'
              << "found by:         " << join(inst.foundBy, "\n                  ") << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    std::string command = "list";
    std::optional<std::string> version;
    bool json = false;

    // flowversions [COMMAND] [VERSION] [--json], in any order of option and words.
    std::vector<std::string> words;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage(std::cout);
            return exitOk;
        } else if (arg == "--json") {
            json = true;
        } else if (arg.starts_with("-")) {
            std::cerr << "flowversions: unknown option: " << arg << '\n';
            usage(std::cerr);
            return exitUsage;
        } else {
            words.push_back(arg);
        }
    }
    if (!words.empty())
        command = words[0];
    if (words.size() > 1)
        version = words[1];
    if (words.size() > 2 || (command == "list" && version)
        || (command != "list" && command != "which" && command != "show")) {
        std::cerr << "flowversions: wrong arguments\n";
        usage(std::cerr);
        return exitUsage;
    }

    int status = exitOk;
    std::vector<std::string> warnings;
    std::optional<VersionRegistry> registry; // outside the try: its warnings explain errors
    try {
        const auto config = Opm::RunFlow::RunflowConfig::load(Opm::RunFlow::configFiles(), warnings);
        registry.emplace(VersionRegistry::standard(config.versions));

        if (command == "list") {
            printList(*registry, json);
        } else {
            const auto& inst = version ? registry->resolve(*version)
                                       : registry->defaultInstallation();
            if (command == "which")
                std::cout << inst.flow.string() << '\n';
            else
                printShow(*registry, inst, json, config.local);
        }
    } catch (const Opm::RunFlow::VersionError& e) {
        std::cerr << "flowversions: " << e.what() << '\n';
        status = exitNotFound;
    } catch (const std::exception& e) {
        std::cerr << "flowversions: " << e.what() << '\n';
        status = exitUsage;
    }
    if (registry)
        warnings.insert(warnings.end(), registry->warnings().begin(), registry->warnings().end());
    for (const auto& warning : warnings)
        std::cerr << "flowversions: warning: " << warning << '\n';
    return status;
}
