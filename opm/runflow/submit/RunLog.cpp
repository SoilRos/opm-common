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

#include <opm/runflow/submit/RunLog.hpp>

#include <array>
#include <ctime>
#include <fstream>

#include <pwd.h>
#include <unistd.h>

namespace Opm::RunFlow {

std::string formatRunRecord(const RunRecord& record, const std::string& date,
                            const std::string& user, const std::string& host)
{
    return date + "  " + user + "@" + host + "  runflow " + record.runflowVersion + "\n"
        + "  flow:    " + record.flowVersion + "  " + record.flow.string() + "\n"
        + "  where:   " + record.where + "\n"
        + "  command: " + record.command + "\n"
        + "  status:  " + std::to_string(record.exitStatus)
        + (record.where == "here" ? " (Flow's)" : " (bsub's)") + "\n";
}

void appendRunLog(const std::filesystem::path& file, const RunRecord& record)
{
    const std::time_t t = std::time(nullptr);
    std::array<char, 64> date{};
    std::strftime(date.data(), date.size(), "%Y-%m-%d %H:%M:%S %Z", std::localtime(&t));

    std::array<char, 256> host{};
    if (::gethostname(host.data(), host.size() - 1) != 0)
        host = {'?'};
    const passwd* account = ::getpwuid(::getuid());
    const std::string user = account ? account->pw_name : std::to_string(::getuid());

    std::ofstream log(file, std::ios::app);
    log << formatRunRecord(record, date.data(), user, host.data());
}

} // namespace Opm::RunFlow
