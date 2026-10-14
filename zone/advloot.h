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

#include "common/emu_versions.h"
#include "common/types.h"

#include "common/timer.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class EQApplicationPacket;

namespace AdvLoot {

	// Sub-commands of OP_AdvLoot (0x3175). The client reads a uint16 sub-command at
	// payload offset 0 and dispatches on it (FUN_140151c60 in the Laurion client).
	// All 19 slots are resolved from the client dispatcher + senders (advloot.md S3.10).
	//
	// NOTE (2026-10-15): the client also has a second, inbound-only AdvLoot opcode, 0x360e, that
	// reuses this same sub-command numbering. It is client->server only (no receive case exists) and
	// is currently unmapped here, so all three of its requests are dropped as OP_Unknown:
	//   0x0D {u32 ask_key}                  - FUN_140150f00, asks for the Manage Loot response list
	//   0x0F {u32 item_id, u32, string}     - FUN_140150c00, free-grab request
	//   0x14 {u32 key, u8 flag, u32 n, n x record} - FUN_1401528d0, corpse item list to chat
	// See advloot.md section 10.7.
	//
	// NOTE (2026-10-15): CManageLootWnd::WndNotification (vtable PTR_LAB_140894010 + 0x120 = FUN_1401a45e0)
	// was decompiled control by control. The Manage Loot window emits NONE of the 0x360e requests - every
	// one of its controls goes out on 0x3175:
	//   ML_ActionButton  state 0 -> FUN_1400a7640(qty, 0) -> FUN_1400a1b70(state 1|2) -> 0x0A (Ask)
	//   ML_ActionButton  state 1|2 -> FUN_1400a7690() -> FUN_14014fca0(key, 3) -> 0x11 (Stop)
	//   ML_ActionButton  state 3|4 -> FUN_1400a7640(qty, 1) -> FUN_14014fca0(key, 1|2) -> 0x11 (re-Ask)
	//   ML_FreeGrabButton state 0 -> FUN_1400a7570(qty) -> FUN_1400a1b70(state 5) -> 0x0A
	//   ML_RollButton    state 4|7 -> FUN_1400a7610() -> FUN_140150240(key) -> 0x12
	//   ML_GiveButton / ML_LeaveOnCorpse -> FUN_1400a75b0(name, flag, qty) -> FUN_140151400 -> 0x0E
	//   ML_AutoRoll      -> FUN_1400a5330(checked) -> FUN_14014fca0(key, 1|2) -> 0x11
	//   ML_PlayerList ND/GD checkbox -> FUN_1400a4d80(name, 3|2|0) -> FUN_1400a81b0 -> 0x0B
	//   ML_ItemIcon / item link (ctrl or alt) -> FUN_14040fa60 -> opcode 0x0497 item inspect
	// See advloot.md section 10.15.
	enum Subcommand : uint16_t {
		SubFilterSetRequest   = 0x02, // C->S only, FUN_140153570 - bare request, clears the filter hash first
		SubFilterSetSync      = 0x03, // BOTH, FUN_140153970 / reader FUN_1400874d0
		SubChatNotify         = 0x06, // S->C only, FUN_140152cb0 / reader FUN_140086d70
		SubCorpseRowNotify    = 0x07, // C->S only, FUN_1401522b0 / serializer FUN_1400859e0
		SubTransactionRequest = 0x08, // C->S only, FUN_140154cd0 / serializer FUN_140085aa0
		SubItemActionNotify   = 0x09, // S->C only, FUN_140151810 / reader FUN_140086e00
		// The answer to a 0x08 request. `action` selects a chat line and, for 1 and 2, erases the item from the
		// client's copy of the corpse. See LootAction below and advloot.md section 10.17.
		SubItemUpdate         = 0x0A, // BOTH, FUN_140154570 + FUN_140153400 / reader FUN_1400871e0
		// 0x0A is what the Manage Loot Ask button and the Free Grab button actually send. FUN_140154570 looks
		// the ask group up in the hash at manager+0x148[ask_key % 0x14], overwrites the record's state field
		// (node+0x48 == record+0x40) with the new state, and serialises the whole record with FUN_140085c80.
		SubRowUpdate          = 0x0B, // BOTH: C->S sender FUN_140154830, S->C reader FUN_140087460
		// FUN_1400a81b0 sends 0x0B with a NON-empty name and a real action whenever the master looter ticks an
		// ND or GD box in Manage Loot (FUN_1400a4d80 -> action 3 = Need, 2 = Greed, 0 = cleared), once per
		// LootDetails entry. FUN_1401a37a0, the ML_PlayerList sort comparator, confirms 3 = ND column and
		// 2 = GD column. So 0x0B is both the ask-key carrier and the per-looter response; the server should
		// store the choice and re-broadcast the group's answers on 0x0D.
		// NOT "SubAddRow". Verified 2026-10-08: FUN_140154a90 cannot insert a row. flag 0 updates the
		// need/greed/never choice on rows that already exist (FUN_1400a8350), flag != 0 removes a row
		// (FUN_1400a1340). See advloot.md section 8.1.9.
		SubChoiceUpdate       = 0x0C, // S->C only, FUN_140154a90 / reader FUN_140086ab0
		// 0x0D is NOT a bulk row insert. It is the Manage Loot window's looter-response list: the client
		// asks for it with opcode 0x360e subcmd 0x0D {u32 ask_key} when the Manage button is clicked on a
		// row whose state is 4 or 7, and the answer must come back on 0x3175 subcmd 0x0D. The body is
		// {u32 ask_key, u32, u64 item_id, u16 count, count x {u32 action, char[64] name}} and the client
		// matches ask_key against record+0x60 (its own ask-group id), not against the item id.
		// See advloot.md section 10.
		SubBulkRows           = 0x0D, // BOTH, FUN_14009f6b0 / reader FUN_140086ab0 (ManageLootWnd player list)
		SubTransaction        = 0x0E, // BOTH, FUN_140151400 + FUN_1400a42f0 / reader FUN_140086cb0
		// Sent by ML_GiveButton (flag 0, name = the looter chosen in ML_GiveSelection) and by ML_LeaveOnCorpse
		// (flag 1, name = the local player). Body from CAdvancedLootWnd__DoSharedAdvLootAction:
		// {u64 item_id, u32 LootDetails key, u32 ask_key, u16 quantity, u8 flag, char[64] name}, one packet per
		// LootDetails entry after the quantity is split against each entry's i16 quantity. Refused locally with
		// eqstr 0x283 if record+0x6C is already set.
		SubSingleTransfer     = 0x0F, // S->C only, FUN_1401509d0 / reader FUN_140086c50
		SubRowUpdate10        = 0x10, // S->C only, reader FUN_140086240
		// Clears record+0x6C (LootInProgress) on a row. See BuildRowUnlockPacket.
		SubSetState           = 0x11, // BOTH, FUN_14014fca0 / reader FUN_140087390
		// {u32 ask_key, u32 new_state}. FUN_14014fca0 validates the transition client-side before sending and
		// prints eqstr 0x165 "You cannot change this from state %s to %s" instead. Accepted: 1 -> {2,3},
		// 2 -> {1,3}, {3,4} -> {1,2}. Anything else never reaches the server, so the server may reject it too.
		SubItemAction         = 0x12, // C->S only, FUN_140150240 - requires record state == 4
		// Sent by ML_RollButton (and only when the row is in state 4 or 7). {u32 ask_key}; FUN_140150240 returns
		// false unless the ask group record's record+0x40 is already 4 Ask Completed, and the caller then
		// clears record+0x6C again.
		SubLootModeList       = 0x13, // S->C only, FUN_140154200 / reader FUN_140086fa0
		SubChatMessage        = 0x14, // S->C only, FUN_140152510 / reader FUN_140086e90
	};

