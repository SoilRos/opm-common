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

// Tests of how runflow runs and submits (opm/runflow/submit) and of running
// programs (opm/runflow/process). bsub is replaced by a shell script that
// records its arguments and standard input; the job script it got is then
// run, as LSF would run it.

#define BOOST_TEST_MODULE RunFlowSubmitTests
#include <boost/test/unit_test.hpp>

#include <opm/json/JsonObject.hpp>
#include <opm/runflow/process/Process.hpp>
#include <opm/runflow/submit/JobSpec.hpp>
#include <opm/runflow/submit/Local.hpp>
#include <opm/runflow/submit/Lsf.hpp>
#include <opm/runflow/submit/RunLog.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
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

void writeFile(const fs::path& file, const std::string& content)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file) << content;
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

// A stand-in for bsub: records its arguments (one per line) and its standard
// input in `dir`, and answers as bsub does.
fs::path fakeBsub(const fs::path& dir)
{
    const auto d = dir.string();
    return script(dir / "bsub",
        "printf '%s\\n' \"$@\" > '" + d + "/bsub.args'\n"
        "cat > '" + d + "/bsub.stdin'\n"
        "pwd > '" + d + "/bsub.cwd'\n"
        "echo 'Job <4242> is submitted to queue <daily>.'\n");
}

// A coupled run as runflow makes it: 1 master rank + 5 slave slots.
JobSpec coupledJob(const fs::path& workdir)
{
    JobSpec job;
    job.command = {"sh", "-c", "echo out; echo err >&2; echo \"$PYTHONPATH\"; pwd; exit 3"};
    job.workdir = workdir;
    job.environment = {{"OMP_NUM_THREADS", "1", EnvSetting::Mode::Set},
                       {"PYTHONPATH", "/new lib", EnvSetting::Mode::Prepend}};
    job.name = "CASE";
    job.queue = "daily";
    job.slots = 6;
    job.stdoutFile = "CASE.LOG";
    job.stderrFile = "CASE.ERR";
    job.banner = {"runflow: flow 2026.04, 1 rank + 5 slave slots"};
    job.scriptFile = "CASE.job";
    job.report = "CASE.{id}.job.done";
    return job;
}

// Sets an environment variable for one test.
struct ScopedEnv
{
    std::string name;
    ScopedEnv(const std::string& n, const std::string& value) : name(n) { ::setenv(n.c_str(), value.c_str(), 1); }
    ~ScopedEnv() { ::unsetenv(name.c_str()); }
};

} // namespace

// ---------------------------------------------------------------- JobSpec

BOOST_AUTO_TEST_CASE(ShellQuoteSurvivesTheShell)
{
    BOOST_CHECK_EQUAL(shellQuote("span[hosts=1]"), "'span[hosts=1]'");
    BOOST_CHECK_EQUAL(shellQuote("--enable-esmry=true"), "--enable-esmry=true");
    BOOST_CHECK_EQUAL(shellQuote("/a/b.DATA"), "/a/b.DATA");

    const std::vector<std::string> words{"a b", "it's", "$HOME", "", "*", "back\\slash", "new\nline", "\"q\""};
    const auto result = captureOutput({"sh", "-c", "printf '%s|' " + shellQuote(words)},
                                      std::chrono::seconds(10));
    std::string expected;
    for (const auto& word : words)
        expected += word + "|";
    BOOST_CHECK_EQUAL(result.output, expected);
}

BOOST_AUTO_TEST_CASE(ApplyEnvironment)
{
    const std::vector<std::string> base{"PATH=/bin", "PYTHONPATH=/old", "EMPTY="};
    const auto env = applyEnvironment(base, {{"PYTHONPATH", "/new", EnvSetting::Mode::Prepend},
                                             {"EMPTY", "/x", EnvSetting::Mode::Prepend},
                                             {"PATH", "/usr/bin", EnvSetting::Mode::Set},
                                             {"NEW", "1", EnvSetting::Mode::Prepend}});
    BOOST_CHECK((env == std::vector<std::string>{"PATH=/usr/bin", "PYTHONPATH=/new:/old",
                                                 "EMPTY=/x", "NEW=1"}));
}

