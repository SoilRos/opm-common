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

// Tests of runflow's configuration files (opm/runflow/config, cli's
// RunflowConfig), Flow version finding (opm/runflow/versions), and where a
// version's opmlaunchinfo is looked for. Flow is replaced by shell scripts
// that answer --build-info (new versions), only --version (older ones), or
// nothing.

#define BOOST_TEST_MODULE RunFlowVersionsTests
#include <boost/test/unit_test.hpp>

#include <opm/json/JsonObject.hpp>
#include <opm/runflow/cli/RunflowConfig.hpp>
#include <opm/runflow/config/ConfigFiles.hpp>
#include <opm/runflow/launch/LaunchPlan.hpp>
#include <opm/runflow/process/Process.hpp>
#include <opm/runflow/versions/BuildInfo.hpp>
#include <opm/runflow/versions/Candidates.hpp>
#include <opm/runflow/versions/PythonPath.hpp>
#include <opm/runflow/versions/VersionRegistry.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;
using namespace Opm::RunFlow;

namespace {

struct TempDir
{
    fs::path path;
    TempDir()
    {
        std::string pattern = (fs::temp_directory_path() / "runflow-test-XXXXXX").string();
        BOOST_REQUIRE(::mkdtemp(pattern.data()) != nullptr);
        path = fs::canonical(pattern);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    fs::path operator/(const std::string& name) const { return path / name; }
};

fs::path writeFile(const fs::path& file, const std::string& content)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file) << content;
    return file;
}

fs::path script(const fs::path& file, const std::string& body)
{
    writeFile(file, "#!/bin/sh\n" + body);
    fs::permissions(file, fs::perms::owner_all);
    return file;
}

// A Flow that knows --build-info.
fs::path newFlow(const fs::path& file, const std::string& version)
{
    return script(file,
        "case \"$1\" in\n"
        "  --build-info) printf 'version=" + version + "\\nhash=1b2958d6e\\nbuild_time=2026-04-15\\n"
        "build_type=Release\\nmpi=Open MPI v5.0.7\\nfuture_key=x\\n';;\n"
        "  --version) echo 'flow " + version + "';;\n"
        "  *) exit 1;;\n"
        "esac\n");
}

// A Flow from before --build-info: it refuses the option, as Flow refuses an
// unknown parameter.
fs::path oldFlow(const fs::path& file, const std::string& version)
{
    return script(file,
        "case \"$1\" in\n"
        "  --version) echo 'flow " + version + "';;\n"
        "  *) echo \"Unknown parameter $1\" >&2; exit 1;;\n"
        "esac\n");
}

// Sets environment variables for one test, and restores them.
class ScopedEnv
{
public:
    void set(const std::string& name, const std::optional<std::string>& value)
    {
        if (!saved_.contains(name)) {
            const char* old = std::getenv(name.c_str());
            saved_[name] = old ? std::optional<std::string>(old) : std::nullopt;
        }
        if (value)
            ::setenv(name.c_str(), value->c_str(), 1);
        else
            ::unsetenv(name.c_str());
    }
    ~ScopedEnv()
    {
        for (const auto& [name, value] : saved_) {
            if (value)
                ::setenv(name.c_str(), value->c_str(), 1);
            else
                ::unsetenv(name.c_str());
        }
    }

private:
    std::map<std::string, std::optional<std::string>> saved_;
};

VersionEntry entry(const fs::path& flow)
{
    VersionEntry e;
    e.flow = flow.string();
    e.origin = "runflow.json";
    return e;
}

// Records which executables were asked, and asks them.
struct CountingDescriber
{
    std::shared_ptr<std::vector<fs::path>> calls = std::make_shared<std::vector<fs::path>>();
    BuildInfo operator()(const fs::path& flow) const
    {
        calls->push_back(flow);
        return describeFlow(flow);
    }
};

} // namespace

// ------------------------------------------------------------ JsonObject

BOOST_AUTO_TEST_CASE(JsonKeysAndBooleans)
{
    const Json::JsonObject json(std::string(R"({"b": true, "a": 1, "c": false})"));
    BOOST_CHECK((json.keys() == std::vector<std::string>{"b", "a", "c"})); // input order
    BOOST_CHECK(json.get_item("b").is_bool() && json.get_item("b").as_bool());
    BOOST_CHECK(!json.get_item("c").as_bool());
    BOOST_CHECK(!json.get_item("a").is_bool());
    BOOST_CHECK_THROW(json.get_item("a").keys(), std::invalid_argument);

    Json::JsonObject out;
    out.add_bool("yes", true);
    BOOST_CHECK(Json::JsonObject(out.dump()).get_item("yes").as_bool());
}

