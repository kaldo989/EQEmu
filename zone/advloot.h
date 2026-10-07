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

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

class EQApplicationPacket;

namespace AdvLoot {

	// Sub-commands of OP_AdvLoot (0x3175). The client reads a uint16 sub-command at
	// payload offset 0 and dispatches on it (FUN_140151c60 in the Laurion client).
	// All 19 slots are resolved from the client dispatcher + senders (advloot.md S3.10).
	enum Subcommand : uint16_t {
		SubFilterSetRequest   = 0x02, // C->S only, FUN_140153570 - bare request, clears the filter hash first
		SubFilterSetSync      = 0x03, // BOTH, FUN_140153970 / reader FUN_1400874d0
		SubChatNotify         = 0x06, // S->C only, FUN_140152cb0 / reader FUN_140086d70
		SubCorpseRowNotify    = 0x07, // C->S only, FUN_1401522b0 / serializer FUN_1400859e0
		SubTransactionRequest = 0x08, // C->S only, FUN_140154cd0 / serializer FUN_140085aa0
		SubItemActionNotify   = 0x09, // S->C only, FUN_140151810 / reader FUN_140086e00
		SubItemUpdate         = 0x0A, // BOTH, FUN_140154570 + FUN_140153400 / reader FUN_1400871e0
		SubRowUpdate          = 0x0B, // S->C only, reader FUN_140087460
		SubAddRow             = 0x0C, // S->C only, FUN_140154a90 (u8 flag + 0x0d layout)
		SubBulkRows           = 0x0D, // BOTH, FUN_140154830 + FUN_14009f6b0 / reader FUN_140086ab0
		SubTransaction        = 0x0E, // BOTH, FUN_140151400 + FUN_1400a42f0 / reader FUN_140086cb0
		SubSingleTransfer     = 0x0F, // S->C only, FUN_1401509d0 / reader FUN_140086c50
		SubRowUpdate10        = 0x10, // S->C only, reader FUN_140086240
		SubSetState           = 0x11, // BOTH, FUN_14014fca0 / reader FUN_140087390
		SubItemAction         = 0x12, // C->S only, FUN_140150240 - requires record state == 4
		SubLootModeList       = 0x13, // S->C only, FUN_140154200 / reader FUN_140086fa0
		SubChatMessage        = 0x14, // S->C only, FUN_140152510 / reader FUN_140086e90
	};

	// eAdvLootState - MacroQuestLS src/eqlib/UI.h L1684
	enum State : uint32_t {
		StateWaiting           = 0,
		StateAsk               = 1,
		StateAskAutoRoll       = 2,
		StateStop              = 3,
		StateAskCompleted      = 4,
		StateFreeGrab          = 5,
		StateFixedAskAutoRoll  = 6,
		StateFixedAskCompleted = 7,
		StateRemoved           = 8,
	};

	// Loot mode names from the client string table at 0x14086b448
	enum Mode : uint32_t {
		ModeInvalid      = 0,
		ModeSolo         = 1,
		ModeMasterLooter = 2,
		ModeGroup        = 3,
		ModeRaid         = 4,
	};

	// LootFilterType bitmask - MacroQuestLS src/eqlib/EQClasses.h L1274
	enum FilterBit : uint32_t {
		FilterBitAutoRoll    = 1 << 0,
		FilterBitAlwaysNeed  = 1 << 1,
		FilterBitAlwaysGreed = 1 << 2,
		FilterBitNeverLoot   = 1 << 3,
	};

	const char* SubcommandName(uint16_t subcmd);
	const char* StateName(uint32_t state);
	const char* ModeName(uint32_t mode);

	// Bounds-checked cursor reader mirroring the client's CUnSerializeBuffer readers.
	// The client leaves a field at 0 when the packet is too short, so we flag
	// truncation instead of reading past the end.
	struct Reader {
		const uint8_t* data;
		uint32_t       size;
		uint32_t       cursor;
		bool           truncated;

		Reader(const uint8_t* data, uint32_t size)
			: data(data), size(size), cursor(0), truncated(false) {}

		uint32_t remaining() const { return size > cursor ? size - cursor : 0; }

		uint8_t read_u8() {
			if (remaining() < 1) { truncated = true; return 0; }
			return data[cursor++];
		}

