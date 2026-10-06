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

#include "advloot.h"

#include "common/emu_opcodes.h"
#include "common/eq_packet.h"
#include "common/eqemu_logsys.h"
#include "common/strings.h"
#include "fmt/format.h"
#include "zone/client.h"
#include "zone/zonedb.h"

#include <cctype>
#include <cstring>

namespace AdvLoot {

	const char* SubcommandName(uint16_t subcmd) {
		switch (subcmd) {
			case SubUnknown02:     return "Unknown02";
			case SubFilterSetSync: return "FilterSetSync";
			case SubCoinSplit:     return "CoinSplit";
			case SubUnknown07:     return "Unknown07";
			case SubAddFilter:     return "AddFilter";
			case SubUnknown09:     return "Unknown09";
			case SubItemUpdate:    return "ItemUpdate";
			case SubRowUpdate:     return "RowUpdate";
			case SubUnknown0C:     return "Unknown0C";
			case SubBulkRows:      return "BulkRows";
			case SubTransaction:   return "Transaction";
			case SubUnknown0F:     return "Unknown0F";
			case SubRowUpdate10:   return "RowUpdate10";
			case SubSetState:      return "SetState";
			case SubItemAction:    return "ItemAction";
			case SubUnknown13:     return "Unknown13";
			case SubStringMessage: return "StringMessage";
			default:               return "UNKNOWN";
		}
	}

	const char* StateName(uint32_t state) {
		switch (state) {
			case StateWaiting:           return "Waiting";
			case StateAsk:               return "Ask";
			case StateAskAutoRoll:       return "AskAutoRoll";
			case StateStop:              return "Stop";
			case StateAskCompleted:      return "AskCompleted";
			case StateFreeGrab:          return "FreeGrab";
			case StateFixedAskAutoRoll:  return "FixedAskAutoRoll";
			case StateFixedAskCompleted: return "FixedAskCompleted";
			case StateRemoved:           return "Removed";
			default:                     return "UNKNOWN";
		}
	}

	const char* ModeName(uint32_t mode) {
		switch (mode) {
			case ModeInvalid:      return "invalid";
			case ModeSolo:         return "solo";
			case ModeMasterLooter: return "molo";
			case ModeGroup:        return "group";
			case ModeRaid:         return "raid";
			default:               return "UNKNOWN";
		}
	}

	static std::string HexDump(const uint8_t* data, uint32_t length, uint32_t max_bytes = 64) {
		std::string out;
		uint32_t limit = length < max_bytes ? length : max_bytes;
		out.reserve(limit * 3 + 1);
		for (uint32_t i = 0; i < limit; i++) {
			if (i) {
				out.push_back(' ');
			}
			out += fmt::format("{:02x}", data[i]);
		}
		if (limit < length) {
			out += fmt::format(" ... (+{} bytes)", length - limit);
		}
		return out;
	}

	static bool IsPrintableName(const std::string& value) {
		if (value.empty()) {
			return false;
		}
		for (char c : value) {
			if (!std::isprint(static_cast<unsigned char>(c))) {
				return false;
			}
		}
		return true;
	}

	// Item record as read by FUN_1400839b0 (the array element inside subcmd 0x03).
	// Field names are inferred from how the client uses them, not confirmed.
	static std::string DescribeItemRecord(Reader& reader) {
		uint64_t item_id   = reader.read_u64();
		uint32_t corpse_id = reader.read_u32();
		uint32_t available = reader.read_u32();
		uint32_t state     = reader.read_u32();
		uint8_t  flag      = reader.read_u8();
		uint32_t assignee  = reader.read_u32();
		uint32_t quantity  = reader.read_u32();
		std::string name   = reader.read_string(64);
		uint64_t guid      = reader.read_u64();

		return fmt::format(
			"item id={} corpse={} avail={} state={}({}) flag={} assignee={} qty={} name='{}' guid={}",
			item_id,
			corpse_id,
			available,
			state,
			StateName(state),
			flag,
			assignee,
			quantity,
			IsPrintableName(name) ? name : "",
			guid
		);
	}

