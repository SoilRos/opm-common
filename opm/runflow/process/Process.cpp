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

#include <opm/runflow/process/Process.hpp>

#include <array>
#include <chrono>
#include <optional>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {

// Closes a file descriptor when it goes out of scope.
struct Fd
{
    int fd = -1;
    Fd() = default;
    explicit Fd(int f) : fd(f) {}
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    ~Fd() { close(); }
    void close()
    {
        if (fd >= 0)
            ::close(fd);
        fd = -1;
    }
};

[[noreturn]] void throwErrno(int error, const std::string& what)
{
    throw std::system_error(error, std::generic_category(), what);
}

void makePipe(Fd& readEnd, Fd& writeEnd)
{
    std::array<int, 2> fds{};
    if (::pipe(fds.data()) != 0)
        throwErrno(errno, "pipe");
    readEnd.fd = fds[0];
    writeEnd.fd = fds[1];
}

// The shell's convention: the exit code, or 128 + the signal that killed it.
int exitStatusOf(int status)
{
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return -1;
}

int waitFor(pid_t pid)
{
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    return status;
}

// Writes all of `data`, also when the descriptor takes it in parts.
void writeAll(int fd, std::string_view data)
{
    while (!data.empty()) {
        const auto n = ::write(fd, data.data(), data.size());
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return; // a closed terminal must not stop the run
        data.remove_prefix(static_cast<std::size_t>(n));
    }
}

// A program's path, searched in `path` (a PATH value) if it has no '/'.
std::string findProgram(const std::string& name, const std::string& path)
{
    if (name.find('/') != std::string::npos)
        return name;
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto end = std::min(path.find(':', start), path.size());
        auto dir = path.substr(start, end - start);
        const auto candidate = (dir.empty() ? std::string(".") : dir) + "/" + name;
        if (::access(candidate.c_str(), X_OK) == 0)
            return candidate;
        start = end + 1;
    }
    return name; // exec fails and reports it
}

std::string pathOf(const std::vector<std::string>& environment)
{
    for (const auto& entry : environment) {
        if (entry.starts_with("PATH="))
            return entry.substr(5);
    }
    return "/usr/bin:/bin";
}

// Ignores SIGPIPE while it exists: a reader that went away (a program that did
// not read its input, a closed terminal) must give a write error, not end
// runflow. Children get the default back (an ignored signal survives exec).
class IgnoreSigpipe
{
public:
    IgnoreSigpipe()
    {
        struct sigaction ignore{};
        ignore.sa_handler = SIG_IGN;
        ::sigaction(SIGPIPE, &ignore, &old_);
    }
    ~IgnoreSigpipe() { ::sigaction(SIGPIPE, &old_, nullptr); }
    IgnoreSigpipe(const IgnoreSigpipe&) = delete;
    IgnoreSigpipe& operator=(const IgnoreSigpipe&) = delete;

private:
    struct sigaction old_{};
};

// The child process of runForeground being waited for; SIGTERM goes to it.
volatile sig_atomic_t foregroundChild = 0;

extern "C" void forwardTerm(int)
{
    if (foregroundChild > 0)
        ::kill(static_cast<pid_t>(foregroundChild), SIGTERM);
}

// Sets signal handling for the duration of runForeground, and restores it.
class ForegroundSignals
{
public:
    explicit ForegroundSignals(pid_t child)
    {
        foregroundChild = child;
        struct sigaction ignore{};
        ignore.sa_handler = SIG_IGN;
        ::sigaction(SIGINT, &ignore, &oldInt_);
        ::sigaction(SIGQUIT, &ignore, &oldQuit_);
        struct sigaction forward{};
        forward.sa_handler = forwardTerm;
        ::sigaction(SIGTERM, &forward, &oldTerm_);
    }
    ~ForegroundSignals()
    {
        ::sigaction(SIGINT, &oldInt_, nullptr);
        ::sigaction(SIGQUIT, &oldQuit_, nullptr);
        ::sigaction(SIGTERM, &oldTerm_, nullptr);
        foregroundChild = 0;
    }
    ForegroundSignals(const ForegroundSignals&) = delete;
    ForegroundSignals& operator=(const ForegroundSignals&) = delete;

private:
    struct sigaction oldInt_{}, oldQuit_{}, oldTerm_{};
};

