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
#include "zone/entity.h"
#include "zone/zonedb.h"

#include <cctype>
#include <cstring>

namespace AdvLoot {

	const char* SubcommandName(uint16_t subcmd) {
		switch (subcmd) {
			case SubFilterSetRequest:   return "FilterSetRequest";
			case SubFilterSetSync:  return "FilterSetSync";
			case SubChatNotify:     return "ChatNotify";
			case SubCorpseRowNotify: return "CorpseRowNotify";
			case SubTransactionRequest: return "TransactionRequest";
			case SubItemActionNotify: return "ItemActionNotify";
			case SubItemUpdate:    return "ItemUpdate";
			case SubRowUpdate:     return "RowUpdate";
			case SubAddRow:        return "AddRow";
			case SubBulkRows:      return "BulkRows";
			case SubTransaction:   return "Transaction";
			case SubSingleTransfer: return "SingleTransfer";
			case SubRowUpdate10:   return "RowUpdate10";
			case SubSetState:      return "SetState";
			case SubItemAction:    return "ItemAction";
			case SubLootModeList:  return "LootModeList";
			case SubChatMessage:   return "ChatMessage";
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
		// FUN_1400839b0 wire order (confirmed): u64, u32, u32, u32, u8, u32, u32, string, block.
		uint64_t item_id   = reader.read_u64();
		uint32_t icon      = reader.read_u32();
		uint32_t shared_qty  = reader.read_u32();   // +0x0C shared / construct loot list
		uint32_t managed     = reader.read_u32();   // +0x10
		uint8_t  flag        = reader.read_u8();    // +0x18
		uint32_t value       = reader.read_u32();   // +0x1C
		uint32_t personal_qty = reader.read_u32();  // +0x14 personal loot list
		std::string name   = reader.read_string(64);
		uint32_t block     = reader.read_u32();   // +0x20 length-prefixed block

		return fmt::format(
			"item id={} icon={} shared={} managed={} flag={} value={} personal={} name='{}' block={}",
			item_id,
			icon,
			shared_qty,
			managed,
			flag,
			value,
			personal_qty,
			IsPrintableName(name) ? name : "",
			block
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
					uint8_t present = reader.read_u8();   // +0x146c loot-rights flag
					reader.read_u8();
					uint32_t expiration = reader.read_u32();  // +4 - seconds, packed as value * 1000 + clock
					uint32_t corpse_key = reader.read_u32();  // +0 hash key
					uint32_t nested  = reader.read_u32();
					std::string gname = reader.read_string(64);

					out += fmt::format(
						"\n  group present={} key={} expiration={} nested={} name='{}'",
						present, corpse_key, expiration, nested, IsPrintableName(gname) ? gname : ""
					);

					for (uint32_t j = 0; j < nested && !reader.truncated; j++) {
						out += "\n    " + DescribeItemRecord(reader);
					}
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

			case SubCorpseRowNotify: {
				// FUN_1400859e0 serializer: u64, u32, u32. Confirmed by the first live capture
				// (2026-10-06): 07 00 | 3F EA 00 00 00 00 00 00 | 00 00 00 00 | 00 00 01 00
				// item_id = 59903, field_b = 0, quantity = 1.
				// FUN_14009e460 sends this when the filter record exists, the window is in managed
				// mode (wnd + 0x18) and the filter carries the NeverLoot bit (1 << 3 = 8).
				uint64_t item_id = reader.read_u64();
				uint32_t field_b = reader.read_u32();
				uint32_t quantity = reader.read_u32();

				out += fmt::format(" item_id={} field_b={}({}) quantity={}", item_id, field_b, ModeName(field_b), quantity);
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

			case SubTransactionRequest: {
				// FUN_140085aa0 serializer: u64, u32, u32, u32, u8. Confirmed by the live capture
				// (2026-10-06) on the green loot button - 23 byte payload.
				// FUN_140154cd0 stamps DAT_140bbea70 (an incrementing transaction counter) into field 4
				// and pushes 0x20 into the pending message queue at pinstCEverQuest + 0x196f0.
				uint64_t item_id     = reader.read_u64();
				uint32_t field_b     = reader.read_u32();
				uint32_t c           = reader.read_u32();
				uint32_t transaction = reader.read_u32();
				uint8_t  flag        = reader.read_u8();

				out += fmt::format(
					" item_id={} field_b={}({}) c={} transaction={} flag={}",
					item_id, field_b, ModeName(field_b), c, transaction, flag
				);
				break;
			}

			case SubChatNotify: {
				// FUN_140086d70 + string. Chat notification - FUN_140152cb0 prints string ids
				// 0x2c3a (with "raid"/"group"), 0x2f27 + n, 0x2c2b.
				uint8_t     flag_a = reader.read_u8();
				uint64_t    id     = reader.read_u64();
				uint8_t     flag_b = reader.read_u8();
				uint32_t    value  = reader.read_u32();
				std::string name   = reader.read_string(64);

				out += fmt::format(
					" flag_a={} id={} flag_b={} value={} name='{}'",
					flag_a, id, flag_b, value, IsPrintableName(name) ? name : ""
				);
				break;
			}

			case SubItemActionNotify: {
				// FUN_140086e00. FUN_140151810 looks the corpse row up in the loot-list hash
				// (manager + 0x38, 30 buckets) and switches on action 1..7. Action 2 decrements the
				// shared pool entry +0xC; actions 1/2 also move the entry in the row array.
				uint16_t action = reader.read_u16();
				uint32_t key    = reader.read_u32();
				uint32_t value  = reader.read_u32();
				uint8_t  flag   = reader.read_u8();

				out += fmt::format(" action={} key={} value={} flag={}", action, key, value, flag);
				break;
			}

			case SubAddRow: {
				// FUN_140154a90 - u8 flag then the 0x0d layout. flag == 0 -> add row, else resolve row.
				uint8_t     flag   = reader.read_u8();
				uint32_t    a      = reader.read_u32();
				uint32_t    b      = reader.read_u32();
				uint64_t    c      = reader.read_u64();
				uint32_t    count  = reader.read_u16();

				out += fmt::format(" flag={} a={} b={} c={} count={}", flag, a, b, c, count);

				for (uint32_t i = 0; i < count && !reader.truncated; i++) {
					uint32_t    id   = reader.read_u32();
					std::string name = reader.read_string(64);
					out += fmt::format("\n  row id={} name='{}'", id, IsPrintableName(name) ? name : "");
				}
				break;
			}

			case SubSingleTransfer: {
				// FUN_140086c50. FUN_1401509d0 moves one unit from the shared pool (+0xC) to the
				// personal pool (+0x14) and prints the chat line when the name matches the local player.
				uint64_t    key  = reader.read_u64();
				std::string name = reader.read_string(64);

				out += fmt::format(" key={} name='{}'", key, IsPrintableName(name) ? name : "");
				break;
			}

			case SubLootModeList: {
				// FUN_140086fa0. FUN_140154200 compares each name against the clean local name and
				// prints the loot mode from the string table at 0x14086b440 (invalid/solo/molo/group/raid).
				// This is the master looter / loot mode announcement path.
				uint32_t a     = reader.read_u32();
				uint32_t b     = reader.read_u32();
				uint8_t  c     = reader.read_u8();
				uint32_t count = reader.read_u16();

				out += fmt::format(" a={} b={} c={} count={}", a, b, c, count);

				for (uint32_t i = 0; i < count && !reader.truncated; i++) {
					uint16_t    mode = reader.read_u16();
					std::string name = reader.read_string(64);
					out += fmt::format(
						"\n  member mode={}({}) name='{}'",
						mode, ModeName(mode), IsPrintableName(name) ? name : ""
					);
				}
				break;
			}

			case SubChatMessage: {
				// FUN_140086e90 - key + flag + count x item record. FUN_140152510 looks the key up in
				// the loot-list hash and writes the item names to the chat window.
				uint32_t key   = reader.read_u32();
				uint8_t  flag  = reader.read_u8();
				uint32_t count = reader.read_u32();

				out += fmt::format(" key={} flag={} count={}", key, flag, count);

				for (uint32_t i = 0; i < count && !reader.truncated; i++) {
					out += "\n  " + DescribeItemRecord(reader);
				}
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

	// FUN_1400839b0 item record wire order: u64 item_id (+0x00), u32 icon (+0x08), u32 shared
	// quantity (+0x0C), u32 managed flag (+0x10), u8 flag (+0x18), u32 value (+0x1C), u32 personal
	// quantity (+0x14), string name (+0x28), u32 block (+0x20).
	static void WriteItemRecord(Writer& writer, const Filter& filter)
	{
		// AddCorpse (FUN_14009e310) creates a Corpse Loot List row when +0x0C is non-zero and a
		// Personal Loot List row when +0x14 is non-zero. Only the shared pool is populated here: the
		// transaction (subcmd 0x0e) moves units out of the shared pool into personal, and the client
		// auto-loots any personal row it is given when LS_AutoLootAllCheckbox is checked
		// (FUN_14009f790 -> FUN_1400a9cc0 -> FUN_140154cd0), so pre-filling +0x14 loots the item for us.
		writer.write_u64(filter.item_id);
		writer.write_u32(filter.icon);              // +0x08
		writer.write_u32(filter.quantity);          // +0x0C shared / construct pool
		writer.write_u32(1);                        // +0x10 managed flag
		writer.write_u8(0);                         // +0x18
		writer.write_u32(0);                        // +0x1C
		writer.write_u32(filter.personal);          // +0x14 personal pool - keep 0 on the initial push
		writer.write_string(filter.name, 64);       // +0x28
		writer.write_u32(0); // empty length-prefixed block (+0x20)
	}

	// FUN_140086370 group record wire order: u8 -> +0x146c (loot-rights flag), u8 -> +3, u32 -> +4
	// (expiration), u32 -> +0 (hash key), u32 -> +8 (nested item count), string -> +0xd (name), then
	// nested_count item records.
	static void WriteGroupRecord(
		Writer& writer,
		uint32_t key,
		const std::string& name,
		uint32_t expiration,
		uint8_t present_at_kill,
		const std::vector<Filter>& items
	) {
		writer.write_u8(present_at_kill);  // +0x146c loot-rights / presence flag
		writer.write_u8(0);                // +3
		writer.write_u32(expiration);      // +4 - FUN_14014f5f0: value * 1000 + clock (seconds)
		writer.write_u32(key);             // +0 hash key
		writer.write_u32(static_cast<uint32_t>(items.size()));  // +8 nested item count
		writer.write_string(name, 64);     // +0xd name

		for (const auto& item : items) {
			WriteItemRecord(writer, item);
		}
	}

	EQApplicationPacket* BuildFilterSetPacket(uint32_t mode, const std::string& name, const std::vector<Filter>& filters)
	{
		Writer writer;

		writer.write_u16(SubFilterSetSync);
		writer.write_u32(mode);
		writer.write_string(name, 64);
		writer.write_u32(static_cast<uint32_t>(filters.size()));

		for (const auto& filter : filters) {
			// The login filter push has no corpse, so the group record doubles as the filter record:
			// the key is the item id and the name is the item name.
			WriteGroupRecord(writer, filter.corpse_key ? filter.corpse_key : filter.item_id, filter.name, filter.expiration, filter.present_at_kill, { filter });
		}

		// Optional blocks are present but empty - the reader only consumes them if there is room.
		writer.write_u8(0);
		writer.write_u8(0);

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	// One group record for the corpse itself, every item on it nested inside. The group name is the NPC
	// name (the window's NPC Name column), not an item name.
	EQApplicationPacket* BuildCorpseGroupPacket(uint32_t mode, const std::string& name, const CorpseGroup& group)
	{
		return BuildCorpseGroupsPacket(mode, name, { group });
	}

	EQApplicationPacket* BuildCorpseGroupsPacket(uint32_t mode, const std::string& name, const std::vector<CorpseGroup>& groups)
	{
		Writer writer;

		writer.write_u16(SubFilterSetSync);
		writer.write_u32(mode);
		writer.write_string(name, 64);
		writer.write_u32(static_cast<uint32_t>(groups.size()));

		for (const auto& group : groups) {
			WriteGroupRecord(writer, group.corpse_key, group.npc_name, group.expiration, group.present_at_kill, group.items);
		}

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

	EQApplicationPacket* BuildCorpseRowNotifyPacket(uint64_t item_id, uint32_t field_b, uint32_t quantity)
	{
		Writer writer;

		writer.write_u16(SubCorpseRowNotify);
		writer.write_u64(item_id);
		writer.write_u32(field_b);
		writer.write_u32(quantity);

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	EQApplicationPacket* BuildItemActionPacket(uint16_t action, uint32_t field_a, uint32_t field_b, uint8_t flag)
	{
		Writer writer;

		writer.write_u16(SubItemActionNotify);
		writer.write_u16(action);
		writer.write_u32(field_a);
		writer.write_u32(field_b);
		writer.write_u8(flag);

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	EQApplicationPacket* BuildTransactionPacket(
		uint32_t record_field_c,
		uint32_t corpse_key,
		uint64_t item_id,
		uint16_t quantity,
		uint8_t flag,
		const std::string& name
	)
	{
		Writer writer;

		writer.write_u16(SubTransaction);
		writer.write_u32(record_field_c);
		writer.write_u32(corpse_key);
		writer.write_u64(item_id);
		writer.write_u16(quantity);
		writer.write_u8(flag);
		writer.write_string(name, 64);

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
	// Default off, then override from the DB. These are also the bytes the Laurion profile
	// encode writes, so they must be set before the OP_PlayerProfile packet is built.
	m_pp.use_advanced_looting   = 0;
	m_pp.master_loot_candidate  = 0;

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
		m_pp.use_advanced_looting   = advloot_enabled ? 1 : 0;
		m_pp.master_loot_candidate  = advloot_master_looter_candidate ? 1 : 0;
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

uint32_t Client::AdvLootFilterBits(uint32_t item_id) const
{
	for (const auto& filter : advloot_filters) {
		if (filter.item_id == item_id) {
			return filter.filter_bits;
		}
	}
	return 0;
}

// Subcmd 0x03 is also the corpse delivery channel: one group record for the corpse, with the corpse
// items as nested records. CAdvancedLootWnd::AddCorpse only creates a row when a per-list quantity is
// non-zero, so the caller must fill quantity (shared) and personal.
void Client::SendAdvLootCorpse(const AdvLoot::CorpseGroup& corpse_group)
{
	if (corpse_group.items.empty()) {
		return;
	}

	EQApplicationPacket* app = AdvLoot::BuildCorpseGroupPacket(advloot_mode, GetCleanName(), corpse_group);
	if (!app) {
		return;
	}

	QueuePacket(app);
	delete app;
}

// Rebuild the window from what is actually left on the tracked corpses. This is the only server-side path
// that clears rows in both panes (FUN_140153970 -> window vtable +0x3b0), so it is how a looted solo row
// is made to disappear. A corpse with nothing left is dropped from the window rather than resent.
void Client::SendAdvLootCorpseRebuild()
{
	if (!advloot_enabled) {
		return;
	}

	std::vector<AdvLoot::CorpseGroup> groups;
	std::vector<uint16_t> alive;

	for (uint16_t corpse_id: advloot_corpses) {
		Corpse* corpse = entity_list.GetCorpseByID(corpse_id);
		if (!corpse) {
			continue;
		}

		if (!corpse->HasAdvLootItems()) {
			corpse->ClearAdvLootPendingRowsFor(CharacterID());
			continue;
		}

		AdvLoot::CorpseGroup group = corpse->BuildAdvLootGroup(this);
		if (group.items.empty()) {
			continue;
		}

		groups.push_back(group);
		alive.push_back(corpse_id);
	}

	advloot_corpses = alive;

	EQApplicationPacket* app = AdvLoot::BuildCorpseGroupsPacket(advloot_mode, GetCleanName(), groups);
	if (!app) {
		return;
	}

	LogLootFilters("rebuild [{}] corpse group(s) for [{}]", groups.size(), GetCleanName());

	QueuePacket(app);
	delete app;
}