	// eAdvLootState - MacroQuestLS src/eqlib/UI.h L1684.
	// Confirmed against the shipping client's behaviour 2026-10-15 (advloot.md section 10.15):
	//   5 FreeGrab          - FUN_1400a8610 sets it; FUN_1400a5450 case 5 swaps in ADLW_FreeGrabBtnTemplate;
	//                         FUN_1401a3930 hides ML_ActionButton + ML_AutoRoll for state 5 only.
	//   6 FixedAskAutoRoll  - pending: FUN_1400a5450 arms the countdown for states {1,2,6}, and FUN_1400a5330
	//                         refuses to toggle auto-roll on a state 6 row.
	//   7 FixedAskCompleted - completed: gets A_Roll* / ML_RollButton alongside 4, and is one of the two states
	//                         that let the Manage button send 0x360e / 0x0D.
	// The client's own 8-entry name table at 0x14086b400 (Waiting, Ask, Ask auto Roll, Stop, Ask Completed,
	// Fixed Ask auto Roll, Fixed Ask Completed, Transaction Removed) omits FreeGrab and is therefore shifted
	// one low for every state >= 5. It is display-only; do not use it to renumber this enum.
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

	// Per-participant opt-in. The client keeps its choice in the row flags (row +0x8e need,
	// +0x8f greed, +0x90 never) which are fed from its own ini loader, and no OP_AdvLoot
	// sub-command carries a choice - the 0x11 state is Ask/AutoRoll only (FUN_1400a5330 sets
	// row +0x68 bAutoRoll and sends 0x11 with state 1 or 2). So the server's choice record comes
	// from /advloot need|greed|never, which is also the only capture path for filter persistence.
	enum Choice : uint32_t {
		ChoiceNone  = 0,
		ChoiceNeed  = 1,
		ChoiceGreed = 2,
		ChoiceNever = 3,
	};