Fd openOutput(const std::filesystem::path& file)
{
    if (file.empty())
        return Fd{};
    const int fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0)
        throwErrno(errno, "cannot write " + file.string());
    return Fd{fd};
}

} // namespace

namespace Opm::RunFlow {

std::vector<std::string> currentEnvironment()
{
    std::vector<std::string> result;
    for (char** entry = environ; entry && *entry; ++entry)
        result.emplace_back(*entry);
    return result;
}

CapturedOutput captureOutput(const std::vector<std::string>& argv,
                             std::chrono::milliseconds timeout,
                             const std::string& input)
{
    if (argv.empty())
        throw std::invalid_argument("captureOutput: no program given");

    Fd outRead, outWrite, errRead, errWrite, inRead, inWrite;
    makePipe(outRead, outWrite);
    makePipe(errRead, errWrite);
    if (!input.empty())
        makePipe(inRead, inWrite);

    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    sigset_t defaults;
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_setsigdefault(&attributes, &defaults);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGDEF);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (input.empty())
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    else
        posix_spawn_file_actions_adddup2(&actions, inRead.fd, STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, outWrite.fd, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, errWrite.fd, STDERR_FILENO);
    for (const int fd : {outRead.fd, outWrite.fd, errRead.fd, errWrite.fd, inRead.fd, inWrite.fd}) {
        if (fd >= 0)
            posix_spawn_file_actions_addclose(&actions, fd);
    }

    std::vector<char*> args;
    for (const auto& arg : argv)
        args.push_back(const_cast<char*>(arg.c_str())); // posix_spawn does not modify them
    args.push_back(nullptr);

    pid_t pid = 0;
    const int error = ::posix_spawnp(&pid, args[0], &actions, &attributes, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (error != 0)
        throwErrno(error, "cannot run " + argv[0]);
    // The child's ends; reading ends when the child closes its own.
    outWrite.close();
    errWrite.close();
    inRead.close();
    if (inWrite.fd >= 0) // never block on input the child does not read yet
        ::fcntl(inWrite.fd, F_SETFL, O_NONBLOCK);
    const IgnoreSigpipe noSigpipe;

    CapturedOutput result;
    std::string_view toWrite = input;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::array<char, 4096> buffer{};
    while (outRead.fd >= 0 || errRead.fd >= 0) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            ::kill(pid, SIGKILL);
            result.timedOut = true;
            break;
        }
        std::array<pollfd, 3> fds{{{outRead.fd, POLLIN, 0},
                                   {errRead.fd, POLLIN, 0},
                                   {inWrite.fd, POLLOUT, 0}}};
        const int ready = ::poll(fds.data(), fds.size(), static_cast<int>(left.count()));
        if (ready <= 0)
            continue; // interrupted, or the timeout: handled at the top of the loop

        if (inWrite.fd >= 0 && (fds[2].revents & (POLLOUT | POLLERR | POLLHUP))) {
            const auto n = ::write(inWrite.fd, toWrite.data(), toWrite.size());
            if (n > 0)
                toWrite.remove_prefix(static_cast<std::size_t>(n));
            if (n < 0 && errno != EAGAIN && errno != EINTR)
                toWrite = {}; // the child stopped reading
            if (toWrite.empty())
                inWrite.close(); // end of input
        }
        const auto drain = [&](Fd& fd, short revents, std::string& into) {
            if (fd.fd < 0 || !(revents & (POLLIN | POLLHUP | POLLERR)))
                return;
            const auto n = ::read(fd.fd, buffer.data(), buffer.size());
            if (n > 0)
                into.append(buffer.data(), static_cast<std::size_t>(n));
            else if (n == 0 || errno != EINTR)
                fd.close(); // end of output
        };
        drain(outRead, fds[0].revents, result.output);
        drain(errRead, fds[1].revents, result.errors);
    }
    inWrite.close();

    const int status = waitFor(pid);
    result.exitCode = (result.timedOut || !WIFEXITED(status)) ? -1 : WEXITSTATUS(status);
    return result;
}

