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

#include <opm/runflow/launch/LaunchInfo.hpp>

#include <opm/json/JsonObject.hpp>

#include <opm/input/eclipse/Deck/Deck.hpp>
#include <opm/input/eclipse/Deck/DeckKeyword.hpp>
#include <opm/input/eclipse/Deck/DeckRecord.hpp>
#include <opm/input/eclipse/Deck/SectionType.hpp>
#include <opm/input/eclipse/Parser/ErrorGuard.hpp>
#include <opm/input/eclipse/Parser/InputErrorAction.hpp>
#include <opm/input/eclipse/Parser/ParseContext.hpp>
#include <opm/input/eclipse/Parser/Parser.hpp>
#include <opm/input/eclipse/Parser/ParserKeywords/S.hpp>

#include <memory>
#include <numeric>
#include <utility>

namespace {

namespace fs = std::filesystem;

// Flow's default parse settings (opm-simulators, setupParseContext() without
// strict parsing): a deck Flow reads, this reads too. One difference: a
// missing include file throws instead of ending the program (EXIT1), since
// runflow reads decks in its own process; it is an error all the same.
Opm::ParseContext flowParseContext()
{
    Opm::ParseContext context(std::vector<std::pair<std::string, Opm::InputErrorAction>>{
        {Opm::ParseContext::PARSE_RANDOM_SLASH, Opm::InputErrorAction::IGNORE},
        {Opm::ParseContext::PARSE_MISSING_DIMS_KEYWORD, Opm::InputErrorAction::WARN},
        {Opm::ParseContext::SUMMARY_UNKNOWN_WELL, Opm::InputErrorAction::WARN},
        {Opm::ParseContext::SUMMARY_UNKNOWN_GROUP, Opm::InputErrorAction::WARN}});
    context.update(Opm::ParseContext::PARSE_MISSING_INCLUDE, Opm::InputErrorAction::THROW_EXCEPTION);
    return context;
}

// Parses `deck`, reading only `sections` if not empty. Errors become a
// LaunchError; ErrorGuard would otherwise end the program when destroyed.
Opm::Deck parse(const fs::path& deck, const std::vector<Opm::Ecl::SectionType>& sections)
{
    const auto context = flowParseContext();
    Opm::ErrorGuard errors;
    std::string failure;
    std::unique_ptr<Opm::Deck> result;
    try {
        result = std::make_unique<Opm::Deck>(Opm::Parser{}.parseFile(deck, context, errors, sections));
    } catch (const std::exception& e) {
        failure = e.what();
    }
    if (errors)
        failure = errors.formattedErrors();
    errors.clear();
    if (!failure.empty() || !result)
        throw Opm::RunFlow::LaunchError("cannot read " + deck.string() + ": " + failure);
    return std::move(*result);
}

} // namespace

namespace Opm::RunFlow {

int LaunchInfo::slaveProcesses() const
{
    return std::accumulate(slaves.begin(), slaves.end(), 0,
                           [](int sum, const SlaveInfo& slave) { return sum + slave.processes; });
}

std::string LaunchInfo::toJson() const
{
    Json::JsonObject root;
    root.add_item("schema", launchInfoSchema);
    auto list = root.add_array("slaves");
    for (const auto& slave : slaves) {
        auto object = list.add_object();
        object.add_item("name", slave.name);
        object.add_item("deck", slave.deck);
        object.add_item("directory", slave.directory);
        object.add_item("num_pe", slave.processes);
    }
    return root.dump();
}

LaunchInfo LaunchInfo::fromJson(const std::string& text)
{
    std::unique_ptr<Json::JsonObject> json; // JsonObject must not be copied
    try {
        json = std::make_unique<Json::JsonObject>(text);
    } catch (const std::invalid_argument&) {
        throw LaunchError("launch information is not valid JSON");
    }
    const auto require = [](bool ok, const std::string& what) {
        if (!ok)
            throw LaunchError("launch information: " + what);
    };
    require(json->is_object() && json->has_item("schema") && json->get_item("schema").is_number(),
            "\"schema\" is missing");
    const int schema = json->get_int("schema");
    require(schema >= 1 && schema <= launchInfoSchema,
            "schema " + std::to_string(schema) + " is not supported (up to "
                + std::to_string(launchInfoSchema) + "): this runflow is older than the Flow version");

    LaunchInfo info;
    if (!json->has_item("slaves"))
        return info;
    const auto list = json->get_item("slaves");
    require(list.is_array(), "\"slaves\" must be a list");
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto item = list.get_array_item(i);
        require(item.is_object() && item.has_item("name") && item.has_item("num_pe")
                    && item.get_item("num_pe").is_number(),
                "a slave needs \"name\" and \"num_pe\"");
        SlaveInfo slave;
        slave.name = item.get_string("name");
        slave.processes = item.get_int("num_pe");
        if (item.has_item("deck"))
            slave.deck = item.get_string("deck");
        if (item.has_item("directory"))
            slave.directory = item.get_string("directory");
        require(slave.processes >= 1, "slave " + slave.name + ": num_pe must be positive");
        info.slaves.push_back(std::move(slave));
    }
    return info;
}

LaunchInfo readLaunchInfo(const fs::path& deckFile)
{
    // SLAVES is in SCHEDULE; RUNSPEC is always needed. Skipping the other
    // sections avoids reading large grid files. The parser can do that only
    // when the section keywords are in the deck itself, in the usual order,
    // with the mandatory PROPS and SOLUTION; otherwise all is read.
    const auto deck = [&] {
        try {
            return parse(deckFile, {Ecl::SectionType::RUNSPEC, Ecl::SectionType::SCHEDULE});
        } catch (const LaunchError&) {
            return parse(deckFile, {});
        }
    }();

    using Kw = ParserKeywords::SLAVES;
    const auto keywords = deck.getKeywordList(Kw::keywordName);
    if (keywords.size() > 1) {
        throw LaunchError(deckFile.string() + ": the SLAVES keyword can only be given once "
                          "in the schedule");
    }

    LaunchInfo info;
    for (const auto* keyword : keywords) {
        for (const auto& record : *keyword) {
            SlaveInfo slave;
            slave.name = record.getItem<Kw::SLAVE_RESERVOIR>().getTrimmedString(0);
            slave.deck = record.getItem<Kw::SLAVE_ECLBASE>().getTrimmedString(0);
            slave.directory = record.getItem<Kw::DIRECTORY>().getTrimmedString(0);
            slave.processes = record.getItem<Kw::NUM_PE>().get<int>(0);
            if (slave.processes <= 0) {
                throw LaunchError(deckFile.string() + ": SLAVES " + slave.name
                                  + ": the number of processors must be positive, not "
                                  + std::to_string(slave.processes));
            }
            // A name given again redefines the slave, as in Flow.
            std::erase_if(info.slaves, [&](const SlaveInfo& s) { return s.name == slave.name; });
            info.slaves.push_back(std::move(slave));
        }
    }
    return info;
}

} // namespace Opm::RunFlow