	// The action value the client's subcmd 0x0C handler passes to FUN_1400a8350 as *param_2. It is NOT the
	// same numbering as Choice above. FUN_1400a8350 writes the row's choice bytes directly:
	//   0 -> word at record+0x8E = 0 (need and greed cleared), byte at record+0x90 = 0 (never cleared)
	//   1 -> need and greed cleared, byte at record+0x90 = 1 (never)
	//   2 -> word at record+0x8E = 0x100, so byte record+0x8F = 1 (greed)
	//   3 -> word at record+0x8E = 0x001, so byte record+0x8E = 1 (need)
	// Row choice bytes are +0x8E need, +0x8F greed, +0x90 never.
	enum ClientChoiceAction : uint32_t {
		ClientChoiceClear = 0,
		ClientChoiceNever = 1,
		ClientChoiceGreed = 2,
		ClientChoiceNeed  = 3,
	};

	// The subcmd 0x0A action codes, read out of the switch in FUN_140151810 and resolved against
	// eqstr_us.txt. They are the client's own loot-refusal reasons, which is why they line up with the
	// failure paths in Corpse::AdvLootTransaction / LootCorpseItem.
	//
	// 1 and 2 both take the "the item is gone from the corpse" path; 2 additionally prints the stack
	// refusal. 4, 5, 6 and 7 only print - they leave the corpse list alone. 0 matches no case, so it is the
	// way to release a pending transaction (decrement DAT_140eb0c04 and drop the group record) without
	// saying anything.
	enum LootAction : uint16_t {
		ActionNone           = 0, // no case: silent, still removes the pending transaction
		ActionLooted         = 1, // silent; erases the entry when quantity == entry+0x14 and entry+0xC == 0
		ActionNoRoomStack    = 2, // eqstr 11308 "You cannot loot this entire stack of items, no room in your inventory."
		ActionLoreConflict   = 3, // eqstr  371 "You cannot loot this Lore Item.  You already have one."
		ActionCombat         = 4, // eqstr 5154 "You cannot loot while a hostile creature is aware of your presence."
		ActionNoRoom         = 5, // eqstr  372 "You cannot loot this item no room in your inventory."
		ActionNotAllowed     = 6, // eqstr 3562 "You are not allowed to loot the item: %1."
		ActionDead           = 7, // eqstr 11318 "You cannot loot while dead."
	};

	// The eqstr_us.txt id each action prints, or 0 for the silent ones.
	uint32_t LootActionStringId(uint16_t action);
	const char* LootActionName(uint16_t action);

	// Map the server's Choice onto the client's subcmd 0x0C action numbering.
	inline uint32_t ChoiceToClientAction(uint32_t choice)
	{
		switch (choice) {
			case ChoiceNeed:  return ClientChoiceNeed;
			case ChoiceGreed: return ClientChoiceGreed;
			case ChoiceNever: return ClientChoiceNever;
			case ChoiceNone:
			default:          return ClientChoiceClear;
		}
	}