// ------------------------------------------------------- configuration files

BOOST_AUTO_TEST_CASE(ConfigFilesInOrder)
{
    ScopedEnv env;
    env.set("HOME", "/home/user");
    env.set("RUNFLOW_CONFIG", "/project/runflow.json");
    const auto files = configFiles();
    BOOST_REQUIRE_EQUAL(files.size(), 4U);
    BOOST_CHECK_EQUAL(files[0].path, installationConfigFile());
    BOOST_CHECK(!files[0].required);
    BOOST_CHECK_EQUAL(files[1].path, fs::path("/home/user/.runflow.json"));
    BOOST_CHECK(!files[1].required);
    BOOST_CHECK_EQUAL(files[2].path, fs::current_path() / "runflow.json");
    BOOST_CHECK(!files[2].required);
    BOOST_CHECK_EQUAL(files[3].path, fs::path("/project/runflow.json"));
    BOOST_CHECK(files[3].required); // the user named it

    // A relative $RUNFLOW_CONFIG is made absolute, from the current directory.
    env.set("RUNFLOW_CONFIG", "project/../project/runflow.json");
    BOOST_CHECK_EQUAL(configFiles().back().path, fs::current_path() / "project/runflow.json");

    env.set("RUNFLOW_CONFIG", std::nullopt);
    BOOST_CHECK_EQUAL(configFiles().size(), 3U);
}

BOOST_AUTO_TEST_CASE(RelativeRunflowConfigKeepsItsPaths)
{
    // Paths in a $RUNFLOW_CONFIG given as a relative path are relative to
    // that file, not to wherever they are used.
    TempDir tmp;
    writeFile(tmp / "project/runflow.json", R"({ "search": { "flow": ["bin/flow*"] } })");
    const auto before = fs::current_path();
    fs::current_path(tmp.path);
    ScopedEnv env;
    env.set("RUNFLOW_CONFIG", "project/runflow.json");
    std::vector<std::string> warnings;
    const auto config = RunflowConfig::load(configFiles(), warnings);
    fs::current_path(before);
    BOOST_REQUIRE_EQUAL(config.versions.search.flow.size(), 1U);
    BOOST_CHECK_EQUAL(config.versions.search.flow[0], tmp / "project/bin/flow*");
}

BOOST_AUTO_TEST_CASE(InstallationConfigFile)
{
    // This test program's own installation: <its dir>/<bin to sysconf>/opm/runflow.json.
    const auto file = installationConfigFile();
    BOOST_CHECK_EQUAL(file.filename(), "runflow.json");
    BOOST_CHECK_EQUAL(file.parent_path().filename(), "opm");
    BOOST_CHECK(executablePath().filename().string().starts_with("RunFlowVersionsTests"));
}

BOOST_AUTO_TEST_CASE(ConfigPaths)
{
    ScopedEnv env;
    env.set("HOME", "/home/user");
    BOOST_CHECK_EQUAL(resolveConfigPath("~/opm/flow", "/etc/opm"), fs::path("/home/user/opm/flow"));
    BOOST_CHECK_EQUAL(resolveConfigPath("bin/flow", "/etc/opm"), fs::path("/etc/opm/bin/flow"));
    BOOST_CHECK_EQUAL(resolveConfigPath("/opt/flow", "/etc/opm"), fs::path("/opt/flow"));
    BOOST_CHECK_EQUAL(resolveConfigPathList("a::/b", "/etc"), "/etc/a:/b");
}