		uint16_t read_u16() {
			if (remaining() < 2) { truncated = true; return 0; }
			uint16_t value = static_cast<uint16_t>(data[cursor] | (data[cursor + 1] << 8));
			cursor += 2;
			return value;
		}

		uint32_t read_u32() {
			if (remaining() < 4) { truncated = true; return 0; }
			uint32_t value = static_cast<uint32_t>(
				data[cursor] | (data[cursor + 1] << 8) | (data[cursor + 2] << 16) | (data[cursor + 3] << 24)
			);
			cursor += 4;
			return value;
		}

		uint64_t read_u64() {
			if (remaining() < 8) { truncated = true; return 0; }
			uint64_t value = 0;
			for (uint32_t i = 0; i < 8; i++) {
				value |= static_cast<uint64_t>(data[cursor + i]) << (i * 8);
			}
			cursor += 8;
			return value;
		}

		// The client reads null-terminated strings here (CUnSerializeBuffer__GetString),
		// not fixed-width char[64] fields.
		std::string read_string(uint32_t max_len) {
			std::string out;
			while (cursor < size && out.size() < max_len) {
				uint8_t c = data[cursor++];
				if (c == 0) {
					break;
				}
				out.push_back(static_cast<char>(c));
			}
			return out;
		}
	};

	// One-line, human readable description of a payload. Falls back to a hex dump for
	// sub-commands whose layout is not yet confirmed.
	struct Filter {
		uint32_t   item_id = 0;
		uint32_t   filter_bits = 0;
		uint32_t   icon = 0;
		std::string name;
		// Two different objects share the +0x0C offset, which is what caused the confusion:
		//  - the wire item record (FUN_1400839b0) has +0x0C = shared/construct loot list quantity and
		//    +0x14 = personal loot list quantity. Dispatcher case 0x0e requires `quantity <= entry[0xC]`
		//    to move the amount from the shared pool to the personal pool, so a zero here means the
		//    transaction reply matches nothing and CLootInProgress (record +0x6C) is never cleared - the
		//    second click then prints string 643 "You cannot perform that action right now".
		//  - the LootDetails entry (AdvancedLootItemNPC, built by FUN_14009c0a0) has +0x0C = Locked,
		//    derived from the group record's first byte, not from this field.
		uint32_t   quantity = 0;  // +0x0C shared / construct loot list
		uint32_t   personal = 0;  // +0x14 personal loot list

		// Group record wire field 3 -> record +4. FUN_14014f5f0 computes `value * 1000 + clock`, so
		// this is an expiration in SECONDS, not a quantity. Writing a quantity here made the corpse
		// row expire about a second after it was created - that is why the window closed by itself.
		// Use the corpse decay time, or a large value for the login filter push.
		uint32_t   expiration = 3600;

		// Group record wire field 1 -> record +0x146c. FUN_14009c0a0 stores
		// `(record + 0x146c == 0)` into the LootDetails entry at +0xC, and FUN_1400a41c0 reads that
		// byte as Locked. So this byte must be 1 for players who have loot rights (were present at
		// the kill) - sending 0 marks every row as locked.
		uint8_t    present_at_kill = 0;

		// Group record wire field 4 -> record +0, which FUN_14014f5f0 uses as the key of the
		// corpse/loot-list hash (manager + 0, 30 buckets). For a corpse delivery this is the corpse
		// entity id; for the login filter push the group record doubles as the filter record so the
		// item id is used. Later sub-commands (0x09, 0x0e, 0x0f) look rows up by this key, so the
		// server must reuse the value it wrote at row creation.
		uint32_t   corpse_key = 0;
	};

	// A corpse delivery is ONE group record - the corpse itself - with every item on it as a nested
	// item record. The group record's string is the NPC name the window shows in its NPC Name column
	// (FUN_14014f5f0 runs CEverQuest__trimName over it), so it must not be an item name. The item name
	// lives in the nested record only.
	struct CorpseGroup {
		uint32_t      corpse_key = 0;      // group record +0 - corpse entity id
		std::string   npc_name;            // group record +0xd - NPC name
		uint32_t      expiration = 3600;   // group record +4 - seconds, packed as value * 1000 + clock
		uint8_t       present_at_kill = 0; // group record +0x146c - loot-rights flag
		std::vector<Filter> items;         // nested item records
	};