	// The other direction. Subcmd 0x0b carries the client's numbering, not the server's, and the Manage Loot
	// ND / GD checkboxes are the only thing that produces a non-zero action there.
	inline uint32_t ClientActionToChoice(uint32_t action)
	{
		switch (action) {
			case ClientChoiceNeed:  return ChoiceNeed;
			case ClientChoiceGreed: return ChoiceGreed;
			case ClientChoiceNever: return ChoiceNever;
			case ClientChoiceClear:
			default:                return ChoiceNone;
		}
	}

	// OP_AdvLootManage (0x360e) sub-commands. The numbering is shared with the Subcommand enum above -
	// the client only ever sends these three on 0x360e and has no receive case for the opcode at all, so
	// every reply has to come back on OP_AdvLoot (advloot.md S10.7).
	enum ManageSubcommand : uint16_t {
		ManageAskResponses = 0x0D, // FUN_140150f00: u32 ask_key -> answer with subcmd 0x0D below
		ManageFreeGrab     = 0x0F, // FUN_140150c00: u32 ask_key (record+0x60), u32 item_id, empty string
		ManageItemList     = 0x14, // FUN_1401528d0: u32 key, u8 flag, u32 count, count x item record
	};

	// The per-looter value of a subcmd 0x0D response row. FUN_1401a2b40 adds one ML_PlayerList row per
	// entry and FUN_1405abcc0(wrapper, value) ticks child #value of that row; ML_PlayerList's columns are
	// Player / ND / GD (EQUI_ManageLootWnd.xml, tooltip "List of players responding to an Ask action"),
	// so child 0 is Need and child 1 is Greed:
	//   3 -> ND ticked
	//   2 -> GD ticked
	//   1 -> the local player's own row; the two checkboxes are hidden for any other name
	//   any other value (0xffffffff) -> neither ticked
	// Members the server does not list are appended by FUN_1401a3000 itself with action 0, which also
	// renders as unchecked, so a reply only has to carry the players who actually responded.
	enum LooterResponse : uint32_t {
		ResponseSelf  = 1,
		ResponseGreed = 2,
		ResponseNeed  = 3,
		ResponseNone  = 0xffffffff,
	};

	inline uint32_t ChoiceToResponse(uint32_t choice)
	{
		switch (choice) {
			case ChoiceNeed:  return ResponseNeed;
			case ChoiceGreed: return ResponseGreed;
			default:          return ResponseNone;
		}
	}

	const char* SubcommandName(uint16_t subcmd);
	const char* ManageSubcommandName(uint16_t subcmd);
	const char* StateName(uint32_t state);
	const char* ModeName(uint32_t mode);
	const char* ChoiceName(uint32_t choice);
	const char* ClientChoiceActionName(uint32_t action);
	const char* LooterResponseName(uint32_t response);

	// The +0x20 optional block of an item record (FUN_1400839b0 -> FUN_1405647b0). The client copies the
	// bytes verbatim into a buffer at record +0x20 and FUN_140576230 uses that buffer as the payload of the
	// row's link widget, so the content has to be the client's own serialized link form: 0x12, the fixed
	// width hex body, the display text, 0x12. SayLinkEngine is the only server side code that emits a body
	// whose leading etag is inside '0' - '9', which the client needs to index its link size table; anything
	// else makes it read a table entry as a length. Returns an empty string when the item is unknown or the
	// engine reports a link error, which makes the caller send the zero length block (no link).
	std::string BuildItemLink(uint32_t item_id, EQ::versions::ClientVersion client_version);


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

