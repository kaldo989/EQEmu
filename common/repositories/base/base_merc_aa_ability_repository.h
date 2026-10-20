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
 * DO NOT MODIFY THIS FILE
 *
 * This repository was automatically generated and is NOT to be modified directly.
 * Any repository modifications are meant to be made to the repository extending the base.
 * Any modifications to base repositories are to be made by the generator only
 *
 * @generator ./utils/scripts/generators/repository-generator.pl
 * @docs https://docs.eqemu.dev/developer/repositories
 * @note Emitted before the tables existed in a live database.  These tables are
 *       created only by the manual import
 *       utils/sql/git/optional/2026_10_18_mercenary_aa_schema_draft.sql -- there
 *       is no manifest entry for them and the server never runs the DDL.  That
 *       file's CREATE TABLE already carries rank_index after group_id, which is
 *       the order reflected here.  Once it has been imported, re-run the perl
 *       generator over these seven tables and it will reproduce this file.
 */

#pragma once

#include "common/database.h"
#include "common/strings.h"

#include <ctime>


class BaseMercAaAbilityRepository {
public:
	struct MercAaAbility {
		uint32_t ability_id;
		int32_t  name_sid;
		int32_t  desc_sid;
		int32_t  max_rank;
		int32_t  group_id;
		int32_t  rank_index;
		int32_t  aa_type;
		int32_t  cost;
		int32_t  currency_id;
		int32_t  level_req;
		int32_t  flags;
		uint8_t  enabled;
	};

	static std::string PrimaryKey()
	{
		return std::string("ability_id");
	}

	static std::vector<std::string> Columns()
	{
		return {
			"ability_id",
			"name_sid",
			"desc_sid",
			"max_rank",
			"group_id",
			"rank_index",
			"aa_type",
			"cost",
			"currency_id",
			"level_req",
			"flags",
			"enabled",
		};
	}

	static std::vector<std::string> SelectColumns()
	{
		return {
			"ability_id",
			"name_sid",
			"desc_sid",
			"max_rank",
			"group_id",
			"rank_index",
			"aa_type",
			"cost",
			"currency_id",
			"level_req",
			"flags",
			"enabled",
		};
	}

	static std::string ColumnsRaw()
	{
		return std::string(Strings::Implode(", ", Columns()));
	}

	static std::string SelectColumnsRaw()
	{
		return std::string(Strings::Implode(", ", SelectColumns()));
	}

	static std::string TableName()
	{
		return std::string("merc_aa_ability");
	}

	static std::string BaseSelect()
	{
		return fmt::format(
			"SELECT {} FROM {}",
			SelectColumnsRaw(),
			TableName()
		);
	}

	static std::string BaseInsert()
	{
		return fmt::format(
			"INSERT INTO {} ({}) ",
			TableName(),
			ColumnsRaw()
		);
	}

	static MercAaAbility NewEntity()
	{
		MercAaAbility e{};

		e.ability_id  = 0;
		e.name_sid    = -1;
		e.desc_sid    = -1;
		e.max_rank    = 1;
		e.group_id    = -1;
		e.rank_index  = 1;
		e.aa_type     = 0;
		e.cost        = 0;
		e.currency_id = 0;
		e.level_req   = 0;
		e.flags       = 0;
		e.enabled     = 1;

		return e;
	}

	static MercAaAbility GetMercAaAbility(
		const std::vector<MercAaAbility> &merc_aa_abilitys,
		int ability_id
	)
	{
		for (auto &merc_aa_ability : merc_aa_abilitys) {
			if (merc_aa_ability.ability_id == ability_id) {
				return merc_aa_ability;
			}
		}

		return NewEntity();
	}