	// Wire writer mirroring the client's serialize side (little endian, null-terminated strings).
	class Writer {
	public:
		std::vector<uint8_t> buffer;

		void write_u8(uint8_t value) { buffer.push_back(value); }

		void write_u16(uint16_t value) {
			buffer.push_back(static_cast<uint8_t>(value & 0xff));
			buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
		}

		void write_u32(uint32_t value) {
			for (uint32_t i = 0; i < 4; i++) {
				buffer.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xff));
			}
		}

		void write_u64(uint64_t value) {
			for (uint32_t i = 0; i < 8; i++) {
				buffer.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xff));
			}
		}

		// The client reads strings with CUnSerializeBuffer__GetString: raw bytes until NUL.
		void write_string(const std::string& value, uint32_t max_len = 64) {
			uint32_t len = static_cast<uint32_t>(value.size()) < max_len ? static_cast<uint32_t>(value.size()) : max_len;
			for (uint32_t i = 0; i < len; i++) {
				buffer.push_back(static_cast<uint8_t>(value[i]));
			}
			buffer.push_back(0);
		}
	};

	// subcmd 0x03 reply: field_a, name, count, count x record, then two optional blocks we leave empty.
	// Record wire order (FUN_140086370): u8, u8, u32 id, u32 filter_bits, u32 icon, string name.
	EQApplicationPacket* BuildFilterSetPacket(uint32_t mode, const std::string& name, const std::vector<Filter>& filters);

	// subcmd 0x03 corpse delivery: one group record (the corpse) with the corpse items nested inside.
	EQApplicationPacket* BuildCorpseGroupPacket(uint32_t mode, const std::string& name, const CorpseGroup& group);

	// The rebuild form of subcmd 0x03: several group records in one packet. FUN_140153970 clears the filter
	// hash and both panes (window vtable +0x3b0) before re-adding, so this is the only server-side way to
	// make a row disappear. Sending zero group records is how an empty corpse is dropped from the window.
	EQApplicationPacket* BuildCorpseGroupsPacket(uint32_t mode, const std::string& name, const std::vector<CorpseGroup>& groups);

	// subcmd 0x11: item_id, assignee, state, flag.
	EQApplicationPacket* BuildStatePacket(uint32_t item_id, uint32_t assignee, uint32_t state, uint8_t flag);

	// subcmd 0x07 - FUN_1400859e0: u64, u32, u32
	EQApplicationPacket* BuildCorpseRowNotifyPacket(uint64_t item_id, uint32_t field_b, uint32_t quantity);

	// subcmd 0x09 - reader FUN_140086e00: u16 action, u32 field_a, u32 field_b, u8 flag.
	// FUN_140151810 hashes field_a into the filter hash at manager + 0x148 (20 buckets) and matches
	// record + 0 against field_b, so both must be the item id for the lookup to land. With action 0 the
	// switch does nothing (no chat line, no hash mutation) and the handler falls through to
	// FUN_1400a8960(wnd, record + 8, record + 0x10, 0), which is the only server->client path that
	// touches a Personal Loot row: it clears row + 0x6D (LootInProgress).
	EQApplicationPacket* BuildItemActionPacket(uint16_t action, uint32_t field_a, uint32_t field_b, uint8_t flag);

	// subcmd 0x0e - FUN_140085890 / reader FUN_140086cb0: u32 transaction_id, u32 corpse_key,
	// u64 item_id, u16 quantity, u8 flag, string name. The client moves `quantity` from the shared
	// pool (entry +0xC) to the personal pool (entry +0x14) and prints the chat line when the name
	// matches the local player and flag == 0.
	EQApplicationPacket* BuildTransactionPacket(
		uint32_t record_field_c,   // wire field 1 -> struct +8, compared with record +0xC by FUN_1400a42f0
		uint32_t corpse_key,       // wire field 2 -> struct +0xC, the corpse/loot-list hash key
		uint64_t item_id,
		uint16_t quantity,
		uint8_t flag,
		const std::string& name
	);

	std::string DescribePayload(const uint8_t* payload, uint32_t size);
}