		// The +0x20 optional block payload is a byte run, not a string: FUN_140563220 copies the declared
		// length verbatim and appends the terminator itself, so the payload has no terminator to stop at.
		std::string read_raw(uint32_t count) {
			std::string out;
			while (cursor < size && out.size() < count) {
				out.push_back(static_cast<char>(data[cursor++]));
			}
			if (out.size() < count) {
				truncated = true;
			}
			return out;
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

	// A parsed subcmd 0x0A record. Reader FUN_1400871e0, serializer FUN_140085c80 - both walk the fields in
	// this order, which the send side proves: it copies the nested-entry count into record+0x10 and then
	// serialises record+0x10 as a single u64, so the count is the low half of that u64 and is also written
	// again as its own u32 straight after the seconds field.
	//
	// In-memory target offsets (record = the ask-group record, node+8 of the hash at manager+0x148):
	//   +0x00 ask_key   +0x10 count   +0x14 field_14   +0x18 expiry clock   +0x20 flag
	//   +0x30 nested-entry array   +0x38 array size   +0x40 state
	//
	// The seconds field is relative: the serializer writes `record+0x18 - clock` and the receive case adds
	// the client's own clock back (0x140151d38: `MOV ECX, [*(0x140e35f70)+0x16c]` / `ADD RDX, RCX`) before
	// handing it to CAdvancedLootWnd__DoAdvLootAction, which stores it at row+0x88 as the countdown deadline.
	// That settles the record+0x88 question from section 8: it is the ask deadline, not a LootDetails capacity.
	struct ItemUpdate {
		uint32_t ask_key  = 0;   // FUN_1400aa1b0 matches this against record+0x60 of every corpse row
		uint32_t field_14 = 0;   // high half of the u64 that follows the key
		uint32_t state    = 0;   // State; written to record+0x40 before serialising
		uint8_t  flag     = 0;   // the client only calls DoAdvLootAction when this is 0 (0x140151d5b)
		uint32_t seconds  = 0;   // remaining ask window
		std::vector<std::pair<uint32_t, uint32_t>> entries;   // {LootDetails key, quantity}
	};

	bool ReadItemUpdate(Reader& reader, ItemUpdate& out);

	// A parsed subcmd 0x0E transaction. Reader FUN_140086cb0, serializer FUN_140085890, and
	// CAdvancedLootWnd__DoSharedAdvLootAction is what fills it: it walks the row's LootDetails array
	// (record+0x78 data, record+0x80 count, stride 0x50, key at entry+0, i16 quantity at entry+4) and emits
	// one packet per entry after clamping each entry's quantity against what is left to move.
	//
	// Struct offsets the client uses: +0x00 item id, +0x08 entry key, +0x0C group key, +0x10 quantity,
	// +0x12 flag, +0x13 name[64].
	//
	// The group key is read from record+0x60 on the send side but is hashed into the corpse hash at
	// manager+0 / manager+8 and compared against node+0x51c8 on the receive side (0x140151e47-0x140151e68),
	// so the two sides disagree and the server has to accept either reading. See advloot.md section 10.16.
	//
	// flag 0 means "give to name": the client creates a Personal Loot row for the viewer only when the name
	// matches the local player and flag is 0 (0x140151f11-0x140151f1f). flag 1 is ML_LeaveOnCorpse, which
	// sends the local player's own name and leaves the quantity in the shared pool.
	struct Transaction {
		uint32_t    entry_key = 0;   // wire field 1 -> struct+0x08
		uint32_t    group_key = 0;   // wire field 2 -> struct+0x0C
		uint64_t    item_id   = 0;   // wire field 3 -> struct+0x00
		uint16_t    quantity  = 0;   // wire field 4 -> struct+0x10
		uint8_t     flag      = 0;   // wire field 5 -> struct+0x12
		std::string name;            // wire field 6 -> struct+0x13
	};

	bool ReadTransaction(Reader& reader, Transaction& out);

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

		// The +0x20 optional block is NOT NUL terminated. FUN_1405647b0 reads an int32 length and, when it
		// is positive, FUN_140563220 copies exactly that many bytes into the record buffer and appends the
		// terminator itself - so a serialized link, which contains 0x12 delimiters and no NUL, has to be
		// written as raw bytes with no terminator of its own.
		void write_raw(const std::string& value) {
			for (char ch : value) {
				buffer.push_back(static_cast<uint8_t>(ch));
			}
		}
	};

	// The client version whose item-link form the +0x20 block is built for. It has to be the version the
	// receiving client actually speaks: the client validates a link body against its own fixed tag width
	// (TagSizes[ETAG_ITEM] - 3, which is 90 on a Laurelion-era client and 56 on RoF2) and rejects the whole
	// link when the length does not match, which is what makes Object Preview open empty.
	EQApplicationPacket* BuildFilterSetPacket(
		uint32_t mode,
		const std::string& name,
		const std::vector<Filter>& filters,
		EQ::versions::ClientVersion client_version
	);