int runForeground(const ForegroundCommand& command)
{
    if (command.argv.empty())
        throw std::invalid_argument("runForeground: no program given");

    const IgnoreSigpipe noSigpipe; // a closed terminal: the files still get everything
    Fd outFile = openOutput(command.stdoutFile);
    // One file for both: one descriptor (a shared offset), or they overwrite each other.
    Fd errFile = (!command.stderrFile.empty() && command.stderrFile == command.stdoutFile)
        ? Fd{::dup(outFile.fd)}
        : openOutput(command.stderrFile);
    Fd outRead, outWrite, errRead, errWrite;
    makePipe(outRead, outWrite);
    makePipe(errRead, errWrite);

    writeAll(command.terminalOut, command.banner);
    if (outFile.fd >= 0)
        writeAll(outFile.fd, command.banner);

    // Everything the child needs is prepared before fork(): after it, the
    // child only calls async-signal-safe functions.
    const auto program = findProgram(command.argv[0], pathOf(command.environment));
    std::vector<char*> args;
    for (const auto& arg : command.argv)
        args.push_back(const_cast<char*>(arg.c_str()));
    args.push_back(nullptr);
    std::vector<char*> env;
    for (const auto& entry : command.environment)
        env.push_back(const_cast<char*>(entry.c_str()));
    env.push_back(nullptr);
    const auto workdir = command.workdir.string();

    const pid_t pid = ::fork();
    if (pid < 0)
        throwErrno(errno, "fork");
    if (pid == 0) {
        // The child: default signal handling, stdin as ours, stdout and stderr to the pipes.
        ::signal(SIGINT, SIG_DFL);
        ::signal(SIGQUIT, SIG_DFL);
        ::signal(SIGTERM, SIG_DFL);
        ::signal(SIGPIPE, SIG_DFL);
        ::dup2(outWrite.fd, STDOUT_FILENO);
        ::dup2(errWrite.fd, STDERR_FILENO);
        ::close(outRead.fd);
        ::close(errRead.fd);
        ::close(outWrite.fd);
        ::close(errWrite.fd);
        if (!workdir.empty() && ::chdir(workdir.c_str()) != 0) {
            constexpr std::string_view message = "runflow: cannot change to the run's directory\n";
            [[maybe_unused]] auto n = ::write(STDERR_FILENO, message.data(), message.size());
            ::_exit(127);
        }
        ::execve(program.c_str(), args.data(), env.data());
        constexpr std::string_view message = "runflow: cannot run the program\n";
        [[maybe_unused]] auto n = ::write(STDERR_FILENO, message.data(), message.size());
        ::_exit(127);
    }

    ForegroundSignals signals(pid);
    outWrite.close();
    errWrite.close();

    // Copy both outputs as they come, until the program closes them. Once the
    // program itself has exited, what is left is read for at most `grace`: a
    // process it left behind may hold the pipes open, and must not hold runflow.
    constexpr auto grace = std::chrono::seconds(2);
    std::optional<int> status;
    std::chrono::steady_clock::time_point stopReading{};
    std::array<char, 8192> buffer{};
    while (outRead.fd >= 0 || errRead.fd >= 0) {
        if (!status) {
            int raw = 0;
            if (::waitpid(pid, &raw, WNOHANG) == pid) {
                status = raw;
                stopReading = std::chrono::steady_clock::now() + grace;
            }
        } else if (std::chrono::steady_clock::now() >= stopReading) {
            break;
        }
        std::array<pollfd, 2> fds{{{outRead.fd, POLLIN, 0}, {errRead.fd, POLLIN, 0}}};
        if (::poll(fds.data(), fds.size(), 200) <= 0)
            continue; // interrupted, or nothing yet: check the program again
        const auto copy = [&](Fd& from, short revents, int terminal, const Fd& file) {
            if (from.fd < 0 || !(revents & (POLLIN | POLLHUP | POLLERR)))
                return;
            const auto n = ::read(from.fd, buffer.data(), buffer.size());
            if (n < 0 && errno == EINTR)
                return;
            if (n <= 0) {
                from.close();
                return;
            }
            const std::string_view data(buffer.data(), static_cast<std::size_t>(n));
            writeAll(terminal, data);
            if (file.fd >= 0)
                writeAll(file.fd, data);
        };
        copy(outRead, fds[0].revents, command.terminalOut, outFile);
        copy(errRead, fds[1].revents, command.terminalErr, errFile);
    }
    return exitStatusOf(status ? *status : waitFor(pid));
}

} // namespace Opm::RunFlow
