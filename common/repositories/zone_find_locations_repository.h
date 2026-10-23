/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * Hand written repository for the Ctrl+F Find window POI content table (docs/findwnd.md Phase 1, 8.1).
 *
 * utils/scripts/generators/repository-generator.pl cannot run here (it needs perl's DBD::mysql, which is
 * not installed), so this file follows the generated shape by hand: struct named like the table,
 * PrimaryKey/TableName/Columns/BaseSelect, GetWhere/CountOf/DeleteWhere with positional row reads.  If the
 * generator is ever run for zone_find_locations, keep this class name, the struct name and the field names
 * so callers do not change.
 *
 * `number` is the per-zone Find id the client sees as the path goal id (16 bit on the wire) and it has to
 * stay below Client::kFindPortPoiIdBase (0xF000, zone/client.h), which is where the synthetic port POIs of
 * docs/findwnd.md 7.2 live.
 */
#pragma once

#include "common/database.h"
#include "common/strings.h"

#include <string>
#include <vector>

class ZoneFindLocationsRepository {
public:

	struct ZoneFindLocations {
		uint32_t    id;
		std::string zone;
		int32_t     version;
		uint16_t    number;
		std::string category;
		std::string name;
		float       x;
		float       y;
		float       z;
		int8_t      min_expansion;
		int8_t      max_expansion;
		std::string content_flags;
		std::string content_flags_disabled;
		uint32_t    client_version_mask;
		std::string note;
	};

	static std::string PrimaryKey()
	{
		return std::string("id");
	}

	static std::string TableName()
	{
		return std::string("zone_find_locations");
	}

	static std::vector<std::string> SelectColumns()
	{
		return {
			"id",
			"zone",
			"version",
			"number",
			"category",
			"name",
			"x",
			"y",
			"z",
			"min_expansion",
			"max_expansion",
			"content_flags",
			"content_flags_disabled",
			"client_version_mask",
			"note",
		};
	}

	static std::string SelectColumnsRaw()
	{
		return std::string(Strings::Implode(", ", SelectColumns()));
	}

	static std::string BaseSelect()
	{
		return fmt::format(
			"SELECT {} FROM {}",
			SelectColumnsRaw(),
			TableName()
		);
	}

	static std::vector<ZoneFindLocations> All(Database& db)
	{
		return GetWhere(db, "");
	}

	static std::vector<ZoneFindLocations> GetWhere(Database& db, const std::string &where_filter)
	{
		std::vector<ZoneFindLocations> all_entries;

		auto results = db.QueryDatabase(
			fmt::format(
				"{} WHERE {}",
				BaseSelect(),
				where_filter
			)
		);

		all_entries.reserve(results.RowCount());

		for (auto row = results.begin(); row != results.end(); ++row) {
			ZoneFindLocations e{};

			e.id                     = row[0] ? static_cast<uint32_t>(strtoul(row[0], nullptr, 10)) : 0;
			e.zone                   = row[1] ? row[1] : "";
			e.version                = row[2] ? static_cast<int32_t>(atoi(row[2])) : 0;
			e.number                 = row[3] ? static_cast<uint16_t>(strtoul(row[3], nullptr, 10)) : 0;
			e.category               = row[4] ? row[4] : "";
			e.name                   = row[5] ? row[5] : "";
			e.x                      = row[6] ? strtof(row[6], nullptr) : 0;
			e.y                      = row[7] ? strtof(row[7], nullptr) : 0;
			e.z                      = row[8] ? strtof(row[8], nullptr) : 0;
			e.min_expansion          = row[9] ? static_cast<int8_t>(atoi(row[9])) : -1;
			e.max_expansion          = row[10] ? static_cast<int8_t>(atoi(row[10])) : -1;
			e.content_flags          = row[11] ? row[11] : "";
			e.content_flags_disabled = row[12] ? row[12] : "";
			e.client_version_mask    = row[13] ? static_cast<uint32_t>(strtoul(row[13], nullptr, 10)) : 4294967295;
			e.note                   = row[14] ? row[14] : "";

			all_entries.push_back(e);
		}

		return all_entries;
	}

	static int32_t CountOf(Database& db, const std::string &where_filter)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"SELECT COUNT(*) FROM {} WHERE {}",
				TableName(),
				where_filter
			)
		);

		auto row = results.begin();

		if (row != results.end()) {
			return row[0] ? static_cast<int32_t>(atoi(row[0])) : 0;
		}

		return 0;
	}

	static int DeleteWhere(Database& db, const std::string &where_filter)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"DELETE FROM {} WHERE {}",
				TableName(),
				where_filter
			)
		);

		return results.Success() ? 1 : 0;
	}

	// Zone rows for one instance version, version -1 meaning "any version of this zone", which is how
	// ZoneDatabase::LoadStaticZonePoints selects zone_points rows. Ordered by number so the list a player sees
	// in the Find window does not shuffle between content edits.
	static std::vector<ZoneFindLocations> GetZoneRows(Database& db, const std::string &zone_short_name, int32_t instance_version, const std::string &content_filter)
	{
		return GetWhere(db,
			fmt::format(
				"`zone` = '{}' AND (`version` = {} OR `version` = -1) {} ORDER BY `number`",
				Strings::Escape(zone_short_name),
				instance_version,
				content_filter
			)
		);
	}
};