	// subcmd 0x03 corpse delivery: one group record (the corpse) with the corpse items nested inside.
	EQApplicationPacket* BuildCorpseGroupPacket(
		uint32_t mode,
		const std::string& name,
		const CorpseGroup& group,
		EQ::versions::ClientVersion client_version
	);

	// The rebuild form of subcmd 0x03: several group records in one packet. FUN_140153970 clears the filter
	// hash and both panes (window vtable +0x3b0) before re-adding, so this is the only server-side way to
	// make a row disappear. Sending zero group records is how an empty corpse is dropped from the window.
	EQApplicationPacket* BuildCorpseGroupsPacket(
		uint32_t mode,
		const std::string& name,
		const std::vector<CorpseGroup>& groups,
		EQ::versions::ClientVersion client_version
	);

	// subcmd 0x0C - reader FUN_140086ab0, handler FUN_140154a90. Wire:
	//   u16 subcmd, u8 flag, u32 field_a (-> wnd+0x57C), u32 field_b (-> wnd+0x578),
	//   u64 field_c (-> wnd+0x570), u16 count, count x (u32 action, string name).
	// flag == 0 -> FUN_1400a8350(DAT_140d830d8, &row, 0, 0), and the row it is handed is
	//   +0x00 u32 action   (ClientChoiceAction)
	//   +0x08 u64 field_c  (compared with record+0x00, the item id)
	//   +0x10 u32 field_b  (compared with record+0x60)
	// A row matches when field_b == record+0x60, or when record+0x60 == 0 and field_c == item id, so
	// sending field_b = 0 with field_c = item_id matches by item id.
	// flag != 0 -> FUN_1400a1340(DAT_140d830d8, &row), which removes the matching row.
	// Neither form creates a row: only subcmd 0x03 (SubFilterSetSync) creates rows.
	EQApplicationPacket* BuildChoiceUpdatePacket(
		uint32_t field_a,
		uint32_t field_b,
		uint64_t field_c,
		uint8_t  remove_row,   // 0 = apply the choice, non-zero = remove the matching row
		const std::vector<std::pair<uint32_t, std::string>>& actions   // (ClientChoiceAction, name)
	);

	// subcmd 0x0A - echo an ask-group record back on the wire. The client's receive case parses it, converts
	// `seconds` into an absolute deadline, calls CAdvancedLootWnd__DoAdvLootAction (which finds the corpse row
	// whose record+0x60 equals ask_key, stores the deadline at record+0x88 and repaints the row), then stores
	// the record in the ask-group table at manager+0x128. This is the packet that makes an Ask started by one
	// client show up as a running countdown in everybody else's window.
	EQApplicationPacket* BuildItemUpdatePacket(const ItemUpdate& update);

	// Convenience wrapper: push one item's need/greed/never choice into the client's window.
	EQApplicationPacket* BuildChoicePacket(uint64_t item_id, uint32_t choice, const std::string& name);

	// Convenience wrapper: drop one item's row from the client's window (subcmd 0x0C, flag non-zero).
	EQApplicationPacket* BuildRowRemovePacket(uint64_t item_id, const std::string& name);

	// subcmd 0x11: item_id, assignee, state, flag.
	EQApplicationPacket* BuildStatePacket(uint32_t item_id, uint32_t assignee, uint32_t state, uint8_t flag);

	// subcmd 0x07 - FUN_1400859e0: u64, u32, u32
	EQApplicationPacket* BuildCorpseRowNotifyPacket(uint64_t item_id, uint32_t field_b, uint32_t quantity);

