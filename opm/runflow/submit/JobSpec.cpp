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

#include <opm/runflow/submit/JobSpec.hpp>

#include <algorithm>
#include <cctype>
#include <string_view>

namespace Opm::RunFlow {

std::string shellQuote(const std::string& word)
{
    // Words of only these characters mean the same to the shell unquoted.
    const bool plain = !word.empty() && std::ranges::all_of(word, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c))
            || std::string_view("@%+=:,./-_").find(c) != std::string_view::npos;
    });
    if (plain)
        return word;
    std::string quoted = "'";
    for (const char c : word) {
        if (c == '\'')
            quoted += "'\\''"; // end the quote, a quoted ', start again
        else
            quoted += c;
    }
    return quoted + "'";
}

std::string shellQuote(const std::vector<std::string>& words)
{
    std::string result;
    for (const auto& word : words)
        result += (result.empty() ? "" : " ") + shellQuote(word);
    return result;
}

std::string jobScript(const JobSpec& job, const std::string& scheduler,
                      const std::string& idVariable)
{
    std::string script = "#!/bin/bash\n"
                         "# Written by runflow: " + job.name + "\n"
                         "cd " + shellQuote(job.workdir.string()) + " || exit 1\n";
    for (const auto& env : job.environment) {
        script += "export " + env.name + "=" + shellQuote(env.value);
        if (env.mode == EnvSetting::Mode::Prepend)
            script += "\"${" + env.name + ":+:$" + env.name + "}\"";
        script += "\n";
    }
    if (!job.stdoutFile.empty()) {
        script += "exec > " + shellQuote(job.stdoutFile)
            + (job.stderrFile.empty() ? std::string(" 2>&1") : " 2> " + shellQuote(job.stderrFile))
            + "\n";
    }
    script += "echo \"" + scheduler + " job ${" + idVariable
        + "} on $(hostname), started $(date)\"\n";
    for (const auto& line : job.banner)
        script += "printf '%s\\n' " + shellQuote(line) + "\n";
    script += "exec " + shellQuote(job.command) + "\n";
    return script;
}

std::vector<std::string> applyEnvironment(std::vector<std::string> base,
                                          const std::vector<EnvSetting>& settings)
{
    for (const auto& env : settings) {
        const auto prefix = env.name + "=";
        auto it = std::ranges::find_if(base, [&](const std::string& entry) {
            return entry.starts_with(prefix);
        });
        std::string value = env.value;
        if (env.mode == EnvSetting::Mode::Prepend && it != base.end()
            && it->size() > prefix.size())
            value += ":" + it->substr(prefix.size());
        if (it != base.end())
            *it = prefix + value;
        else
            base.push_back(prefix + value);
    }
    return base;
}

} // namespace Opm::RunFlow