BOOST_AUTO_TEST_CASE(RunflowConfigInLayers)
{
    TempDir tmp;
    writeFile(tmp / "site/runflow.json", R"({
        "default_version": "stable",
        "search": { "flow": ["/project/res/bin/flow*"] },
        "versions": { "stable": { "flow": "/opt/opm/2026.04/bin/flow" },
                      "dev": { "flow": "/opt/opm/old/bin/flow" } },
        "lsf": { "bsub": "/global/bin/bsub", "default_queue": "daily", "single_host": false },
        "local": { "launcher": "mpiexec", "launcher_options": ["-n", "{np}"] } })");
    writeFile(tmp / "home/.runflow.json", R"({
        "search": { "flow": ["bin/flow*"] },
        "versions": { "dev": { "flow": "build/flow", "launchinfo": "build/opmlaunchinfo",
                               "launcher": "mpi/bin/mpirun", "pythonpath": ["py", "/abs"] },
                      "named": { "flow": "flow", "launcher": "mpirun" } },
        "lsf": { "default_queue": "long" },
        "colour": "blue" })");

    std::vector<std::string> warnings;
    const auto config = RunflowConfig::load({{tmp / "site/runflow.json", false},
                                             {tmp / "home/.runflow.json", false},
                                             {tmp / "missing.json", false}}, warnings);
    BOOST_CHECK_EQUAL(config.versions.defaultVersion.value(), "stable"); // the site's
    BOOST_REQUIRE_EQUAL(config.versions.search.flow.size(), 1U);          // lists are replaced
    BOOST_CHECK_EQUAL(config.versions.search.flow[0], tmp / "home/bin/flow*"); // relative to the file
    BOOST_REQUIRE_EQUAL(config.versions.versions.size(), 3U);             // merged by name
    const auto& dev = config.versions.versions.at("dev");
    // Programs with a '/' are paths relative to the file; bare names stay names.
    BOOST_CHECK_EQUAL(dev.flow, (tmp / "home/build/flow").string());
    BOOST_CHECK_EQUAL(dev.launchInfo.value(), (tmp / "home/build/opmlaunchinfo").string());
    BOOST_CHECK_EQUAL(dev.launcher.value(), (tmp / "home/mpi/bin/mpirun").string());
    BOOST_CHECK((dev.pythonPath == std::vector<fs::path>{tmp / "home/py", "/abs"}));
    const auto& named = config.versions.versions.at("named");
    BOOST_CHECK_EQUAL(named.flow, "flow");
    BOOST_CHECK_EQUAL(named.launcher.value(), "mpirun");
    BOOST_CHECK_EQUAL(config.local.launcher, "mpiexec");
    BOOST_CHECK((config.local.launcherOptions == std::vector<std::string>{"-n", "{np}"}));
    BOOST_CHECK(!config.lsf.singleHost);
    BOOST_CHECK_EQUAL(config.lsf.bsub, "/global/bin/bsub"); // the site's
    BOOST_CHECK_EQUAL(config.lsf.defaultQueue, "long");     // the user's
    BOOST_CHECK_EQUAL(warnings.size(), 1U);                 // "colour"
}

BOOST_AUTO_TEST_CASE(EachSettingCombinesItsOwnWay)
{
    TempDir tmp;
    writeFile(tmp / "site.json", R"({
        "default_version": "stable",
        "search": { "flow": ["/site/a/flow*", "/site/b/flow*"] },
        "versions": { "stable": { "flow": "/opt/stable/flow" }, "dev": { "flow": "/opt/dev/flow" } },
        "local": { "launcher": "mpirun", "launcher_options": ["-np", "{np}"] },
        "lsf": { "bsub": "/global/bin/bsub", "bsub_options": ["-P", "site"],
                 "launcher": "mpirun", "launcher_options": ["-np", "{np}", "-x", "A=1"] } })");
    writeFile(tmp / "user.json", R"({
        "default_version": "dev",
        "search": { "flow": ["/mine/flow*"] },
        "versions": { "dev": { "flow": "/home/dev/flow" }, "mine": { "flow": "/home/mine/flow" } },
        "lsf": { "bsub_options": ["-P", "mine"] },
        "local": { "launcher": "mpiexec", "launcher_options": ["-n", "{np}"] } })");
    std::vector<std::string> warnings;
    const auto config = RunflowConfig::load({{tmp / "site.json", false}, {tmp / "user.json", false}},
                                            warnings);
    // replace
    BOOST_CHECK_EQUAL(config.versions.defaultVersion.value(), "dev");
    // replace the list: the site's patterns are gone
    BOOST_CHECK((config.versions.search.flow == std::vector<fs::path>{"/mine/flow*"}));
    // merge by name: stable kept, dev replaced whole, mine added
    const auto& versions = config.versions.versions;
    BOOST_REQUIRE_EQUAL(versions.size(), 3U);
    BOOST_CHECK_EQUAL(versions.at("stable").flow, "/opt/stable/flow");
    BOOST_CHECK_EQUAL(versions.at("dev").flow, "/home/dev/flow");
    BOOST_CHECK_EQUAL(versions.at("mine").flow, "/home/mine/flow");
    // lsf: merged by key (bsub kept), its bsub_options list replaced
    BOOST_CHECK_EQUAL(config.lsf.bsub, "/global/bin/bsub");
    BOOST_CHECK((config.lsf.bsubOptions == std::vector<std::string>{"-P", "mine"}));
    // local: both given, both replaced; lsf's launcher kept
    BOOST_CHECK_EQUAL(config.local.launcher, "mpiexec");
    BOOST_CHECK((config.local.launcherOptions == std::vector<std::string>{"-n", "{np}"}));
    BOOST_CHECK_EQUAL(config.lsf.launcherOptions.back(), "A=1");
}

