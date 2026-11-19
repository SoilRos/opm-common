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

#include <opm/runflow/cli/CommandLine.hpp>

#include <sstream>

#include <getopt.h>
#include <unistd.h>

namespace {

// Restarts getopt's scanning; it keeps its state in globals.
void resetGetopt()
{
#if defined(__APPLE__) || defined(__FreeBSD__)
    optreset = 1;
    optind = 1;
#else
    optind = 0; // glibc: 0 also resets its internal state
#endif
}

enum LongOnly { DryRun = 256 };

int positiveNumber(const std::string& value)
{
    const bool digits = !value.empty() && value.find_first_not_of("0123456789") == std::string::npos;
    if (!digits || value.size() > 6 || std::stoi(value) < 1)
        throw Opm::RunFlow::UsageError("the number of processes must be a positive number, not '"
                                       + value + "'");
    return std::stoi(value);
}

} // namespace

namespace Opm::RunFlow {

CommandLine parseCommandLine(const std::vector<std::string>& args)
{
    // The legacy script: getopt -a --options=q:ilv:n:o:h
    //   --longoptions=queue:,interactive,list,version:,np:,op:,help
    static const option longOptions[] = {
        {"queue", required_argument, nullptr, 'q'},
        {"interactive", no_argument, nullptr, 'i'},
        {"list", no_argument, nullptr, 'l'},
        {"version", required_argument, nullptr, 'v'},
        {"np", required_argument, nullptr, 'n'},
        {"op", required_argument, nullptr, 'o'},
        {"help", no_argument, nullptr, 'h'},
        {"dry-run", no_argument, nullptr, DryRun},
        {nullptr, 0, nullptr, 0},
    };

    std::vector<std::string> copy = args; // getopt permutes argv
    std::vector<char*> argv;
    for (auto& arg : copy)
        argv.push_back(arg.data());
    argv.push_back(nullptr);

    CommandLine line;
    resetGetopt();
    opterr = 0; // messages are ours
    // A leading ':' reports a missing value as ':' instead of '?'.
    const char* shortOptions = ":q:ilv:n:o:h";
    int option = 0;
    while ((option = ::getopt_long_only(static_cast<int>(argv.size() - 1), argv.data(),
                                        shortOptions, longOptions, nullptr)) != -1) {
        const std::string value = optarg ? optarg : "";
        switch (option) {
        case 'q': line.queue = value; break;
        case 'i': line.interactive = true; break;
        case 'l': line.list = true; break;
        case 'v': line.version = value; break;
        case 'n': line.processes = positiveNumber(value); break;
        case 'o': line.flowOptions = value; break;
        case 'h': line.help = true; break;
        case DryRun: line.dryRun = true; break;
        case ':':
            throw UsageError("option '" + std::string(argv[optind - 1]) + "' needs a value");
        default:
            // optopt names an unknown letter in a group such as -iz; 0 for a long option.
            throw UsageError("unknown option '"
                             + (optopt != 0 ? std::string("-") + static_cast<char>(optopt)
                                            : std::string(argv[optind - 1])) + "'");
        }
    }
    for (int i = optind; i < static_cast<int>(argv.size() - 1); ++i)
        line.decks.emplace_back(argv[i]);
    return line;
}

std::vector<std::string> splitFlowOptions(const std::string& options)
{
    std::vector<std::string> words;
    std::istringstream in(options);
    for (std::string word; in >> word;)
        words.push_back(word);
    return words;
}

DeckLocation locateDeck(const std::string& argument)
{
    const auto given = std::filesystem::absolute(argument).lexically_normal();
    DeckLocation location;
    location.workdir = given.parent_path();
    const auto name = given.filename().string();
    location.root = name.substr(0, name.find('.'));
    location.deck = location.root + ".DATA";
    return location;
}

} // namespace Opm::RunFlow