BOOST_AUTO_TEST_CASE(JobScriptRunsTheJob)
{
    TempDir tmp;
    const auto workdir = tmp / "run dir"; // a space, to test the quoting
    fs::create_directories(workdir);
    const auto job = coupledJob(workdir);
    const ScopedEnv jobId("LSB_JOBID", "77");
    const ScopedEnv python("PYTHONPATH", "/old");

    const auto result = captureOutput({"bash", "-s"}, std::chrono::seconds(20), lsfJobScript(job));
    BOOST_CHECK_EQUAL(result.exitCode, 3); // the command's
    BOOST_CHECK(result.output.empty());    // everything went to the files

    const auto log = readLines(workdir / "CASE.LOG");
    BOOST_REQUIRE_EQUAL(log.size(), 5U);
    BOOST_CHECK(log[0].starts_with("LSF job 77 on "));
    BOOST_CHECK_EQUAL(log[1], job.banner[0]);
    BOOST_CHECK_EQUAL(log[2], "out");
    BOOST_CHECK_EQUAL(log[3], "/new lib:/old");
    BOOST_CHECK_EQUAL(log[4], workdir.string());
    BOOST_CHECK_EQUAL(readFile(workdir / "CASE.ERR"), "err\n");
}

BOOST_AUTO_TEST_CASE(JobScriptWithOneLogFile)
{
    TempDir tmp;
    auto job = coupledJob(tmp.path);
    job.stderrFile.clear(); // errors go with the output: "2>&1"
    const auto result = captureOutput({"bash", "-s"}, std::chrono::seconds(20), lsfJobScript(job));
    BOOST_CHECK_EQUAL(result.exitCode, 3);
    const auto log = readFile(tmp / "CASE.LOG");
    BOOST_CHECK(contains(log, "out\n") && contains(log, "err\n"));
}

// --------------------------------------------------------------- Process

BOOST_AUTO_TEST_CASE(CaptureWithInputAndErrors)
{
    const auto echoed = captureOutput({"cat"}, std::chrono::seconds(10), "line 1\nline 2\n");
    BOOST_CHECK_EQUAL(echoed.output, "line 1\nline 2\n");

    const auto errors = captureOutput({"sh", "-c", "echo bad >&2; exit 4"}, std::chrono::seconds(10));
    BOOST_CHECK_EQUAL(errors.errors, "bad\n");
    BOOST_CHECK_EQUAL(errors.exitCode, 4);

    // A program that does not read its input: no SIGPIPE for runflow.
    const auto ignored = captureOutput({"true"}, std::chrono::seconds(10), std::string(1 << 20, 'x'));
    BOOST_CHECK_EQUAL(ignored.exitCode, 0);
}

BOOST_AUTO_TEST_CASE(ForegroundExitStatus)
{
    TempDir tmp;
    const int devnull = ::open("/dev/null", O_WRONLY);
    ForegroundCommand command;
    command.environment = currentEnvironment();
    command.workdir = tmp.path;
    command.terminalOut = command.terminalErr = devnull;

    command.argv = {"sh", "-c", "kill -TERM $$"};
    BOOST_CHECK_EQUAL(runForeground(command), 128 + 15);
    command.argv = {"no-such-program-anywhere"};
    BOOST_CHECK_EQUAL(runForeground(command), 127);
    ::close(devnull);
}

BOOST_AUTO_TEST_CASE(ForegroundSignals)
{
    TempDir tmp;
    const int devnull = ::open("/dev/null", O_WRONLY);
    ForegroundCommand command;
    command.environment = currentEnvironment();
    command.workdir = tmp.path;
    command.terminalOut = command.terminalErr = devnull;

    // Ctrl+C reaches runflow too: it waits for the program, which decides.
    command.argv = {"sh", "-c", "kill -INT $PPID; sleep 0.2; exit 5"};
    BOOST_CHECK_EQUAL(runForeground(command), 5); // and this test is still running

    // SIGTERM to runflow is passed on to the program.
    const auto start = std::chrono::steady_clock::now();
    command.argv = {"sh", "-c", "kill -TERM $PPID; sleep 30 & wait"};
    BOOST_CHECK_EQUAL(runForeground(command), 128 + 15);
    BOOST_CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(10));
    ::close(devnull);
}

BOOST_AUTO_TEST_CASE(ForegroundKeepsWritingFilesWhenTheTerminalCloses)
{
    TempDir tmp;
    int fds[2];
    BOOST_REQUIRE(::pipe(fds) == 0);
    ::close(fds[0]); // nobody reads the "terminal"

    ForegroundCommand command;
    command.argv = {"sh", "-c", "echo one; echo two"};
    command.environment = currentEnvironment();
    command.workdir = tmp.path;
    command.stdoutFile = tmp / "out.log";
    command.terminalOut = command.terminalErr = fds[1];
    BOOST_CHECK_EQUAL(runForeground(command), 0);
    BOOST_CHECK_EQUAL(readFile(tmp / "out.log"), "one\ntwo\n");
    ::close(fds[1]);
}


