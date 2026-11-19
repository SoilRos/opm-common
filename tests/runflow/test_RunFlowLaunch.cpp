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

// Tests of what runflow reads from a deck and how it plans a launch
// (opm/runflow/launch), and of the opmlaunchinfo program.
//
// Argument (from ExtraTests.cmake): the opmlaunchinfo executable.

#define BOOST_TEST_MODULE RunFlowLaunchTests
#include <boost/test/unit_test.hpp>

#include <opm/json/JsonObject.hpp>
#include <opm/runflow/launch/LaunchInfo.hpp>
#include <opm/runflow/launch/LaunchPlan.hpp>
#include <opm/runflow/launch/Launcher.hpp>
#include <opm/runflow/process/Process.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <stdlib.h>

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

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

// A minimal deck with the mandatory sections; `grid` and `schedule` are
// put into theirs.
std::string deck(const std::string& schedule, const std::string& grid = "")
{
    return "RUNSPEC\nDIMENS\n 1 1 1 /\nOIL\nSTART\n 1 JAN 2020 /\n"
           "GRID\n" + grid +
           "PROPS\nSOLUTION\n"
           "SCHEDULE\n" + schedule;
}

const std::string coupledSchedule =
    "SLAVES\n"
    "  'RES-1'  'RC-01_MOD1_HIST'  '*'  '../mod1'  4 /\n"
    "  'RES-2'  'RC-01_MOD2_HIST'  '*'  '../mod2'  /\n"
    "/\n";

Installation flowAt(const fs::path& path)
{
    Installation inst;
    inst.flow = path;
    inst.build.opmVersion = "2026.04";
    inst.build.buildType = "Release";
    return inst;
}

LaunchInfo twoSlaves()
{
    LaunchInfo info;
    info.slaves = {{"RES-1", "RC-01_MOD1_HIST", "../mod1", 4},
                   {"RES-2", "RC-01_MOD2_HIST", "../mod2", 1}};
    return info;
}

} // namespace

// ------------------------------------------------------------ LaunchInfo

BOOST_AUTO_TEST_CASE(LaunchInfoJson)
{
    const auto info = twoSlaves();
    const auto back = LaunchInfo::fromJson(info.toJson());
    BOOST_REQUIRE_EQUAL(back.slaves.size(), 2U);
    BOOST_CHECK_EQUAL(back.slaves[0].name, "RES-1");
    BOOST_CHECK_EQUAL(back.slaves[0].directory, "../mod1");
    BOOST_CHECK_EQUAL(back.slaves[0].deck, "RC-01_MOD1_HIST");
    BOOST_CHECK_EQUAL(back.slaves[0].processes, 4);
    BOOST_CHECK_EQUAL(back.slaveProcesses(), 5);
    BOOST_CHECK_EQUAL(Json::JsonObject(info.toJson()).get_int("schema"), launchInfoSchema);

    // Fields of a newer version of schema 1 are ignored; a newer schema is refused.
    const auto future = LaunchInfo::fromJson(
        R"({"schema": 1, "gpus": 2, "slaves": [{"name": "A", "num_pe": 3, "host": "x"}]})");
    BOOST_CHECK_EQUAL(future.slaveProcesses(), 3);
    BOOST_CHECK(LaunchInfo::fromJson(R"({"schema": 1})").slaves.empty());
    BOOST_CHECK_THROW(LaunchInfo::fromJson(R"({"schema": 2, "slaves": []})"), LaunchError);
    BOOST_CHECK_THROW(LaunchInfo::fromJson("not json"), LaunchError);
    BOOST_CHECK_THROW(LaunchInfo::fromJson(R"({"schema": 1, "slaves": [{"name": "A", "num_pe": 0}]})"),
                      LaunchError);
}

BOOST_AUTO_TEST_CASE(ReadCoupledDeck)
{
    TempDir tmp;
    const auto info = readLaunchInfo(writeFile(tmp / "MASTER.DATA", deck(coupledSchedule)));
    BOOST_REQUIRE_EQUAL(info.slaves.size(), 2U);
    BOOST_CHECK_EQUAL(info.slaves[0].name, "RES-1");
    BOOST_CHECK_EQUAL(info.slaves[0].deck, "RC-01_MOD1_HIST");
    BOOST_CHECK_EQUAL(info.slaves[0].directory, "../mod1");
    BOOST_CHECK_EQUAL(info.slaves[0].processes, 4);
    BOOST_CHECK_EQUAL(info.slaves[1].processes, 1); // NUM_PE defaulted
}

