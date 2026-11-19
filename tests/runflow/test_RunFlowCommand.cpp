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

// Tests of runflow's command line and configuration (opm/runflow/cli), and of
// the runflow program end to end: Flow, mpirun and LSF's commands are
// replaced by shell scripts, in a configuration of the test's own.
//
// Argument (from ExtraTests.cmake): the runflow executable.

#define BOOST_TEST_MODULE RunFlowCommandTests
#include <boost/test/unit_test.hpp>

#include <opm/json/JsonObject.hpp>
#include <opm/runflow/cli/CommandLine.hpp>
#include <opm/runflow/cli/RunflowConfig.hpp>
#include <opm/runflow/process/Process.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

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

std::string readFile(const fs::path& file)
{
    std::ifstream in(file);
    std::stringstream content;
    content << in.rdbuf();
    return content.str();
}

std::vector<std::string> readLines(const fs::path& file)
{
    std::vector<std::string> lines;
    std::ifstream in(file);
    for (std::string line; std::getline(in, line);)
        lines.push_back(line);
    return lines;
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

CommandLine parse(std::vector<std::string> args)
{
    args.insert(args.begin(), "runflow");
    return parseCommandLine(args);
}

std::string deck(const std::string& schedule)
{
    return "RUNSPEC\nDIMENS\n 1 1 1 /\nOIL\nSTART\n 1 JAN 2020 /\nGRID\nPROPS\nSOLUTION\n"
           "SCHEDULE\n" + schedule;
}

// Sets environment variables for one test, and restores them.
class ScopedEnv
{
public:
    void set(const std::string& name, const std::string& value)
    {
        if (!saved_.contains(name)) {
            const char* old = std::getenv(name.c_str());
            saved_[name] = old ? std::optional<std::string>(old) : std::nullopt;
        }
        ::setenv(name.c_str(), value.c_str(), 1);
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

// A machine for runflow: two Flow versions, mpirun and LSF stand-ins, decks,
// and a configuration of its own (nothing of the user's is read).
struct Site
{
    TempDir tmp;
    ScopedEnv env;
    fs::path runflow;
    fs::path lsf;

    Site()
    {
        const auto& suite = boost::unit_test::framework::master_test_suite();
        BOOST_REQUIRE_MESSAGE(suite.argc >= 2, "the runflow executable is needed as argument");
        runflow = suite.argv[1];

        // Flow: says what it got; `--version` as Flow's.
        const std::string flow =
            "[ \"$1\" = --version ] && { echo 'flow 2026.04'; exit 0; }\n"
            "echo \"flow args: $*\"\n"
            "echo \"cwd: $(pwd)\"\n"
            "echo \"pythonpath: $PYTHONPATH\"\n"
            "echo 'flow says hi' >&2\n"
            "exit ${FAKE_FLOW_EXIT:-0}\n";
        script(tmp / "opm/dev/bin/flow", flow);
        script(tmp / "site/flowrc47", flow);
        // mpirun: records its arguments, then runs the program (the first absolute path).
        script(tmp / "bin/mpirun",
               "echo \"mpirun $*\" >> '" + (tmp / "mpirun.log").string() + "'\n"
               "while [ $# -gt 0 ]; do case \"$1\" in /*) break;; esac; shift; done\n"
               "exec \"$@\"\n");
        // LSF.
        lsf = tmp / "lsf";
        const auto d = lsf.string();
        // bsub: records its arguments and input, and answers as bsub does
        // (the queue is its 4th argument when -q is given).
        script(lsf / "bsub", "printf '%s\\n' \"$@\" > '" + d + "/bsub.args'\ncat > '" + d
                                 + "/bsub.stdin'\necho \"Job <4242> is submitted to queue <$4>.\"\n");
        script(lsf / "bjobs", "echo 'RUN -'\n");
        script(lsf / "bkill", "exit 0\n");

        // The user's file holds this site's configuration; override() adds a
        // file on top, as $RUNFLOW_CONFIG.
        writeFile(tmp / "home/.runflow.json", R"({
            "default_version": "dev",
            "search": { "flow": [")" + (tmp / "site").string() + R"(/flow*"] },
            "versions": { "dev": { "flow": ")" + (tmp / "opm/dev/bin/flow").string() + R"(",
                                   "pythonpath": ["/dev/python"] } },
            "lsf": { "bsub": ")" + d + R"(/bsub", "bjobs": ")" + d + R"(/bjobs",
                     "bkill": ")" + d + R"(/bkill", "default_queue": "daily" } })");

        writeFile(tmp / "cases/CASE.DATA", deck("TSTEP\n 1 /\n"));
        writeFile(tmp / "cases/MASTER.DATA", deck("SLAVES\n 'RES-1' 'S1' '*' '../s1' 4 /\n"
                                                  " 'RES-2' 'S2' '*' '../s2' 1 /\n/\n"));

        env.set("HOME", (tmp / "home").string());
        env.set("PATH", (tmp / "bin").string() + ":/usr/bin:/bin");
        env.set("PYTHONPATH", "/user/python");
    }

    // Settings over the user's, in a file named by $RUNFLOW_CONFIG.
    void override(const std::string& json)
    {
        writeFile(tmp / "override.json", json);
        env.set("RUNFLOW_CONFIG", (tmp / "override.json").string());
    }

    CapturedOutput run(std::vector<std::string> args) const
    {
        args.insert(args.begin(), runflow.string());
        return captureOutput(args, std::chrono::seconds(60));
    }
};

} // namespace

// ------------------------------------------------------------ CommandLine

BOOST_AUTO_TEST_CASE(LegacyOptions)
{
    const auto line = parse({"-q", "long", "-i", "-v", "flowrc47", "-n", "8", "-o", "--a=1 --b=2", "CASE.DATA"});
    BOOST_CHECK_EQUAL(line.queue.value(), "long");
    BOOST_CHECK(line.interactive);
    BOOST_CHECK_EQUAL(line.version.value(), "flowrc47");
    BOOST_CHECK_EQUAL(line.processes, 8);
    BOOST_CHECK_EQUAL(line.flowOptions, "--a=1 --b=2");
    BOOST_CHECK((line.decks == std::vector<std::string>{"CASE.DATA"}));
    BOOST_CHECK(!line.list && !line.help && !line.dryRun);
}

BOOST_AUTO_TEST_CASE(LegacySpellings)
{
    // getopt -a: long options with one dash, prefixes, options after the deck.
    const auto line = parse({"-np", "4", "-op", "--x=1", "-queue", "q", "CASE", "--inter", "-version=v1"});
    BOOST_CHECK_EQUAL(line.processes, 4);
    BOOST_CHECK_EQUAL(line.flowOptions, "--x=1");
    BOOST_CHECK_EQUAL(line.queue.value(), "q");
    BOOST_CHECK(line.interactive);
    BOOST_CHECK_EQUAL(line.version.value(), "v1");
    BOOST_CHECK((line.decks == std::vector<std::string>{"CASE"}));

    BOOST_CHECK(parse({"--np=2", "--list"}).list);
    BOOST_CHECK_EQUAL(parse({"-n2", "D"}).processes, 2);
    BOOST_CHECK(parse({"-il", "D"}).list);
    BOOST_CHECK(parse({"--dry-run", "D"}).dryRun);
    BOOST_CHECK(parse({"-h"}).help);
    BOOST_CHECK_EQUAL(parse({"-n", "2", "-n", "3", "D"}).processes, 3); // the last one
}

BOOST_AUTO_TEST_CASE(CommandLineErrors)
{
    const auto message = [](std::vector<std::string> args) {
        try {
            parse(std::move(args));
        } catch (const UsageError& e) {
            return std::string(e.what());
        }
        return std::string("no error");
    };
    BOOST_CHECK(contains(message({"--bogus", "D"}), "--bogus"));
    BOOST_CHECK(contains(message({"-iz", "D"}), "-z"));
    BOOST_CHECK(contains(message({"D", "-n"}), "needs a value"));
    BOOST_CHECK(contains(message({"-n", "four", "D"}), "four"));
    BOOST_CHECK(contains(message({"-n", "0", "D"}), "positive"));
}

BOOST_AUTO_TEST_CASE(FlowOptionsAndDecks)
{
    BOOST_CHECK((splitFlowOptions("  --a=1\t--b=2\n x ") == std::vector<std::string>{"--a=1", "--b=2", "x"}));
    BOOST_CHECK(splitFlowOptions("").empty());

    const auto here = locateDeck("A.B.DATA");
    BOOST_CHECK_EQUAL(here.root, "A");
    BOOST_CHECK_EQUAL(here.deck, "A.DATA");
    BOOST_CHECK_EQUAL(here.workdir, fs::current_path());
    const auto there = locateDeck("/data/cases/CASE");
    BOOST_CHECK_EQUAL(there.workdir, fs::path("/data/cases"));
    BOOST_CHECK_EQUAL(there.deck, "CASE.DATA");
}

// ------------------------------------------------------ runflow, end to end

BOOST_AUTO_TEST_CASE(ListsVersions)
{
    Site site;
    const auto result = site.run({"-l"});
    BOOST_CHECK_EQUAL(result.exitCode, 0);
    const auto& out = result.output;
    BOOST_CHECK(out.starts_with("Available versions (for '-v' option without absolute path)\n"));
    BOOST_CHECK(contains(out, "dev ") && contains(out, "(default)"));
    BOOST_CHECK(contains(out, "flowrc47"));
}

BOOST_AUTO_TEST_CASE(Help)
{
    Site site;
    const auto result = site.run({"--help"});
    BOOST_CHECK_EQUAL(result.exitCode, 0);
    BOOST_CHECK(contains(result.output, "usage: runflow [-q queue] [-i] [-l] [-v version]"));
    BOOST_CHECK(contains(result.output, "default is daily"));  // from runflow.json
    BOOST_CHECK(contains(result.output, "default is dev,"));   // from runflow.json
}

BOOST_AUTO_TEST_CASE(InteractiveRun)
{
    Site site;
    site.env.set("FAKE_FLOW_EXIT", "3");
    const auto result = site.run({"-i", "-o", "--enable-esmry=true  --x=1", (site.tmp / "cases/CASE.DATA").string()});

    BOOST_CHECK_EQUAL(result.exitCode, 3); // Flow's
    BOOST_CHECK(contains(result.output, "flow args: CASE.DATA --enable-esmry=true --x=1"));
    BOOST_CHECK(contains(result.output, "cwd: " + (site.tmp / "cases").string()));
    BOOST_CHECK(contains(result.output, "pythonpath: /dev/python:/user/python"));
    BOOST_CHECK(contains(result.errors, "flow says hi"));
    BOOST_CHECK(!fs::exists(site.tmp / "mpirun.log")); // one process: Flow alone

    // The same in the log files, after the banner.
    const auto log = readFile(site.tmp / "cases/CASE.LOG");
    BOOST_CHECK(log.starts_with("local run on "));
    BOOST_CHECK(contains(log, "runflow: Flow 2026.04, " + (site.tmp / "opm/dev/bin/flow").string()));
    BOOST_CHECK(contains(log, "runflow: launch information from runflow's parser"));
    BOOST_CHECK(contains(log, "flow args: CASE.DATA"));
    BOOST_CHECK_EQUAL(readFile(site.tmp / "cases/CASE.ERR"), "flow says hi\n");

    const auto runLog = readFile(site.tmp / "cases/CASE.runflow.log");
    BOOST_CHECK(contains(runLog, "  where:   here\n"));
    BOOST_CHECK(contains(runLog, "  command: " + (site.tmp / "opm/dev/bin/flow").string()
                                     + " CASE.DATA --enable-esmry=true --x=1\n"));
    BOOST_CHECK(contains(runLog, "  status:  3 (Flow's)\n"));
}

BOOST_AUTO_TEST_CASE(InteractiveRunWithMpi)
{
    Site site;
    const auto result = site.run({"-i", "-n", "2", "-v", "flowrc47", (site.tmp / "cases/CASE").string()});
    BOOST_CHECK_EQUAL(result.exitCode, 0);
    BOOST_CHECK((readLines(site.tmp / "mpirun.log") == std::vector<std::string>{
                     "mpirun --bind-to none -np 2 " + (site.tmp / "site/flowrc47").string() + " CASE.DATA"}));
    BOOST_CHECK(contains(result.output, "pythonpath: /user/python")); // flowrc47 has none
}

BOOST_AUTO_TEST_CASE(LauncherNextToFlowAndChecked)
{
    // The RPM layout: a Flow and its own mpirun in one directory, which is not
    // in PATH. runflow uses that mpirun, not PATH's, and checks it is Flow's MPI.
    Site site;
    const auto rpm = site.tmp / "usr/lib64/openmpi/bin";
    script(rpm / "flow",
           "case \"$1\" in\n"
           "  --build-info) printf 'version=2026.04\\nbuild_type=Release\\nmpi=Open MPI v5.0.7\\n'; exit 0;;\n"
           "  --version) echo 'flow 2026.04'; exit 0;;\n"
           "esac\n"
           "echo \"rpm flow args: $*\"\n");
    const auto writeMpirun = [&](const std::string& version) {
        script(rpm / "mpirun",
               "[ \"$1\" = --version ] && { echo '" + version + "'; exit 0; }\n"
               "echo \"rpm mpirun $*\" >> '" + (site.tmp / "rpm-mpirun.log").string() + "'\n"
               "while [ $# -gt 0 ]; do case \"$1\" in /*) break;; esac; shift; done\n"
               "exec \"$@\"\n");
    };
    writeMpirun("mpirun (Open MPI) 5.0.7");
    site.override(R"({ "versions": { "rpm": { "flow": ")" + (rpm / "flow").string() + R"(" } } })");

    const auto result = site.run({"-i", "-n", "2", "-v", "rpm", (site.tmp / "cases/CASE.DATA").string()});
    BOOST_CHECK_EQUAL(result.exitCode, 0);
    BOOST_CHECK(contains(readFile(site.tmp / "rpm-mpirun.log"), "rpm mpirun --bind-to none -np 2 "));
    BOOST_CHECK(!fs::exists(site.tmp / "mpirun.log")); // PATH's mpirun was not used
    BOOST_CHECK(contains(readFile(site.tmp / "cases/CASE.LOG"),
                         "runflow: MPI launcher " + (rpm / "mpirun").string()
                             + " (next to flow; Open MPI 5.0.7, as Flow's)"));

    // Another MPI's launcher is refused before anything runs.
    writeMpirun("HYDRA build details:\n    Version: 4.1.2");
    fs::remove(site.tmp / "rpm-mpirun.log");
    const auto mismatch = site.run({"-i", "-n", "2", "-v", "rpm", (site.tmp / "cases/CASE.DATA").string()});
    BOOST_CHECK_EQUAL(mismatch.exitCode, 1);
    BOOST_CHECK(contains(mismatch.errors, "MPICH 4.1.2"));
    BOOST_CHECK(contains(mismatch.errors, "Open MPI 5.0.7"));
    BOOST_CHECK(!fs::exists(site.tmp / "rpm-mpirun.log"));
}

BOOST_AUTO_TEST_CASE(SubmitsToLsf)
{
    Site site;
    fs::create_directories(site.tmp / "work");
    fs::current_path(site.tmp / "work"); // runflow from another directory
    const auto result = site.run({"-q", "long", "-np", "4", "-o", "--a=1", "../cases/CASE.DATA"});
    BOOST_CHECK_EQUAL(result.exitCode, 0);
    BOOST_CHECK_EQUAL(result.output, "Job <4242> is submitted to queue <long>.\n"); // bsub's own answer

    const auto cases = site.tmp / "cases";
    BOOST_CHECK((readLines(site.lsf / "bsub.args") == std::vector<std::string>{
                     "-n", "4", "-q", "long", "-J", "CASE", "-R", "span[hosts=1]",
                     "-o", "CASE.%J.job.done"})); // bsub runs in the deck's directory
    BOOST_REQUIRE(fs::exists(cases / "CASE.job"));
    BOOST_CHECK_EQUAL(readFile(site.lsf / "bsub.stdin"), readFile(cases / "CASE.job"));
    BOOST_CHECK(!fs::exists(cases / "CASE.LOG")); // written by the job, not by runflow

    // What was run is in the run log next to the deck.
    const auto runLog = readFile(cases / "CASE.runflow.log");
    BOOST_CHECK(contains(runLog, "  where:   LSF\n"));
    BOOST_CHECK(contains(runLog, "bsub -n 4 -q long -J CASE -R 'span[hosts=1]' -o CASE.%J.job.done < CASE.job"));
    BOOST_CHECK(contains(runLog, "  status:  0 (bsub's)\n"));

    // Run the job as LSF would.
    site.env.set("LSB_JOBID", "4242");
    const auto job = captureOutput({"bash", (cases / "CASE.job").string()}, std::chrono::seconds(60));
    BOOST_CHECK_EQUAL(job.exitCode, 0);
    const auto log = readFile(cases / "CASE.LOG");
    BOOST_CHECK(log.starts_with("LSF job 4242 on "));
    BOOST_CHECK(contains(log, "runflow: 4 ranks"));
    BOOST_CHECK(contains(log, "flow args: CASE.DATA --a=1"));
    BOOST_CHECK(contains(readFile(site.tmp / "mpirun.log"),
                         "mpirun --bind-to none -np 4 -x OMP_NUM_THREADS=1 "));
}

BOOST_AUTO_TEST_CASE(CoupledJobReservesTheSlaves)
{
    Site site;
    const auto result = site.run({(site.tmp / "cases/MASTER.DATA").string()});
    BOOST_CHECK_EQUAL(result.exitCode, 0);
    const auto args = readLines(site.lsf / "bsub.args");
    BOOST_REQUIRE(args.size() > 3);
    BOOST_CHECK_EQUAL(args[1], "6"); // bsub -n 6: 1 rank + 4 + 1 for the slaves
    BOOST_CHECK_EQUAL(args[3], "daily"); // default queue from runflow.json
    const auto script = readFile(site.lsf / "bsub.stdin");
    BOOST_CHECK(contains(script, "1 rank + 5 for the slaves (RES-1: 4, RES-2: 1) = 6 slots"));
    BOOST_CHECK(contains(script, "-np 1 -x OMP_NUM_THREADS=1")); // MPI, for one rank too
}

BOOST_AUTO_TEST_CASE(CoupledJobThatCannotFit)
{
    Site site;
    site.override(R"({ "lsf": { "max_slots_per_host": 4 } })");
    const auto result = site.run({(site.tmp / "cases/MASTER.DATA").string()});
    BOOST_CHECK_EQUAL(result.exitCode, 1);
    BOOST_CHECK(contains(result.errors, "6 slots on one host, but a host has 4"));
    BOOST_CHECK(!fs::exists(site.lsf / "bsub.args")); // nothing submitted
    BOOST_CHECK(!fs::exists(site.tmp / "cases/MASTER.job"));
    BOOST_CHECK(!fs::exists(site.tmp / "cases/MASTER.runflow.log")); // nothing run
}

BOOST_AUTO_TEST_CASE(DryRun)
{
    Site site;
    const auto result = site.run({"--dry-run", "-n", "2", (site.tmp / "cases/CASE.DATA").string()});
    BOOST_CHECK_EQUAL(result.exitCode, 0);
    // The exact line system() would run, and the script.
    BOOST_CHECK(contains(result.output, "# dry run: would run\ncd " + (site.tmp / "cases").string()
                                            + " && " + (site.lsf / "bsub").string()
                                            + " -n 2 -q daily -J CASE -R 'span[hosts=1]'"));
    BOOST_CHECK(contains(result.output, "# with CASE.job:\n#!/bin/bash"));
    // The launcher by its absolute path: what was checked is what runs.
    BOOST_CHECK(contains(result.output, "exec " + (site.tmp / "bin/mpirun").string()
                                            + " --bind-to none -np 2"));
    BOOST_CHECK(!fs::exists(site.lsf / "bsub.args"));
    BOOST_CHECK(!fs::exists(site.tmp / "cases/CASE.job"));
    BOOST_CHECK(!fs::exists(site.tmp / "cases/CASE.runflow.log"));

    const auto here = site.run({"--dry-run", "-i", (site.tmp / "cases/CASE.DATA").string()});
    BOOST_CHECK(contains(here.output, "would run here"));
    BOOST_CHECK(!fs::exists(site.tmp / "cases/CASE.LOG"));
}

BOOST_AUTO_TEST_CASE(SettingsOfTheCurrentDirectory)
{
    // runflow.json where runflow runs overrides ~/.runflow.json, and
    // $RUNFLOW_CONFIG overrides both.
    Site site;
    writeFile(site.tmp / "project/runflow.json", R"({ "lsf": { "default_queue": "project" } })");
    fs::current_path(site.tmp / "project");
    const auto deckPath = (site.tmp / "cases/CASE.DATA").string();

    BOOST_CHECK(contains(site.run({"--dry-run", deckPath}).output, "-q project"));
    site.override(R"({ "lsf": { "default_queue": "named" } })");
    BOOST_CHECK(contains(site.run({"--dry-run", deckPath}).output, "-q named"));
}

BOOST_AUTO_TEST_CASE(ExitCodesAsTheLegacyScript)
{
    Site site;
    const auto deckPath = (site.tmp / "cases/CASE.DATA").string();
    BOOST_CHECK_EQUAL(site.run({"--bogus", deckPath}).exitCode, 2);
    BOOST_CHECK_EQUAL(site.run({"-n", "x", deckPath}).exitCode, 2);
    BOOST_CHECK_EQUAL(site.run({}).exitCode, 4);
    BOOST_CHECK_EQUAL(site.run({deckPath, deckPath}).exitCode, 4);
    BOOST_CHECK_EQUAL(site.run({(site.tmp / "cases/NONE.DATA").string()}).exitCode, 4);

    const auto missing = site.run({"-v", "flowrc99", deckPath});
    BOOST_CHECK_EQUAL(missing.exitCode, 1);
    BOOST_CHECK(contains(missing.errors, "!Error, Flow version can not be found."));

    // bsub's status is runflow's, as in the legacy script.
    site.override(R"({ "lsf": { "bsub": "/nonexistent/bsub" } })");
    const auto noLsf = site.run({deckPath});
    BOOST_CHECK_EQUAL(noLsf.exitCode, 127);
    BOOST_CHECK(contains(noLsf.errors, "bsub not found"));
    BOOST_CHECK(contains(readFile(site.tmp / "cases/CASE.runflow.log"), "status:  127 (bsub's)"));
}