BOOST_AUTO_TEST_CASE(InheritMarkerInLists)
{
    TempDir tmp;
    writeFile(tmp / "site.json", R"({
        "search": { "flow": ["/site/a/flow*", "/site/b/flow*", "@inherit"] },
        "lsf": { "bsub_options": ["-P", "site"] } })");
    writeFile(tmp / "user.json", R"({
        "search": { "flow": ["/mine/flow*", "@inherit"] },
        "lsf": { "bsub_options": ["@inherit", "-W", "1:00"] } })");
    writeFile(tmp / "project.json", R"({
        "search": { "flow": ["@inherit", "/project/flow*", "@inherit"] } })");
    std::vector<std::string> warnings;
    const auto config = RunflowConfig::load({{tmp / "site.json", false},
                                             {tmp / "user.json", false},
                                             {tmp / "project.json", false}}, warnings);

    // The first file has nothing earlier: its marker stands for an empty list.
    // Each later marker stands for the list so far, where it is put.
    BOOST_CHECK((config.versions.search.flow
                 == std::vector<fs::path>{"/mine/flow*", "/site/a/flow*", "/site/b/flow*", "/project/flow*"}));
    BOOST_CHECK((config.lsf.bsubOptions == std::vector<std::string>{"-P", "site", "-W", "1:00"}));

    // A second marker in one list is dropped, with a warning naming the file.
    BOOST_REQUIRE_EQUAL(warnings.size(), 1U);
    BOOST_CHECK(warnings[0].find("project.json") != std::string::npos);
    BOOST_CHECK(warnings[0].find("search.flow") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(RunflowConfigErrors)
{
    TempDir tmp;
    std::vector<std::string> warnings;
    // A file the user named must exist; the others may not.
    BOOST_CHECK_THROW(RunflowConfig::load({{tmp / "named.json", true}}, warnings), std::runtime_error);
    writeFile(tmp / "broken.json", "{ not json");
    BOOST_CHECK_THROW(RunflowConfig::load({{tmp / "broken.json", false}}, warnings), std::runtime_error);

    RunflowConfig config;
    const auto apply = [&](const std::string& json) {
        config.read(Json::JsonObject(json), "runflow.json", warnings);
    };
    BOOST_CHECK_THROW(apply(R"({"search": {"flow": "/bin/flow*"}})"), std::runtime_error); // not a list
    BOOST_CHECK_THROW(apply(R"({"scheduler": "slurm"})"), std::runtime_error);             // not yet
    BOOST_CHECK_THROW(apply(R"({"versions": {"x": {}}})"), std::runtime_error);
    BOOST_CHECK_THROW(apply(R"({"lsf": {"single_host": 1}})"), std::runtime_error);
    BOOST_CHECK_THROW(apply(R"({"versions": {"x": {"flow": "f", "pythonpath": "/a:/b"}}})"),
                      std::runtime_error);                                                       // not a list
    BOOST_CHECK_THROW(apply(R"({"local": {"launcher_options": ["-np"]}})"), std::runtime_error); // no {np}
    BOOST_CHECK_THROW(apply(R"({"lsf": {"launcher": ["mpirun"]}})"), std::runtime_error);        // not a string
}

// ------------------------------------------------------------- BuildInfo

BOOST_AUTO_TEST_CASE(ParseBuildInfo)
{
    const auto info = parseBuildInfo("version=2026.04\nhash=1b2958d6e\nbuild_time=2026-04-15 at 10:12:00 hrs\n"
                                     "build_type=Release\nmpi=Open MPI v5.0.7\nnew_key=ignored\n");
    BOOST_REQUIRE(info.has_value());
    BOOST_CHECK_EQUAL(info->opmVersion.value(), "2026.04");
    BOOST_CHECK_EQUAL(info->gitHash.value(), "1b2958d6e");
    BOOST_CHECK_EQUAL(info->buildTime.value(), "2026-04-15 at 10:12:00 hrs");
    BOOST_CHECK_EQUAL(info->buildType.value(), "Release");
    BOOST_CHECK_EQUAL(info->mpi.value(), "Open MPI v5.0.7");

    BOOST_CHECK(!parseBuildInfo("build_type=\nversion=1\n")->buildType.has_value()); // empty: unknown
    BOOST_CHECK(!parseBuildInfo("hash=abc\n").has_value()); // no version: not --build-info's output
    BOOST_CHECK(!parseBuildInfo("").has_value());

    BOOST_CHECK_EQUAL(parseFlowVersion("flow 2027.04-pre\nmore\n").value(), "2027.04-pre");
    BOOST_CHECK(!parseFlowVersion("flow\n").has_value());
}

BOOST_AUTO_TEST_CASE(DescribeFlow)
{
    TempDir tmp;
    const auto fresh = describeFlow(newFlow(tmp / "new/flow", "2027.04"));
    BOOST_CHECK_EQUAL(fresh.opmVersion.value(), "2027.04");
    BOOST_CHECK_EQUAL(fresh.buildType.value(), "Release");
    BOOST_CHECK_EQUAL(fresh.mpi.value(), "Open MPI v5.0.7");

    const auto old = describeFlow(oldFlow(tmp / "old/flow", "2025.10")); // --version only
    BOOST_CHECK_EQUAL(old.opmVersion.value(), "2025.10");
    BOOST_CHECK(!old.gitHash && !old.buildType && !old.mpi);

    const auto none = describeFlow(script(tmp / "mute/flow", "exit 1\n"));
    BOOST_CHECK(!none.opmVersion.has_value());
    BOOST_CHECK(!describeFlow(tmp / "missing/flow").opmVersion.has_value());
}

// -------------------------------------------------------------- Candidates

BOOST_AUTO_TEST_CASE(CandidatesInOrderAndMerged)
{
    TempDir tmp;
    const auto installed = newFlow(tmp / "opt/2026.04/bin/flow", "2026.04");
    fs::create_directories(tmp / "path1");
    fs::create_symlink(installed, tmp / "path1/flow"); // the same Flow, through PATH
    const auto other = oldFlow(tmp / "path2/flow", "2025.10");
    const auto rc46 = oldFlow(tmp / "site/flowrc46", "2025.10");
    const auto rc47 = newFlow(tmp / "site/flowrc47", "2026.04");
    writeFile(tmp / "site/README", "not a flow");

    VersionsConfig config;
    config.search.flow = {tmp / "site/flow*"};
    config.versions["stable"] = entry(installed);
    config.versions["gone"] = entry(tmp / "gone/flow");

    std::vector<std::string> warnings;
    const auto all = findCandidates(config, {tmp / "path1", tmp / "path2"}, warnings);
    BOOST_REQUIRE_EQUAL(all.size(), 4U);
    BOOST_CHECK_EQUAL(all[0].flow, installed); // configured first
    BOOST_CHECK((all[0].names == std::vector<std::string>{"stable"}));
    BOOST_CHECK_EQUAL(all[0].paths.size(), 2U); // as configured, and through PATH
    BOOST_CHECK_EQUAL(all[0].paths[1], tmp / "path1/flow");
    BOOST_CHECK_EQUAL(all[0].foundBy.size(), 2U);
    BOOST_CHECK_EQUAL(all[1].flow, rc46);       // search.flow, in name order
    BOOST_CHECK_EQUAL(all[2].flow, rc47);
    BOOST_CHECK_EQUAL(all[3].flow, other);      // PATH
    BOOST_CHECK_EQUAL(warnings.size(), 1U);     // "gone"
}

BOOST_AUTO_TEST_CASE(UnreadableSearchDirIsAWarning)
{
    // A directory runflow may not read is skipped with a warning; the other
    // candidates are still found.
    TempDir tmp;
    newFlow(tmp / "locked/flowrc1", "2026.04");
    const auto open = newFlow(tmp / "open/flowrc2", "2026.04");
    fs::permissions(tmp / "locked", fs::perms::none);
    if (::access((tmp / "locked").c_str(), R_OK) == 0) { // e.g. running as root
        fs::permissions(tmp / "locked", fs::perms::owner_all);
        BOOST_TEST_MESSAGE("permissions are not enforced here: skipped");
        return;
    }
    VersionsConfig config;
    config.search.flow = {tmp / "locked/flow*", tmp / "open/flow*"};
    std::vector<std::string> warnings;
    const auto all = findCandidates(config, {}, warnings);
    fs::permissions(tmp / "locked", fs::perms::owner_all); // so TempDir can remove it
    BOOST_REQUIRE_EQUAL(all.size(), 1U);
    BOOST_CHECK_EQUAL(all[0].flow, open);
    BOOST_REQUIRE_EQUAL(warnings.size(), 1U);
    BOOST_CHECK(warnings[0].find((tmp / "locked").string()) != std::string::npos);
}

// ------------------------------------------------------ search.pythonpath

BOOST_AUTO_TEST_CASE(ExpandPythonPath)
{
    // The legacy site's layout: each version's bindings under the Python it
    // was built for.
    TempDir tmp;
    const auto lib = tmp / "lib";
    fs::create_directories(lib / "python3.11/site-packages/flowrc46");
    fs::create_directories(lib / "python3.12/site-packages/flowrc47");
    fs::create_directories(lib / "python3.11/site-packages/twice");
    fs::create_directories(lib / "python3.12/site-packages/twice");
    fs::create_directories(lib / "extra");
    const std::vector<fs::path> pattern{lib / "python3.*/site-packages/{name}"};
    std::vector<std::string> warnings;

    BOOST_CHECK_EQUAL(expandPythonPath(pattern, "flowrc47", warnings).value(),
                      (lib / "python3.12/site-packages/flowrc47").string());
    BOOST_CHECK_EQUAL(expandPythonPath(pattern, "flowrc46", warnings).value(),
                      (lib / "python3.11/site-packages/flowrc46").string());
    BOOST_CHECK(!expandPythonPath(pattern, "flow", warnings).has_value()); // no such directory
    BOOST_CHECK(warnings.empty());

    // Several entries, each matched; one that matches nothing is left out.
    BOOST_CHECK_EQUAL(expandPythonPath({pattern[0], lib / "extra", "/nowhere"},
                                       "flowrc47", warnings).value(),
                      (lib / "python3.12/site-packages/flowrc47").string() + ":" + (lib / "extra").string());

    // Several matches: left out, with a warning.
    BOOST_CHECK(!expandPythonPath(pattern, "twice", warnings).has_value());
    BOOST_REQUIRE_EQUAL(warnings.size(), 1U);
    BOOST_CHECK(warnings[0].find("twice") != std::string::npos);

    // A name is taken literally, wildcards included.
    fs::create_directories(lib / "python3.12/site-packages/odd*name");
    BOOST_CHECK(expandPythonPath(pattern, "odd*name", warnings).has_value());
    BOOST_CHECK(!expandPythonPath(pattern, "odd*", warnings).has_value());
}

BOOST_AUTO_TEST_CASE(SearchPythonPathForFoundVersions)
{
    TempDir tmp;
    oldFlow(tmp / "site/flowrc46", "2025.10");
    oldFlow(tmp / "site/flowrc47", "2026.04");
    const auto own = oldFlow(tmp / "own/flow", "2026.10");
    fs::create_directories(tmp / "lib/python3.11/site-packages/flowrc46");
    fs::create_directories(tmp / "lib/python3.12/site-packages/flowrc47");
    writeFile(tmp / "runflow.json", R"({
        "search": { "flow": ["site/flow*"],
                    "pythonpath": ["lib/python3.*/site-packages/{name}"] },
        "versions": { "own": { "flow": "own/flow", "pythonpath": ["/mine"] } } })");
    std::vector<std::string> warnings;
    const auto config = RunflowConfig::load({{tmp / "runflow.json", false}}, warnings);
    BOOST_CHECK((config.versions.search.pythonPath
                 == std::vector<fs::path>{tmp / "lib/python3.*/site-packages/{name}"})); // relative to the file

    const auto all = findCandidates(config.versions, {}, warnings);
    BOOST_REQUIRE_EQUAL(all.size(), 3U);
    BOOST_CHECK_EQUAL(all[0].flow, own);
    BOOST_CHECK_EQUAL(all[0].pythonPath.value(), "/mine"); // its own wins
    BOOST_CHECK_EQUAL(all[1].pythonPath.value(), (tmp / "lib/python3.11/site-packages/flowrc46").string());
    BOOST_CHECK_EQUAL(all[2].pythonPath.value(), (tmp / "lib/python3.12/site-packages/flowrc47").string());

    // A version given by path gets it too.
    oldFlow(tmp / "loose/flowrc47", "2026.04");
    ScopedEnv env;
    env.set("PATH", "/nonexistent");
    const auto before = fs::current_path();
    fs::current_path(tmp.path);
    auto registry = VersionRegistry::standard(config.versions);
    const auto& given = registry.resolve((tmp / "loose/flowrc47").string());
    fs::current_path(before);
    BOOST_CHECK_EQUAL(given.pythonPath.value(), (tmp / "lib/python3.12/site-packages/flowrc47").string());
}