BOOST_AUTO_TEST_CASE(ReadUncoupledDeck)
{
    TempDir tmp;
    const auto info = readLaunchInfo(writeFile(tmp / "CASE.DATA", deck("TSTEP\n 1 /\n")));
    BOOST_CHECK(info.slaves.empty());
    BOOST_CHECK_EQUAL(info.slaveProcesses(), 0);
}

BOOST_AUTO_TEST_CASE(ReadSkipsTheGrid)
{
    // GRID includes a file that does not exist: only RUNSPEC and SCHEDULE are read.
    TempDir tmp;
    const auto file = writeFile(tmp / "BIG.DATA",
                                deck(coupledSchedule, "INCLUDE\n 'huge_grid_not_here.grdecl' /\n"));
    BOOST_CHECK_EQUAL(readLaunchInfo(file).slaveProcesses(), 5);
}

BOOST_AUTO_TEST_CASE(ReadWithSectionsInIncludes)
{
    // SCHEDULE is in an included file: the parser cannot skip sections, so
    // the whole deck is read.
    TempDir tmp;
    writeFile(tmp / "schedule.inc", "SCHEDULE\n" + coupledSchedule);
    const auto file = writeFile(tmp / "MASTER.DATA",
                                "RUNSPEC\nDIMENS\n 1 1 1 /\nOIL\nSTART\n 1 JAN 2020 /\n"
                                "GRID\nINCLUDE\n 'schedule.inc' /\n");
    BOOST_CHECK_EQUAL(readLaunchInfo(file).slaveProcesses(), 5);
}

BOOST_AUTO_TEST_CASE(ReadRefusesWhatFlowRefuses)
{
    TempDir tmp;
    BOOST_CHECK_THROW(readLaunchInfo(writeFile(tmp / "TWICE.DATA",
                                               deck(coupledSchedule + "TSTEP\n 1 /\n" + coupledSchedule))),
                      LaunchError);
    try {
        readLaunchInfo(writeFile(tmp / "ZERO.DATA",
                                 deck("SLAVES\n 'RES-1' 'BASE' '*' '../mod1' 0 /\n/\n")));
        BOOST_FAIL("no error");
    } catch (const LaunchError& e) {
        BOOST_CHECK(contains(e.what(), "RES-1") && contains(e.what(), "positive"));
    }
    BOOST_CHECK_THROW(readLaunchInfo(tmp / "MISSING.DATA"), LaunchError);

    // A missing include in a section that is read is an error, not the end of runflow.
    BOOST_CHECK_THROW(readLaunchInfo(writeFile(tmp / "INC.DATA",
                                               deck("INCLUDE\n 'not_here.inc' /\n"))),
                      LaunchError);
}

// --------------------------------------------------------------- Launcher

namespace {

// A stand-in mpirun that says which MPI it is.
fs::path fakeMpirun(const fs::path& file, const std::string& versionLine)
{
    return script(file, "echo '" + versionLine + "'\n");
}

const std::vector<std::string> legacyOptions{"--bind-to", "none", "-np", "{np}"};

Launcher launcherAt(const fs::path& program, std::vector<std::string> arguments)
{
    return Launcher{program, std::move(arguments), "test"};
}

} // namespace

