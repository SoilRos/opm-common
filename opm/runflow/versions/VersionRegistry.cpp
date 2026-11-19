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

#include <opm/runflow/versions/VersionRegistry.hpp>

#include <opm/runflow/config/ConfigFiles.hpp>
#include <opm/runflow/versions/BuildInfo.hpp>
#include <opm/runflow/versions/Candidates.hpp>
#include <opm/runflow/versions/PythonPath.hpp>
#include <opm/runflow/versions/VersionsConfig.hpp>

#include <algorithm>
#include <system_error>
#include <utility>

namespace {

namespace fs = std::filesystem;

bool contains(const std::vector<std::string>& list, const std::string& value)
{
    return std::ranges::find(list, value) != list.end();
}

} // namespace

namespace Opm::RunFlow {

VersionRegistry::VersionRegistry(std::vector<Installation> candidates,
                                 std::optional<std::string> defaultVersion,
                                 Describer describer)
    : installations_(std::make_move_iterator(candidates.begin()),
                     std::make_move_iterator(candidates.end()))
    , described_(installations_.size(), false)
    , defaultVersion_(std::move(defaultVersion))
    , describer_(std::move(describer))
{}

VersionRegistry VersionRegistry::standard(const VersionsConfig& config)
{
    std::vector<std::string> warnings;
    auto candidates = findCandidates(config, pathFromEnvironment(), warnings);
    VersionRegistry registry(std::move(candidates), config.defaultVersion, describeFlow);
    registry.pythonPathPatterns_ = config.search.pythonPath;
    registry.warnings_ = std::move(warnings);
    return registry;
}

void VersionRegistry::describe(std::size_t index)
{
    if (described_[index])
        return; // each binary is asked at most once
    described_[index] = true;
    if (describer_)
        installations_[index].build = describer_(installations_[index].flow);
}

void VersionRegistry::describeAll()
{
    for (std::size_t i = 0; i < installations_.size(); ++i)
        describe(i);
}

std::optional<std::size_t>
VersionRegistry::matchOne(const std::string& value, const std::string& what,
                          const std::function<bool(const Installation&)>& matches) const
{
    std::vector<std::size_t> found;
    for (std::size_t i = 0; i < installations_.size(); ++i) {
        if (matches(installations_[i]))
            found.push_back(i);
    }
    if (found.size() > 1) {
        std::string message = "Flow version \"" + value + "\" is ambiguous: it is the "
            + what + " of";
        for (const auto i : found)
            message += "\n  " + installations_[i].flow.string();
        message += "\nGive a path, or name one in runflow.json.";
        throw VersionError(message);
    }
    if (found.empty())
        return std::nullopt;
    return found.front();
}

std::size_t VersionRegistry::resolveIndex(const std::string& value)
{
    if (value.empty())
        throw VersionError("Flow version not found: the version is empty");

    // 1. A path (as the legacy runflow: an existing file is a path).
    std::error_code ec;
    if (value.find('/') != std::string::npos || value.starts_with("~")
        || fs::is_regular_file(value, ec)) {
        const auto given = resolveConfigPath(value, fs::current_path());
        if (!isExecutableFile(given))
            throw VersionError("Flow version not found: " + value + " is not an executable");
        Installation inst;
        inst.flow = fs::canonical(given, ec);
        if (ec)
            throw VersionError("Flow version not found: " + value + ": " + ec.message());
        inst.paths.push_back(given);
        inst.executableNames.push_back(given.filename().string());
        inst.foundBy.push_back("given path");
        // The one search by executable: a path may name an installation found before.
        if (auto it = std::ranges::find(installations_, inst.flow, &Installation::flow);
            it != installations_.end()) {
            merge(*it, inst);
            return static_cast<std::size_t>(std::distance(installations_.begin(), it));
        }
        applyDefaultPythonPath(inst, pythonPathPatterns_, warnings_);
        installations_.push_back(std::move(inst));
        described_.push_back(false);
        return installations_.size() - 1;
    }

    // 2. and 3.: names, without running anything.
    if (const auto i = matchOne(value, "name",
            [&](const Installation& inst) { return contains(inst.names, value); }))
        return *i;
    if (const auto i = matchOne(value, "executable name",
            [&](const Installation& inst) { return contains(inst.executableNames, value); }))
        return *i;

    // 4. An OPM version: every installation must be described.
    describeAll();
    if (const auto i = matchOne(value, "OPM version",
            [&](const Installation& inst) { return inst.build.opmVersion == value; }))
        return *i;

    throw VersionError("Flow version not found: " + value
                       + " (flowversions, or runflow -l, lists the versions found)");
}

const Installation& VersionRegistry::resolve(const std::string& value)
{
    const auto index = resolveIndex(value);
    describe(index);
    return installations_[index];
}

std::optional<std::size_t> VersionRegistry::defaultIndex()
{
    // Resolved once; installations are only appended, so the index stays valid.
    if (!defaultResolved_) {
        defaultResolved_ = true;
        try {
            if (defaultVersion_) {
                defaultIndex_ = resolveIndex(*defaultVersion_);
            } else {
                for (std::size_t i = 0; i < installations_.size() && !defaultIndex_; ++i) {
                    if (contains(installations_[i].foundBy, "PATH"))
                        defaultIndex_ = i;
                }
                if (!defaultIndex_)
                    defaultError_ = "No default Flow version: there is no flow in PATH, "
                                    "and default_version is not set in runflow.json";
            }
        } catch (const VersionError& e) {
            defaultError_ = e.what();
        }
    }
    return defaultIndex_;
}

const Installation& VersionRegistry::defaultInstallation()
{
    const auto index = defaultIndex();
    if (!index)
        throw VersionError(defaultError_);
    describe(*index);
    return installations_[*index];
}

bool VersionRegistry::isDefault(const Installation& inst)
{
    const auto index = defaultIndex();
    return index && installations_[*index].flow == inst.flow; // the identity
}

} // namespace Opm::RunFlow