	// subcmd 0x09 - reader FUN_140086e00: u16 action, u32 quantity, u32 transaction_id, u8 flag.
	//
	// This is the server's answer to a subcmd 0x08 loot request, and the transaction_id is the key that ties
	// the two together. FUN_140154cd0 (the 0x08 sender) writes its own counter into the request at +0x10
	// (`param_2[4] = DAT_140bbea70`), serialises it, then inserts a pending-transaction record into the hash
	// at manager+0x38 (30 buckets, count at manager+0x20, list head at manager+0x30) keyed by that same id:
	//   node+0x00 transaction_id   node+0x08 u64 item_id   node+0x10 corpse_key
	//   node+0x14 quantity         node+0x18 transaction_id (copy of the request)
	// It also increments DAT_140eb0c04, the client's "an action is in flight" counter.
	//
	// FUN_140151810 is the only thing that undoes either of those: it looks the transaction_id up in that
	// hash, decrements DAT_140eb0c04, and always calls FUN_140153fa0 to remove the record. An 0x08 that is
	// never answered therefore leaks a pending transaction and leaves the counter above zero, which makes
	// FUN_14018d700 report the UI as busy and makes FUN_14014ad90 bail out early.
	//
	// `quantity` is only used by actions 1 and 2, where it must equal the corpse item entry's personal pool
	// (entry+0x14) while the shared pool (entry+0xC) is zero; only then is the entry erased from the corpse's
	// item array (node+0x50, count node+8, stride 0x68) and the corpse group either dropped (vtable +0x3a0)
	// or refreshed (vtable +0x3a8).
	EQApplicationPacket* BuildItemActionPacket(uint16_t action, uint32_t quantity, uint32_t transaction_id, uint8_t flag);

	// subcmd 0x10 - reader FUN_140086240: u32 ask_key, u64 item_id, u32 loot_details_key.
	//
	// Clears the AdvLoot row's LootInProgress flag (record+0x6C), which is what unlocks a row for another
	// action. FUN_1400a42f0 (the 0x0e handler) also clears it, but only when manager+0x241 is set, the row
	// already has it set, and the packet's name matches the local player with flag 0 - so the client that
	// clicked Give on someone else's behalf, and any row whose transaction was refused, stay locked forever
	// unless the server sends this. Two addressing modes, matching the handler:
	//   ask_key != 0  -> the first row whose record+0x60 equals ask_key
	//   ask_key == 0  -> the row whose item id equals item_id and which has a LootDetails entry whose key
	//                    equals loot_details_key
	EQApplicationPacket* BuildRowUnlockPacket(uint32_t ask_key, uint64_t item_id, uint32_t loot_details_key);

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

	// Client string block eqstr_us.txt 11300-11323 - the Advanced Loot wording. Using the client's
	// own ids keeps the chat identical to the live client.
	namespace StringId {
		const uint32 MasterLooter        = 11300;  // "Master Looter"
		const uint32 GroupMasterLooter   = 11301;  // "Group Master Looter"
		const uint32 RaidMasterLooter    = 11302;  // "Raid Master Looter"
		const uint32 MLDelegated         = 11303;  // "%1 has been delegated Raid Master Looter."
		const uint32 MLUndelegated       = 11304;  // "%1 is no longer delegated Raid Master Looter."
		const uint32 CorpseAccessList    = 11305;  // "You have been added to the corpse access list."
		const uint32 AdvLootCorpse       = 11306;  // "This corpse is using advanced looting..."
		const uint32 MLLooted            = 11307;  // "The master looter, %1, looted %2 from the corpse%3."
		const uint32 NoRoomForStack      = 11308;  // "You cannot loot this entire stack of items, no room in your inventory."
		const uint32 YouLeftOnCorpse     = 11309;  // "--You left %1 %2 on %3.--"
		const uint32 GivenTo             = 11310;  // "%1 %2 %3 given to %4."
		const uint32 AlreadyHasLore      = 11311;  // "%1 already has %2 and it is lore."
		const uint32 DoesNotWant         = 11312;  // "%1 does not want %2. It is either on their never list or they have selected No."
		const uint32 LeftOnCorpse        = 11313;  // "--%1 left %2 %3 on %4.--"
		const uint32 WonRoll             = 11314;  // "%1 won the %2 roll on %3 item(s): %4 with a roll of %5."
		const uint32 NoOneInterested     = 11315;  // "No one was interested in the %1 item(s): %2..."
		const uint32 RolledOn            = 11316;  // "%1 rolled a %2 on %3."
		const uint32 CannotNeedGreedLore = 11317;  // "You cannot choose to need or greed on %1 because you already have one and it is lore."
		const uint32 CannotLootWhileDead = 11318;  // "You cannot loot while dead."
		const uint32 AskAlreadyCompleted = 11319;  // "The ask has already completed. Your selection has been changed from %1 back to %2."
		const uint32 MLUpdatedChoice     = 11320;  // "The master looter has updated your choice on %1 from %2 to %3."
		const uint32 ItemsLocked         = 11321;  // "These item(s) are locked because you were not present when the enemy died..."
		const uint32 SplitShare          = 11322;  // "Alive %1 members received %2 as their share of the split from the corpse."
		const uint32 GrabbedFrom         = 11323;  // "%1 grabbed %2 %3 from %4."
	}