// ------------------------------------------------------------ local runs

BOOST_AUTO_TEST_CASE(LocalRunWritesTerminalAndFiles)
{
    TempDir tmp;
    const auto terminalOut = ::open((tmp / "terminal.out").c_str(), O_WRONLY | O_CREAT, 0600);
    const auto terminalErr = ::open((tmp / "terminal.err").c_str(), O_WRONLY | O_CREAT, 0600);
    const ScopedEnv python("PYTHONPATH", "/old");

    const int status = runLocal(coupledJob(tmp.path), 8, terminalOut, terminalErr);
    ::close(terminalOut);
    ::close(terminalErr);
    BOOST_CHECK_EQUAL(status, 3);

    const auto log = readLines(tmp / "CASE.LOG");
    BOOST_REQUIRE_EQUAL(log.size(), 5U);
    BOOST_CHECK(log[0].starts_with("local run on "));
    BOOST_CHECK_EQUAL(log[2], "out");
    BOOST_CHECK_EQUAL(log[3], "/new lib:/old");
    BOOST_CHECK_EQUAL(log[4], tmp.path.string());
    BOOST_CHECK_EQUAL(readFile(tmp / "CASE.ERR"), "err\n");
    BOOST_CHECK_EQUAL(readFile(tmp / "terminal.out"), readFile(tmp / "CASE.LOG"));
    BOOST_CHECK_EQUAL(readFile(tmp / "terminal.err"), "err\n");
    BOOST_CHECK(!fs::exists(tmp / "CASE.job")); // nothing is submitted
}

BOOST_AUTO_TEST_CASE(LocalRunNeedsTheSlots)
{
    TempDir tmp;
    BOOST_CHECK_THROW(checkLocalSlots(coupledJob(tmp.path), 4), SubmitError); // 6 > 4
    try {
        runLocal(coupledJob(tmp.path), 4);
        BOOST_FAIL("no error");
    } catch (const SubmitError& e) {
        BOOST_CHECK(contains(e.what(), "6") && contains(e.what(), "4"));
    }
    BOOST_CHECK(!fs::exists(tmp / "CASE.LOG")); // refused before anything ran

    const auto described = describeLocal(coupledJob(tmp.path));
    BOOST_CHECK(contains(described, "export PYTHONPATH='/new lib'"));
    BOOST_CHECK(contains(described, "# output also to CASE.LOG and CASE.ERR"));
}

// ------------------------------------------------------------------- LSF

BOOST_AUTO_TEST_CASE(LsfCommandLine)
{
    TempDir tmp;
    LsfSettings settings;
    settings.bsub = "/global/bin/bsub";
    settings.bsubOptions = {"-P", "reservoir"};
    const auto command = lsfCommand(coupledJob(tmp / "a case"), settings);
    BOOST_CHECK_EQUAL(command, "cd '" + (tmp / "a case").string() + "' && /global/bin/bsub -n 6 -q daily -J CASE"
                               " -R 'span[hosts=1]' -o CASE.%J.job.done -P reservoir < CASE.job");

    // No queue anywhere: LSF's default; one in the settings: that one.
    auto job = coupledJob(tmp.path);
    job.queue.clear();
    job.singleHost = false;
    job.report.clear();
    BOOST_CHECK(contains(lsfCommand(job, LsfSettings{}), "&& bsub -n 6 -J CASE < CASE.job"));
    settings.defaultQueue = "normal";
    BOOST_CHECK(contains(lsfCommand(job, settings), "-q normal"));

    // Too many slots for one host is refused.
    job.singleHost = true;
    settings.maxSlotsPerHost = 4;
    BOOST_CHECK_THROW(lsfCommand(job, settings), SubmitError);
}

