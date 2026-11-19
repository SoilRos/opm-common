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

#ifndef OPM_RUNFLOW_PYTHON_PATH_HPP
#define OPM_RUNFLOW_PYTHON_PATH_HPP

#include <opm/runflow/versions/Installation.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Opm::RunFlow {

/// The PYTHONPATH that patterns (search.pythonpath) give a version found as
/// `name`, e.g. "/project/res/lib/python3.*/site-packages/{name}" and
/// "flowrc47". For each pattern:
///   1. "{name}" becomes `name` (taken literally, even if it has * or ?);
///   2. the entry is matched against existing directories, as the shell
///      matches *, ? and [...]: one directory is used; none leaves the entry
///      out; several leave it out too, with a warning, since two Pythons'
///      files in one PYTHONPATH would be wrong.
/// Nothing if no entry is left.
std::optional<std::string> expandPythonPath(const std::vector<std::filesystem::path>& patterns,
                                            const std::string& name,
                                            std::vector<std::string>& warnings);

/// Gives `inst` the PYTHONPATH of `patterns`, if it has none of its own
/// (a configured "pythonpath" wins). Its name is the file name it was found
/// as first, e.g. "flowrc47".
void applyDefaultPythonPath(Installation& inst, const std::vector<std::filesystem::path>& patterns,
                            std::vector<std::string>& warnings);

} // namespace Opm::RunFlow

#endif // OPM_RUNFLOW_PYTHON_PATH_HPP