	std::string DescribePayload(const uint8_t* payload, uint32_t size) {
		Reader reader(payload, size);
		uint16_t subcmd = reader.read_u16();

		std::string out = fmt::format(
			"subcmd={:#04x} ({})",
			subcmd,
			SubcommandName(subcmd)
		);

		switch (subcmd) {
			case SubFilterSetSync: {
				// FUN_1400874d0
				uint32_t    field_a = reader.read_u32();
				std::string name    = reader.read_string(64);
				uint32_t    count   = reader.read_u32();

				out += fmt::format(" field_a={}({}) name='{}' count={}", field_a, ModeName(field_a), name, count);

				for (uint32_t i = 0; i < count && !reader.truncated; i++) {
					out += "\n  " + DescribeItemRecord(reader);
				}

				uint8_t has_second = reader.read_u8();
				if (has_second) {
					uint32_t count2 = reader.read_u16();
					out += fmt::format(" block2_count={}", count2);
					for (uint32_t i = 0; i < count2 && !reader.truncated; i++) {
						uint32_t a = reader.read_u32();
						uint64_t b = reader.read_u64();
						uint32_t c = reader.read_u32();
						out += fmt::format("\n  block2 a={} b={} c={}", a, b, c);
					}
				}

				uint8_t has_third = reader.read_u8();
				if (has_third) {
					out += fmt::format(" block3_present bytes_left={}", reader.remaining());
				}
				break;
			}

			case SubItemUpdate: {
				// FUN_1400871e0
				uint32_t a     = reader.read_u32();
				uint64_t b     = reader.read_u64();
				uint32_t c     = reader.read_u32();
				uint8_t  d     = reader.read_u8();
				uint32_t e     = reader.read_u32();
				uint32_t count = reader.read_u32();

				out += fmt::format(" a={} b={} c={}({}) d={} e={} count={}", a, b, c, StateName(c), d, e, count);

				for (uint32_t i = 0; i < count && !reader.truncated; i++) {
					out += fmt::format("\n  entry x={} y={}", reader.read_u32(), reader.read_u32());
				}
				break;
			}

			case SubRowUpdate:
			case SubRowUpdate10: {
				// FUN_140087460 / FUN_140086240 - identical layout
				uint32_t a = reader.read_u32();
				uint64_t b = reader.read_u64();
				uint32_t c = reader.read_u32();
				out += fmt::format(" a={} b={} c={}({})", a, b, c, StateName(c));
				break;
			}

			case SubBulkRows: {
				// FUN_140086ab0
				uint32_t a     = reader.read_u32();
				uint32_t b     = reader.read_u32();
				uint64_t c     = reader.read_u64();
				uint32_t count = reader.read_u16();

				out += fmt::format(" a={} b={} c={} count={}", a, b, c, count);

				for (uint32_t i = 0; i < count && !reader.truncated; i++) {
					uint32_t    id   = reader.read_u32();
					std::string name = reader.read_string(64);
					out += fmt::format("\n  row id={} name='{}'", id, name);
				}
				break;
			}

			case SubTransaction: {
				// FUN_140086cb0
				uint32_t    a       = reader.read_u32();
				uint32_t    b       = reader.read_u32();
				uint64_t    item_id = reader.read_u64();
				uint32_t    quantity = reader.read_u16();
				uint8_t     flag    = reader.read_u8();
				std::string name    = reader.read_string(64);

				out += fmt::format(
					" a={} b={} item_id={} qty={} flag={} name='{}'",
					a, b, item_id, quantity, flag, name
				);
				break;
			}

			case SubSetState: {
				// FUN_140087390 - verified from both the send side (FUN_14014fca0) and
				// the receive side (FUN_140151c60 default case)
				uint32_t item_id  = reader.read_u32();
				uint32_t assignee = reader.read_u32();
				uint32_t state    = reader.read_u32();
				uint8_t  flag     = reader.read_u8();

				out += fmt::format(
					" item_id={} assignee={} state={}({}) flag={}",
					item_id,
					assignee,
					state,
					StateName(state),
					flag
				);
				break;
			}

			case SubItemAction: {
				// FUN_140150240 - subcmd + uint32 id
				uint32_t item_id = reader.read_u32();
				out += fmt::format(" item_id={}", item_id);
				break;
			}

			case SubStringMessage: {
				// FUN_140151400 - subcmd + string
				std::string text = reader.read_string(64);
				out += fmt::format(" text='{}'", text);
				break;
			}

			default: {
				out += fmt::format(" raw=[{}]", HexDump(payload + 2, size - 2));
				break;
			}
		}

		if (reader.truncated) {
			out += " [TRUNCATED]";
		}

		return out;
	}