BOOST_AUTO_TEST_CASE(FindLauncherAsTheShellWouldAfterModuleLoad)
{
    TempDir tmp;
    // The RPM layout: flow and its mpirun in /usr/lib64/openmpi/bin.
    auto flow = flowAt(tmp / "openmpi/bin/flow");
    flow.paths = {tmp / "openmpi/bin/flow"};
    const auto inPath = fakeMpirun(tmp / "path/mpirun", "HYDRA build details:\n    Version: 4.1.2");

    // Nothing next to flow: PATH's.
    auto launcher = findLauncher(flow, "mpirun", legacyOptions, {tmp / "path"});
    BOOST_REQUIRE(launcher.has_value());
    BOOST_CHECK_EQUAL(launcher->program, inPath);
    BOOST_CHECK_EQUAL(launcher->how, "PATH");

    // Next to flow wins over PATH.
    const auto nextToFlow = fakeMpirun(tmp / "openmpi/bin/mpirun", "mpirun (Open MPI) 5.0.7");
    launcher = findLauncher(flow, "mpirun", legacyOptions, {tmp / "path"});
    BOOST_REQUIRE(launcher.has_value());
    BOOST_CHECK_EQUAL(launcher->program, nextToFlow);
    BOOST_CHECK_EQUAL(launcher->how, "next to flow");
    BOOST_CHECK((launcher->command(4, 6) == std::vector<std::string>{
                     nextToFlow.string(), "--bind-to", "none", "-np", "4"}));

    // A path in the template is used as it is.
    const auto other = fakeMpirun(tmp / "other/mpiexec", "mpirun (Open MPI) 5.0.7");
    launcher = findLauncher(flow, other.string(), {"-n", "{np}"}, {tmp / "path"});
    BOOST_REQUIRE(launcher.has_value());
    BOOST_CHECK_EQUAL(launcher->program, other);

    // The version's own launcher wins over the section's, and keeps the section's options.
    flow.launcher = (tmp / "mine/mpirun").string();
    BOOST_CHECK(!findLauncher(flow, "mpirun", legacyOptions, {tmp / "path"}).has_value()); // not there yet
    fakeMpirun(tmp / "mine/mpirun", "mpirun (Open MPI) 5.0.7");
    launcher = findLauncher(flow, "mpirun", legacyOptions, {tmp / "path"});
    BOOST_REQUIRE(launcher.has_value());
    BOOST_CHECK_EQUAL(launcher->program, tmp / "mine/mpirun");
    BOOST_CHECK_EQUAL(launcher->how, "the version's launcher");
    BOOST_CHECK((launcher->arguments == std::vector<std::string>{"--bind-to", "none", "-np", "{np}"}));

    // A version's launcher given as a name is found as the section's would be.
    flow.launcher = "mpiexec";
    fakeMpirun(tmp / "openmpi/bin/mpiexec", "mpirun (Open MPI) 5.0.7");
    launcher = findLauncher(flow, "mpirun", legacyOptions, {tmp / "path"});
    BOOST_REQUIRE(launcher.has_value());
    BOOST_CHECK_EQUAL(launcher->program, tmp / "openmpi/bin/mpiexec");
    BOOST_CHECK_EQUAL(launcher->how, "the version's launcher, next to flow");

    // None anywhere.
    auto lonely = flowAt(tmp / "alone/flow");
    BOOST_CHECK(!findLauncher(lonely, "mpirun", legacyOptions, {tmp / "nowhere"}).has_value());
}

BOOST_AUTO_TEST_CASE(IdentifyMpi)
{
    const auto check = [](const std::string& text, const std::string& implementation,
                          const std::string& version) {
        const auto mpi = identifyMpi(text);
        BOOST_REQUIRE_MESSAGE(mpi.has_value(), text);
        BOOST_CHECK_EQUAL(mpi->implementation, implementation);
        BOOST_CHECK_EQUAL(mpi->version, version);
    };
    // Flow's mpi= (MPI_Get_library_version) and the launchers' --version.
    check("Open MPI v5.0.7", "Open MPI", "5.0.7");
    check("mpirun (Open MPI) 5.0.7\n\nReport bugs to https://www.open-mpi.org/community/help/",
          "Open MPI", "5.0.7");
    check("MPICH Version:\t4.1.2", "MPICH", "4.1.2");
    check("HYDRA build details:\n    Version:                 4.1.2\n", "MPICH", "4.1.2");
    check("Intel(R) MPI Library 2021.10 for Linux* OS", "Intel MPI", "2021.10");
    check("Intel(R) MPI Library for Linux* OS, Version 2021.10 Build 20230619", "Intel MPI", "2021.10");
    BOOST_CHECK(!identifyMpi("some launcher 1.0").has_value());
    BOOST_CHECK(!identifyMpi("Open MPI").has_value()); // no version
}

