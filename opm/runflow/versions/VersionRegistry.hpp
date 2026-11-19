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

#ifndef OPM_RUNFLOW_VERSION_REGISTRY_HPP
#define OPM_RUNFLOW_VERSION_REGISTRY_HPP

#include <opm/runflow/versions/Installation.hpp>

#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Opm::RunFlow {

struct VersionsConfig;

/// A version that cannot be found, or that matches several installations.
class VersionError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/// The Flow versions found, and how a user's choice selects one of them.
/// Each installation is described by its binary (describeFlow()) only when
/// needed, and at most once.
class VersionRegistry
{
public:
    /// Asks a Flow binary about itself (describeFlow(), or a stand-in in tests).
    using Describer = std::function<BuildInfo(const std::filesystem::path&)>;

    VersionRegistry(std::vector<Installation> candidates,
                    std::optional<std::string> defaultVersion,
                    Describer describer);

    /// The candidates of `config` and PATH (findCandidates()), described by
    /// describeFlow(). Problems on the way go to warnings().
    static VersionRegistry standard(const VersionsConfig& config);

    /// Asks every installation's binary about itself (once each), as a
    /// listing needs: afterwards, each one's `build` is filled in.
    void describeAll();

    /// Every installation, in the order found. Their `build` is filled in only
    /// for those described so far (describeAll(), resolve(), ...).
    const std::deque<Installation>& installations() const { return installations_; }

    /// The installation a user means by `value`, tried in this order:
    ///   1. a path: a value with a '/', or an existing file;
    ///   2. a name from the configuration;
    ///   3. an executable's file name, e.g. flowrc47;
    ///   4. an OPM version, e.g. 2026.04 (describes every installation).
    /// The first rule that matches decides. Throws VersionError if nothing
    /// matches, or if one rule matches several installations. The result is
    /// described.
    const Installation& resolve(const std::string& value);

    /// The configured default_version; without one, the first flow in PATH.
    /// Throws VersionError if there is none. The result is described.
    const Installation& defaultInstallation();

    /// Whether `inst` is the default installation (false if there is none).
    /// The default is resolved once, so a listing asks this cheaply.
    bool isDefault(const Installation& inst);

    /// Problems found on the way that did not stop the search.
    std::vector<std::string>& warnings() { return warnings_; }

private:
    // Positions in installations_. Invariant: installations are only appended,
    // never removed or reordered, so a position stays valid.
    // Fills in installations_[index].build by asking its binary (describer_),
    // the first time only.
    void describe(std::size_t index);
    std::size_t resolveIndex(const std::string& value);
    std::optional<std::size_t> defaultIndex();
    std::optional<std::size_t> matchOne(const std::string& value, const std::string& what,
                                        const std::function<bool(const Installation&)>& matches) const;

    // A deque, so that references handed out stay valid when a path is added.
    std::deque<Installation> installations_;
    std::vector<bool> described_; // parallel to installations_: describe() has run
    std::optional<std::string> defaultVersion_;
    std::vector<std::filesystem::path> pythonPathPatterns_; // search.pythonpath, for versions given by path
    bool defaultResolved_ = false;            // defaultIndex() has run
    std::optional<std::size_t> defaultIndex_; // its result
    std::string defaultError_;                // why there is none
    Describer describer_;
    std::vector<std::string> warnings_;
};

} // namespace Opm::RunFlow

#endif // OPM_RUNFLOW_VERSION_REGISTRY_HPP