	static MercAaAbility FindOne(
		Database& db,
		int ability_id
	)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"{} WHERE {} = {} LIMIT 1",
				BaseSelect(),
				PrimaryKey(),
				ability_id
			)
		);

		auto row = results.begin();
		if (results.RowCount() == 1) {
			MercAaAbility e{};

			e.ability_id = row[0] ? static_cast<uint32_t>(strtoul(row[0], nullptr, 10)) : 0;
			e.name_sid   = row[1] ? static_cast<int32_t>(atoi(row[1])) : -1;
			e.desc_sid   = row[2] ? static_cast<int32_t>(atoi(row[2])) : -1;
			e.max_rank   = row[3] ? static_cast<int32_t>(atoi(row[3])) : 1;
			e.group_id   = row[4] ? static_cast<int32_t>(atoi(row[4])) : -1;
			e.rank_index = row[5] ? static_cast<int32_t>(atoi(row[5])) : 1;
			e.aa_type    = row[6] ? static_cast<int32_t>(atoi(row[6])) : 0;
			e.cost       = row[7] ? static_cast<int32_t>(atoi(row[7])) : 0;
			e.currency_id = row[8] ? static_cast<int32_t>(atoi(row[8])) : 0;
			e.level_req  = row[9] ? static_cast<int32_t>(atoi(row[9])) : 0;
			e.flags      = row[10] ? static_cast<int32_t>(atoi(row[10])) : 0;
			e.enabled    = row[11] ? static_cast<uint8_t>(strtoul(row[11], nullptr, 10)) : 1;

			return e;
		}

		return NewEntity();
	}

	static int DeleteOne(
		Database& db,
		int ability_id
	)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"DELETE FROM {} WHERE {} = {}",
				TableName(),
				PrimaryKey(),
				ability_id
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static int UpdateOne(
		Database& db,
		const MercAaAbility &e
	)
	{
		std::vector<std::string> v;

		auto columns = Columns();

		v.push_back(columns[1] + " = " + std::to_string(e.name_sid));
		v.push_back(columns[2] + " = " + std::to_string(e.desc_sid));
		v.push_back(columns[3] + " = " + std::to_string(e.max_rank));
		v.push_back(columns[4] + " = " + std::to_string(e.group_id));
		v.push_back(columns[5] + " = " + std::to_string(e.rank_index));
		v.push_back(columns[6] + " = " + std::to_string(e.aa_type));
		v.push_back(columns[7] + " = " + std::to_string(e.cost));
		v.push_back(columns[8] + " = " + std::to_string(e.currency_id));
		v.push_back(columns[9] + " = " + std::to_string(e.level_req));
		v.push_back(columns[10] + " = " + std::to_string(e.flags));
		v.push_back(columns[11] + " = " + std::to_string(e.enabled));

		auto results = db.QueryDatabase(
			fmt::format(
				"UPDATE {} SET {} WHERE {} = {}",
				TableName(),
				Strings::Implode(", ", v),
				PrimaryKey(),
				e.ability_id
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static MercAaAbility InsertOne(
		Database& db,
		MercAaAbility e
	)
	{
		std::vector<std::string> v;

		v.push_back(std::to_string(e.ability_id));
		v.push_back(std::to_string(e.name_sid));
		v.push_back(std::to_string(e.desc_sid));
		v.push_back(std::to_string(e.max_rank));
		v.push_back(std::to_string(e.group_id));
		v.push_back(std::to_string(e.rank_index));
		v.push_back(std::to_string(e.aa_type));
		v.push_back(std::to_string(e.cost));
		v.push_back(std::to_string(e.currency_id));
		v.push_back(std::to_string(e.level_req));
		v.push_back(std::to_string(e.flags));
		v.push_back(std::to_string(e.enabled));

		auto results = db.QueryDatabase(
			fmt::format(
				"{} VALUES ({})",
				BaseInsert(),
				Strings::Implode(",", v)
			)
		);

		if (results.Success()) {
			e.ability_id = results.LastInsertedID();
			return e;
		}

		e = NewEntity();

		return e;
	}

	static int InsertMany(
		Database& db,
		const std::vector<MercAaAbility> &entries
	)
	{
		std::vector<std::string> insert_chunks;

		for (auto &e: entries) {
			std::vector<std::string> v;

			v.push_back(std::to_string(e.ability_id));
			v.push_back(std::to_string(e.name_sid));
			v.push_back(std::to_string(e.desc_sid));
			v.push_back(std::to_string(e.max_rank));
			v.push_back(std::to_string(e.group_id));
		v.push_back(std::to_string(e.rank_index));
			v.push_back(std::to_string(e.aa_type));
			v.push_back(std::to_string(e.cost));
			v.push_back(std::to_string(e.currency_id));
			v.push_back(std::to_string(e.level_req));
			v.push_back(std::to_string(e.flags));
			v.push_back(std::to_string(e.enabled));

			insert_chunks.push_back("(" + Strings::Implode(",", v) + ")");
		}

		std::vector<std::string> v;

		auto results = db.QueryDatabase(
			fmt::format(
				"{} VALUES {}",
				BaseInsert(),
				Strings::Implode(",", insert_chunks)
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static std::vector<MercAaAbility> All(Database& db)
	{
		std::vector<MercAaAbility> all_entries;

		auto results = db.QueryDatabase(
			fmt::format(
				"{}",
				BaseSelect()
			)
		);

		all_entries.reserve(results.RowCount());

		for (auto row = results.begin(); row != results.end(); ++row) {
			MercAaAbility e{};

			e.ability_id = row[0] ? static_cast<uint32_t>(strtoul(row[0], nullptr, 10)) : 0;
			e.name_sid   = row[1] ? static_cast<int32_t>(atoi(row[1])) : -1;
			e.desc_sid   = row[2] ? static_cast<int32_t>(atoi(row[2])) : -1;
			e.max_rank   = row[3] ? static_cast<int32_t>(atoi(row[3])) : 1;
			e.group_id   = row[4] ? static_cast<int32_t>(atoi(row[4])) : -1;
			e.rank_index = row[5] ? static_cast<int32_t>(atoi(row[5])) : 1;
			e.aa_type    = row[6] ? static_cast<int32_t>(atoi(row[6])) : 0;
			e.cost       = row[7] ? static_cast<int32_t>(atoi(row[7])) : 0;
			e.currency_id = row[8] ? static_cast<int32_t>(atoi(row[8])) : 0;
			e.level_req  = row[9] ? static_cast<int32_t>(atoi(row[9])) : 0;
			e.flags      = row[10] ? static_cast<int32_t>(atoi(row[10])) : 0;
			e.enabled    = row[11] ? static_cast<uint8_t>(strtoul(row[11], nullptr, 10)) : 1;

			all_entries.push_back(e);
		}

		return all_entries;
	}

	static std::vector<MercAaAbility> GetWhere(Database& db, const std::string &where_filter)
	{
		std::vector<MercAaAbility> all_entries;

		auto results = db.QueryDatabase(
			fmt::format(
				"{} WHERE {}",
				BaseSelect(),
				where_filter
			)
		);

		all_entries.reserve(results.RowCount());

		for (auto row = results.begin(); row != results.end(); ++row) {
			MercAaAbility e{};

			e.ability_id = row[0] ? static_cast<uint32_t>(strtoul(row[0], nullptr, 10)) : 0;
			e.name_sid   = row[1] ? static_cast<int32_t>(atoi(row[1])) : -1;
			e.desc_sid   = row[2] ? static_cast<int32_t>(atoi(row[2])) : -1;
			e.max_rank   = row[3] ? static_cast<int32_t>(atoi(row[3])) : 1;
			e.group_id   = row[4] ? static_cast<int32_t>(atoi(row[4])) : -1;
			e.rank_index = row[5] ? static_cast<int32_t>(atoi(row[5])) : 1;
			e.aa_type    = row[6] ? static_cast<int32_t>(atoi(row[6])) : 0;
			e.cost       = row[7] ? static_cast<int32_t>(atoi(row[7])) : 0;
			e.currency_id = row[8] ? static_cast<int32_t>(atoi(row[8])) : 0;
			e.level_req  = row[9] ? static_cast<int32_t>(atoi(row[9])) : 0;
			e.flags      = row[10] ? static_cast<int32_t>(atoi(row[10])) : 0;
			e.enabled    = row[11] ? static_cast<uint8_t>(strtoul(row[11], nullptr, 10)) : 1;

			all_entries.push_back(e);
		}

		return all_entries;
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

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static int Truncate(Database& db)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"TRUNCATE TABLE {}",
				TableName()
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static int64 GetMaxId(Database& db)
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"SELECT COALESCE(MAX({}), 0) FROM {}",
				PrimaryKey(),
				TableName()
			)
		);

		return (results.Success() && results.begin()[0] ? strtoll(results.begin()[0], nullptr, 10) : 0);
	}

	static int64 Count(Database& db, const std::string &where_filter = "")
	{
		auto results = db.QueryDatabase(
			fmt::format(
				"SELECT COUNT(*) FROM {} {}",
				TableName(),
				(where_filter.empty() ? "" : "WHERE " + where_filter)
			)
		);

		return (results.Success() && results.begin()[0] ? strtoll(results.begin()[0], nullptr, 10) : 0);
	}

	static std::string BaseReplace()
	{
		return fmt::format(
			"REPLACE INTO {} ({}) ",
			TableName(),
			ColumnsRaw()
		);
	}

	static int ReplaceOne(
		Database& db,
		const MercAaAbility &e
	)
	{
		std::vector<std::string> v;

		v.push_back(std::to_string(e.ability_id));
		v.push_back(std::to_string(e.name_sid));
		v.push_back(std::to_string(e.desc_sid));
		v.push_back(std::to_string(e.max_rank));
		v.push_back(std::to_string(e.group_id));
		v.push_back(std::to_string(e.rank_index));
		v.push_back(std::to_string(e.aa_type));
		v.push_back(std::to_string(e.cost));
		v.push_back(std::to_string(e.currency_id));
		v.push_back(std::to_string(e.level_req));
		v.push_back(std::to_string(e.flags));
		v.push_back(std::to_string(e.enabled));

		auto results = db.QueryDatabase(
			fmt::format(
				"{} VALUES ({})",
				BaseReplace(),
				Strings::Implode(",", v)
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}

	static int ReplaceMany(
		Database& db,
		const std::vector<MercAaAbility> &entries
	)
	{
		std::vector<std::string> insert_chunks;

		for (auto &e: entries) {
			std::vector<std::string> v;

			v.push_back(std::to_string(e.ability_id));
			v.push_back(std::to_string(e.name_sid));
			v.push_back(std::to_string(e.desc_sid));
			v.push_back(std::to_string(e.max_rank));
			v.push_back(std::to_string(e.group_id));
		v.push_back(std::to_string(e.rank_index));
			v.push_back(std::to_string(e.aa_type));
			v.push_back(std::to_string(e.cost));
			v.push_back(std::to_string(e.currency_id));
			v.push_back(std::to_string(e.level_req));
			v.push_back(std::to_string(e.flags));
			v.push_back(std::to_string(e.enabled));

			insert_chunks.push_back("(" + Strings::Implode(",", v) + ")");
		}

		std::vector<std::string> v;

		auto results = db.QueryDatabase(
			fmt::format(
				"{} VALUES {}",
				BaseReplace(),
				Strings::Implode(",", insert_chunks)
			)
		);

		return (results.Success() ? results.RowsAffected() : 0);
	}
};