	EQApplicationPacket* BuildFilterSetPacket(uint32_t mode, const std::string& name, const std::vector<Filter>& filters)
	{
		Writer writer;

		writer.write_u16(SubFilterSetSync);
		writer.write_u32(mode);
		writer.write_string(name, 64);
		writer.write_u32(static_cast<uint32_t>(filters.size()));

		for (const auto& filter : filters) {
			// FUN_140086370 reads: u8, u8, u32 id, u32 types, u32 icon, string name
			writer.write_u8(0);
			writer.write_u8(0);
			writer.write_u32(filter.item_id);
			writer.write_u32(filter.filter_bits);
			writer.write_u32(filter.icon);
			writer.write_string(filter.name, 64);
		}

		// Optional blocks are present but empty - the reader only consumes them if there is room.
		writer.write_u8(0);
		writer.write_u8(0);

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	EQApplicationPacket* BuildStatePacket(uint32_t item_id, uint32_t assignee, uint32_t state, uint8_t flag)
	{
		Writer writer;

		writer.write_u16(SubSetState);
		writer.write_u32(item_id);
		writer.write_u32(assignee);
		writer.write_u32(state);
		writer.write_u8(flag);

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

}

void Client::LoadAdvLootFilters()
{
	advloot_filters.clear();

	const auto query = fmt::format(
		"SELECT `item_id`, `filter_bits`, `icon`, `name` FROM `character_loot_filters` WHERE `char_id` = {}",
		CharacterID()
	);

	auto results = database.QueryDatabase(query);
	if (!results.Success()) {
		return;
	}

	for (auto row = results.begin(); row != results.end(); ++row) {
		AdvLoot::Filter filter;
		filter.item_id     = Strings::ToUnsignedInt(row[0]);
		filter.filter_bits = Strings::ToUnsignedInt(row[1]);
		filter.icon        = Strings::ToUnsignedInt(row[2]);
		filter.name        = row[3];
		advloot_filters.push_back(filter);
	}
}

void Client::LoadAdvLootSettings()
{
	const auto query = fmt::format(
		"SELECT `use_advanced_looting`, `master_looter_candidate` FROM `character_loot_settings` WHERE `char_id` = {}",
		CharacterID()
	);

	auto results = database.QueryDatabase(query);
	if (!results.Success()) {
		return;
	}

	for (auto row = results.begin(); row != results.end(); ++row) {
		advloot_enabled                = Strings::ToUnsignedInt(row[0]) != 0;
		advloot_master_looter_candidate = Strings::ToUnsignedInt(row[1]) != 0;
		break;
	}
}

void Client::SaveAdvLootFilter(uint32_t item_id, uint32_t filter_bits, uint32_t icon, const std::string& name)
{
	const auto query = fmt::format(
		"INSERT INTO `character_loot_filters` (`char_id`, `item_id`, `filter_bits`, `icon`, `name`) "
		"VALUES ({}, {}, {}, {}, '{}') "
		"ON DUPLICATE KEY UPDATE `filter_bits` = {}, `icon` = {}, `name` = '{}'",
		CharacterID(),
		item_id,
		filter_bits,
		icon,
		Strings::Escape(name),
		filter_bits,
		icon,
		Strings::Escape(name)
	);

	if (!database.QueryDatabase(query).Success()) {
		LogLootFilters("Failed to save filter for char [{}] item [{}]", GetName(), item_id);
		return;
	}

	for (auto& filter : advloot_filters) {
		if (filter.item_id == item_id) {
			filter.filter_bits = filter_bits;
			filter.icon        = icon;
			filter.name        = name;
			return;
		}
	}

	advloot_filters.push_back(AdvLoot::Filter{item_id, filter_bits, icon, name});
}

void Client::SendAdvLootFilterSet()
{
	EQApplicationPacket* app = AdvLoot::BuildFilterSetPacket(advloot_mode, GetCleanName(), advloot_filters);
	if (!app) {
		return;
	}

	QueuePacket(app);
	delete app;
}