BOOST_AUTO_TEST_CASE(SearchFlowPatternsMatchExecutables)
{
    // Patterns match executables; one matching only directories says so.
    TempDir tmp;
    const auto rpm = newFlow(tmp / "usr/lib64/openmpi/bin/flow_openmpi", "2026.04");
    newFlow(tmp / "usr/lib64/openmpi/bin/flow_old", "2025.04");
    writeFile(tmp / "usr/lib64/openmpi/bin/flow.txt", "not executable");
    fs::create_directories(tmp / "opt/opm/2026.04/bin");
    VersionsConfig config;
    config.search.flow = {tmp / "usr/lib64/*/bin/flow_openmpi", tmp / "opt/opm/*"};
    std::vector<std::string> warnings;
    const auto all = findCandidates(config, {}, warnings);
    BOOST_REQUIRE_EQUAL(all.size(), 1U);
    BOOST_CHECK_EQUAL(all[0].flow, rpm);
    BOOST_REQUIRE_EQUAL(warnings.size(), 1U);
    BOOST_CHECK(warnings[0].find("matches directories") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(SearchFlowPatternOfMissingDirectoryMatchesNothing)
{
    // As in the shell: a directory that is not there is no error, so a site
    // file may name directories that only some machines have.
    TempDir tmp;
    VersionsConfig config;
    config.search.flow = {tmp / "missing/bin/flow*", tmp / "missing/*/flow"};
    std::vector<std::string> warnings;
    BOOST_CHECK(findCandidates(config, {}, warnings).empty());
    BOOST_CHECK(warnings.empty());

    bool unreadable = true;
    BOOST_CHECK(globMatches((tmp / "missing/flow*").string(), &unreadable).empty());
    BOOST_CHECK(!unreadable);
}

// --------------------------------------------------------- VersionRegistry

BOOST_AUTO_TEST_CASE(RegistryDescribesOnlyWhatIsNeeded)
{
    TempDir tmp;
    const auto a = newFlow(tmp / "a/flow", "2026.04");
    const auto b = oldFlow(tmp / "b/flow", "2025.10");
    VersionsConfig config;
    std::vector<std::string> warnings;
    CountingDescriber describer;
    VersionRegistry registry(findCandidates(config, {tmp / "a", tmp / "b"}, warnings),
                             std::nullopt, describer);

    // The default: the first flow in PATH, and only it is asked.
    const auto& chosen = registry.defaultInstallation();
    BOOST_CHECK_EQUAL(chosen.flow, a);
    BOOST_CHECK_EQUAL(chosen.build.opmVersion.value(), "2026.04");
    BOOST_CHECK_EQUAL(describer.calls->size(), 1U);

    // Listing asks the others, each once.
    registry.describeAll();
    const auto& all = registry.installations();
    BOOST_CHECK_EQUAL(all[1].build.opmVersion.value(), "2025.10");
    registry.describeAll();
    BOOST_CHECK_EQUAL(describer.calls->size(), 2U);
}

BOOST_AUTO_TEST_CASE(RegistryResolvesTheDefaultOnce)
{
    // A listing asks isDefault() for each installation: the default is
    // resolved once, and nothing is described twice.
    TempDir tmp;
    VersionsConfig config;
    for (const auto* name : {"a", "b", "c", "d"})
        config.versions[name] = entry(newFlow(tmp / name / "flow", "2026.04"));
    std::vector<std::string> warnings;
    CountingDescriber describer;
    VersionRegistry registry(findCandidates(config, {}, warnings), std::string("c"), describer);

    std::size_t defaults = 0;
    registry.describeAll();
    for (const auto& inst : registry.installations())
        defaults += registry.isDefault(inst) ? 1 : 0;
    registry.describeAll();
    for (const auto& inst : registry.installations())
        defaults += registry.isDefault(inst) ? 1 : 0;
    BOOST_CHECK_EQUAL(defaults, 2U);                    // c, in each listing
    BOOST_CHECK_EQUAL(describer.calls->size(), 4U);     // each binary asked once
    BOOST_CHECK(registry.isDefault(Installation{registry.installations()[2]})); // a copy, too
    BOOST_CHECK_EQUAL(registry.defaultInstallation().names.front(), "c");

    // Without a default, every question has the same answer, and the error stays.
    VersionRegistry none(findCandidates(config, {}, warnings), std::string("zzz"), describer);
    BOOST_CHECK(!none.isDefault(none.installations()[0]));
    BOOST_CHECK_THROW(none.defaultInstallation(), VersionError);
    BOOST_CHECK_THROW(none.defaultInstallation(), VersionError);
}

BOOST_AUTO_TEST_CASE(RegistryResolves)
{
    TempDir tmp;
    const auto a = newFlow(tmp / "a/flow", "2026.04");
    const auto b = oldFlow(tmp / "b/flow", "2026.04");
    const auto rc47 = newFlow(tmp / "site/flowrc47", "2026.04-rc47");
    const auto loose = newFlow(tmp / "loose/flow-mine", "2027.04-pre");
    const auto named = oldFlow(tmp / "c/flow", "2025.10");

    VersionsConfig config;
    config.search.flow = {tmp / "site/flow*"};
    config.versions["flowrc47"] = entry(named); // a name that is also an executable's
    std::vector<std::string> warnings;
    VersionRegistry registry(findCandidates(config, {tmp / "a", tmp / "b"}, warnings),
                             std::string("flowrc47"), describeFlow);

    BOOST_CHECK_EQUAL(registry.resolve(loose.string()).flow, loose);   // 1. a path
    BOOST_CHECK_EQUAL(registry.resolve("flowrc47").flow, named);       // 2. a name, before 3.
    BOOST_CHECK_EQUAL(registry.resolve("2025.10").flow, named);        // 4. an OPM version
    BOOST_CHECK_EQUAL(registry.resolve("2026.04-rc47").flow, rc47);
    BOOST_CHECK_EQUAL(registry.defaultInstallation().flow, named);     // default_version

    // Several matches of one rule are an error naming them, not a guess.
    BOOST_CHECK_THROW(registry.resolve("flow"), VersionError);    // a, b, c
    try {
        registry.resolve("2026.04");                               // a (new) and b (old)
        BOOST_FAIL("no error");
    } catch (const VersionError& e) {
        BOOST_CHECK(std::string(e.what()).find(a.string()) != std::string::npos);
        BOOST_CHECK(std::string(e.what()).find(b.string()) != std::string::npos);
    }
    BOOST_CHECK_THROW(registry.resolve("nothing"), VersionError);
    BOOST_CHECK_THROW(registry.resolve((tmp / "none/flow").string()), VersionError);
}

BOOST_AUTO_TEST_CASE(RegistryWithoutDefault)
{
    TempDir tmp;
    std::vector<std::string> warnings;
    VersionRegistry registry(findCandidates(VersionsConfig{}, {tmp / "empty"}, warnings),
                             std::nullopt, describeFlow);
    BOOST_CHECK(registry.installations().empty());
    BOOST_CHECK_THROW(registry.defaultInstallation(), VersionError);
}

// ------------------------------------------------- the version's opmlaunchinfo

BOOST_AUTO_TEST_CASE(FindLaunchInfo)
{
    TempDir tmp;
    const auto flow = newFlow(tmp / "opt/bin/flow", "2026.04");
    Installation inst;
    inst.flow = flow;
    inst.paths = {tmp / "usr/bin/flow"}; // as found, e.g. a symbolic link
    BOOST_CHECK(!findLaunchInfo(inst).has_value());

    // Next to the canonical flow (an installation's bin directory).
    const auto installed = script(tmp / "opt/bin/opmlaunchinfo", "exit 0\n");
    BOOST_CHECK_EQUAL(findLaunchInfo(inst).value(), installed);

    // Next to the path it was found as comes first.
    const auto linked = script(tmp / "usr/bin/opmlaunchinfo", "exit 0\n");
    BOOST_CHECK_EQUAL(findLaunchInfo(inst).value(), linked);

    // A configured one wins, also if it does not exist (deckResources() then says so).
    inst.launchInfo = (tmp / "configured/opmlaunchinfo").string();
    BOOST_CHECK_EQUAL(findLaunchInfo(inst).value(), tmp / "configured/opmlaunchinfo");
}