	// One row on one corpse: the roll the server owns. The client's row state mirrors this.
	struct Participant {
		uint32_t    character_id = 0;
		std::string name;
		uint32_t    choice = ChoiceNone;
		uint32_t    roll = 0;          // 1..1000, drawn at resolution
		uint32_t    opt_in_order = 0;  // monotonic counter, the tie-break (decision 2)
	};

	struct Roll {
		uint32_t      corpse_key = 0;
		uint32_t      item_id = 0;
		std::string   item_name;
		uint32_t      state = StateWaiting;
		Timer         ask_timer;
		bool          armed = false;   // timer running
		std::vector<Participant> participants;
	};

	// subcmd 0x13 - FUN_140086fa0: u32, u32, u8, u16 count, count x (u16 mode, string name[64]).
	// The client compares each name with the local name and prints the loot mode from the string
	// table at 0x14086b440 (invalid/solo/molo/group/raid). This is the master looter announcement.
	EQApplicationPacket* BuildLootModePacket(
		uint32_t field_a,
		uint32_t field_b,
		uint8_t  field_c,
		const std::vector<std::pair<uint16_t, std::string>>& members
	);

	// subcmd 0x0D - the answer to a 0x360e / 0x0D request. Reader FUN_140086ab0, handler case 0x140151f43.
	// Wire (NO flag byte, unlike subcmd 0x0C which reads one first):
	//   u16 subcmd, u32 ask_key, u32 field_b, u64 field_c, u16 count,
	//   count x (u32 response, string name)
	// FUN_14009f6b0 walks the corpse list and matches ask_key against record+0x60, the client's own
	// ask-group id (allocated by FUN_1401501c0 from the counter at manager+0x1e8) - not the item id, so
	// ask_key must be echoed back exactly or nothing matches and the window stays empty. field_b and
	// field_c are not read by the 0x0D handler at all; they are carried for symmetry with the 0x0B sender.
	// Each entry becomes one ML_PlayerList row: see LooterResponse above.
	EQApplicationPacket* BuildAskResponsePacket(
		uint32_t ask_key,
		uint32_t field_b,
		uint64_t field_c,
		const std::vector<std::pair<uint32_t, std::string>>& responses
	);

	// subcmd 0x0F - reader FUN_140086c50: u64 key, string name. FUN_1401509d0 moves one unit from the
	// shared pool (entry +0xC) to the personal pool (entry +0x14) and prints the chat line when the name
	// matches the local player. This is the reply to a 0x360e / 0x0F Free Grab.
	EQApplicationPacket* BuildSingleTransferPacket(uint64_t key, const std::string& name);

	// subcmd 0x14 - reader FUN_140086e90: u32 key, u8 flag, u32 count, count x item record (FUN_140085440,
	// the same layout WriteItemRecord emits). FUN_140152510 looks key up in the loot-list hash and writes
	// the item names to chat. The client builds this payload itself on 0x360e, so the server relays it.
	EQApplicationPacket* BuildItemListChatPacket(
		uint32_t key,
		uint8_t  flag,
		const std::vector<Filter>& items,
		EQ::versions::ClientVersion client_version
	);

	// One-line, human readable description of a payload. Falls back to a hex dump for
	// sub-commands whose layout is not yet confirmed.
	std::string DescribePayload(const uint8_t* payload, uint32_t size);

	// Same for OP_AdvLootManage (0x360e), whose three sub-commands reuse the 0x3175 numbering.
	std::string DescribeManagePayload(const uint8_t* payload, uint32_t size);
}

