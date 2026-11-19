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

#include <opm/runflow/versions/PythonPath.hpp>

#include <opm/runflow/versions/Candidates.hpp>

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace {

// `text` with the characters glob() treats specially escaped, so that a
// name is matched as it is.
std::string escapeGlob(const std::string& text)
{
    std::string escaped;
    for (const char c : text) {
        if (c == '*' || c == '?' || c == '[' || c == ']' || c == '\\')
            escaped += '\\';
        escaped += c;
    }
    return escaped;
}

} // namespace

namespace Opm::RunFlow {

std::optional<std::string> expandPythonPath(const std::vector<std::filesystem::path>& patterns,
                                            const std::string& name,
                                            std::vector<std::string>& warnings)
{
    std::string result;
    for (const auto& pattern : patterns) {
        auto entry = pattern.string();
        for (auto pos = entry.find("{name}"); pos != std::string::npos;
             pos = entry.find("{name}", pos)) {
            const auto escaped = escapeGlob(name);
            entry.replace(pos, 6, escaped);
            pos += escaped.size();
        }
        std::vector<std::filesystem::path> dirs;
        for (const auto& match : globMatches(entry)) {
            std::error_code ec;
            if (std::filesystem::is_directory(match, ec))
                dirs.push_back(match);
        }
        if (dirs.size() > 1) {
            std::string message = "search.pythonpath: for " + name + ", " + entry + " matches";
            for (const auto& dir : dirs)
                message += " " + dir.string();
            warnings.push_back(message + "; left out");
        } else if (dirs.size() == 1) {
            result += (result.empty() ? "" : ":") + dirs.front().string();
        }
    }
    if (result.empty())
        return std::nullopt;
    return result;
}

void applyDefaultPythonPath(Installation& inst, const std::vector<std::filesystem::path>& patterns,
                            std::vector<std::string>& warnings)
{
    if (inst.pythonPath || patterns.empty() || inst.executableNames.empty())
        return;
    inst.pythonPath = expandPythonPath(patterns, inst.executableNames.front(), warnings);
}

} // namespace Opm::RunFlow