BOOST_AUTO_TEST_CASE(CheckLauncherAgainstFlowsMpi)
{
    TempDir tmp;
    auto flow = flowAt(tmp / "bin/flow");
    const auto openmpi507 = launcherAt(fakeMpirun(tmp / "a/mpirun", "mpirun (Open MPI) 5.0.7"), {});
    const auto openmpi506 = launcherAt(fakeMpirun(tmp / "b/mpirun", "mpirun (Open MPI) 5.0.6"), {});
    const auto openmpi41 = launcherAt(fakeMpirun(tmp / "c/mpirun", "mpirun (Open MPI) 4.1.6"), {});
    const auto mpich = launcherAt(fakeMpirun(tmp / "d/mpiexec", "HYDRA build details:\n Version: 5.0.7"), {});
    const auto mute = launcherAt(fakeMpirun(tmp / "e/mpirun", "who knows"), {});

    // Flow does not say: not checked (older Flow without --build-info).
    BOOST_CHECK(checkLauncher(flow, openmpi507).result == LauncherCheck::Result::FlowUnknown);

    flow.build.mpi = "Open MPI v5.0.7";
    BOOST_CHECK(checkLauncher(flow, openmpi507).result == LauncherCheck::Result::Match);
    BOOST_CHECK(checkLauncher(flow, openmpi506).result == LauncherCheck::Result::Match); // patch level
    BOOST_CHECK(checkLauncher(flow, mute).result == LauncherCheck::Result::LauncherUnknown);
    BOOST_CHECK_THROW(checkLauncher(flow, openmpi41), LaunchError); // another minor version
    try {
        checkLauncher(flow, mpich); // another implementation
        BOOST_FAIL("no error");
    } catch (const LaunchError& e) {
        const std::string what = e.what();
        BOOST_CHECK(contains(what, "MPICH 5.0.7") && contains(what, "Open MPI 5.0.7"));
        BOOST_CHECK(contains(what, "\"launcher\""));
    }

    flow.build.mpi = "none"; // built without MPI
    BOOST_CHECK_THROW(checkLauncher(flow, openmpi507), LaunchError);
}

// ------------------------------------------------------------- LaunchPlan

BOOST_AUTO_TEST_CASE(PlanOneRankWithoutMpi)
{
    auto flow = flowAt("/opt/opm/bin/flow");
    flow.pythonPath = "/opt/opm/lib/python3.12/site-packages";
    const LaunchRequest request{"CASE.DATA", 1, {"--enable-esmry=true"}};
    BOOST_CHECK(!needsMpi(request, LaunchInfo{}));
    const auto plan = planLaunch(flow, request, LaunchInfo{}, std::nullopt); // no launcher needed
    BOOST_CHECK((plan.command == std::vector<std::string>{"/opt/opm/bin/flow", "CASE.DATA",
                                                          "--enable-esmry=true"}));
    BOOST_CHECK_EQUAL(plan.slots, 1);
    BOOST_REQUIRE_EQUAL(plan.environment.size(), 1U);
    BOOST_CHECK_EQUAL(plan.environment[0].name, "PYTHONPATH");
    BOOST_CHECK(plan.environment[0].mode == EnvSetting::Mode::Prepend);
}

BOOST_AUTO_TEST_CASE(PlanRanksThroughTheLauncher)
{
    const auto flow = flowAt("/opt/opm/bin/flow");
    const LaunchRequest request{"CASE.DATA", 4, {}};
    BOOST_CHECK(needsMpi(request, LaunchInfo{}));
    const auto plan = planLaunch(flow, request, LaunchInfo{},
        launcherAt("/usr/lib64/openmpi/bin/mpirun", {"--bind-to", "none", "-np", "{np}", "-x", "OMP_NUM_THREADS=1"}));
    BOOST_CHECK((plan.command == std::vector<std::string>{
                     "/usr/lib64/openmpi/bin/mpirun", "--bind-to", "none", "-np", "4",
                     "-x", "OMP_NUM_THREADS=1", "/opt/opm/bin/flow", "CASE.DATA"}));
    BOOST_CHECK_EQUAL(plan.slots, 4);
    BOOST_CHECK(plan.environment.empty()); // no PYTHONPATH configured
}

