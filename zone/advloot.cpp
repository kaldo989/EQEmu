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
#include "common/eq_constants.h"
#include "common/eq_packet.h"
#include "common/eq_limits.h"
#include "common/eqemu_logsys.h"
#include "common/random.h"
#include "common/say_link.h"
#include "common/seperator.h"
#include "common/strings.h"
#include "fmt/format.h"
#include "zone/client.h"
#include "zone/corpse.h"
#include "zone/entity.h"
#include "zone/groups.h"
#include "zone/raids.h"
#include "zone/zonedb.h"

#include <cctype>
#include <cstring>
#include <list>

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
			case SubChoiceUpdate:  return "ChoiceUpdate";
			case SubBulkRows:      return "BulkRows";
			case SubTransaction:   return "Transaction";
			case SubSingleTransfer: return "SingleTransfer";
			case SubRowUpdate10:   return "RowUpdate10";
			case SubSetState:      return "SetState";
			case SubItemAction:    return "ItemAction";
			case SubRollResults:    return "RollResults";
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

	const char* ChoiceName(uint32_t choice) {
		switch (choice) {
			case ChoiceNone:  return "None";
			case ChoiceNeed:  return "Need";
			case ChoiceGreed: return "Greed";
			case ChoiceNever: return "Never";
			default:          return "UNKNOWN";
		}
	}

	const char* ClientChoiceActionName(uint32_t action) {
		switch (action) {
			case ClientChoiceClear: return "Clear";
			case ClientChoiceNever: return "Never";
			case ClientChoiceGreed: return "Greed";
			case ClientChoiceNeed:  return "Need";
			default:                return "UNKNOWN";
		}
	}

	const char* ManageSubcommandName(uint16_t subcmd) {
		switch (subcmd) {
			case ManageAskResponses: return "AskResponses";
			case ManageFreeGrab:     return "FreeGrab";
			case ManageItemList:     return "ItemList";
			default:                 return "UNKNOWN";
		}
	}

	const char* LooterResponseName(uint32_t response) {
		switch (response) {
			case ResponseSelf:  return "Self";
			case ResponseGreed: return "GD";
			case ResponseNeed:  return "ND";
			case ResponseNone:  return "None";
			default:            return "UNKNOWN";
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

		// +0x20 length-prefixed block. FUN_1405647b0 treats a non positive length as "no block" and does
		// not consume any payload, so only read the run when the length is positive.
		int32_t  block_len = static_cast<int32_t>(reader.read_u32());
		std::string block;
		if (block_len > 0) {
			block = reader.read_raw(static_cast<uint32_t>(block_len));
		}

		std::string block_text;
		if (block_len <= 0) {
			block_text = "none";
		}
		else if (block.size() >= 2 && block.front() == '\x12' && block.back() == '\x12') {
			// SayLinkEngine form: 0x12, fixed width hex body, display text, 0x12.
			block_text = fmt::format(
				"link len={} body='{}' text='{}'",
				block.size(),
				block.substr(1, EQ::constants::GetSayLinkBodySize(EQ::versions::ClientVersion::RoF2)),
				block.substr(1 + EQ::constants::GetSayLinkBodySize(EQ::versions::ClientVersion::RoF2),
				             block.size() - 2 - EQ::constants::GetSayLinkBodySize(EQ::versions::ClientVersion::RoF2))
			);
		}
		else {
			block_text = fmt::format("len={} raw=[{}]", block_len, HexDump(reinterpret_cast<const uint8_t*>(block.data()), static_cast<uint32_t>(block.size())));
		}

		return fmt::format(
			"item id={} icon={} shared={} managed={} flag={} value={} personal={} name='{}' +0x20={}",
			item_id,
			icon,
			shared_qty,
			managed,
			flag,
			value,
			personal_qty,
			IsPrintableName(name) ? name : "",
			block_text
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
				uint32_t    mode  = reader.read_u32();
				std::string name  = reader.read_string(64);
				uint32_t    count = reader.read_u32();

				out += fmt::format(" mode={}({}) name='{}' count={}", mode, ModeName(mode), name, count);

				for (uint32_t i = 0; i < count && !reader.truncated; i++) {
					uint8_t present = reader.read_u8();   // +0x146c loot-rights flag
					reader.read_u8();
					uint32_t decay_expires_in = reader.read_u32();  // +4 - seconds, packed as value * 1000 + clock
					uint32_t corpse_key = reader.read_u32();  // +0 hash key
					uint32_t nested  = reader.read_u32();
					std::string gname = reader.read_string(64);

					out += fmt::format(
						"\n  group present={} key={} decay_expires_in={} nested={} name='{}'",
						present, corpse_key, decay_expires_in, nested, IsPrintableName(gname) ? gname : ""
					);

					for (uint32_t j = 0; j < nested && !reader.truncated; j++) {
						out += "\n    " + DescribeItemRecord(reader);
					}
				}

				// Whatever follows the group records is the client's own filter-set tail and its layout is not
				// confirmed. An earlier version of this describer guessed a u8 + u16 count + count x {u32, u64,
				// u32} block and a second flag, which printed plausible-looking numbers out of bytes nobody had
				// verified. Report the size and the bytes instead - the handler already logs the whole packet as
				// hex, so nothing is lost and nothing is invented.
				if (reader.remaining()) {
					out += fmt::format(
						" tail bytes={} raw=[{}]",
						reader.remaining(),
						HexDump(reader.data + reader.cursor, reader.remaining())
					);
				}
				break;
			}

			case SubCorpseRowNotify: {
				// FUN_1400859e0 serializer: u64, u32, u32. Confirmed by the first live capture
				// (2026-10-06): 07 00 | 3F EA 00 00 00 00 00 00 | 00 00 00 00 | 00 00 01 00
				// item_id = 59903, corpse_key = 0, quantity = 1.
				// FUN_14009e460 sends it on the Never tick when the filter record exists, the window is in
				// managed mode (wnd + 0x18) and the filter carries the NeverLoot bit (1 << 3 = 8), from
				// record+0x00 (item id), group record+0 (the corpse key) and record+0x14 (the personal pool).
				// The capture's corpse_key is 0 because that click came from a filter row, which has no corpse.
				uint64_t item_id    = reader.read_u64();
				uint32_t corpse_key = reader.read_u32();
				uint32_t quantity   = reader.read_u32();

				out += fmt::format(" item_id={} corpse_key={} quantity={}", item_id, corpse_key, quantity);
				break;
			}

			case SubItemUpdate: {
				// FUN_1400871e0 - see AdvLoot::ItemUpdate. Sent by the Manage Loot Ask button (state 1 or 2)
				// and the Free Grab button (state 5) via FUN_140154570.
				ItemUpdate update;
				ReadItemUpdate(reader, update);

				out += fmt::format(
					" ask_key={} item_id={} state={} ({}) remaining_ms={} count={}",
					update.ask_key,
					update.item_id,
					update.state,
					AdvLoot::StateName(update.state),
					update.remaining_ms,
					update.entries.size()
				);

				for (const auto& entry : update.entries) {
					out += fmt::format("\n  entry key={} qty={}", entry.first, entry.second);
				}
				break;
			}

			case SubRowUpdate10: {
				// FUN_140086240 -> FUN_14009e1e0: clears record+0x6C (LootInProgress) on one row.
				uint32_t ask_key  = reader.read_u32();
				uint64_t item_id  = reader.read_u64();
				uint32_t entry_key = reader.read_u32();

				out += fmt::format(
					" ask_key={} item_id={} entry_key={} ({})",
					ask_key,
					item_id,
					entry_key,
					ask_key ? "row matched by ask group" : "row matched by item id + LootDetails key"
				);
				break;
			}

			case SubRowUpdate: {
				// FUN_140087460 - same layout as 0x10 but a different handler (FUN_1400a8350, flag forced to 1)
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
				// FUN_140086cb0 - see AdvLoot::Transaction. Sent by ML_GiveButton (flag 0) and
				// ML_LeaveOnCorpse (flag 1) via FUN_140151400, one packet per LootDetails entry.
				Transaction txn;
				ReadTransaction(reader, txn);

				out += fmt::format(
					" ask_key={} corpse_key={} item_id={} qty={} flag={}({}) name='{}'",
					txn.ask_key,
					txn.corpse_key,
					txn.item_id,
					txn.quantity,
					txn.flag,
					txn.flag ? "leave on corpse" : "give to name",
					txn.name
				);
				break;
			}

			case SubSetState: {
				// FUN_140087390, confirmed against the send side FUN_14014fca0 -> FUN_140085ec0 and the receive
				// side at 0x140151fcb: {u32 ask_key, u32 remaining_ms, u32 state, u8 flag}.
				uint32_t ask_key      = reader.read_u32();
				uint32_t remaining_ms = reader.read_u32();
				uint32_t state        = reader.read_u32();
				uint8_t  flag         = reader.read_u8();

				out += fmt::format(
					" ask_key={} remaining_ms={} state={}({}) flag={}",
					ask_key,
					remaining_ms,
					state,
					StateName(state),
					flag
				);
				break;
			}

			case SubItemAction: {
				// FUN_140150240 - subcmd + u32 ask key (record+0x60). Sent by ML_RollButton, and the sender
				// refuses unless the ask-group record's record+0x40 is already 4 (Ask Completed).
				uint32_t ask_key = reader.read_u32();
				out += fmt::format(" ask_key={}", ask_key);
				break;
			}

			case SubTransactionRequest: {
				// FUN_140085aa0 serializer, confirmed against its caller FUN_1400a9cc0 (the green loot button),
				// which fills the struct from the personal row and its LootDetails entries:
				//   u64 item_id        <- row +0x00
				//   u32 corpse_key     <- LootDetails entry +0x00 (the group record hash key, i.e. the corpse id)
				//   u32 quantity       <- clamped against the entry's i16 quantity at entry +0x04
				//   u32 transaction_id <- stamped by FUN_140154cd0 from DAT_140bbea70
				//   u8  flag           <- the caller's param_4
				// One packet per LootDetails entry; 23 byte payload in the 2026-10-06 live capture.
				uint64_t item_id     = reader.read_u64();
				uint32_t corpse_key  = reader.read_u32();
				uint32_t quantity    = reader.read_u32();
				uint32_t transaction = reader.read_u32();
				uint8_t  flag        = reader.read_u8();

				out += fmt::format(
					" item_id={} corpse_key={}({}) quantity={} transaction={} flag={}",
					item_id, corpse_key, ModeName(corpse_key), quantity, transaction, flag
				);
				break;
			}

			case SubChatNotify: {
				// FUN_140086d70 -> FUN_140152cb0. Money only: the u64 is a total in EQ's canonical unit and
				// FUN_140084bc0 splits it platinum / gold / silver / copper with the same 1000 / 100 / 10 chain
				// Client::AddMoneyToPP uses. There is no trailing string - the master-looter name in the 11307
				// branch comes from the client's own manager + 0x1f4, not from this packet.
				uint8_t  announce = reader.read_u8();
				uint64_t money    = reader.read_u64();
				uint8_t  personal = reader.read_u8();
				uint32_t bonus    = reader.read_u32();

				const char *line = "?";
				if (announce && !personal) {
					line = "11322 group/raid split";
				} else if (announce && personal) {
					line = "12071 you receive your split";
				} else if (!announce && personal) {
					line = "12072 you receive from corpse (managed mode only)";
				} else {
					line = "11307 master looter looted";
				}

				out += fmt::format(
					" announce={} money={} ({}) personal={} bonus={} -> {}",
					announce,
					money,
					Strings::Money(
						static_cast<uint32>(money / 1000),
						static_cast<uint32>(money / 100 % 10),
						static_cast<uint32>(money / 10 % 10),
						static_cast<uint32>(money % 10)
					),
					personal,
					bonus,
					line
				);
				break;
			}

			case SubItemActionNotify: {
				// FUN_140086e00 -> FUN_140151810. The transaction_id is the key: FUN_140154cd0 wrote it into the
				// 0x08 request and registered a pending transaction in the hash at manager+0x38 (30 buckets).
				uint16_t action        = reader.read_u16();
				uint32_t quantity      = reader.read_u32();
				uint32_t transaction   = reader.read_u32();
				uint8_t  flag          = reader.read_u8();

				out += fmt::format(
					" action={}({}) quantity={} transaction_id={} flag={} string={}",
					action,
					LootActionName(action),
					quantity,
					transaction,
					flag,
					LootActionStringId(action)
				);
				break;
			}

			case SubChoiceUpdate: {
				// FUN_140154a90 - u8 flag then the FUN_140086ab0 block. flag == 0 applies the need/greed/never
				// choice to rows that already exist (FUN_1400a8350); flag != 0 removes the matching row
				// (FUN_1400a1340). Neither form creates a row.
				uint8_t     flag   = reader.read_u8();
				uint32_t    a      = reader.read_u32();
				uint32_t    b      = reader.read_u32();
				uint64_t    c      = reader.read_u64();
				uint32_t    count  = reader.read_u16();

				out += fmt::format(
					" {} a={} b={} c(item_id)={} count={}",
					flag ? "REMOVE-ROW" : "APPLY-CHOICE", a, b, c, count
				);

				for (uint32_t i = 0; i < count && !reader.truncated; i++) {
					uint32_t    action = reader.read_u32();
					std::string name   = reader.read_string(64);
					out += fmt::format(
						"\n  action={}({}) name='{}'",
						action, ClientChoiceActionName(action), IsPrintableName(name) ? name : ""
					);
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

			case SubRollResults: {
				// FUN_140086fa0. FUN_140154200 prints "%1 rolled a %2 on %3." per entry and hands the row
				// addressed by ask_key to the highest roller. See BuildRollResultsPacket.
				uint32_t ask_key = reader.read_u32();
				uint32_t b       = reader.read_u32();
				uint8_t  c       = reader.read_u8();
				uint32_t count   = reader.read_u16();

				out += fmt::format(" ask_key={} b={} c={} count={}", ask_key, b, c, count);

				for (uint32_t i = 0; i < count && !reader.truncated; i++) {
					uint16_t    roll = reader.read_u16();
					std::string name = reader.read_string(64);
					out += fmt::format(
						"\n  roll name='{}' roll={}",
						IsPrintableName(name) ? name : "", roll
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

	// OP_AdvLootManage (0x360e). Three request shapes, all client->server, all reusing the 0x3175
	// sub-command numbering (advloot.md S10.7).
	std::string DescribeManagePayload(const uint8_t* payload, uint32_t size) {
		Reader reader(payload, size);
		uint16_t subcmd = reader.read_u16();

		std::string out = fmt::format(
			"subcmd={:#04x} ({})",
			subcmd,
			ManageSubcommandName(subcmd)
		);

		switch (subcmd) {
			case ManageAskResponses: {
				// FUN_140150f00: a u16 subcmd and then the four bytes of the ask key, nothing else.
				uint32_t ask_key = reader.read_u32();
				out += fmt::format(" ask_key={}", ask_key);
				break;
			}

			case ManageFreeGrab: {
				// FUN_140150c00 -> FUN_1400857b0. Built at 0x1400abd39-0x1400abd66: [RBP-0x60] = record+0x60
				// (ask key), [RBP-0x5C] = *FUN_1400a23d0(record) (the row's LootDetails entry +0x00, the item
				// id), [RBP-0x58..] = 0 -> an empty name.
				uint32_t   ask_key = reader.read_u32();
				uint32_t   item_id = reader.read_u32();
				std::string name   = reader.read_string(64);
				out += fmt::format(
					" ask_key={} item_id={} name='{}'",
					ask_key,
					item_id,
					IsPrintableName(name) ? name : ""
				);
				break;
			}

			case ManageItemList: {
				// FUN_1401528d0 -> FUN_140085bb0: u32 key, u8 flag, u32 count, count x item record.
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

	std::string BuildItemLink(uint32_t item_id, EQ::versions::ClientVersion client_version)
	{
		const EQ::ItemData* item = database.GetItem(item_id);
		if (!item) {
			return std::string();
		}

		EQ::SayLinkEngine linker;
		linker.SetLinkType(EQ::saylink::SayLinkItemData);
		linker.SetItemData(item);
		linker.SetClientVersion(client_version);

		const std::string& link = linker.GenerateLink();
		if (linker.LinkError()) {
			// GenerateLink() replaces a rejected body with "<LINKER ERROR>"; sending that would put a
			// non link string where the client expects a link payload, so fall back to no block at all.
			return std::string();
		}

		return link;
	}

	// FUN_1400839b0 item record wire order: u64 item_id (+0x00), u32 icon (+0x08), u32 shared
	// quantity (+0x0C), u32 managed flag (+0x10), u8 flag (+0x18), u32 value (+0x1C), u32 personal
	// quantity (+0x14), string name (+0x28), u32 block (+0x20).
	static void WriteItemRecord(Writer& writer, const Filter& filter, EQ::versions::ClientVersion client_version)
	{
		// AddCorpse (FUN_14009e310) creates a Corpse Loot List row when +0x0C is non-zero and a
		// Personal Loot List row when +0x14 is non-zero. Only the shared pool is populated here: the
		// transaction (subcmd 0x0e) moves units out of the shared pool into personal, and the client
		// auto-loots any personal row it is given when LS_AutoLootAllCheckbox is checked
		// (FUN_14009f790 -> FUN_1400a9cc0 -> FUN_140154cd0), so pre-filling +0x14 loots the item for us.
		writer.write_u64(filter.item_id);
		writer.write_u32(filter.icon);              // +0x08 icon
		writer.write_u32(filter.shared_qty);        // +0x0C shared / construct pool
		writer.write_u32(filter.stack_size);        // +0x10 max stack - row+0x50; row+0x4C bStackable = 1 < this
		writer.write_u8(filter.no_drop);            // +0x18 no drop - row+0x54; 1 = no drop, 0 = droppable
		writer.write_u32(0);                        // +0x1C combo id - row+0x58. 0: no combo column in this schema.
		writer.write_u32(filter.personal_qty);      // +0x14 personal pool - keep 0 on the initial push
		writer.write_string(filter.name, 64);       // +0x28

		// +0x20 optional block. FUN_1405647b0 only materialises record +0x20 when the length is positive,
		// so an empty block is the "no link" case and is what older/broken clients expect.
		// The length has to match the receiving client's own tag width or it discards the link outright.
		std::string link;
		if (RuleB(Loot, AdvLootSendItemLinks)) {
			link = BuildItemLink(filter.item_id, client_version);
		}

		writer.write_u32(static_cast<uint32_t>(link.size()));
		if (!link.empty()) {
			writer.write_raw(link);
		}
	}

	// FUN_140086370 group record wire order: u8 -> +0x146c (loot-rights flag), u8 -> +3, u32 -> +4
	// (decay_expires_in), u32 -> +0 (hash key), u32 -> +8 (nested item count), string -> +0xd (name), then
	// nested_count item records.
	static void WriteGroupRecord(
		Writer& writer,
		uint32_t key,
		const std::string& name,
		uint32_t decay_expires_in,
		uint8_t present_at_kill,
		const std::vector<Filter>& items,
		EQ::versions::ClientVersion client_version
	) {
		writer.write_u8(present_at_kill);  // +0x146c loot-rights / presence flag
		writer.write_u8(0);                // +3
		writer.write_u32(decay_expires_in); // +4 - FUN_14014f5f0: value * 1000 + clock (seconds)
		writer.write_u32(key);             // +0 hash key
		writer.write_u32(static_cast<uint32_t>(items.size()));  // +8 nested item count
		writer.write_string(name, 64);     // +0xd name

		for (const auto& item : items) {
			WriteItemRecord(writer, item, client_version);
		}
	}

	EQApplicationPacket* BuildFilterSetPacket(
		uint32_t mode,
		const std::string& name,
		const std::vector<Filter>& filters,
		EQ::versions::ClientVersion client_version
	)
	{
		Writer writer;

		writer.write_u16(SubFilterSetSync);
		writer.write_u32(mode);
		writer.write_string(name, 64);
		writer.write_u32(static_cast<uint32_t>(filters.size()));

		for (const auto& filter : filters) {
			// The login filter push has no corpse, so the group record doubles as the filter record:
			// the key is the item id and the name is the item name.
			WriteGroupRecord(
				writer,
				filter.corpse_key ? filter.corpse_key : filter.item_id,
				filter.name,
				filter.decay_expires_in,
				filter.present_at_kill,
				{ filter },
				client_version
			);
		}

		// Optional blocks are present but empty - the reader only consumes them if there is room.
		writer.write_u8(0);
		writer.write_u8(0);

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	// The rebuild form of subcmd 0x03: several group records in one packet. Each group record is one corpse,
	// with that corpse's items nested inside, and the group name is the NPC name (the window's NPC Name
	// column), not an item name.

	EQApplicationPacket* BuildCorpseGroupsPacket(
		uint32_t mode,
		const std::string& name,
		const std::vector<CorpseGroup>& groups,
		EQ::versions::ClientVersion client_version
	)
	{
		Writer writer;

		writer.write_u16(SubFilterSetSync);
		writer.write_u32(mode);
		writer.write_string(name, 64);
		writer.write_u32(static_cast<uint32_t>(groups.size()));

		for (const auto& group : groups) {
			WriteGroupRecord(
				writer,
				group.corpse_key,
				group.npc_name,
				group.decay_expires_in,
				group.present_at_kill,
				group.items,
				client_version
			);
		}

		writer.write_u8(0);
		writer.write_u8(0);

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	// subcmd 0x0C. See the header note: the block is read by FUN_140086ab0 into window fields
	// +0x57C / +0x578 / +0x570 and then handed to FUN_1400a8350 (flag 0) or FUN_1400a1340 (flag != 0) as
	// {u32 action, <pad>, u64 item_id, u32 ask_key}. item_id is compared with the row record's item id at
	// record+0x00, ask_key with record+0x60, so ask_key = 0 plus item_id = the item matches by item id.
	EQApplicationPacket* BuildChoiceUpdatePacket(
		uint32_t ask_key_unused,
		uint32_t ask_key,
		uint64_t item_id,
		uint8_t  remove_row,
		const std::vector<std::pair<uint32_t, std::string>>& actions
	) {
		Writer writer;

		writer.write_u16(SubChoiceUpdate);
		writer.write_u8(remove_row ? 1 : 0);
		writer.write_u32(ask_key_unused);
		writer.write_u32(ask_key);
		writer.write_u64(item_id);
		writer.write_u16(static_cast<uint16_t>(actions.size()));

		for (const auto& action : actions) {
			writer.write_u32(action.first);
			writer.write_string(action.second, 64);
		}

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	EQApplicationPacket* BuildChoicePacket(uint64_t item_id, uint32_t choice, const std::string& name)
	{
		return BuildChoiceUpdatePacket(
			0,
			0,
			item_id,
			0,
			{ { ChoiceToClientAction(choice), name } }
		);
	}

	EQApplicationPacket* BuildRowRemovePacket(uint64_t item_id, const std::string& name)
	{
		return BuildChoiceUpdatePacket(
			0,
			0,
			item_id,
			1,
			{ { ClientChoiceClear, name } }
		);
	}

	EQApplicationPacket* BuildSetStatePacket(uint32_t ask_key, uint32_t remaining_ms, uint32_t state, uint8_t flag)
	{
		Writer writer;

		writer.write_u16(SubSetState);
		writer.write_u32(ask_key);
		writer.write_u32(remaining_ms);
		writer.write_u32(state);
		writer.write_u8(flag);

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	EQApplicationPacket* BuildItemActionPacket(uint16_t action, uint32_t quantity, uint32_t transaction_id, uint8_t flag)
	{
		Writer writer;

		writer.write_u16(SubItemActionNotify);
		writer.write_u16(action);
		writer.write_u32(quantity);
		writer.write_u32(transaction_id);
		writer.write_u8(flag);

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	EQApplicationPacket* BuildRowUnlockPacket(uint32_t ask_key, uint64_t item_id, uint32_t loot_details_key)
	{
		Writer writer;

		writer.write_u16(SubRowUpdate10);
		writer.write_u32(ask_key);
		writer.write_u64(item_id);
		writer.write_u32(loot_details_key);

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	// subcmd 0x0B, the form FUN_1400a81b0 sends: a looter's Need / Greed / Never answer, named.
	//
	// Layout, taken from the 2026-10-16 16:52 capture (25 bytes, one entry, empty name):
	//   u32 ask_key   u32 corpse_key   u64 item_id   u16 count
	//   count x { u32 ClientChoiceAction, string name }
	// The name is NUL-terminated and variable-length, so an empty name costs exactly one byte - which is how
	// the 25-byte size closes.
	//
	// The client's own sender for a row dropdown uses the EMPTY name (FUN_1400a82a0), meaning "this is my
	// answer"; only the Manage Loot player list fills a name in, meaning "the master looter set this looter's
	// answer". A server relay therefore has to name the looter, because no other client can otherwise learn it.
	EQApplicationPacket* BuildRowUpdatePacket(
		uint32_t ask_key,
		uint32_t corpse_key,
		uint64_t item_id,
		uint32_t action,
		const std::string& name
	)
	{
		Writer writer;

		writer.write_u16(SubRowUpdate);
		writer.write_u32(ask_key);
		writer.write_u32(corpse_key);
		writer.write_u64(item_id);
		writer.write_u16(1);              // one entry
		writer.write_u32(action);
		writer.write_string(name, 64);

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	uint32_t LootActionStringId(uint16_t action)
	{
		switch (action) {
			case ActionNoRoomStack:  return 11308;
			case ActionLoreConflict: return 371;
			case ActionCombat:       return 5154;
			case ActionNoRoom:       return 372;
			case ActionNotAllowed:   return 3562;
			case ActionDead:         return 11318;
			case ActionNone:
			case ActionLooted:
			default:                 return 0;
		}
	}

	const char* LootActionName(uint16_t action)
	{
		switch (action) {
			case ActionNone:         return "None";
			case ActionLooted:       return "Looted";
			case ActionNoRoomStack:  return "NoRoomForStack";
			case ActionLoreConflict: return "LoreConflict";
			case ActionCombat:       return "Combat";
			case ActionNoRoom:       return "NoRoom";
			case ActionNotAllowed:   return "NotAllowed";
			case ActionDead:         return "Dead";
			default:                 return "Unknown";
		}
	}

	EQApplicationPacket* BuildTransactionPacket(
		uint32_t ask_key,
		uint32_t corpse_key,
		uint64_t item_id,
		uint16_t quantity,
		uint8_t flag,
		const std::string& name
	)
	{
		Writer writer;

		writer.write_u16(SubTransaction);
		writer.write_u32(ask_key);
		writer.write_u32(corpse_key);
		writer.write_u64(item_id);
		writer.write_u16(quantity);
		writer.write_u8(flag);
		writer.write_string(name, 64);

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	// Read the body of a subcmd 0x0A record. See AdvLoot::ItemUpdate for the field provenance: the u64 after
	// the key is the item id, not a packed {count, field_14}.
	bool ReadItemUpdate(Reader& reader, ItemUpdate& out)
	{
		out = ItemUpdate{};

		out.ask_key      = reader.read_u32();
		out.item_id      = reader.read_u64();   // record+0x08
		out.state        = reader.read_u32();   // record+0x40
		out.flag         = reader.read_u8();    // record+0x20
		out.remaining_ms = reader.read_u32();   // record+0x18 - client clock, in ms

		uint32_t count = reader.read_u32();   // record+0x10
		if (count && count <= 64) {
			out.entries.reserve(count);
		}

		for (uint32_t i = 0; i < count && !reader.truncated; i++) {
			uint32_t entry_key = reader.read_u32();
			uint32_t quantity  = reader.read_u32();
			out.entries.emplace_back(entry_key, quantity);
		}

		return !reader.truncated;
	}

	EQApplicationPacket* BuildItemUpdatePacket(const ItemUpdate& update)
	{
		Writer writer;

		writer.write_u16(SubItemUpdate);
		writer.write_u32(update.ask_key);
		writer.write_u64(update.item_id);                                // record+0x08
		writer.write_u32(update.state);
		writer.write_u8(update.flag);
		writer.write_u32(update.remaining_ms);
		writer.write_u32(static_cast<uint32_t>(update.entries.size()));

		for (const auto& entry : update.entries) {
			writer.write_u32(entry.first);
			writer.write_u32(entry.second);
		}

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	// Read the body of a subcmd 0x0E transaction. See AdvLoot::Transaction for the field provenance.
	bool ReadTransaction(Reader& reader, Transaction& out)
	{
		out = Transaction{};

		out.ask_key    = reader.read_u32();
		out.corpse_key = reader.read_u32();
		out.item_id    = reader.read_u64();
		out.quantity   = reader.read_u16();
		out.flag       = reader.read_u8();
		out.name       = reader.read_string(64);

		return !reader.truncated;
	}

	// subcmd 0x13 - FUN_140086fa0 reader: u32 ask_key, u32, u8, u16 count, count x (u16 roll, string name[64]).
	// FUN_140154200 prints "%1 rolled a %2 on %3." per entry and gives the row to whoever rolled highest.
	// Kept as the capture-verified layout for the roll-announcement path; nothing sends it yet, because the
	// server's own chat lines already cover the text and the client's winner branch would move the row into the
	// personal store a second time. See docs/advlootdesignV2.md C28.
	EQApplicationPacket* BuildRollResultsPacket(
		uint32_t ask_key,
		uint32_t unused_u32,
		uint8_t  unused_u8,
		const std::vector<std::pair<uint16_t, std::string>>& results
	)
	{
		Writer writer;

		writer.write_u16(SubRollResults);
		writer.write_u32(ask_key);
		writer.write_u32(unused_u32);
		writer.write_u8(unused_u8);
		writer.write_u16(static_cast<uint16_t>(results.size()));

		for (const auto& result: results) {
			writer.write_u16(result.first);
			writer.write_string(result.second, 64);
		}

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	// subcmd 0x0D - the reply to a 0x360e / 0x0D request. FUN_140151c60 case 0x140151f43 calls the reader
	// FUN_140086ab0 directly, with no flag byte in front of it (subcmd 0x0C reads a u8 flag first, which is
	// why BuildChoiceUpdatePacket is not reusable here).
	EQApplicationPacket* BuildAskResponsePacket(
		uint32_t ask_key,
		uint32_t pad_u32,
		uint64_t pad_u64,
		const std::vector<std::pair<uint32_t, std::string>>& responses
	)
	{
		Writer writer;

		writer.write_u16(SubBulkRows);
		writer.write_u32(ask_key);
		writer.write_u32(pad_u32);
		writer.write_u64(pad_u64);
		writer.write_u16(static_cast<uint16_t>(responses.size()));

		for (const auto& response : responses) {
			writer.write_u32(response.first);
			writer.write_string(response.second, 64);
		}

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	// subcmd 0x0F - reader FUN_140086c50: u64 key, string name.
	EQApplicationPacket* BuildSingleTransferPacket(uint32_t ask_key, uint32_t corpse_key, const std::string& name)
	{
		Writer writer;

		writer.write_u16(SubSingleTransfer);
		writer.write_u32(ask_key);
		writer.write_u32(corpse_key);
		writer.write_string(name, 64);

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	// subcmd 0x06 - reader FUN_140086d70: u8 announce, u64 money, u8 personal, u32 bonus. See the header for
	// which of the client's four money lines each flag pair produces. The client reads every field behind a
	// bounds check, so a short packet just leaves the rest at zero.
	EQApplicationPacket* BuildChatNotifyPacket(
		uint8_t  announce,
		uint64_t money_total,
		uint8_t  personal,
		uint32_t bonus
	)
	{
		Writer writer;

		writer.write_u16(SubChatNotify);
		writer.write_u8(announce);
		writer.write_u64(money_total);
		writer.write_u8(personal);
		writer.write_u32(bonus);

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	// subcmd 0x14 - reader FUN_140086e90: u32 key, u8 flag, u32 count, count x item record. The client's
	// own serializer for this body is FUN_140085bb0, whose record writer FUN_140085440 matches the
	// FUN_1400839b0 layout WriteItemRecord already emits.
	EQApplicationPacket* BuildItemListChatPacket(
		uint32_t key,
		uint8_t  flag,
		const std::vector<Filter>& items,
		EQ::versions::ClientVersion client_version
	)
	{
		Writer writer;

		writer.write_u16(SubChatMessage);
		writer.write_u32(key);
		writer.write_u8(flag);
		writer.write_u32(static_cast<uint32_t>(items.size()));

		for (const auto& item : items) {
			WriteItemRecord(writer, item, client_version);
		}

		EQApplicationPacket* app = new EQApplicationPacket(OP_AdvLoot, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	// Re-derive the mode of every Advanced Loot corpse any of `viewers` has rows on, and re-render each corpse
	// exactly once, to the clients that already have rows on it.
	void RefreshCorpsesFor(const std::vector<Client *> &viewers)
	{
		std::vector<Corpse *> corpses;

		for (Client *c: viewers) {
			if (!c || !c->advloot_enabled) {
				continue;
			}

			for (uint16_t corpse_id: c->advloot_corpses) {
				Corpse *corpse = entity_list.GetCorpseByID(corpse_id);
				if (!corpse || !corpse->GetAdvLootSession()) {
					continue;
				}

				if (std::find(corpses.begin(), corpses.end(), corpse) == corpses.end()) {
					corpses.push_back(corpse);
				}
			}
		}

		for (Corpse *corpse: corpses) {
			AdvLoot::Session &session = corpse->AdvLootSession();
			session.RefreshAuthority();

			// Re-render to the clients that already have rows here - not to the whole zone, and not to the whole
			// construct. A player who joined the group after this kill has no claim on the corpse: pushing it to
			// them is how an unaccepted group invite made a locked corpse appear out of nowhere the moment the
			// inviter killed (2026-10-16, docs/advlootpcap.md §10.1). RenderAll() stays the kill-time push and the
			// cash-distribution list, and it is kill-credit-only for the same reason.
			for (uint32_t character_id: session.ViewerIDs()) {
				if (Client *c = entity_list.GetClientByCharID(character_id)) {
					session.Render(c);
				}
			}
		}
	}

	// OP_MasterLooter (0x3a60) - reader FUN_140087430: u32, then a NUL-terminated name. See the header for what
	// the client does with it and why the flag it sets is the one the countdown bar hangs off.
	EQApplicationPacket* BuildMasterLooterPacket(uint32_t mode, const std::string& name)
	{
		Writer writer;

		writer.write_u32(mode);
		writer.write_string(name, 64);

		EQApplicationPacket* app = new EQApplicationPacket(OP_MasterLooter, writer.buffer.size());
		memcpy(app->pBuffer, writer.buffer.data(), writer.buffer.size());
		return app;
	}

	void BroadcastMasterLooter(Group *group)
	{
		if (!group) {
			return;
		}

		// GetMasterLooter() already falls back to the group leader, so a group always has one - which is the
		// point. The client has no other source for it, and its fallback makes every member the master looter.
		Client *master = group->GetMasterLooter();
		if (!master) {
			return;
		}

		std::list<Client *> clients;
		group->GetClientList(clients, true);

		EQApplicationPacket *app = BuildMasterLooterPacket(0, master->GetCleanName());
		if (!app) {
			return;
		}

		for (Client *c: clients) {
			if (c) {
				c->QueuePacket(app);
			}
		}

		safe_delete(app);

		LogLootFilters(
			"master looter [{}] broadcast to [{}] group member(s)",
			master->GetCleanName(),
			clients.size()
		);
	}

	void BroadcastMasterLooter(Raid *raid)
	{
		if (!raid) {
			return;
		}

		Client *master = raid->GetMasterLooter();
		if (!master) {
			return;
		}

		std::vector<Client *> clients;

		for (int i = 0; i < MAX_RAID_MEMBERS; ++i) {
			if (raid->members[i].member && !raid->members[i].is_bot) {
				clients.push_back(raid->members[i].member);
			}
		}

		EQApplicationPacket *app = BuildMasterLooterPacket(0, master->GetCleanName());
		if (!app) {
			return;
		}

		for (Client *c: clients) {
			if (c) {
				c->QueuePacket(app);
			}
		}

		safe_delete(app);

		LogLootFilters(
			"master looter [{}] broadcast to [{}] raid member(s)",
			master->GetCleanName(),
			clients.size()
		);
	}

	// subcmd 0x13 is the dice-result announcement, not a mode list - see BuildRollResultsPacket in the header.
	// AdvLoot::BroadcastLootMode used to send the group's loot modes here on every membership change, and the
	// client read them as rolls: the 2026-10-17 11:12 capture shows both windows printing
	//   Gunadie rolled a 2 on .
	//   Joka rolled a 3 on .
	// at the instant the group formed, because BuildLootModePacket put ModeMasterLooter(2) and ModeGroup(3) in
	// the u16 slot and 0 in the ask-key slot, so %2 was the mode number and %3 - the row looked up by row+0x60 -
	// matched nothing. It also ran FUN_1400a7bd0 with a key that matched no row. The master looter is announced
	// by OP_MasterLooter (0x3a60) and by the name field of the subcmd 0x03 record; nothing else is needed.

}

// Fill the nested item record's descriptor fields from the item cache. FUN_14009c0a0 copies +0x10 into
// row+0x50 MaxStack and derives row+0x4C bStackable from it, and FUN_14009e460 only merges two rows of the
// same item id when 1 < +0x10 - a hardcoded 1 there costs real stackable merges. +0x18 is a no-drop flag in
// the plain sense (1 = no drop), which is the inverse of ItemData::NoDrop (0 = no drop): see
// AdvLoot::Filter::no_drop and docs/advlootpcap.md §7.1. See docs/advlootdesignV2.md C16.
static void FillAdvLootItemDescriptor(AdvLoot::Filter& filter)
{
	const EQ::ItemData *item = database.GetItem(filter.item_id);
	if (!item) {
		return;
	}

	filter.stack_size = item->StackSize > 1 ? static_cast<uint16_t>(item->StackSize) : 1;
	filter.no_drop    = item->NoDrop == 0 ? 1 : 0;
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
		FillAdvLootItemDescriptor(filter);
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
			FillAdvLootItemDescriptor(filter);
			return;
		}
	}

	AdvLoot::Filter filter{item_id, filter_bits, icon, name};
	FillAdvLootItemDescriptor(filter);
	advloot_filters.push_back(filter);
}

// The subcmd 0x03 record's top-level name is not a label - it is the master looter announcement, and it is
// re-read on every push. FUN_140153970 (the 0x03 receive path) reads the record with FUN_1400874d0, whose
// first field after a u32 is a 64-byte string at record+0x00, then at 0x140153ad7:
//
//   RDX = DAT_140e33f50                      ; local player record
//   CALL FUN_140560db0                       ; record+0x00 -> const char*
//   RDX = RBX + 0xa8                         ; the local player's own name
//   CALL 0x1406ce580                         ; compare
//   match    -> manager+0x241 = 1
//   no match -> manager+0x241 = 0
//   window->vtable+0x380(window, name, 1)    ; UpdateMasterLooter
//
// So a 0x03 that names the viewer re-asserts "I am the master looter" for that viewer and undoes the
// OP_MasterLooter that arrived moments earlier. AdvLootRender() used to pass GetCleanName(), which is why the
// 2026-10-17 11:12 run showed both members as the master looter again even though 0x3a60 and the 0x13 mode
// list were both on the wire and correct: the corpse render at 11:12:11 overwrote the announcement from
// 11:11:59. Solo keeps the player's own name, which is what the client expects when ungrouped.
std::string Client::AdvLootMasterLooterName()
{
	if (Raid *raid = GetRaid()) {
		Client *master = raid->GetMasterLooter();
		return master ? master->GetCleanName() : GetCleanName();
	}

	if (Group *group = GetGroup()) {
		Client *master = group->GetMasterLooter();
		return master ? master->GetCleanName() : GetCleanName();
	}

	return GetCleanName();
}

// The mode a subcmd 0x03 record carries. advloot_mode is only written by /advloot and defaults to ModeSolo,
// so every group corpse render on the 2026-10-17 11:12 run went out as mode 1 (ModeSolo) while the session log
// said mode [group]. The client stores it at record+0x28e648 and nothing in FUN_140153970 reads it, so this is
// for consistency and for the logs rather than for a gate - but a 0x03 that disagrees with the session is a
// capture that has to be re-read before it can be believed.
uint32_t Client::AdvLootModeForViewer()
{
	if (!advloot_enabled) {
		return AdvLoot::ModeInvalid;
	}

	if (Raid *raid = GetRaid()) {
		return AdvLoot::ModeRaid;
	}

	if (Group *group = GetGroup()) {
		return group->GetMasterLooter() == this ? AdvLoot::ModeMasterLooter : AdvLoot::ModeGroup;
	}

	return AdvLoot::ModeSolo;
}

void Client::SendAdvLootFilterSet()
{
	EQApplicationPacket* app = AdvLoot::BuildFilterSetPacket(AdvLootModeForViewer(), AdvLootMasterLooterName(), advloot_filters, ClientVersion());
	if (!app) {
		return;
	}

	QueuePacket(app);
	delete app;

	// The same request is the client's "give me my Advanced Loot state" moment - it is what it sends when the
	// window opens and after a zone entry - so the master looter has to travel with it. A player who logged
	// into an existing group otherwise has nothing on manager+0x1f4 until the next membership change, and
	// FUN_1401500a0 fills the gap with the player's own name.
	if (Raid* raid = GetRaid()) {
		AdvLoot::BroadcastMasterLooter(raid);
	} else if (Group* group = GetGroup()) {
		AdvLoot::BroadcastMasterLooter(group);
	}
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

// Re-render every corpse this client has rows for, as one subcmd 0x03. This is the only server-side path that
// emits a 0x03 loot-group push, and it is the only one that can: FUN_140153970 clears rows in both panes
// (window vtable +0x3b0) before re-adding, so the packet must carry the viewer's whole live set at once or
// the corpses it leaves out vanish. Every group record in it comes from AdvLoot::Session::BuildGroupFor, so
// what the window shows is always what the sessions think it should show.
void Client::AdvLootRender()
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

		AdvLoot::Session* session = corpse->GetAdvLootSession();
		if (!session) {
			corpse->ClearAdvLootPendingRowsFor(CharacterID());
			continue;
		}

		if (!corpse->HasAdvLootItems()) {
			corpse->ClearAdvLootPendingRowsFor(CharacterID());
			continue;
		}

		const AdvLoot::CorpseGroup group = session->RenderGroupFor(this);
		if (group.items.empty()) {
			continue;
		}

		groups.push_back(group);
		alive.push_back(corpse_id);
	}

	advloot_corpses = alive;

	EQApplicationPacket* app = AdvLoot::BuildCorpseGroupsPacket(AdvLootModeForViewer(), AdvLootMasterLooterName(), groups, ClientVersion());
	if (!app) {
		return;
	}

	LogLootFilters("rebuild [{}] corpse group(s) for [{}]", groups.size(), GetCleanName());

	QueuePacket(app);
	delete app;

	// FUN_140153970 wipes the ask table this rebuild just replaced: it clears manager+0x128 (the filter-hash
	// buckets at manager+0x148[]) and resets the allocator at manager+0x1e8 to zero. Every row is recreated from
	// the group records with row+0x60 == 0. So every ask key this viewer was holding is now gone from the client,
	// and a server-side copy of it is worse than nothing: FindAskGroup would answer with a key no client row is
	// bound to, which is what makes 0x0e / 0x0f / 0x12 / 0x13 miss.
	//
	// Clear first, then let the re-issue below put the live asks back. Because the table is empty, each re-issued
	// key is new to the client and binds cleanly - that is the guarantee ReissueAsks exists to provide, and this
	// is the half of it that used to be missing.
	for (uint16_t corpse_id: alive) {
		Corpse* corpse = entity_list.GetCorpseByID(corpse_id);
		if (corpse && corpse->GetAdvLootSession()) {
			corpse->AdvLootSession().ForgetAskKeys(CharacterID());
		}
	}

	// Rule R2, and after the rebuild for the same reason Session::Render does it: this 0x03 cleared the ask
	// table for every corpse in it at once, so the asks that are still outstanding follow it, one corpse at a
	// time and in the same order the group records were listed.
	for (uint16_t corpse_id: alive) {
		Corpse* corpse = entity_list.GetCorpseByID(corpse_id);
		if (corpse && corpse->GetAdvLootSession()) {
			corpse->AdvLootSession().ReissueAsks(this);
		}
	}
}

// Step 8. This client is leaving the zone, disconnecting, or being destroyed. The corpse outlives the client,
// so its View has to be dropped explicitly: without this a logged-out looter keeps their personal pool, and
// because Resolve() only checks whether the winner is online they could still win a row nobody can collect.
void Client::ForgetAdvLootViewers()
{
	for (uint16_t corpse_id: advloot_corpses) {
		Corpse* corpse = entity_list.GetCorpseByID(corpse_id);
		if (corpse) {
			corpse->ForgetAdvLootViewer(CharacterID());
		}
	}

	advloot_corpses.clear();
	advloot_last_corpse_id = 0;
}

// Step 8. The looter died. They leave every ask still running on their AdvLoot corpses - a dead player cannot
// collect a row they won - but their awards survive, so a wipe does not hand an item the master looter already
// gave them back to the pool. Their rows are cleared because FUN_1400a9cc0 refuses them anyway (eqstr 11318).
void Client::AdvLootViewerDied()
{
	for (uint16_t corpse_id: advloot_corpses) {
		Corpse* corpse = entity_list.GetCorpseByID(corpse_id);
		if (corpse) {
			corpse->AdvLootViewerDied(CharacterID());
		}
	}
}

// Step 8. The client's construct changed. AuthorityFor() already reads the raid or group live, so the server
// has been applying the new rule since the previous packet; what this fixes is what the window shows. The mode
// and the Master Looter marker travel in the 0x03 group record, so re-derive them and re-render every viewer
// of each corpse this client has rows on - the other members' authority changed too, not just this one's.
void Client::RefreshAdvLootAuthority()
{
	AdvLoot::RefreshCorpsesFor({this});
}

// The one refusal every gated Advanced Loot decision uses. eqstr_us.txt 643 is the client's own wording for
// a decision it would have refused locally, so the server says the same thing rather than inventing one.
void Client::RefuseAdvLootAction(const char* what, Corpse* corpse, uint32_t ask_key, uint32_t item_id, uint32_t loot_details_key)
{
	const uint32_t authority = corpse
		? corpse->AdvLootSession().AuthorityFor(this)
		: AdvLoot::AuthorityNone;

	LogLootFilters(
		"[{}] {} item [{}] corpse [{}] refused - authority [{}], mode [{}], ask_key [{}]",
		GetCleanName(),
		what,
		item_id,
		corpse ? corpse->GetID() : 0,
		AdvLoot::AuthorityName(authority),
		corpse ? corpse->AdvLootSession().ModeName() : "none",
		ask_key
	);

	MessageString(Chat::White, AdvLoot::StringId::CannotActNow);

	// CAdvancedLootWnd__DoSharedAdvLootAction set record+0x6C before it sent the request. Without this the
	// row stays locked until the corpse unlock timer expires and every later click errors with eqstr 643.
	EQApplicationPacket* unlock = AdvLoot::BuildRowUnlockPacket(ask_key, item_id, loot_details_key);
	if (unlock) {
		QueuePacket(unlock);
		safe_delete(unlock);
	}
}

// subcmd 0x0C with flag 0 (FUN_140154a90 -> FUN_1400a8350): restate the need/greed/never choice on a
// row the client has already drawn. It cannot create a row - only subcmd 0x03 does that - so this is only
// useful after the corpse has been pushed by AdvLoot::Session::Render or Client::AdvLootRender.
void Client::SendAdvLootChoice(uint32_t item_id, uint32_t choice, const std::string& name)
{
	if (!advloot_enabled) {
		return;
	}

	EQApplicationPacket* app = AdvLoot::BuildChoicePacket(item_id, choice, name);
	if (!app) {
		return;
	}

	LogLootFilters(
		"[{}] advloot choice [{}] -> [{}]",
		GetCleanName(),
		item_id,
		AdvLoot::ChoiceName(choice)
	);

	QueuePacket(app);
	delete app;
}

// subcmd 0x0C with flag non-zero (FUN_140154a90 -> FUN_1400a1340): drop one row from the window without
// rebuilding the whole corpse.
void Client::SendAdvLootRowRemoved(uint32_t item_id, const std::string& name)
{
	if (!advloot_enabled) {
		return;
	}

	EQApplicationPacket* app = AdvLoot::BuildRowRemovePacket(item_id, name);
	if (!app) {
		return;
	}

	LogLootFilters("[{}] advloot row remove [{}]", GetCleanName(), item_id);

	QueuePacket(app);
	delete app;
}

// Learn the ask-group id the client allocated for a (corpse, item) pair. Subcmd 0x0B is the only packet
// that carries it: FUN_1400a82a0 copies record+0x60 into the block's first u32 and FUN_140154830 sends the
// block on 0x3175. Without this the server cannot resolve a 0x360e / 0x0D request, because that request
// carries nothing but the id.
//
// The id belongs to a corpse cycle - the client drops all of them on the next subcmd 0x03 rebuild - so it is
// stored in that corpse's AdvLoot::Session under this character's View and dies with the corpse.
void Client::RememberAdvLootAskGroup(uint32_t ask_key, uint32_t corpse_key, uint32_t item_id)
{
	if (!ask_key) {
		return;
	}

	Corpse* corpse = corpse_key ? entity_list.GetCorpseByID(static_cast<uint16_t>(corpse_key)) : nullptr;
	if (!corpse) {
		corpse = AdvLootCorpse();
	}
	if (!corpse) {
		corpse = entity_list.GetCorpseByOwner(this);
	}

	if (!corpse) {
		LogLootFilters(
			"[{}] ask group [{}] for corpse [{}] item [{}] dropped - no live corpse",
			GetCleanName(),
			ask_key,
			corpse_key,
			item_id
		);
		return;
	}

	corpse->AdvLootSession().RememberAskKey(CharacterID(), ask_key, corpse->GetID(), item_id);
}

// The reverse of RememberAdvLootAskGroup: the ask key a row currently answers to, or 0 when the row has
// never had an ask installed. 0 is the value the client itself puts in the field for such a row - it sends
// record+0x60, which FUN_1400a0480 only ever writes when an ask record is installed - so 0 is a correct
// reply, not a missing one.
uint32_t Client::FindAdvLootAskKey(uint32_t corpse_key, uint32_t item_id) const
{
	for (uint16_t corpse_id : advloot_corpses) {
		Corpse* corpse = entity_list.GetCorpseByID(corpse_id);
		if (!corpse) {
			continue;
		}

		const AdvLoot::Session* session = corpse->GetAdvLootSession();
		if (!session) {
			continue;
		}

		const uint32_t ask_key = session->FindAskKey(CharacterID(), corpse_key, item_id);
		if (ask_key) {
			return ask_key;
		}
	}

	if (Corpse* corpse = AdvLootCorpse()) {
		if (const AdvLoot::Session* session = corpse->GetAdvLootSession()) {
			return session->FindAskKey(CharacterID(), corpse_key, item_id);
		}
	}

	return 0;
}

// Look an ask key the client allocated up in the AdvLoot::Session views this character owns. This is what a
// 0x360e / 0x0D or 0x360e / 0x0F request is resolved with, because those carry nothing but the id.
bool Client::FindAdvLootAskGroup(uint32_t ask_key, uint32_t *corpse_key, uint32_t *item_id) const
{
	if (!ask_key) {
		return false;
	}

	std::vector<uint16_t> candidates = advloot_corpses;
	if (Corpse* last = AdvLootCorpse()) {
		candidates.push_back(last->GetID());
	}

	for (uint16_t corpse_id : candidates) {
		Corpse* corpse = entity_list.GetCorpseByID(corpse_id);
		if (!corpse) {
			continue;
		}

		const AdvLoot::Session* session = corpse->GetAdvLootSession();
		if (!session) {
			continue;
		}

		AdvLoot::AskGroup group;
		if (session->FindAskGroup(CharacterID(), ask_key, &group)) {
			if (corpse_key) {
				*corpse_key = group.corpse_key;
			}
			if (item_id) {
				*item_id = group.item_id;
			}
			return true;
		}
	}

	return false;
}

// The corpse this client most recently had rows for. Resolved through entity_list every time, so a corpse
// that decayed between two packets cannot be dereferenced.
Corpse* Client::AdvLootCorpse() const
{
	Corpse* corpse = advloot_last_corpse_id ? entity_list.GetCorpseByID(advloot_last_corpse_id) : nullptr;
	if (corpse) {
		return corpse;
	}

	for (uint16_t corpse_id : advloot_corpses) {
		if ((corpse = entity_list.GetCorpseByID(corpse_id))) {
			return corpse;
		}
	}

	return nullptr;
}

// Answer a 0x360e / 0x0D request with subcmd 0x0D. FUN_14009f6b0 only reaches FUN_1401a3000 for a corpse
// row whose record+0x60 equals the packet's first u32, so the ask key is echoed unchanged and the body is
// one {LooterResponse, name} row per looter. The client appends every ML_GiveSelection entry that the
// reply did not mention as an unchecked row (FUN_1401a3000, action 0), so only the players that actually
// responded have to be listed. Sent to this client alone: the first row is the viewer's own name and the
// client hides the ND / GD checkboxes on any action-1 row that is not the local player.
void Client::SendAdvLootAskResponses(uint32_t ask_key, uint32_t corpse_key, uint32_t item_id)
{
	Corpse* corpse = corpse_key ? entity_list.GetCorpseByID(static_cast<uint16_t>(corpse_key)) : nullptr;
	const AdvLoot::Roll* roll = corpse ? corpse->GetAdvLootRoll(item_id) : nullptr;

	std::vector<std::pair<uint32_t, std::string>> responses;

	// Action 1 is the only value that renders a row without the two checkboxes, which is how the client
	// shows the local player in its own Manage Loot window.
	responses.emplace_back(AdvLoot::ResponseSelf, GetCleanName());

	if (roll) {
		for (const auto& participant : roll->participants) {
			if (participant.character_id == CharacterID() || participant.name.empty()) {
				continue;
			}
			responses.emplace_back(AdvLoot::ChoiceToResponse(participant.choice), participant.name);
		}
	}

	EQApplicationPacket* app = AdvLoot::BuildAskResponsePacket(ask_key, corpse_key, item_id, responses);
	if (!app) {
		return;
	}

	LogLootFilters(
		"[{}] ask responses ask_key [{}] corpse [{}] item [{}] -> [{}] row(s)",
		GetCleanName(),
		ask_key,
		corpse_key,
		item_id,
		responses.size()
	);

	QueuePacket(app);
	delete app;
}

// Resolve the key a subcmd 0x0A / 0x0B / 0x0E / 0x12 packet carries into a corpse.
//
// Which key it is depends on the sub-command, and the codec now names them separately:
//   0x0A / 0x0B / 0x12 -> ItemUpdate::ask_key / the 0x0B ask key, i.e. the key of the client's ask-group
//                        record. The server only ever learns it from 0x0B (advloot.md 10.6), so it is looked
//                        up in the AdvLoot::Session views this client owns.
//   0x0E               -> Transaction::corpse_key, which is the group record's hash key and the LootDetails
//                        entry key, i.e. the corpse entity id the server pushed with subcmd 0x03.
// Both readings have to work from every call site, so try them in order and fall back to the corpse this
// client last opened, exactly like the 0x08 handler does.
Corpse* Client::ResolveAdvLootGroup(uint32_t key, uint32_t item_id_hint, uint32_t *out_item_id)
{
	if (out_item_id) {
		*out_item_id = item_id_hint;
	}

	// First reading: the key is one of this client's ask keys. Ask keys live in the corpse's session, so
	// search the corpses this client has rows for.
	for (uint16_t corpse_id : advloot_corpses) {
		Corpse* candidate = entity_list.GetCorpseByID(corpse_id);
		if (!candidate) {
			continue;
		}

		const AdvLoot::Session* session = candidate->GetAdvLootSession();
		if (!session) {
			continue;
		}

		AdvLoot::AskGroup group;
		if (session->FindAskGroup(CharacterID(), key, &group)) {
			if (out_item_id && group.item_id) {
				*out_item_id = group.item_id;
			}
			return candidate;
		}
	}

	// Second reading: the key is simply the corpse entity id, which is what the client's own receive path
	// assumes.
	Corpse* corpse = key ? entity_list.GetCorpseByID(static_cast<uint16_t>(key)) : nullptr;

	if (!corpse) {
		corpse = AdvLootCorpse();
	}

	if (!corpse) {
		corpse = entity_list.GetCorpseByOwner(this);
	}

	if (!corpse) {
		return nullptr;
	}

	// The AdvLoot window can hold rows from several corpses at once, so the resolved corpse wins over a
	// group-key corpse id that only matched by coincidence.
	if (out_item_id && !*out_item_id) {
		for (const auto& entry : corpse->GetAdvLootRolls()) {
			*out_item_id = entry.first;
			break;
		}
	}

	return corpse;
}

// Echo a subcmd 0x0A ask-group record to the rest of the construct. The sender already applied it locally,
// so the group/raid queues (which exclude the sender) are the whole job; solo needs its own copy because
// nothing else will ever send it back.
void Client::SendAdvLootItemUpdate(const AdvLoot::ItemUpdate& update)
{
	EQApplicationPacket* app = AdvLoot::BuildItemUpdatePacket(update);
	if (!app) {
		return;
	}

	LogLootFilters(
		"[{}] item update relay ask_key [{}] state [{}]({}) remaining_ms [{}] entries [{}]",
		GetCleanName(),
		update.ask_key,
		update.state,
		AdvLoot::StateName(update.state),
		update.remaining_ms,
		update.entries.size()
	);

	// Every member of the construct gets the record, the sender included. QueueClients excludes `this`, which is
	// right for a broadcast but wrong here: the initiating client installed its own record with
	// `record+0x18 == 0`, so its row countdown is a wrapped clock and the red time-left bar over the icon never
	// draws. This relay carries the window the server actually armed, and the client's 0x0a receive path
	// re-installs the record with it - the only way the player who started the ask sees the countdown.
	if (Raid* raid = GetRaid()) {
		raid->QueueClients(this, app, true);
		QueuePacket(app);
	} else if (Group* group = GetGroup()) {
		group->QueueClients(this, app, true);
		QueuePacket(app);
	} else {
		QueuePacket(app);
	}

	delete app;
}

// Decision 4: the AdvLoot mode is a derived display value, not a configured setting. Raid wins over
// group, group wins over solo, and the member that is the master looter reads molo. Authority comes
// from RaidLootType, which is a separate rule and is not changed here.
//
// This no longer sends anything. It used to send OP_AdvLoot subcmd 0x13, which is the dice-result
// announcement rather than a mode list - see AdvLoot::BuildRollResultsPacket and C28. advloot_mode is kept
// because the session-open and refusal logs print it.
void Client::RefreshAdvLootMode()
{
	advloot_mode = AdvLootModeForViewer();
}

// /advloot need|greed|never <item_id> | /advloot mol [name]
//
// The choice sub-commands exist because the client has no wire path for a Need/Greed selection:
// FUN_1400a5330 sends subcmd 0x11 with state 1 or 2 derived from row +0x68 (bAutoRoll), and the Loot
// Filters window only writes its own ini files. So the command is the server's capture path, and it is
// also the only way to populate character_loot_filters from the client side.
void command_advloot(Client *c, const Seperator *sep)
{
	if (!c->advloot_enabled) {
		c->Message(Chat::Red, "Advanced Loot is not enabled for this character. Enable it in the Loot Settings window.");
		return;
	}

	std::string sub = Strings::ToLower(sep->arg[1]);

	if (sub == "mol") {
		Group* group = c->GetGroup();
		Raid* raid = c->GetRaid();

		if (!group && !raid) {
			c->Message(Chat::Red, "/advloot mol needs a group or a raid.");
			return;
		}

		Client* current = raid ? raid->GetMasterLooter() : group->GetMasterLooter();
		if (!current) {
			c->Message(Chat::Red, "You are not the master looter.");
			return;
		}

		if (!sep->arg[2][0]) {
			c->Message(Chat::Yellow, "[%s] is the master looter.", current->GetCleanName());
			return;
		}

		Client* target = entity_list.GetClientByName(sep->arg[2]);
		if (!target) {
			c->Message(Chat::Red, "No player named [%s] is in the zone.", sep->arg[2]);
			return;
		}

		// Decision 4: the override is candidate-gated.
		if (!target->advloot_master_looter_candidate) {
			c->Message(Chat::Red, "[%s] is not a master looter candidate.", target->GetCleanName());
			return;
		}

		current->MessageString(Chat::Yellow, AdvLoot::StringId::MLUndelegated, current->GetCleanName());

		if (raid) {
			raid->SetMasterLooter(target);
		} else {
			group->SetMasterLooter(target);
		}

		target->MessageString(Chat::Yellow, AdvLoot::StringId::MLDelegated, target->GetCleanName());

		// SetMasterLooter() now runs RefreshAdvLootAuthority() over the whole construct, which re-announces the
		// master looter with OP_MasterLooter and re-renders every corpse its viewers share - and each re-render
		// carries the new name in its 0x03 record. So the gate and the window both follow the change from here.
		return;
	}

	if (sub == "assign") {
		// Decision 3: assignment is an override that resolves immediately and cancels the row's timer.
		// The client has no confirmed packet path for a master looter right-click assignment, so the
		// command is the server-side trigger.
		Group* group = c->GetGroup();
		Raid* raid = c->GetRaid();

		Client* ml = raid ? raid->GetMasterLooter() : (group ? group->GetMasterLooter() : c);
		if (ml != c) {
			c->Message(Chat::Red, "Only the master looter can assign an item.");
			return;
		}

		uint32_t item_id = Strings::ToUnsignedInt(sep->arg[2]);
		Client* target = entity_list.GetClientByName(sep->arg[3]);

		if (!item_id || !target) {
			c->Message(Chat::Red, "Usage: /advloot assign <item_id> <name>");
			return;
		}

		bool assigned = false;
		for (uint16_t corpse_id : c->advloot_corpses) {
			Corpse* corpse = entity_list.GetCorpseByID(corpse_id);
			if (!corpse) {
				continue;
			}

			AdvLoot::Roll* roll = corpse->GetAdvLootRoll(item_id);
			if (!roll) {
				continue;
			}

			corpse->AssignAdvLootRoll(*roll, target);
			assigned = true;
		}

		if (!assigned) {
			c->Message(Chat::Red, "No tracked corpse row for item [%u].", item_id);
		}

		return;
	}

	// additem / removeitem. Verified 2026-10-08: no AdvLoot sub-command inserts a row. Subcmd 0x0C only
	// restates a choice (FUN_1400a8350) or removes a row (FUN_1400a1340); rows are created solely by the
	// subcmd 0x03 rebuild (FUN_140153970 -> CAdvancedLootWnd::AddCorpse FUN_14009e310 -> FUN_14009e460),
	// which draws a Corpse Loot List row when the item record's +0x0C is non-zero and a Personal Loot
	// List row when +0x14 is non-zero. So adding a row means putting the item on the corpse and re-sending
	// 0x03. See docs/advloot.md 8.1.9.
	if (sub == "additem" || sub == "removeitem") {
		const bool add = (sub == "additem");

		uint32_t item_id = Strings::ToUnsignedInt(sep->arg[2]);
		if (!item_id) {
			c->Message(Chat::Red, "Usage: /advloot %s <item_id> [quantity]", sub.c_str());
			return;
		}

		const auto* item = database.GetItem(item_id);
		if (!item) {
			c->Message(Chat::Red, "Item [%u] is not in the item table.", item_id);
			return;
		}

		// The command acts on the corpse whose rows the window is currently showing.
		Corpse* corpse = nullptr;
		for (uint16_t corpse_id : c->advloot_corpses) {
			Corpse* candidate = entity_list.GetCorpseByID(corpse_id);
			if (candidate) {
				corpse = candidate;
				break;
			}
		}

		if (!corpse) {
			corpse = c->AdvLootCorpse();
		}

		// Last resort: the corpse this player most recently opened with a right-click. Right-clicking a
		// corpse registers it here even when no AdvLoot rows were pushed for it yet, which is what lets
		// /advloot additem be used straight after the loot window is opened.
		if (!corpse) {
			corpse = entity_list.GetCorpseByOwner(c);
		}

		if (!corpse) {
			c->Message(Chat::Red, "You have no advanced-loot corpse to modify.");
			return;
		}

		if (add) {
			uint32_t quantity = sep->arg[3][0] ? Strings::ToUnsignedInt(sep->arg[3]) : 1;
			if (!quantity) {
				quantity = 1;
			}

			corpse->AddItem(item_id, static_cast<uint16_t>(quantity));

			if (std::find(c->advloot_corpses.begin(), c->advloot_corpses.end(), corpse->GetID()) == c->advloot_corpses.end()) {
				c->advloot_corpses.push_back(corpse->GetID());
			}

			c->advloot_last_corpse_id = corpse->GetID();
			corpse->SetAdvLootPendingRows(c->CharacterID(), static_cast<uint32_t>(quantity));

			// 0x03 is the only packet that draws rows; it clears both panes first, so every corpse this
			// client has rows for is re-pushed in one packet rather than just the one that changed.
			c->AdvLootRender();

			c->Message(
				Chat::Yellow,
				"Added %u x [%s] to the advanced loot rows for corpse [%u].",
				quantity,
				item->Name,
				corpse->GetID()
			);
		} else {
			uint32_t quantity = sep->arg[3][0] ? Strings::ToUnsignedInt(sep->arg[3]) : 0;

			// Drop it from the corpse first so no later rebuild redraws the row, then tell the window to
			// remove the row it already drew.
			// RemoveItemByID stops as soon as removed_count reaches quantity, so 0 removes nothing -
			// omitting the argument means "every stack of this item on the corpse".
			corpse->RemoveItemByID(item_id, quantity ? static_cast<int>(quantity) : 0x7fffffff);
			c->SendAdvLootRowRemoved(item_id, item->Name);

			if (!corpse->HasAdvLootItems()) {
				corpse->ClearAdvLootPendingRowsFor(c->CharacterID());
				c->advloot_corpses.erase(
					std::find(c->advloot_corpses.begin(), c->advloot_corpses.end(), corpse->GetID())
				);
			}

			c->Message(
				Chat::Yellow,
				"Removed %s from the advanced loot rows for corpse [%u].",
				item->Name,
				corpse->GetID()
			);
		}

		return;
	}

	uint32_t choice = AdvLoot::ChoiceNone;
	if (sub == "need") {
		choice = AdvLoot::ChoiceNeed;
	} else if (sub == "greed") {
		choice = AdvLoot::ChoiceGreed;
	} else if (sub == "never") {
		choice = AdvLoot::ChoiceNever;
	}

	if (choice == AdvLoot::ChoiceNone) {
		c->Message(Chat::Yellow, "Usage: /advloot need|greed|never <item_id> | /advloot additem|removeitem <item_id> [quantity] | /advloot assign <item_id> <name> | /advloot mol [name]");
		return;
	}

	uint32_t item_id = Strings::ToUnsignedInt(sep->arg[2]);
	if (!item_id) {
		c->Message(Chat::Red, "Usage: /advloot %s <item_id>", sub.c_str());
		return;
	}

	const auto* item = database.GetItem(item_id);
	if (!item) {
		c->Message(Chat::Red, "Item [%u] is not in the item table.", item_id);
		return;
	}

	// FilterBit: AutoRoll 1 << 0, AlwaysNeed 1 << 1, AlwaysGreed 1 << 2, NeverLoot 1 << 3, so
	// 1 << choice lands on the right bit for Need (2), Greed (4) and Never (8).
	c->SaveAdvLootFilter(item_id, 1 << choice, item->Icon, item->Name);

	bool applied = false;
	for (uint16_t corpse_id : c->advloot_corpses) {
		Corpse* corpse = entity_list.GetCorpseByID(corpse_id);
		if (!corpse) {
			continue;
		}

		corpse->RecordAdvLootChoice(c, item_id, choice);
		applied = true;

		// Push the choice onto the row the client already drew (subcmd 0x0C, flag 0).
		c->SendAdvLootChoice(item_id, choice, item->Name);
	}

	c->Message(
		Chat::Yellow,
		"[%s] set to %s - %s",
		item->Name,
		AdvLoot::ChoiceName(choice),
		applied ? "applied to the tracked corpse" : "saved for future corpse rows"
	);
}