BOOST_AUTO_TEST_CASE(SubmitsThroughSystem)
{
    TempDir tmp;
    LsfSettings settings;
    settings.bsub = fakeBsub(tmp / "lsf").string();
    const auto job = coupledJob(tmp / "case");
    fs::create_directories(job.workdir);

    BOOST_CHECK_EQUAL(submitToLsf(job, settings), 0);
    BOOST_CHECK((readLines(tmp / "lsf/bsub.args") == std::vector<std::string>{
                     "-n", "6", "-q", "daily", "-J", "CASE", "-R", "span[hosts=1]",
                     "-o", "CASE.%J.job.done"}));
    BOOST_CHECK_EQUAL(readFile(tmp / "lsf/bsub.cwd"), job.workdir.string() + "\n"); // in the deck's directory
    BOOST_CHECK_EQUAL(readFile(tmp / "lsf/bsub.stdin"), readFile(job.workdir / "CASE.job"));

    // Run the job as LSF would.
    const ScopedEnv jobId("LSB_JOBID", "4242");
    const auto result = captureOutput({"bash", (job.workdir / "CASE.job").string()}, std::chrono::seconds(20));
    BOOST_CHECK_EQUAL(result.exitCode, 3);
    BOOST_CHECK(readFile(job.workdir / "CASE.LOG").starts_with("LSF job 4242 on "));
}

BOOST_AUTO_TEST_CASE(SubmitReturnsBsubsStatus)
{
    TempDir tmp;
    LsfSettings settings;
    settings.bsub = script(tmp / "lsf/bsub",
        "cat > /dev/null\necho 'Bad queue name. Job not submitted.' >&2\nexit 255\n").string();
    BOOST_CHECK_EQUAL(submitToLsf(coupledJob(tmp.path), settings), 255);
    settings.bsub = (tmp / "nowhere/bsub").string();
    BOOST_CHECK_EQUAL(submitToLsf(coupledJob(tmp.path), settings), 127); // not found
}

BOOST_AUTO_TEST_CASE(LsfSettingsMergeByName)
{
    LsfSettings settings;
    std::vector<std::string> warnings;
    settings.merge(Json::JsonObject(std::string(R"({
        "bsub": "/global/bin/bsub", "default_queue": "daily",
        "bsub_options": ["-P", "x"], "max_slots_per_host": 128, "future": true,
        "launcher": "/usr/lib64/openmpi/bin/mpirun" })")), "site.json", warnings);
    settings.merge(Json::JsonObject(std::string(R"({ "bsub_options": ["-P", "y"],
        "launcher_options": ["@inherit", "--mca", "btl_tcp_if_include", "lo"] })")), "user.json", warnings);
    BOOST_CHECK_EQUAL(settings.bsub, "/global/bin/bsub");                     // kept
    BOOST_CHECK((settings.bsubOptions == std::vector<std::string>{"-P", "y"}));   // list replaced
    BOOST_CHECK_EQUAL(settings.launcher, "/usr/lib64/openmpi/bin/mpirun");      // kept
    BOOST_CHECK_EQUAL(settings.launcherOptions.front(), "--bind-to");          // the default, inherited
    BOOST_CHECK_EQUAL(settings.launcherOptions.back(), "lo");
    BOOST_CHECK_EQUAL(settings.maxSlotsPerHost.value(), 128);
    BOOST_CHECK_EQUAL(warnings.size(), 1U);
    BOOST_CHECK_THROW(settings.merge(Json::JsonObject(std::string(R"({"bsub_options": "-P x"})")),
                                     "r.json", warnings), std::runtime_error);
    BOOST_CHECK_THROW(settings.merge(Json::JsonObject(std::string(R"({"max_slots_per_host": 0})")),
                                     "r.json", warnings), std::runtime_error);
}

// --------------------------------------------------------------- run log

BOOST_AUTO_TEST_CASE(RunLog)
{
    RunRecord record{"2026.10 (8d562c844)", "2026.04 Release", "/opt/opm/bin/flow", "LSF",
                     "cd /data && bsub -n 6 < CASE.job", 0};
    BOOST_CHECK_EQUAL(formatRunRecord(record, "2026-11-24 14:02:11 CET", "ana", "myhost"),
                      "2026-11-24 14:02:11 CET  ana@myhost  runflow 2026.10 (8d562c844)\n"
                      "  flow:    2026.04 Release  /opt/opm/bin/flow\n"
                      "  where:   LSF\n"
                      "  command: cd /data && bsub -n 6 < CASE.job\n"
                      "  status:  0 (bsub's)\n");

    TempDir tmp;
    appendRunLog(tmp / "CASE.runflow.log", record);
    record.where = "here";
    record.exitStatus = 3;
    appendRunLog(tmp / "CASE.runflow.log", record);
    const auto log = readFile(tmp / "CASE.runflow.log");
    BOOST_CHECK(contains(log, "status:  0 (bsub's)") && contains(log, "status:  3 (Flow's)")); // appended
    BOOST_CHECK_NO_THROW(appendRunLog(tmp / "no/such/dir/x.log", record)); // only records
}
