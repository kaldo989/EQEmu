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
	// Names marked UNCONFIRMED are placeholders until a capture confirms the layout.
	enum Subcommand : uint16_t {
		SubUnknown02        = 0x02, // C->S, FUN_140153570 - UNCONFIRMED
		SubFilterSetSync    = 0x03, // S->C, FUN_140153970 / reader FUN_1400874d0
		SubCoinSplit        = 0x06, // S->C, FUN_140152cb0 - UNCONFIRMED
		SubUnknown07        = 0x07, // C->S, FUN_1401522b0 - UNCONFIRMED
		SubAddFilter        = 0x08, // C->S only (FUN_140154cd0) - client adds a filter record and sends it
		SubUnknown09        = 0x09, // S->C, FUN_140151810 - UNCONFIRMED
		SubItemUpdate       = 0x0A, // S->C, FUN_140153400 / reader FUN_1400871e0
		SubRowUpdate        = 0x0B, // S->C, reader FUN_140087460
		SubUnknown0C        = 0x0C, // S->C, FUN_140154a90 - UNCONFIRMED
		SubBulkRows         = 0x0D, // S->C, reader FUN_140086ab0
		SubTransaction      = 0x0E, // BOTH, FUN_140151400 / reader FUN_140086cb0
		SubUnknown0F        = 0x0F, // S->C, FUN_1401509d0 - UNCONFIRMED
		SubRowUpdate10      = 0x10, // S->C, reader FUN_140086240
		SubSetState         = 0x11, // BOTH, FUN_14014fca0 / reader FUN_140087390
		SubItemAction       = 0x12, // C->S, FUN_140150240
		SubUnknown13        = 0x13, // S->C, FUN_140154200 - UNCONFIRMED
		SubStringMessage    = 0x14, // BOTH, FUN_140151400 / FUN_140152510 - UNCONFIRMED
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

	// subcmd 0x11: item_id, assignee, state, flag.
	EQApplicationPacket* BuildStatePacket(uint32_t item_id, uint32_t assignee, uint32_t state, uint8_t flag);

	std::string DescribePayload(const uint8_t* payload, uint32_t size);
}