BOOST_AUTO_TEST_CASE(PlanCoupledRun)
{
    const auto flow = flowAt("/opt/opm/bin/flow");
    const LaunchRequest request{"MASTER.DATA", 1, {}};
    BOOST_CHECK(needsMpi(request, twoSlaves())); // one rank, but coupled
    const auto plan = planLaunch(flow, request, twoSlaves(),
                                 launcherAt("/mpi/mpirun", {"-np", "{np}", "--slots={slots}"}));

    // One rank, through MPI all the same, and slots for the slaves.
    BOOST_CHECK((plan.command == std::vector<std::string>{
                     "/mpi/mpirun", "-np", "1", "--slots=6", "/opt/opm/bin/flow", "MASTER.DATA"}));
    BOOST_CHECK_EQUAL(plan.ranks, 1);
    BOOST_CHECK_EQUAL(plan.slots, 6);
    BOOST_REQUIRE_EQUAL(plan.banner.size(), 3U);
    BOOST_CHECK_EQUAL(plan.banner[0], "runflow: Flow 2026.04 Release, /opt/opm/bin/flow");
    BOOST_CHECK_EQUAL(plan.banner[1], "runflow: 1 rank + 5 for the slaves (RES-1: 4, RES-2: 1) = 6 slots");
}

BOOST_AUTO_TEST_CASE(PlanRefusals)
{
    const auto flow = flowAt("/opt/opm/bin/flow");
    BOOST_CHECK_THROW(planLaunch(flow, {"CASE.DATA", 0, {}}, LaunchInfo{}, std::nullopt), LaunchError);
    try {
        planLaunch(flow, {"CASE.DATA", 2, {}}, LaunchInfo{}, std::nullopt); // MPI, but no launcher
        BOOST_FAIL("no error");
    } catch (const LaunchError& e) {
        BOOST_CHECK(contains(e.what(), "no MPI launcher found"));
    }
}

// ---------------------------------------------------- deckResources

BOOST_AUTO_TEST_CASE(DeckResourcesFromTheVersionsOwnReader)
{
    TempDir tmp;
    const auto deckFile = writeFile(tmp / "MASTER.DATA", deck(coupledSchedule));
    auto flow = flowAt(tmp / "bin/flow");

    // A version without opmlaunchinfo: read here.
    const auto here = deckResources(flow, deckFile);
    BOOST_CHECK_EQUAL(here.info.slaveProcesses(), 5);
    BOOST_CHECK_EQUAL(here.source, "runflow's parser");

    // With one: its answer, whatever this parser would say.
    flow.launchInfo = script(tmp / "bin/opmlaunchinfo",
        "echo '{\"schema\": 1, \"slaves\": [{\"name\": \"X\", \"num_pe\": 7}]}'\n").string();
    const auto own = deckResources(flow, deckFile);
    BOOST_CHECK_EQUAL(own.info.slaveProcesses(), 7);
    BOOST_CHECK(contains(own.source, "bin/opmlaunchinfo"));

    // Its errors are the user's errors.
    flow.launchInfo = script(tmp / "bin/opmlaunchinfo-fails",
        "echo 'opmlaunchinfo: Problem with keyword SLAVES' >&2\nexit 1\n").string();
    try {
        deckResources(flow, deckFile);
        BOOST_FAIL("no error");
    } catch (const LaunchError& e) {
        BOOST_CHECK(contains(e.what(), "Problem with keyword SLAVES"));
    }
}

BOOST_AUTO_TEST_CASE(OpmLaunchInfoProgram)
{
    const auto& suite = boost::unit_test::framework::master_test_suite();
    if (suite.argc < 2) {
        BOOST_TEST_MESSAGE("no opmlaunchinfo given: skipped");
        return;
    }
    TempDir tmp;
    const auto ok = captureOutput({suite.argv[1], writeFile(tmp / "M.DATA", deck(coupledSchedule)).string()},
                                  std::chrono::seconds(60));
    BOOST_CHECK_EQUAL(ok.exitCode, 0);
    BOOST_CHECK_EQUAL(LaunchInfo::fromJson(ok.output).slaveProcesses(), 5); // stdout is only JSON

    const auto bad = captureOutput({suite.argv[1], (tmp / "NONE.DATA").string()}, std::chrono::seconds(60));
    BOOST_CHECK_EQUAL(bad.exitCode, 1);
    BOOST_CHECK(contains(bad.errors, "NONE.DATA"));
    BOOST_CHECK(bad.output.empty());

    BOOST_CHECK_EQUAL(captureOutput({suite.argv[1]}, std::chrono::seconds(10)).exitCode, 2);
}
