/*	EQEMu: Everquest Server Emulator

	Copyright (C) 2001-2016 EQEMu Development Team (http://eqemulator.net)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; version 2 of the License.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY except by those people which sell it, which
	are required to give you total support for your newly bought product;
	without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "../global_define.h"
#include "../eqemu_config.h"
#include "../eqemu_logsys.h"
#include "laurion.h"
#include "../opcodemgr.h"

#include "../eq_stream_ident.h"
#include "../crc32.h"

#include "../eq_constants.h"
#include "../eq_limits.h"
#include "../eq_packet_structs.h"
#include "../misc_functions.h"
#include "../strings.h"
#include "../say_link.h"
#include "../inventory_profile.h"
#include "laurion_structs.h"
#include "../rulesys.h"
#include "../path_manager.h"
#include "../classes.h"
#include "../races.h"
#include "../bodytypes.h"
#include "../raid.h"

#include <iostream>
#include <sstream>
#include <numeric>
#include <cassert>
#include <cinttypes>
#include <cstdint>
#include <cstring>

namespace Laurion
{
	static const char* name = "Laurion";
	static OpcodeManager* opcodes = nullptr;
	static Strategy struct_strategy;

	void SerializeItem(SerializeBuffer &buffer, const EQ::ItemInstance* inst, int16 slot_id, uint8 depth, ItemPacketType packet_type);

	// message link converters
	static inline void ServerToLaurionConvertLinks(std::string& message_out, const std::string& message_in);
	static inline void LaurionToServerConvertLinks(std::string& message_out, const std::string& message_in);

	// SpawnAppearance
	static inline uint32 ServerToLaurionSpawnAppearanceType(uint32 server_type);
	static inline uint32 LaurionToServerSpawnAppearanceType(uint32 laurion_type, uint32 laurion_parameter);

	// server to client inventory location converters
	static inline structs::InventorySlot_Struct ServerToLaurionSlot(uint32 server_slot);
	static inline structs::InventorySlot_Struct ServerToLaurionCorpseSlot(uint32 server_corpse_slot);
	static inline uint32 ServerToLaurionCorpseMainSlot(uint32 server_corpse_slot);
	static inline structs::TypelessInventorySlot_Struct ServerToLaurionTypelessSlot(uint32 server_slot, int16 server_type);

	// client to server inventory location converters
	static inline uint32 LaurionToServerSlot(structs::InventorySlot_Struct laurion_slot);
	static inline uint32 LaurionToServerCorpseSlot(structs::InventorySlot_Struct laurion_corpse_slot);
	static inline uint32 LaurionToServerCorpseMainSlot(uint32 laurion_corpse_slot);
	static inline uint32 LaurionToServerTypelessSlot(structs::TypelessInventorySlot_Struct laurion_slot, int16 laurion_type);
	static inline structs::InventorySlot_Struct LaurionCastingInventorySlotToInventorySlot(structs::CastSpellInventorySlot_Struct laurion_slot);
	static inline structs::CastSpellInventorySlot_Struct LaurionInventorySlotToCastingInventorySlot(structs::InventorySlot_Struct laurion_slot);

	// Item packet types
	static item::ItemPacketType ServerToLaurionItemPacketType(ItemPacketType laurion_type);

	// casting slots
	static inline spells::CastingSlot ServerToLaurionCastingSlot(EQ::spells::CastingSlot slot);
	static inline EQ::spells::CastingSlot LaurionToServerCastingSlot(spells::CastingSlot slot);

	// buff slots
	static inline int ServerToLaurionBuffSlot(int index);
	static inline int LaurionToServerBuffSlot(int index);

	// Laurion numbers its inventory window as 23 equipment slots (0-22), 12 general slots (23-34) and a
	// cursor at 35.  EQEmu's canonical slot-id space is RoF2 based: 10 general slots (23-32) and cursor 33.
	// Laurion's two extra general slots are carried on the otherwise unused canonical ids 34 and 35, which
	// keeps every existing RoF2/UF/Ti slot id (and the DB rows) untouched - see EQ::invslot::IsGeneralSlot
	// and EQ::invbag::IsGeneralBagSlot.
	static inline int16 LaurionGeneralSlotToServer(int16 client_slot)
	{
		if (client_slot == invslot::slotCursor) return EQ::invslot::slotCursor;
		if (client_slot == invslot::slotGeneral11) return 34;
		if (client_slot == invslot::slotGeneral12) return 35;
		return client_slot;
	}

	static inline int16 ServerGeneralSlotToLaurion(int16 server_slot)
	{
		if (server_slot == EQ::invslot::slotCursor) return invslot::slotCursor;
		if (server_slot == 34) return invslot::slotGeneral11;
		if (server_slot == 35) return invslot::slotGeneral12;
		return server_slot;
	}

	// EQEmu addresses a corpse with canonical ids 23..58, the client's corpse container is indexed 0..35.
	static inline bool IsLaurionCorpseSlot(int16 server_slot)
	{
		return server_slot >= EQ::invslot::CORPSE_BEGIN && server_slot <= EQ::invslot::CORPSE_END;
	}

	void Register(EQStreamIdentifier& into)
	{
		//create our opcode manager if we havent already
		if (opcodes == nullptr) {

			std::string opfile = fmt::format("{}/patch_{}.conf", PathManager::Instance()->GetPatchPath(), name);

			//load up the opcode manager.
			//TODO: figure out how to support shared memory with multiple patches...
			opcodes = new RegularOpcodeManager();
			if (!opcodes->LoadOpcodes(opfile.c_str())) {
				LogNetcode("[OPCODES] Error loading opcodes file [{}]. Not registering patch [{}]", opfile.c_str(), name);
				return;
			}
		}

		//ok, now we have what we need to register.

		EQStreamInterface::Signature signature;
		std::string pname;

		//register our world signature.
		//Laurion client sends 464-byte payload (username\0 + password\0 + "Unknown"\0 + zero-padding)
		//first_length must be the actual payload length: CheckSignature() only ignores length when
		//the *packet* length is 0, so first_length = 0 never matches a 464 byte login packet.
		pname = std::string(name) + "_world";
		signature.ignore_eq_opcode = 0;
		signature.first_length = sizeof(structs::LoginInfo_Struct);
		signature.first_eq_opcode = opcodes->EmuToEQ(OP_SendLoginInfo);
		into.RegisterPatch(signature, pname.c_str(), &opcodes, &struct_strategy);

		//register our zone signature.
		pname = std::string(name) + "_zone";
		signature.ignore_eq_opcode = opcodes->EmuToEQ(OP_AckPacket);
		signature.first_length = sizeof(structs::ClientZoneEntry_Struct);
		signature.first_eq_opcode = opcodes->EmuToEQ(OP_ZoneEntry);
		into.RegisterPatch(signature, pname.c_str(), &opcodes, &struct_strategy);
		LogNetcode("[StreamIdentify] Registered patch [{}]", name);
	}

	void Reload()
	{
		//we have a big problem to solve here when we switch back to shared memory
		//opcode managers because we need to change the manager pointer, which means
		//we need to go to every stream and replace it's manager.

		if (opcodes != nullptr) {
			std::string opfile = fmt::format("{}/patch_{}.conf", PathManager::Instance()->GetPatchPath(), name);
			if (!opcodes->ReloadOpcodes(opfile.c_str())) {
				LogNetcode("[OPCODES] Error reloading opcodes file [{}] for patch [{}]", opfile.c_str(), name);
				return;
			}
			LogNetcode("[OPCODES] Reloaded opcodes for patch [{}]", name);
		}
	}

	Strategy::Strategy() : StructStrategy()
	{
		//all opcodes default to passthrough.
#include "ss_register.h"
#include "laurion_ops.h"
	}

	std::string Strategy::Describe() const
	{
		std::string r;
		r += "Patch ";
		r += name;
		return(r);
	}

	const EQ::versions::ClientVersion Strategy::ClientVersion() const
	{
		return EQ::versions::ClientVersion::Laurion;
	}

#include "ss_define.h"

	// ENCODE methods
	ENCODE(OP_Action) {
		ENCODE_LENGTH_EXACT(Action_Struct);
		SETUP_DIRECT_ENCODE(Action_Struct, structs::MissileHitInfo);

		//This is mostly figured out; there's two unknowns, only unknown1 is read by the client
		OUT(target);
		OUT(source);
		eq->spell_id = emu->spell;
		eq->effect_type = emu->effect_flag;
		eq->effective_casting_level = 0; //if you set this to != 0 it will use this level instead of calculating it
		eq->unknown1 = 0;
		eq->unknown2 = 0;
		eq->damage = 0; //client doesn't read this but live sends it here, can just set 0
		eq->modifier = 1.0f + (emu->instrument_mod - 10) / 10.0f;
		OUT(force);
		OUT(hit_heading);
		OUT(hit_pitch);
		eq->skill = emu->type;
		OUT(level);

		FINISH_ENCODE();
	}

	ENCODE(OP_AltCurrency)
	{
		// Laurion 0x01d0. Client: ZonePacket__dispatchRecv case 0x01d0 -> FUN_1401f3eb0.
		//
		// This is what fills pinstLocalPC + 0x2188, the alternate-currency definition table. Without it
		// every FUN_140665fb0(PC + 0x2188, currency_id, 1) lookup returns 0 and the caller renders
		// CDBStr__GetString(0, 17) as the literal label "Unknown DB String 0-17". The merchant Recover
		// tab hits this directly: its page handler (FUN_1404570e0) hardcodes currency 20 (Tokens of
		// Reclamation) and prints eqstr 4620/4621, "<merchant> told you, 'That'll be 1 <currency name>
		// to recover the <item>.'", with the balance check PlayerPointManager(PC + 0x2158, 20, 1) >= 1.
		EQApplicationPacket *in = *p;
		*p = nullptr;

		if (in->size < sizeof(uint32)) {
			LogNetcode("Wrong size on outbound [OP_AltCurrency]: Got [{}], expected at least [4]", in->size);
			safe_delete(in);
			return;
		}

		const uint32 mode = *reinterpret_cast<uint32 *>(in->pBuffer);

		if (mode == AlternateCurrencyMode::Populate) {
			if (in->size < sizeof(AltCurrencyPopulate_Struct)) {
				LogNetcode("Wrong size on outbound [OP_AltCurrency populate]: Got [{}], expected at least [{}]", in->size, sizeof(AltCurrencyPopulate_Struct));
				safe_delete(in);
				return;
			}

			AltCurrencyPopulate_Struct *emu = reinterpret_cast<AltCurrencyPopulate_Struct *>(in->pBuffer);
			const uint32 count = emu->count;

			if (in->size < sizeof(AltCurrencyPopulate_Struct) + sizeof(AltCurrencyPopulateEntry_Struct) * count) {
				LogNetcode("Wrong size on outbound [OP_AltCurrency populate]: Got [{}], expected at least [{}]", in->size, sizeof(AltCurrencyPopulate_Struct) + sizeof(AltCurrencyPopulateEntry_Struct) * count);
				safe_delete(in);
				return;
			}

			// Laurion's entry record is 29 bytes against RoF2's 25, so the payload grows with the
			// currency count: 8 + 29 * 63 = 1835 bytes for the current PEQ table.
			auto outapp = new EQApplicationPacket(
				OP_AltCurrency,
				sizeof(structs::AltCurrencyPopulate_Struct) + sizeof(structs::AltCurrencyPopulateEntry_Struct) * count
			);
			auto eq = reinterpret_cast<structs::AltCurrencyPopulate_Struct *>(outapp->pBuffer);

			eq->opcode = AlternateCurrencyMode::Populate;
			eq->count = count;
			for (uint32 i = 0; i < count; ++i) {
				eq->entries[i].currency_number = emu->entries[i].currency_number;
				eq->entries[i].kind            = emu->entries[i].unknown00;
				eq->entries[i].dbstr_id        = emu->entries[i].currency_number2;
				eq->entries[i].item_id         = emu->entries[i].item_id;
				eq->entries[i].item_icon       = emu->entries[i].item_icon;
				eq->entries[i].stack_size      = emu->entries[i].stack_size;
				eq->entries[i].unknown024      = 0;
				eq->entries[i].display         = 1;
			}

			dest->FastQueuePacket(&outapp, ack_req);
		}
		else if (mode == AlternateCurrencyMode::Update) {
			if (in->size < sizeof(AltCurrencyUpdate_Struct)) {
				LogNetcode("Wrong size on outbound [OP_AltCurrency update]: Got [{}], expected at least [{}]", in->size, sizeof(AltCurrencyUpdate_Struct));
				safe_delete(in);
				return;
			}

			AltCurrencyUpdate_Struct *emu = reinterpret_cast<AltCurrencyUpdate_Struct *>(in->pBuffer);

			auto outapp = new EQApplicationPacket(OP_AltCurrency, sizeof(structs::AltCurrencyUpdate_Struct));
			auto eq = reinterpret_cast<structs::AltCurrencyUpdate_Struct *>(outapp->pBuffer);

			eq->opcode = AlternateCurrencyMode::Update;
			memcpy(eq->name, emu->name, sizeof(eq->name));
			eq->currency_number = emu->currency_number;
			eq->kind            = emu->unknown072;
			// EQEmu only carries the new total. Laurion wants the total at +0x50 and keeps +0x4c for the
			// chat line, so both get the total and the announce flag stays clear: the zone already prints
			// its own currency messages.
			eq->amount       = emu->amount;
			eq->new_balance  = emu->amount;
			eq->announce     = 0;

			dest->FastQueuePacket(&outapp, ack_req);
		}
		else {
			LogNetcode("Unknown OP_AltCurrency mode [{}]", mode);
		}

		safe_delete(in);
	}

	ENCODE(OP_Animation)
	{
		ENCODE_LENGTH_EXACT(Animation_Struct);
		SETUP_DIRECT_ENCODE(Animation_Struct, structs::Animation_Struct);

		OUT(spawnid);
		OUT(action);
		OUT(speed);

		FINISH_ENCODE();
	}

	ENCODE(OP_ApplyPoison)
	{
		ENCODE_LENGTH_EXACT(ApplyPoison_Struct);
		SETUP_DIRECT_ENCODE(ApplyPoison_Struct, structs::ApplyPoison_Struct);

		eq->inventorySlot = ServerToLaurionTypelessSlot(emu->inventorySlot, EQ::invtype::typePossessions);
		OUT(success);

		FINISH_ENCODE();
	}

	ENCODE(OP_AugmentInfo)
	{
		ENCODE_LENGTH_EXACT(AugmentInfo_Struct);
		SETUP_DIRECT_ENCODE(AugmentInfo_Struct, structs::AugmentInfo_Struct);

		OUT(itemid);
		OUT(window);
		strn0cpy(eq->augment_info, emu->augment_info, 64);

		FINISH_ENCODE();
	}

	ENCODE(OP_BeginCast)
	{
		ENCODE_LENGTH_EXACT(BeginCast_Struct);
		SETUP_DIRECT_ENCODE(BeginCast_Struct, structs::BeginCast_Struct);

		OUT(spell_id);
		OUT(caster_id);
		OUT(cast_time);
		eq->unknown0e = 1; //not sure what this is; but its usually 1 on live

		FINISH_ENCODE();
	}

	ENCODE(OP_BlockedBuffs)
	{
		ENCODE_LENGTH_EXACT(BlockedBuffs_Struct);
		SETUP_DIRECT_ENCODE(BlockedBuffs_Struct, structs::BlockedBuffs_Struct);

		for (uint32 i = 0; i < BLOCKED_BUFF_COUNT; ++i)
			eq->SpellID[i] = emu->SpellID[i];

		for (uint32 i = BLOCKED_BUFF_COUNT; i < structs::BLOCKED_BUFF_COUNT; ++i)
			eq->SpellID[i] = -1;

		OUT(Count);
		OUT(Pet);
		OUT(Initialise);
		OUT(Flags);

		FINISH_ENCODE();
	}

	ENCODE(OP_Buff)
	{
		ENCODE_LENGTH_EXACT(SpellBuffPacket_Struct);
		SETUP_DIRECT_ENCODE(SpellBuffPacket_Struct, structs::EQAffectPacket_Struct);

		eq->entity_id = emu->entityid;
		eq->unknown004 = 0;

		//fill in affect info
		eq->affect.caster_id.Id = emu->buff.player_id;
		eq->affect.flags = 0;
		eq->affect.spell_id = emu->buff.spellid;
		eq->affect.duration = emu->buff.duration;
		eq->affect.initial_duration = emu->buff.duration;
		eq->affect.hit_count = emu->buff.num_hits;
		eq->affect.viral_timer = 0;
		eq->affect.modifier = emu->buff.bard_modifier == 10 ? 1.0f : emu->buff.bard_modifier / 10.0f;
		eq->affect.y = emu->buff.y;
		eq->affect.x = emu->buff.x;
		eq->affect.z = emu->buff.z;
		eq->affect.level = emu->buff.level;

		eq->slot_id = ServerToLaurionBuffSlot(emu->slotid);
		if (emu->bufffade == 1)
		{
			eq->buff_fade = 1;
		}
		else
		{
			eq->buff_fade = 2;
		}

		EQApplicationPacket* outapp = nullptr;
		if (emu->bufffade == 1)
		{
			// Bit of a hack. OP_Buff appears to add/remove the buff while OP_BuffCreate adds/removes the actual buff icon
			outapp = new EQApplicationPacket(OP_BuffCreate, 30);
			outapp->WriteUInt32(emu->entityid);
			outapp->WriteUInt32(0);	// tic timer
			outapp->WriteUInt8(0);		// Type of OP_BuffCreate packet ?
			outapp->WriteUInt16(1);		// 1 buff in this packet
			outapp->WriteUInt32(ServerToLaurionBuffSlot(emu->slotid));
			outapp->WriteUInt32(0xffffffff);		// SpellID (0xffff to remove)
			outapp->WriteUInt32(0);			// Duration
			outapp->WriteUInt32(0);			// numhits
			outapp->WriteUInt8(0);		// Caster name
			outapp->WriteUInt8(0);		// Type
			outapp->WriteUInt8(0);		// Type
		}

		FINISH_ENCODE();

		if (outapp) {
			dest->FastQueuePacket(&outapp);
		}
	}

	ENCODE(OP_BuffCreate)
	{
		SETUP_VAR_ENCODE(BuffIcon_Struct);

		//Laurion has one extra 0x00 byte before the end byte
		uint32 sz = 13 + (17 * emu->count) + emu->name_lengths; // 17 includes nullterm
		__packet->size = sz;
		__packet->pBuffer = new unsigned char[sz];
		memset(__packet->pBuffer, 0, sz);

		__packet->WriteUInt32(emu->entity_id);
		__packet->WriteUInt32(emu->tic_timer);
		__packet->WriteUInt8(emu->all_buffs);			// 1 indicates all buffs on the player (0 to add or remove a single buff)
		__packet->WriteUInt16(emu->count);

		for (int i = 0; i < emu->count; ++i)
		{
			__packet->WriteUInt32(emu->type == 0 ? ServerToLaurionBuffSlot(emu->entries[i].buff_slot) : emu->entries[i].buff_slot);
			__packet->WriteUInt32(emu->entries[i].spell_id);
			__packet->WriteUInt32(emu->entries[i].tics_remaining);
			__packet->WriteUInt32(emu->entries[i].num_hits); // Unknown
			__packet->WriteString(emu->entries[i].caster);
		}
		__packet->WriteUInt8(0); // Unknown1
		__packet->WriteUInt8(emu->type); // Unknown2

		FINISH_ENCODE();
	}

	// Target_BuffWindow feed. Laurion client: recv handler 0x1401d879a ->
	// CTargetWnd::RefreshTargetBuffs (0x1404eb150). Wire layout is the same as OP_BuffCreate above;
	// see docs/targetwnd.md. Sent by Mob::SendBuffsToClient() via Handle_OP_TargetMouse.
	ENCODE(OP_TargetBuffs)
	{
		SETUP_VAR_ENCODE(BuffIcon_Struct);

		// Recompute from the caster strings instead of trusting emu->name_lengths so a caller that
		// leaves it at 0 cannot produce a short packet.
		uint32 name_lengths = 0;
		for (int i = 0; i < emu->count; ++i)
			name_lengths += static_cast<uint32>(strlen(emu->entries[i].caster)) + 1; // + nullterm

		uint32 sz = 13 + (16 * emu->count) + name_lengths;
		__packet->size = sz;
		__packet->pBuffer = new unsigned char[sz];
		memset(__packet->pBuffer, 0, sz);

		__packet->WriteUInt32(emu->entity_id);		// client skips these 4 bytes, but they must be present
		__packet->WriteUInt32(emu->tic_timer);		// sub-tick remainder in ms, added to every duration
		__packet->WriteUInt8(emu->all_buffs);		// 1 = full list (client clears the gaps), 0 = incremental
		__packet->WriteUInt16(emu->count);

		for (int i = 0; i < emu->count; ++i)
		{
			// No ServerToLaurionBuffSlot() remap here: the target window indexes its own 400 slot array
			// directly and only accepts slots 0-41 for a player target, 0-399 for an NPC/PC target.
			__packet->WriteUInt32(emu->entries[i].buff_slot);
			__packet->WriteUInt32(emu->entries[i].spell_id);
			__packet->WriteUInt32(emu->entries[i].tics_remaining);	// <0 = permanent, 0 = clear the slot
			__packet->WriteUInt32(emu->entries[i].num_hits);		// parsed, ignored by the target window
			__packet->WriteString(emu->entries[i].caster);
		}

		// Trailing byte #1 -> CTargetWnd::Type (0x34c): 1 self, 4 group member, 5 PC, 7 NPC.
		// Trailing byte #2 -> target spawn + 0x215 (the same 0/1 flag CharacterZoneClient::HitBySpell writes).
		// Note this is the opposite order from OP_BuffCreate above, which is harmless there because the
		// self packet always carries type == 0.
		__packet->WriteUInt8(emu->type);
		__packet->WriteUInt8(0);

		FINISH_ENCODE();
	}

	// PetInfoWindow's PetBuffWindow feed. Laurion client: recv handler 0x1401d870a ->
	// CPetInfoWnd::RefreshPetBuffs (0x14047ec10). Same BuffIcon layout as OP_TargetBuffs above;
	// see docs/petwnd.md. Sent by Mob::SendPetBuffsToClient().
	ENCODE(OP_PetBuffWindow)
	{
		SETUP_VAR_ENCODE(BuffIcon_Struct);

		// Recompute from the caster strings instead of trusting emu->name_lengths.
		uint32 name_lengths = 0;
		for (int i = 0; i < emu->count; ++i)
			name_lengths += static_cast<uint32>(strlen(emu->entries[i].caster)) + 1; // + nullterm

		uint32 sz = 13 + (16 * emu->count) + name_lengths;
		__packet->size = sz;
		__packet->pBuffer = new unsigned char[sz];
		memset(__packet->pBuffer, 0, sz);

		__packet->WriteUInt32(emu->entity_id);		// REQUIRED here: the dispatcher pre-reads this spawn id,
							// resolves it through the client spawn manager and drops the whole packet
							// (no window call at all) when the spawn is unknown. The window parser then
							// rewinds the cursor to 0 and skips these same 4 bytes.
		__packet->WriteUInt32(emu->tic_timer);		// sub-tick remainder in ms, added to every duration
		__packet->WriteUInt8(emu->all_buffs);		// 1 = full list (client clears every slot the packet omits), 0 = incremental
		__packet->WriteUInt16(emu->count);

		for (int i = 0; i < emu->count; ++i)
		{
			// No ServerToLaurionBuffSlot() remap: CPetInfoWnd indexes its own 400 entry array
			// (records of 0x78 bytes at CPetInfoWnd+0x378) straight from the wire slot, gate is slot < 400.
			__packet->WriteUInt32(emu->entries[i].buff_slot);
			__packet->WriteUInt32(emu->entries[i].spell_id);
			__packet->WriteUInt32(emu->entries[i].tics_remaining);	// <0 = permanent, 0 = expires after tic_timer ms
			__packet->WriteUInt32(emu->entries[i].num_hits);		// parsed, ignored by the pet window
			__packet->WriteString(emu->entries[i].caster);
		}

		// Trailing byte #1 is skipped by RefreshPetBuffs (CPetInfoWnd has no CTargetWnd::Type equivalent).
		// Trailing byte #2 -> the local PC's pet spawn + 0x215, the same 0/1 buffed/hit flag the target
		// window writes. Note the client reads the pet from pinstLocalPC+0x2850 -> +0x578, NOT from
		// entity_id, so this byte always lands on the player's own pet.
		__packet->WriteUInt8(0);
		__packet->WriteUInt8(0);

		FINISH_ENCODE();
	}

	ENCODE(OP_CancelTrade)
	{
		ENCODE_LENGTH_EXACT(CancelTrade_Struct);
		SETUP_DIRECT_ENCODE(CancelTrade_Struct, structs::CancelTrade_Struct);

		OUT(fromid);
		OUT(action);

		FINISH_ENCODE();
	}

	ENCODE(OP_CastSpell)
	{
		ENCODE_LENGTH_EXACT(CastSpell_Struct);
		SETUP_DIRECT_ENCODE(CastSpell_Struct, structs::CastSpell_Struct);

		eq->slot = static_cast<uint32>(ServerToLaurionCastingSlot(static_cast<EQ::spells::CastingSlot>(emu->slot)));

		OUT(spell_id);
		//we should double check this cause it feels wrong
		eq->inventory_slot = LaurionInventorySlotToCastingInventorySlot(ServerToLaurionSlot(emu->inventoryslot));
		//OUT(inventoryslot);
		OUT(target_id);

		FINISH_ENCODE();
	}

	ENCODE(OP_ChannelMessage)
	{
		EQApplicationPacket* in = *p;
		*p = nullptr;

		ChannelMessage_Struct* emu = (ChannelMessage_Struct*)in->pBuffer;

		unsigned char* __emu_buffer = in->pBuffer;

		std::string old_message = emu->message;
		std::string new_message;
		ServerToLaurionConvertLinks(new_message, old_message);

		in->size = strlen(emu->sender) + strlen(emu->targetname) + new_message.length() + 43;

		in->pBuffer = new unsigned char[in->size];

		char* OutBuffer = (char*)in->pBuffer;

		VARSTRUCT_ENCODE_STRING(OutBuffer, emu->sender);
		VARSTRUCT_ENCODE_STRING(OutBuffer, emu->targetname);
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0);	// Unknown
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0);	// Unknown
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, emu->language);
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, emu->chan_num);
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0);	// Unknown
		VARSTRUCT_ENCODE_TYPE(uint8, OutBuffer, 0);	// Unknown
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, emu->skill_in_language);
		VARSTRUCT_ENCODE_STRING(OutBuffer, new_message.c_str());

		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0);	// Unknown
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0);	// Unknown
		VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0);	// Unknown
		VARSTRUCT_ENCODE_TYPE(uint16, OutBuffer, 0);	// Unknown
		VARSTRUCT_ENCODE_TYPE(uint8, OutBuffer, 0);	// Unknown

		delete[] __emu_buffer;
		dest->FastQueuePacket(&in, ack_req);
	}

	ENCODE(OP_CharInventory) {
		//consume the packet
		EQApplicationPacket* in = *p;
		*p = nullptr;

		if (!in->size) {
			in->size = 4;
			in->pBuffer = new uchar[in->size];
			memset(in->pBuffer, 0, in->size);

			dest->FastQueuePacket(&in, ack_req);
			return;
		}

		//store away the emu struct
		uchar* __emu_buffer = in->pBuffer;

		int item_count = in->size / sizeof(EQ::InternalSerializedItem_Struct);
		if (!item_count || (in->size % sizeof(EQ::InternalSerializedItem_Struct)) != 0) {
			LogNetcode("[STRUCTS] Wrong size on outbound {}: Got {}, expected multiple of {}",
				opcodes->EmuToName(in->GetOpcode()), in->size, sizeof(EQ::InternalSerializedItem_Struct));

			delete in;
			return;
		}

		EQ::InternalSerializedItem_Struct* eq = (EQ::InternalSerializedItem_Struct*)in->pBuffer;
		SerializeBuffer buffer;
		buffer.WriteUInt32(item_count);

		for (int index = 0; index < item_count; ++index, ++eq) {
			SerializeItem(buffer, (const EQ::ItemInstance*)eq->inst, eq->slot_id, 0, ItemPacketCharInventory);
		}

		in->size = buffer.size();
		in->pBuffer = new unsigned char[buffer.size()];
		memcpy(in->pBuffer, buffer.buffer(), buffer.size());

		delete[] __emu_buffer;

		dest->FastQueuePacket(&in, ack_req);
	}

	// Echo of the click, broadcast to every client in the zone so the ground item actor is dropped.
	// Receive branch: ZonePacket__dispatchRecv 0x1401e4828 (CMP 0x687e) -> 0x1401e7749. It reads
	// player_id at +4 and, when that is 0 or equals the local player's id (FUN_1401d4d90), clears the
	// "someone else is using this" state at DAT_140e3b9e0. It then resolves the ground item by
	// drop_id (FUN_14020b4a0 compares record + 0x20) and removes it when record + 0x98 - the weight
	// dword ENCODE(OP_GroundSpawn) writes from object_type - is >= 0, or when the flag byte at +8 is
	// non-zero. We send flag = 1 so the removal does not depend on that weight value.
	ENCODE(OP_ClickObject)
	{
		ENCODE_LENGTH_EXACT(ClickObject_Struct);
		SETUP_DIRECT_ENCODE(ClickObject_Struct, structs::ClickObject_Struct);

		OUT(drop_id);
		OUT(player_id);
		eq->unknown08 = 1;

		FINISH_ENCODE();
	}

	ENCODE(OP_ClickObjectAction)
	{
		ENCODE_LENGTH_EXACT(ClickObjectAction_Struct);
		SETUP_DIRECT_ENCODE(ClickObjectAction_Struct, structs::ClickObjectAction_Struct);

		OUT(drop_id);
		eq->unknown04 = -1;
		eq->unknown08 = -1;
		OUT(type);
		OUT(icon);
		eq->unknown16 = 0;
		OUT_str(object_name);

		FINISH_ENCODE();
	}

	ENCODE(OP_ClientUpdate)
	{
		ENCODE_LENGTH_EXACT(PlayerPositionUpdateServer_Struct);
		SETUP_DIRECT_ENCODE(PlayerPositionUpdateServer_Struct, structs::PlayerPositionUpdateServer_Struct);

		OUT(spawn_id);
		OUT(vehicle_id);
		eq->position.x = emu->x_pos;
		eq->position.y = emu->y_pos;
		eq->position.z = emu->z_pos;
		eq->position.heading = emu->heading;
		eq->position.deltaX = emu->delta_x;
		eq->position.deltaY = emu->delta_y;
		eq->position.deltaZ = emu->delta_z;
		eq->position.deltaHeading = emu->delta_heading;
		eq->position.animation = emu->animation;

		FINISH_ENCODE();
	}

	ENCODE(OP_Consider)
	{
		ENCODE_LENGTH_EXACT(Consider_Struct);
		SETUP_DIRECT_ENCODE(Consider_Struct, structs::Consider_Struct);

		OUT(playerid);
		OUT(targetid);
		OUT(faction);
		OUT(level);

		FINISH_ENCODE();
	}

	ENCODE(OP_Damage) {
		SETUP_DIRECT_ENCODE(CombatDamage_Struct, structs::CombatDamage_Struct);

		OUT(target);
		OUT(source);
		OUT(type);
		OUT(spellid);
		OUT(damage);
		OUT(force);
		OUT(hit_heading);
		OUT(hit_pitch);
		OUT(special);

		FINISH_ENCODE();
	}

	ENCODE(OP_Death)
	{
		ENCODE_LENGTH_EXACT(Death_Struct);
		SETUP_DIRECT_ENCODE(Death_Struct, structs::Death_Struct);

		OUT(spawn_id);
		OUT(killer_id);
		OUT(spell_id);
		OUT(attack_skill);
		OUT(damage);

		//This is a hack, we need to actually fix the ordering in source as this wont respect filters etc
		if (emu->attack_skill != 231) {
			auto combat_packet = new EQApplicationPacket(OP_Damage, sizeof(structs::CombatDamage_Struct));
			structs::CombatDamage_Struct* cds = (structs::CombatDamage_Struct*)combat_packet->pBuffer;

			cds->target = emu->spawn_id;
			cds->source = emu->killer_id;
			cds->type = emu->attack_skill;
			cds->damage = emu->damage;
			cds->spellid = -1;

			dest->FastQueuePacket(&combat_packet, ack_req);
		}

		FINISH_ENCODE();
	}

	ENCODE(OP_DeleteCharge)
	{
		LogNetcodeDetail("Laurion::ENCODE(OP_DeleteCharge)");

		ENCODE_FORWARD(OP_MoveItem);
	}

	ENCODE(OP_DeleteItem)
	{
		ENCODE_LENGTH_EXACT(DeleteItem_Struct);
		SETUP_DIRECT_ENCODE(DeleteItem_Struct, structs::DeleteItem_Struct);

		eq->from_slot = ServerToLaurionSlot(emu->from_slot);
		eq->to_slot = ServerToLaurionSlot(emu->to_slot);
		OUT(number_in_stack);

		FINISH_ENCODE();
	}

	ENCODE(OP_DeleteSpawn)
	{
		ENCODE_LENGTH_EXACT(DeleteSpawn_Struct);
		SETUP_DIRECT_ENCODE(DeleteSpawn_Struct, structs::DeleteSpawn_Struct);

		OUT(spawn_id);
		eq->unknown04 = 1;	// Observed

		FINISH_ENCODE();
	}

	ENCODE(OP_DisciplineUpdate)
	{
		ENCODE_LENGTH_EXACT(Disciplines_Struct);
		SETUP_DIRECT_ENCODE(Disciplines_Struct, structs::Disciplines_Struct);

		memcpy(&eq->values, &emu->values, sizeof(Disciplines_Struct));

		FINISH_ENCODE();
	}

	ENCODE(OP_ExpansionInfo)
	{
		ENCODE_LENGTH_EXACT(ExpansionInfo_Struct);
		SETUP_DIRECT_ENCODE(ExpansionInfo_Struct, structs::ExpansionInfo_Struct);

		OUT(Expansions);

		FINISH_ENCODE();
	}

	ENCODE(OP_ExpUpdate)
	{
		SETUP_DIRECT_ENCODE(ExpUpdate_Struct, structs::ExpUpdate_Struct);

		//later we should change the underlying server to use this more accurate value
		//and encode the 330 in the other patches
		eq->exp = emu->exp * 100000 / 330;

		FINISH_ENCODE();
	}

	ENCODE(OP_FeatureList)
	{
		// Variable length packet: flag byte, entry count, then count * (feature id, value) pairs.
		if ((*p)->size < sizeof(FeatureList_Struct)) {
			LogNetcode("Wrong size on outbound [{}] (FeatureList_Struct): Got [{}], expected at least [{}]", opcodes->EmuToName((*p)->GetOpcode()), (*p)->size, sizeof(FeatureList_Struct));
			delete *p;
			*p = nullptr;
			return;
		}

		uint32 count = ((FeatureList_Struct*)(*p)->pBuffer)->count;
		if ((*p)->size < sizeof(FeatureList_Struct) + count * sizeof(FeatureEntry_Struct)) {
			LogNetcode("Truncated feature list on outbound [{}]: Got [{}], expected [{}]", opcodes->EmuToName((*p)->GetOpcode()), (*p)->size, sizeof(FeatureList_Struct) + count * sizeof(FeatureEntry_Struct));
			delete *p;
			*p = nullptr;
			return;
		}

		SETUP_VAR_ENCODE(FeatureList_Struct);
		ALLOC_LEN_ENCODE(sizeof(FeatureList_Struct) + count * sizeof(structs::FeatureEntry_Struct));

		// Laurion reads the payload as a byte packed stream, which is exactly EQEmu's layout.
		__packet->SetWritePosition(0);
		__packet->WriteUInt8(emu->refresh_ui);
		__packet->WriteUInt32(count);
		for (uint32 i = 0; i < count; ++i) {
			__packet->WriteUInt32(emu->entries[i].feature_id);
			__packet->WriteUInt32(emu->entries[i].value);
		}

		FINISH_ENCODE();
	}

	ENCODE(OP_FormattedMessage)
	{
		EQApplicationPacket* in = *p;
		*p = nullptr;

		FormattedMessage_Struct* emu = (FormattedMessage_Struct*)in->pBuffer;

		char* old_message_ptr = (char*)in->pBuffer;
		old_message_ptr += sizeof(FormattedMessage_Struct);

		std::string old_message_array[9];

		for (int i = 0; i < 9; ++i) {
			if (*old_message_ptr == 0) { break; }
			old_message_array[i] = old_message_ptr;
			old_message_ptr += old_message_array[i].length() + 1;
		}

		SerializeBuffer buffer;
		buffer.WriteUInt32(emu->unknown0);
		buffer.WriteUInt8(0); // Observed
		buffer.WriteUInt32(emu->string_id);
		buffer.WriteUInt32(emu->type);

		for (int i = 0; i < 9; ++i) {
			std::string new_message;
			ServerToLaurionConvertLinks(new_message, old_message_array[i]);
			buffer.WriteLengthString(new_message);
		}

		auto outapp = new EQApplicationPacket(OP_FormattedMessage, buffer.size());
		outapp->WriteData(buffer.buffer(), buffer.size());
		dest->FastQueuePacket(&outapp, ack_req);

		delete in;
	}

	ENCODE(OP_GMTraining) {
		ENCODE_LENGTH_EXACT(GMTrainee_Struct);
		SETUP_DIRECT_ENCODE(GMTrainee_Struct, structs::GMTrainee_Struct);

		OUT(npcid);
		OUT(playerid);

		for (int i = 0; i < 100; ++i) {
			OUT(skills[i]);
		}

		eq->unknown408[0] = 1;
		eq->unknown408[1] = 0xC9;
		eq->unknown408[2] = 0xC9;
		eq->unknown408[3] = 0xC9;
		eq->unknown408[4] = 0xC9;
		eq->unknown408[5] = 0xC9;
		eq->unknown408[6] = 0xC9;
		eq->unknown408[7] = 0xC9;
		eq->unknown408[8] = 0xC9;
		eq->unknown408[9] = 0xC9;
		eq->unknown408[10] = 0xC9;
		eq->unknown408[11] = 0xC9;
		eq->unknown408[12] = 0xC9;
		eq->unknown408[13] = 0xC9;
		eq->unknown408[14] = 0xC9;
		eq->unknown408[15] = 0xC9;
		eq->unknown408[16] = 0xC9;
		eq->unknown408[17] = 0xC9;
		eq->unknown408[18] = 0xC9;
		eq->unknown408[19] = 0xC9;
		eq->unknown408[20] = 0xC9;
		eq->unknown408[21] = 0xC9;
		eq->unknown408[22] = 0xC9;
		eq->unknown408[23] = 0xC9;
		eq->unknown408[24] = 0xC9;
		eq->unknown408[25] = 0xC9;
		eq->unknown408[26] = 0xC9;
		eq->unknown408[27] = 0xC9;
		eq->unknown408[28] = 0xC9;
		eq->unknown408[29] = 0xC9;
		eq->unknown408[30] = 0xC9;
		eq->unknown408[31] = 0xC9;
		eq->unknown408[32] = 0xC9;
		eq->unknown408[33] = 0xCA; //the client far as I can tell doesn't read past the 1 byte + 32 0xc9s, but still setting it to what we saw
		eq->unknown408[34] = 0x8C;
		eq->unknown408[35] = 0xEC;

		FINISH_ENCODE();
	}

	ENCODE(OP_GMTrainSkillConfirm)
	{
		ENCODE_LENGTH_EXACT(GMTrainSkillConfirm_Struct);
		SETUP_DIRECT_ENCODE(GMTrainSkillConfirm_Struct, structs::GMTrainSkillConfirm_Struct);

		OUT(SkillID);
		OUT(Cost);
		OUT(NewSkill);
		OUT_str(TrainerName);

		FINISH_ENCODE();
	}

	ENCODE(OP_GroundSpawn)
	{
		EQApplicationPacket* in = *p;
		*p = nullptr;
		Object_Struct* emu = (Object_Struct*)in->pBuffer;

		SerializeBuffer buffer;
		buffer.WriteUInt32(emu->drop_id);
		buffer.WriteString(emu->object_name);
		buffer.WriteUInt16(emu->zone_id);
		buffer.WriteUInt16(emu->zone_instance);
		buffer.WriteUInt32(emu->drop_id); //this is some other sub but it's okay to duplicate
		buffer.WriteUInt32(0); //expires
		buffer.WriteFloat(emu->heading);
		buffer.WriteFloat(emu->tilt_x);
		buffer.WriteFloat(emu->tilt_y);
		buffer.WriteFloat(emu->size != 0 && (float)emu->size < 5000.f ? (float)((float)emu->size / 100.0f) : 1.f); //size, with weird peq hack
		buffer.WriteFloat(emu->y);
		buffer.WriteFloat(emu->x);
		buffer.WriteFloat(emu->z);
		buffer.WriteFloat(emu->object_type); //weight

		auto outapp = new EQApplicationPacket(OP_GroundSpawn, buffer.size());
		outapp->WriteData(buffer.buffer(), buffer.size());
		dest->FastQueuePacket(&outapp, ack_req);

		delete in;
	}

	ENCODE(OP_HPUpdate)
	{
		SETUP_DIRECT_ENCODE(SpawnHPUpdate_Struct, structs::SpawnHPUpdate_Struct);

		OUT(spawn_id);
		OUT(cur_hp);
		OUT(max_hp);

		FINISH_ENCODE();
	}

	ENCODE(OP_Illusion)
	{
		ENCODE_LENGTH_EXACT(Illusion_Struct);
		SETUP_DIRECT_ENCODE(Illusion_Struct, structs::Illusion_Struct);

		OUT(spawnid);
		OUT_str(charname);
		OUT(race);
		eq->unknown006[0] = 0;
		eq->unknown006[1] = 0;
		OUT(gender);
		OUT(texture);
		OUT(helmtexture);
		OUT(face);
		OUT(hairstyle);
		OUT(haircolor);
		OUT(beard);
		OUT(beardcolor);
		OUT(size);
		OUT(drakkin_heritage);
		OUT(drakkin_tattoo);
		OUT(drakkin_details);

		FINISH_ENCODE();
	}

	ENCODE(OP_ItemPacket)
	{
		EQApplicationPacket* in = *p;
		*p = nullptr;
		uchar* __emu_buffer = in->pBuffer;
		ItemPacket_Struct* old_item_pkt = (ItemPacket_Struct*)__emu_buffer;

		auto type = ServerToLaurionItemPacketType(old_item_pkt->PacketType);
		if (type == item::ItemPacketType::ItemPacketInvalid) {
			delete in;
			return;
		}

		switch (type)
		{
		case item::ItemPacketType::ItemPacketParcel: {
			ParcelMessaging_Struct       pms{};
			EQ::Util::MemoryStreamReader ss(reinterpret_cast<char*>(in->pBuffer), in->size);
			cereal::BinaryInputArchive   ar(ss);
			ar(pms);

			uint32 player_name_length = pms.player_name.length();
			uint32 note_length = pms.note.length();

			auto* int_struct = (EQ::InternalSerializedItem_Struct*)pms.serialized_item.data();

			SerializeBuffer buffer;
			buffer.WriteInt32((int32_t)type);
			SerializeItem(buffer, (const EQ::ItemInstance*)int_struct->inst, int_struct->slot_id, 0, old_item_pkt->PacketType);

			buffer.WriteUInt32(pms.sent_time);
			buffer.WriteLengthString(pms.player_name);
			buffer.WriteLengthString(pms.note);

			auto outapp = new EQApplicationPacket(OP_ItemPacket, buffer.size());
			outapp->WriteData(buffer.buffer(), buffer.size());
			dest->FastQueuePacket(&outapp, ack_req);
			break;
		}
		default: {
			EQ::InternalSerializedItem_Struct* int_struct = (EQ::InternalSerializedItem_Struct*)(&__emu_buffer[4]);
			SerializeBuffer buffer;
			buffer.WriteInt32((int32_t)type);
			SerializeItem(buffer, (const EQ::ItemInstance*)int_struct->inst, int_struct->slot_id, 0, old_item_pkt->PacketType);

			auto outapp = new EQApplicationPacket(OP_ItemPacket, buffer.size());
			outapp->WriteData(buffer.buffer(), buffer.size());
			dest->FastQueuePacket(&outapp, ack_req);
		}
		}

		delete in;
	}

	// The two item-preview replies below both arrive carrying the internal carrier SendItemPacket
	// builds: an ItemPacket_Struct whose SerializedItem holds the 10 byte InternalSerializedItem_Struct
	// { int16 slot_id; const void* inst; }. The instance pointer is only meaningful inside the zone
	// process, which is why the item is serialized here rather than in SendItemPacket.
	//
	// The slot is the tradeskill combine slot on purpose. SerializeItem only emits the SubContentSize
	// field when the slot survives its own container resolution, so an invalid slot would silently drop
	// it and desynchronize the client's reader. SLOT_TRADESKILL_EXPERIMENT_COMBINE (1000) resolves to
	// { typeWorld, slot -1, sub -1, aug -1 } in ServerToLaurionSlot: a complete, well formed record that
	// points at no container the client renders, so a preview item can never disturb the inventory.
	static const int16 PreviewItemSlot = EQ::invslot::SLOT_TRADESKILL_EXPERIMENT_COMBINE;

	// Laurion alt+left-click inspect reply: 0x0497, the same opcode the request came in on.
	//
	// Client: ZonePacket__dispatchRecv case 0x1401d90fb -> FUN_140200390(payload, size):
	//   i32 luck, i32 charges, u32 flags, then ItemClient__CreateItemClient on the rest of the buffer.
	// If the item deserializes it calls CItemDisplayManager::ShowItem(pinstCItemDisplayManager, &item,
	// flags) (FUN_140410DE0), which finds or creates a CItemDisplayWnd and runs
	// CItemDisplayWnd::SetItem (FUN_1404100C0). That is the only writer of CItemDisplayWnd::pItem
	// (window +0x3F8), and pItem is exactly what FUN_140410B20 - the 0x609c handler - matches a reply
	// against. Answering 0x0497 with 0x609c therefore cannot work: the window has no item, so the
	// enrich reply matches nothing and is discarded.
	ENCODE(OP_ItemLinkInspect) // 0x0497
	{
		EQApplicationPacket* in = *p;
		*p = nullptr;

		EQ::InternalSerializedItem_Struct* int_struct = (EQ::InternalSerializedItem_Struct*)(&in->pBuffer[4]);
		const EQ::ItemInstance* inst = (const EQ::ItemInstance*)int_struct->inst;
		if (!inst || !inst->GetItem()) {
			delete in;
			return;
		}

		SerializeBuffer buffer;
		buffer.WriteInt32(0);                                              // luck: no per instance augment luck yet
		buffer.WriteInt32(inst->GetCharges());                             // charges
		buffer.WriteUInt32(structs::ItemDisplayPreventLink);              // ShowItem flags
		SerializeItem(buffer, inst, PreviewItemSlot, 0, ItemPacketViewLink);

		auto outapp = new EQApplicationPacket(OP_ItemLinkInspect, buffer.size());
		outapp->WriteData(buffer.buffer(), buffer.size());
		dest->FastQueuePacket(&outapp, ack_req);

		delete in;
	}

	// Laurion alt+right-click Object Preview reply: 0x6159, again the request's own opcode.
	//
	// Client: ZonePacket__dispatchRecv case 0x1401e2c28:
	//   FUN_1401D1DA0(&buffer, payload, size)                 wrap, no fields consumed
	//   ItemClient__CreateItemClient(&holder, &buffer)       deserialize the item
	//   FUN_1400C4F40(&holder)                               bail when the item is null
	//   FUN_1401D4A90(&buffer, &trailing_byte)               one u8 read AFTER the item
	//   icon = ItemBase__GetImageNum(def, 0x24)
	//   b92  = 1, cleared only when !FUN_140614F40(item) and icon is 0x3F or 0x40 (no image)
	//   FUN_14046C730(DAT_140d83170, &holder, b92 || trailing, trailing)
	// FUN_14046C730 sets the item on the preview's inner display window (ObjectPreviewWnd +0x2E8) and
	// then FUN_14046C860 shows the window and its eleven children (+0x348 .. +0x3A0). The request side,
	// FUN_14046C5C0, only sends the request and raises the z-order (FUN_1405A3380) - it never installs
	// an item, which is why the window came up on "No item preview available".
	ENCODE(OP_ItemLinkClick) // 0x6159
	{
		EQApplicationPacket* in = *p;
		*p = nullptr;

		EQ::InternalSerializedItem_Struct* int_struct = (EQ::InternalSerializedItem_Struct*)(&in->pBuffer[4]);
		const EQ::ItemInstance* inst = (const EQ::ItemInstance*)int_struct->inst;
		if (!inst || !inst->GetItem()) {
			delete in;
			return;
		}

		SerializeBuffer buffer;
		SerializeItem(buffer, inst, PreviewItemSlot, 0, ItemPacketViewLink);
		buffer.WriteUInt8(0); // show_3d_preview: 0 -> FUN_1402BAA90 builds the actor preview

		auto outapp = new EQApplicationPacket(OP_ItemLinkClick, buffer.size());
		outapp->WriteData(buffer.buffer(), buffer.size());
		dest->FastQueuePacket(&outapp, ack_req);

		delete in;
	}

	ENCODE(OP_ItemLinkResponse) // 0x609c
	{
		// Reply to CItemDisplayWnd::SendItemLink, consumed by FUN_1401fb540 -> FUN_140410b20. Every
		// string here is length prefixed and not null terminated (FUN_1405646e0). The client only
		// enriches a display window that is already open, so this cannot open the window by itself.
		EQApplicationPacket* in = *p;
		*p = nullptr;
		uchar* __emu_buffer = in->pBuffer;

		EQ::InternalSerializedItem_Struct* int_struct = (EQ::InternalSerializedItem_Struct*)(&__emu_buffer[4]);
		const EQ::ItemInstance* inst = (const EQ::ItemInstance*)int_struct->inst;

		if (!inst) {
			delete in;
			return;
		}

		const EQ::ItemData* item = inst->GetItem();
		if (!item) {
			delete in;
			return;
		}

		// Field order as read by FUN_1401fb540, and where FUN_140410b20 lands each one:
		//   u32 item number   -> matched against CItemDisplayWnd::pItem (offset 0x3f8)
		//   u8  bCollected    -> window offset 0x488
		//   u8  bScribed      -> window offset 0x498
		//   IString           -> display name, window offset 0x490 (falls back to the definition when empty)
		//   IString           -> advanced lore text, window offset 0x3d8
		//   u8                -> window offset 0x48b
		//   u32               -> window offset 0x48c, only applied when non zero
		//   IString           -> "Made By:" text, window offset 0x3e0
		// EQEmu has no maker or advanced lore tables and no per character collected/scribed state, so
		// those stay empty and the client keeps whatever the item definition already gave it.
		SerializeBuffer buffer;
		buffer.WriteUInt32(item->ID);
		buffer.WriteUInt8(0);
		buffer.WriteUInt8(0);
		buffer.WriteLengthString(std::string(item->Name));
		buffer.WriteLengthString(std::string(""));
		buffer.WriteUInt8(0);
		buffer.WriteUInt32(0);
		buffer.WriteLengthString(std::string(""));

		auto outapp = new EQApplicationPacket(OP_ItemLinkResponse, buffer.size());
		outapp->WriteData(buffer.buffer(), buffer.size());
		dest->FastQueuePacket(&outapp, ack_req);

		delete in;
	}

	ENCODE(OP_ItemLuck) // 0x1d00
	{
		// Item instance luck roll, consumed by FUN_1401fb190. Variable length wire form:
		//   null terminated GUID string  (FUN_1401f0b50 keeps at most 16 bytes, then zero pads)
		//   u32 length + bytes           (FUN_1405646e0; this handler reads it and never uses it)
		//   u32                          (item number, also unused by this handler)
		//   u32 luck                     (gated by ItemDefinition::MinLuck .. MaxLuck, FUN_14060e5c0)
		// The GUID is resolved by FUN_1402cfce0, which walks the profile container array and compares
		// ItemBase + 0xc / + 0x14 / + 0x1c. On a match the client writes ItemBase::Luck (0x100) and then
		// calls CItemDisplayManager::UpdateItem(item, 2) and CItemFuseWnd::UpdateItem(item). Both only
		// repaint windows that already show this GUID, so this packet can refresh an open item stats
		// popup but cannot open one.
		EQApplicationPacket* in = *p;
		*p = nullptr;
		uchar* __emu_buffer = in->pBuffer;

		uint32_t item_number = *(uint32_t*)&__emu_buffer[0];
		uint32_t luck        = *(uint32_t*)&__emu_buffer[4];

		EQ::InternalSerializedItem_Struct* int_struct = (EQ::InternalSerializedItem_Struct*)(&__emu_buffer[8]);
		const EQ::ItemInstance* inst = (const EQ::ItemInstance*)int_struct->inst;

		if (!inst) {
			delete in;
			return;
		}

		const EQ::ItemData* item = inst->GetItem();
		if (!item) {
			delete in;
			return;
		}

		// SerializeItemDefinition writes MinLuck and MaxLuck as 0, so the client gate currently accepts
		// only 0. Anything else is dropped before the display window is touched.
		SerializeBuffer buffer;
		buffer.WriteString(fmt::format("{:016}", inst->GetSerialNumber()));
		buffer.WriteLengthString(std::string(""));
		buffer.WriteUInt32(item_number ? item_number : item->ID);
		buffer.WriteUInt32(luck);

		auto outapp = new EQApplicationPacket(OP_ItemLuck, buffer.size());
		outapp->WriteData(buffer.buffer(), buffer.size());
		dest->FastQueuePacket(&outapp, ack_req);

		delete in;
	}

	ENCODE(OP_LogServer) {
		SETUP_VAR_ENCODE(LogServer_Struct);
		ALLOC_LEN_ENCODE(1840);

		//pvp
		if (emu->enable_pvp) {
			*(char*)&__packet->pBuffer[0x04] = 1;
		}

		if (emu->enable_FV) {
			//FV sets these both to 1
			//one appears to enable the no drop flag the other just marks the server as special?
			*(char*)&__packet->pBuffer[0x08] = 1;
			*(char*)&__packet->pBuffer[0x0a] = 1;
		}

		//This has something to do with heirloom and prestige items but im not sure what it does
		//Seems to sit at 0
		*(char*)&__packet->pBuffer[0x71d] = 0;

		//not sure what this does, something to do with server select
		*(char*)&__packet->pBuffer[0x09] = 0;

		//this appears to have some effect on the tradeskill system; disabling made by tags perhaps?
		*(char*)&__packet->pBuffer[0x0b] = 0;

		//not sure, setting it to the value ive seen
		*(char*)&__packet->pBuffer[0x0c] = 1;

		//Something to do with languages
		*(char*)&__packet->pBuffer[0x0d] = 1;

		//These seem to affect if server has betabuff enabled
		*(char*)&__packet->pBuffer[0x5c0] = 0;
		*(char*)&__packet->pBuffer[0x5c1] = 0;
		//This is set on test so it's probably indicating this is a test server
		*(char*)&__packet->pBuffer[0x5c2] = 0;

		//not sure, but it's grouped with the beta and test stuff
		*(char*)&__packet->pBuffer[0x5c3] = 0;

		//world short name
		strncpy((char*)&__packet->pBuffer[0x15], emu->worldshortname, 32);

		//not sure, affects some player calculation but didn't care to look more
		*(char*)&__packet->pBuffer[0x5c2] = 0;

		//Looks right
		if (emu->enablemail) {
			*(char*)&__packet->pBuffer[0x5b5] = 1;
		}

		//Looks right
		if (emu->enablevoicemacros) {
			*(char*)&__packet->pBuffer[0x5b4] = 1;
		}

		//Not sure, sending what we've seen
		*(char*)&__packet->pBuffer[0x5b6] = 0;

		//Not sure sending what we've seen
		*(char*)&__packet->pBuffer[0x5b8] = 1;

		//Not sure sending what we've seen
		*(int32_t*)&__packet->pBuffer[0x5fc] = -1;

		//Test sets this to 1, everyone else seems to set it to 0
		*(int32_t*)&__packet->pBuffer[0x600] = 0;

		//Disassembly puts it next to code dealing with commands, ive not seen anyone send anything but 0
		*(char*)&__packet->pBuffer[0x705] = 0;

		//Something about item restrictions, seems to always be set to 1
		*(char*)&__packet->pBuffer[0x710] = 1;

		//This and 0x724 are often multiplied together in guild favor calcs, live and test send 1.0f
		*(float*)&__packet->pBuffer[0x720] = 1.0f;
		*(float*)&__packet->pBuffer[0x724] = 1.0f;

		//This and 0x72c are often multiplied together in non-guild favor calcs, live and test send 1.0f
		*(float*)&__packet->pBuffer[0x728] = 1.0f;
		*(float*)&__packet->pBuffer[0x72c] = 1.0f;

		FINISH_ENCODE();
	}

	ENCODE(OP_ManaChange) {
		ENCODE_LENGTH_EXACT(ManaChange_Struct);
		SETUP_DIRECT_ENCODE(ManaChange_Struct, structs::ManaChange_Struct);

		OUT(new_mana);
		OUT(stamina);
		OUT(spell_id);
		OUT(keepcasting);
		OUT(slot);

		FINISH_ENCODE();
	}

	ENCODE(OP_MemorizeSpell) {
		ENCODE_LENGTH_EXACT(MemorizeSpell_Struct);
		SETUP_DIRECT_ENCODE(MemorizeSpell_Struct, structs::MemorizeSpell_Struct);

		// EQEmu stores the semantic action (EQ::ScribeSpellActions). Laurion re-numbered them, so the
		// wire value is not the same number: Scribe 0, BookRefresh 1, Memorize 2, Unmemorize 3,
		// RecastTimer 4, FullRefresh -1. Getting this wrong makes the client apply a memorize as an
		// unmemorize, and it never acks the pending action counter that gates every later action.
		eq->action = LaurionSpellAction::ToWire(emu->scribing);

		// Only the recast-timer action carries a casting slot; the book actions carry a spellbook slot
		// and the bar actions carry a gem index, both of which are used as-is.
		if (emu->scribing == ScribeSpellActions::Spellbar)
			eq->slot = static_cast<uint32>(ServerToLaurionCastingSlot(static_cast<EQ::spells::CastingSlot>(emu->slot)));
		else
			OUT(slot);

		OUT(spell_id);
		OUT(reduction);

		FINISH_ENCODE();
	}

	ENCODE(OP_MobHealth) {
		ENCODE_LENGTH_EXACT(SpawnHPUpdate_Struct2);
		SETUP_DIRECT_ENCODE(SpawnHPUpdate_Struct2, structs::MobHealth_Struct);

		OUT(spawn_id);
		OUT(hp);

		FINISH_ENCODE();
	}

	ENCODE(OP_MoneyOnCorpse)
	{
		ENCODE_LENGTH_EXACT(moneyOnCorpseStruct);
		SETUP_DIRECT_ENCODE(moneyOnCorpseStruct, structs::moneyOnCorpseStruct);

		eq->type = emu->response;
		OUT(platinum);
		OUT(gold);
		OUT(silver);
		OUT(copper);
		eq->flags = 0;

		FINISH_ENCODE();
	}

	ENCODE(OP_MoveItem)
	{
		ENCODE_LENGTH_EXACT(MoveItem_Struct);
		SETUP_DIRECT_ENCODE(MoveItem_Struct, structs::MoveItem_Struct);

		LogNetcodeDetail("Laurion::ENCODE(OP_MoveItem)");

		eq->from_slot = ServerToLaurionSlot(emu->from_slot);
		eq->to_slot = ServerToLaurionSlot(emu->to_slot);
		OUT(number_in_stack);

		FINISH_ENCODE();
	}

	// Variable length: count + count * 36.  The wire entry layout is identical to EQEmu's
	// MultiMoveItemSub_Struct, so the packet is translated in place - only the slot numbering differs.
	ENCODE(OP_MoveMultipleItems)
	{
		ENCODE_LENGTH_ATLEAST(MultiMoveItem_Struct);

		MultiMoveItem_Struct* emu = (MultiMoveItem_Struct*)(*p)->pBuffer;
		uint32 count = emu->count;
		uint32 expected = sizeof(MultiMoveItem_Struct) + count * sizeof(MultiMoveItemSub_Struct);

		if ((*p)->size != expected) {
			LogNetcode("Laurion::ENCODE(OP_MoveMultipleItems): size [{}] does not match count [{}] (expected [{}])", (*p)->size, count, expected);
			delete *p;
			*p = nullptr;
			return;
		}

		for (uint32 i = 0; i < count; i++) {
			if (emu->moves[i].from_slot.Type == EQ::invtype::typePossessions) {
				emu->moves[i].from_slot.Slot = ServerGeneralSlotToLaurion(emu->moves[i].from_slot.Slot);
			}
			if (emu->moves[i].to_slot.Type == EQ::invtype::typePossessions) {
				emu->moves[i].to_slot.Slot = ServerGeneralSlotToLaurion(emu->moves[i].to_slot.Slot);
			}
		}

		dest->FastQueuePacket(p, ack_req);
	}

	ENCODE(OP_NewSpawn) { ENCODE_FORWARD(OP_ZoneSpawns); }

	ENCODE(OP_NewZone) {
		EQApplicationPacket* in = *p;
		*p = nullptr;

		unsigned char* __emu_buffer = in->pBuffer;
		NewZone_Struct* emu = (NewZone_Struct*)__emu_buffer;

		SerializeBuffer buffer;
		/*
		char Shortname[];
		char Longname[];
		char WeatherType[];
		char WeatherTypeOverride[];
		char SkyType[];
		char SkyTypeOverride[];
		*/
		buffer.WriteString(emu->zone_short_name);
		buffer.WriteString(emu->zone_long_name);
		buffer.WriteString("");
		buffer.WriteString("");
		buffer.WriteString("");
		buffer.WriteString("");

		/*
		s32 ZoneType;
		s16 ZoneId;
		s16 ZoneInstance;
		float ZoneExpModifier;
		s32 GroupLvlExpRelated;
		s32 FilterID;
		s32 Unknown1;
		*/
		buffer.WriteInt32(emu->ztype);
		buffer.WriteInt16(emu->zone_id);
		buffer.WriteInt16(emu->zone_instance);
		buffer.WriteFloat(emu->zone_exp_multiplier);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);

		//float FogDensity;
		buffer.WriteFloat(emu->fog_density);

		//WeatherState state[4];
		for (int i = 0; i < 4; ++i) {
			/*
			float FogStart;
			float FogEnd;
			u8 FogRed;
			u8 FogGreen;
			u8 FogBlue;
			u8 RainChance;
			u8 RainDuration;
			u8 SnowChance;
			u8 SnowDuration;
			*/

			buffer.WriteFloat(emu->fog_minclip[i]);
			buffer.WriteFloat(emu->fog_maxclip[i]);
			buffer.WriteUInt8(emu->fog_red[i]);
			buffer.WriteUInt8(emu->fog_green[i]);
			buffer.WriteUInt8(emu->fog_blue[i]);
			buffer.WriteUInt8(emu->rain_chance[i]);
			buffer.WriteUInt8(emu->rain_duration[i]);
			buffer.WriteUInt8(emu->snow_chance[i]);
			buffer.WriteUInt8(emu->snow_duration[i]);

		}

		/*
		u8 PrecipitationType;
		float BloomIntensity;
		float ZoneGravity;
		s32 LavaDamage;
		s32 MinLavaDamage;
		*/
		buffer.WriteUInt8(emu->sky);
		buffer.WriteFloat(1.0f);
		buffer.WriteFloat(emu->gravity);
		buffer.WriteInt32(emu->lava_damage);
		buffer.WriteInt32(emu->min_lava_damage);

		/*
		s32 TimeStringID;
		s32 Unknown3;
		s32 SkyLock;
		s32 SkyLockOverride;
		*/
		buffer.WriteInt32(0);
		buffer.WriteInt32(1);
		buffer.WriteInt32(0);
		buffer.WriteInt32(-1);

		/*
		float SafeY;
		float SafeX;
		float SafeZ;
		float SafeHeading;
		float Ceiling;
		float Floor;
		*/

		buffer.WriteFloat(emu->safe_y);
		buffer.WriteFloat(emu->safe_x);
		buffer.WriteFloat(emu->safe_z);
		buffer.WriteFloat(emu->safe_heading);
		buffer.WriteFloat(emu->max_z);
		buffer.WriteFloat(emu->underworld);

		/*
		float MinClip;
		float MaxClip;
		s32 FallThroughWorldTeleportID;
		*/
		buffer.WriteFloat(emu->minclip);
		buffer.WriteFloat(emu->maxclip);
		buffer.WriteInt32(emu->underworld_teleport_index);

		/*
		s32 Unknown4;
		s32 ScriptIDHour;
		s32 ScriptIDMinute;
		s32 ScriptIDTick;
		s32 ScriptIDOnPlayerDeath;
		s32 ScriptIDOnNPCDeath;
		s32 ScriptIDPlayerEnteringZone;
		s32 ScriptIDOnZonePop;
		s32 ScriptIDNPCLoot;
		s32 Unknown4b;
		s32 ScriptIDOnFishing;
		s32 ScriptIDOnForage;
		s32 Unknown4c;
		*/
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);

		/*
		s32 NPCAgroMaxDist;
		*/
		buffer.WriteInt32(600);

		/*
		s32 ForageLow;
		s32 ForageMedium;
		s32 ForageHigh;
		s32 ForageSpecial;
		s32 FishingLow;
		s32 FishingMedium;
		s32 FishingHigh;
		s32 FishingRelated;
		*/
		buffer.WriteInt32(-1);
		buffer.WriteInt32(-1);
		buffer.WriteInt32(-1);
		buffer.WriteInt32(-1);
		buffer.WriteInt32(-1);
		buffer.WriteInt32(-1);
		buffer.WriteInt32(-1);
		buffer.WriteInt32(-1);

		/*
		s32 CanPlaceCampsite;
		s32 CanPlaceGuildBanner;
		s32 Unknown4d;
		*/

		buffer.WriteInt32(2);
		buffer.WriteInt32(2);
		buffer.WriteInt32(0);

		/*
		s32 FastRegenHP;
		s32 FastRegenMana;
		s32 FastRegenEndurance;
		*/
		buffer.WriteInt32(emu->fast_regen_hp);
		buffer.WriteInt32(emu->fast_regen_mana);
		buffer.WriteInt32(emu->fast_regen_endurance);

		/*
		u8 NewEngineZone;
		u8 SkyEnabled;
		u8 FogOnOff;
		u8 ClimateType;
		u8 bNoPlayerLight;
		*/
		buffer.WriteUInt8(0); //not sure what happens if we set this incorrectly but we probably need to add this to the zone database
		buffer.WriteUInt8(1);
		buffer.WriteUInt8(1);
		buffer.WriteUInt8(0);
		buffer.WriteUInt8(0);

		/*
		u8 bUnknown5;
		u8 bNoAttack;
		u8 bAllowPVP;
		u8 bNoEncumber;
		u8 Unknown6;
		u8 Unknown7;
		*/
		buffer.WriteUInt8(1);
		buffer.WriteUInt8(0);
		buffer.WriteUInt8(1);
		buffer.WriteUInt8(0);
		buffer.WriteUInt8(0);
		buffer.WriteUInt8(0);

		/*
		u8 bNoLevitate;
		u8 bNoBuffExpiration;
		u8 bDisallowManaStone;
		u8 bNoBind;
		u8 bNoCallOfTheHero;
		u8 bUnknown8;
		u8 bNoFear;
		u8 bUnknown9;
		*/

		buffer.WriteUInt8(0);
		buffer.WriteUInt8(emu->suspend_buffs);
		buffer.WriteUInt8(1);
		buffer.WriteUInt8(0);
		buffer.WriteUInt8(0);
		buffer.WriteUInt8(0);
		buffer.WriteUInt8(0);
		buffer.WriteUInt8(0);

		auto outapp = new EQApplicationPacket(OP_NewZone, buffer.size());
		outapp->WriteData(buffer.buffer(), buffer.size());
		dest->FastQueuePacket(&outapp, ack_req);

		delete in;
	}

	ENCODE(OP_OnLevelMessage)
	{
		EQApplicationPacket* in = *p;
		*p = nullptr;
		OnLevelMessage_Struct* emu = (OnLevelMessage_Struct*)in->pBuffer;
		SerializeBuffer buffer;

		buffer.WriteLengthString(emu->Title);
		buffer.WriteLengthString(emu->Text);
		buffer.WriteLengthString(emu->ButtonName0);
		buffer.WriteLengthString(emu->ButtonName1);
		buffer.WriteUInt8(emu->Buttons);
		buffer.WriteUInt8(emu->SoundControls);
		buffer.WriteUInt32(emu->Duration);
		buffer.WriteUInt32(emu->PopupID);
		buffer.WriteUInt32(emu->NegativeID);
		buffer.WriteUInt32(0); //seen -1 & 0
		buffer.WriteUInt32(0); //seen 0

		auto outapp = new EQApplicationPacket(OP_OnLevelMessage, buffer.size());
		outapp->WriteData(buffer.buffer(), buffer.size());
		dest->FastQueuePacket(&outapp, ack_req);

		delete in;
	}

	ENCODE(OP_PlayerProfile) {
		EQApplicationPacket* in = *p;
		*p = nullptr;

		unsigned char* __emu_buffer = in->pBuffer;
		PlayerProfile_Struct* emu = (PlayerProfile_Struct*)__emu_buffer;

		SerializeBuffer out;

		/*
		u32 crc;
		u32 length;
		*/
		out.WriteUInt32(0);
		out.WriteUInt32(0);

		//PcProfile begin
		/*
		u32 profile_type;
		u32 profile_id;
		u32 shroud_template_id;
		*/
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);

		/*
		u8 gender;
		u32 race;
		u32 class;
		u8 level;
		u8 level1;
		*/
		out.WriteUInt8(emu->gender);
		out.WriteUInt32(emu->race);
		out.WriteUInt32(emu->class_);
		out.WriteUInt8(emu->level);
		out.WriteUInt8(emu->level);

		//u32 bind_count;
		out.WriteUInt32(5);

		for (int r = 0; r < 5; r++)
		{
			/*
			u32 zoneid;
			float x;
			float y;
			float z;
			float heading;
			*/
			out.WriteUInt32(emu->binds[r].zone_id);
			out.WriteFloat(emu->binds[r].x);
			out.WriteFloat(emu->binds[r].y);
			out.WriteFloat(emu->binds[r].z);
			out.WriteFloat(emu->binds[r].heading);
		}

		/*
		u32 deity;
		u32 intoxication;
		*/
		out.WriteUInt32(emu->deity);
		out.WriteUInt32(emu->intoxication);

		//u32 property_count;
		out.WriteUInt32(10); // properties count

		//u32 properties[property_count]; 
		for (int i = 0; i < 10; i++) {
			out.WriteUInt32(0);
		}

		//u32 armor_prop_count;
		out.WriteUInt32(22); //armor count
		for (int i = 0; i < 22; ++i) {
			/*
			s32 type;
			s32 variation;
			s32 material;
			s32 newArmorId;
			s32 newArmorType;
			*/
			out.WriteUInt32(0);
			out.WriteUInt32(0);
			out.WriteUInt32(0);
			out.WriteUInt32(0);
			out.WriteUInt32(0);
		}

		//u32 base_armor_prop_count;
		out.WriteUInt32(9); //base armor count
		for (int i = 0; i < 9; ++i) {
			/*
			s32 type;
			s32 variation;
			s32 material;
			s32 newArmorId;
			s32 newArmorType;
			*/
			out.WriteUInt32(0);
			out.WriteUInt32(0);
			out.WriteUInt32(0);
			out.WriteUInt32(0);
			out.WriteUInt32(0);
		}

		//u32 body_tint_count;
		out.WriteUInt32(9); //body_tint_count
		//u32 body_tints[body_tint_count];
		for (int i = 0; i < 9; ++i) {
			out.WriteUInt32(0);
		}

		//u32 equip_tint_count;
		out.WriteUInt32(9); //equip_tint_count
		//u32 equip_tints[equip_tint_count];
		for (int i = 0; i < 9; ++i) {
			out.WriteUInt32(0);
		}

		/*
		u8 hair_color;
		u8 facial_hair_color;
		u32 npc_tint_index;
		u8 eye_color1;
		u8 eye_color2;
		u8 hair_style;
		u8 facial_hair;
		u8 face;
		u8 old_face;
		u32 heritage;
		u32 tattoo;
		u32 details;
		*/
		out.WriteUInt8(emu->haircolor);
		out.WriteUInt8(emu->beardcolor);
		out.WriteUInt32(0); //npc tint index
		out.WriteUInt8(emu->eyecolor1);
		out.WriteUInt8(emu->eyecolor2);
		out.WriteUInt8(emu->hairstyle);
		out.WriteUInt8(emu->beard);
		out.WriteUInt8(emu->face);
		out.WriteUInt8(0); //old face
		out.WriteUInt32(emu->drakkin_heritage);
		out.WriteUInt32(emu->drakkin_tattoo);
		out.WriteUInt32(emu->drakkin_details);

		/*
		u8 texture_type;
		u8 material;
		u8 variation;
		*/
		out.WriteUInt8(0);
		out.WriteUInt8(0);
		out.WriteUInt8(0);

		/*
		float height;
		float width;
		float length;
		float view_height;
		*/
		out.WriteFloat(5.0f);
		out.WriteFloat(3.0f);
		out.WriteFloat(2.5f);
		out.WriteFloat(5.5f);

		/*
		u32 primary;
		u32 secondary;
		*/
		out.WriteUInt32(0);
		out.WriteUInt32(0);

		/*
		u32 practices;
		u32 base_mana;
		u32 base_hp;
		u32 base_str;
		u32 base_sta;
		u32 base_cha;
		u32 base_dex;
		u32 base_int;
		u32 base_agi;
		u32 base_wis;
		u32 base_heroic_str;
		u32 base_heroic_sta;
		u32 base_heroic_cha;
		u32 base_heroic_dex;
		u32 base_heroic_int;
		u32 base_heroic_agi;
		u32 base_heroic_wis;
		*/
		out.WriteUInt32(emu->points);
		out.WriteUInt32(emu->mana);
		out.WriteUInt32(emu->cur_hp);
		out.WriteUInt32(emu->STR);
		out.WriteUInt32(emu->STA);
		out.WriteUInt32(emu->CHA);
		out.WriteUInt32(emu->DEX);
		out.WriteUInt32(emu->INT);
		out.WriteUInt32(emu->AGI);
		out.WriteUInt32(emu->WIS);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);

		//u32 aa_count;
		out.WriteUInt32(300);
		for (int i = 0; i < 240; ++i) {
			/*
			s32 index;
			s32 points_spent;
			s32 charges_spent;
			u8 unknown1;
			*/
			out.WriteUInt32(emu->aa_array[i].AA);
			out.WriteUInt32(emu->aa_array[i].value);
			out.WriteUInt32(emu->aa_array[i].charges);
			out.WriteUInt8(0);
		}

		for (int i = 0; i < 60; ++i) {
			/*
			s32 index;
			s32 points_spent;
			s32 charges_spent;
			u8 unknown1; //not sure about this one
			*/
			out.WriteUInt32(0);
			out.WriteUInt32(0);
			out.WriteUInt32(0);
			out.WriteUInt8(0);
		}

		/*u32 skill_count;*/
		out.WriteUInt32(100);
		//s32 skills[skill_count];
		for (int i = 0; i < 100; ++i) {
			out.WriteUInt32(emu->skills[i]);
		}

		//u32 innate_skill_count;
		out.WriteUInt32(25);
		//s32 innate_skills[innate_skill_count];
		for (int i = 0; i < 25; ++i) {
			out.WriteUInt32(emu->InnateSkills[i]);
		}

		/*
		u32 combat_ability_count;
		*/
		out.WriteUInt32(300);
		//s32 combat_abilities[combat_ability_count];
		for (int i = 0; i < 100; ++i) {
			out.WriteUInt32(emu->disciplines.values[i]);
		}

		for (int i = 0; i < 200; ++i) {
			out.WriteUInt32(0);
		}

		//u32 combat_ability_timer_count;
		out.WriteUInt32(25);
		//s32 combat_ability_timers[combat_ability_timer_count];
		for (int i = 0; i < 20; ++i) {
			out.WriteUInt32(emu->disciplines.values[i]);
		}

		for (int i = 0; i < 5; ++i) {
			out.WriteUInt32(0);
		}

		//u32 unk_ability_count;
		out.WriteUInt32(0);

		//u32 linked_spell_timer_count;
		out.WriteUInt32(25);
		//s32 linked_spell_timers[linked_spell_timer_count];
		for (int i = 0; i < 25; ++i) {
			out.WriteUInt32(0);
		}

		//u32 item_recast_timer_count;
		out.WriteUInt32(100);
		//s32 item_recast_timers[item_recast_timer_count];
		for (int i = 0; i < 100; ++i) {
			out.WriteUInt32(0);
		}

		//u32 spell_book_slot_count;
		out.WriteUInt32(1120);

		//s32 spell_book_slots[spell_book_slot_count];
		for (int i = 0; i < 720; ++i) {
			out.WriteUInt32(emu->spell_book[i]);
		}

		for (int i = 0; i < 400; ++i) {
			out.WriteUInt32(0xFFFFFFFF);
		}

		//u32 spell_gem_count;
		out.WriteUInt32(18);
		//s32 spell_gems[spell_gem_count];
		for (int i = 0; i < 12; ++i) {
			out.WriteUInt32(emu->mem_spells[i]);
		}

		for (int i = 0; i < 6; ++i) {
			out.WriteUInt32(0xFFFFFFFF);
		}

		/*
		u32 spell_recast_timer_count;
		*/
		out.WriteUInt32(15);

		//s32 spell_recast_timers[spell_recast_timer_count];
		for (int i = 0; i < 12; ++i) {
			out.WriteUInt32(emu->spellSlotRefresh[i]);
		}

		for (int i = 0; i < 3; ++i) {
			out.WriteUInt32(0);
		}

		//u8 max_allowed_spell_slots;
		out.WriteUInt8(0);

		//u32 buff_count;
		out.WriteUInt32(42);

		//PackedEQAffect buffs[buff_count];
		//todo: fix
		for (int i = 0; i < 42; ++i) {
			/*
			struct EQAffect
			{
				float modifier;
				EqGuid caster;
				u32 duration;
				u32 max_duration;
				u8 level;
				s32 spell_id;
				s32 hitcount;
				u32 flags;
				u32 viral_timer;
				u8 type;
				SlotData slots[6];
			};

			*/
			out.WriteFloat(1.0f);
			out.WriteUInt32(0);
			out.WriteUInt32(0);
			out.WriteUInt32(0);
			out.WriteUInt32(0);
			out.WriteUInt8(0);
			out.WriteUInt32(0xFFFFFFFF);
			out.WriteUInt32(0);
			out.WriteUInt8(0);
			out.WriteUInt32(0);
			out.WriteUInt32(0);
			//SlotData slots[6];
			for (int j = 0; j < 6; ++j) {
				/*
				s32 slot_id;
				s64 value;
				*/
				out.WriteInt32(-1);
				out.WriteUInt64(0);
			}
		}

		//Coin coin;
		/*
		u32 platinum;
		u32 gold;
		u32 silver;
		u32 copper;
		*/
		out.WriteUInt32(emu->platinum);
		out.WriteUInt32(emu->gold);
		out.WriteUInt32(emu->silver);
		out.WriteUInt32(emu->copper);

		//Coin cursor_coin;
		/*
		u32 platinum;
		u32 gold;
		u32 silver;
		u32 copper;
		*/
		out.WriteUInt32(emu->platinum_cursor);
		out.WriteUInt32(emu->gold_cursor);
		out.WriteUInt32(emu->silver_cursor);
		out.WriteUInt32(emu->copper_cursor);

		/*
		u32 disc_timer;
		u32 mend_timer;
		u32 forage_timer;
		u32 thirst;
		u32 hunger;
		*/

		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(emu->thirst_level);
		out.WriteUInt32(emu->hunger_level);

		//u32 aa_spent;
		out.WriteUInt32(emu->aapoints_spent);

		//u32 aa_window_count;
		out.WriteUInt32(6);
		//u32 aa_window_stats[aa_window_count];
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);

		//u32 aa_points_unspent;
		out.WriteUInt32(emu->aapoints);

		/*
		u8 sneak;
		u8 hide;
		*/
		out.WriteUInt8(0);
		out.WriteUInt8(0);

		//u32 bandolier_count;
		out.WriteUInt32(20);

		//BandolierSet bandolier_sets[bandolier_count];
		for (int i = 0; i < 20; ++i) {
			//char name[];
			out.WriteString(emu->bandoliers[i].Name);

			//BandolierItemInfo items[4];
			for (int j = 0; j < 4; ++j) {
				//char name[];
				out.WriteString(emu->bandoliers[i].Items[j].Name);
				//s32 item_id;
				out.WriteUInt32(emu->bandoliers[i].Items[j].ID);
				//s32 icon;
				out.WriteUInt32(emu->bandoliers[i].Items[j].Icon);
			}
		}

		//u32 invslot_bitmask;
		out.WriteUInt32(0xFFFFFFFF);

		/*
		u32 basedata_hp;
		u32 basedata_mana;
		u32 basedata_endur;
		u32 basedata_mr;
		u32 basedata_fr;
		u32 basedata_cr;
		u32 basedata_pr;
		u32 basedata_dr;
		u32 basedata_corrupt;
		u32 basedata_phr;
		*/

		out.WriteUInt32(5);
		out.WriteUInt32(5);
		out.WriteUInt32(5);
		out.WriteUInt32(25);
		out.WriteUInt32(25);
		out.WriteUInt32(25);
		out.WriteUInt32(15);
		out.WriteUInt32(15);
		out.WriteUInt32(15);
		out.WriteUInt32(15);

		/*
		float basedata_walkspeed;
		float basedata_runspeed;
		*/

		out.WriteFloat(0.46f);
		out.WriteFloat(0.7f);

		/*
		u32 basedata_hpregen;
		u32 basedata_manaregen;
		u32 basedata_mountmanaregen;
		u32 basedata_endurregen;
		u32 basedata_ac;
		u32 basedata_atk;
		u32 basedata_dmg;
		u32 basedata_delay;
		u32 endurance;
		u32 heroic_type;
		*/

		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(emu->endurance);
		out.WriteUInt32(0);

		//ItemIndex keyring_item_index[5];
		for (int i = 0; i < 5; ++i) {
			/*
			s16 slot1;
			s16 slot2;
			s16 slot3;
			*/

			out.WriteInt16(-1);
			out.WriteInt16(-1);
			out.WriteInt16(-1);
		}

		/*
		u64 exp;
		u32 aa_exp;
		*/
		out.WriteUInt64(emu->exp);
		out.WriteUInt32(emu->expAA);

		//PcClient begin
		/*
		u32 character_id;
		u16 world_id;
		u16 reserved;
		*/
		out.WriteUInt32(emu->char_id);
		out.WriteUInt16(RuleI(World, Id));
		out.WriteUInt16(0);

		/*u32 name_length;*/
		/*char name[name_length];*/
		out.WriteLengthString(64, emu->name);

		//u32 last_name_length;
		//char last_name[last_name_length];
		out.WriteLengthString(32, emu->last_name);

		/*
		u32 creation_time;
		u32 account_creation_time;
		u32 last_played_time;
		u32 played_minutes;
		u32 entitled_days;
		u32 expansion_flags;
		*/

		out.WriteUInt32(emu->birthday);
		out.WriteUInt32(emu->birthday);
		out.WriteUInt32(emu->lastlogin);
		out.WriteUInt32(5000);
		out.WriteUInt32(6000);
		out.WriteUInt32(0x3FFFFFFF);

		//u32 language_count;
		out.WriteUInt32(32);
		for (int i = 0; i < 28; i++)
		{
			//u8 languages[language_count];
			out.WriteUInt8(emu->languages[i]);
		}

		for (int i = 0; i < 4; i++)
		{
			out.WriteUInt8(0);
		}

		/*
		u32 current_zone;
		float current_x;
		float current_y;
		float current_z;
		float current_heading;
		*/
		out.WriteUInt16(emu->zone_id);
		out.WriteUInt16(emu->zoneInstance);
		out.WriteFloat(emu->x);
		out.WriteFloat(emu->y);
		out.WriteFloat(emu->z);
		out.WriteFloat(emu->heading);

		/*
		u8 animation;
		u8 pvp;
		u8 anon;
		u8 gm;
		*/
		out.WriteUInt8(100);
		out.WriteUInt8(emu->pvp);
		out.WriteUInt8(emu->anon);
		out.WriteUInt8(emu->gm);

		/*
		u64 guild_id;
		u8 guild_show_sprite;
		u8 status;

		The client stores this u64 as PcClient::GuildID (pinstLocalPC + 0x1730). Its empty EqGuid is
		0, not 0xFFFFFFFF, and FUN_1401fa510 (the OP_GuildInvite handler) only shows the invite popup
		when that field is 0 or equal to the guild in the invite record. Sending EQEmu's GUILD_NONE
		here leaves the client holding 0xFFFFFFFF, so an unguilded player can never see an invite and
		auto-declines with response 9. Write 0 for the no-guild case.
		*/

		out.WriteInt32(emu->guild_id == 0xFFFFFFFF ? 0 : emu->guild_id);
		out.WriteUInt32(0);
		out.WriteUInt8(0);
		out.WriteUInt8(5);

		//Coin coin;
		out.WriteUInt32(emu->platinum);
		out.WriteUInt32(emu->gold);
		out.WriteUInt32(emu->silver);
		out.WriteUInt32(emu->copper);

		//Coin bank;
		out.WriteUInt32(emu->platinum_bank);
		out.WriteUInt32(emu->gold_bank);
		out.WriteUInt32(emu->silver_bank);
		out.WriteUInt32(emu->copper_bank);

		//u32 bank_shared_plat;
		out.WriteUInt32(emu->platinum_shared);

		//u32 claim_count;
		out.WriteUInt32(0);
		//Claim claims[claim_count];

		//Tribute tribute;
		/*
		u32 BenefitTimer;
		s32 unknown1;
		s32 current_favor;
		s32 unknown2;
		s32 all_time_favor;
		s32 unknown3; //some of these are probably the bools on the pcclient;
		u16 unknown4;
		*/
		out.WriteUInt32(600000);
		out.WriteInt32(-1);
		out.WriteUInt32(emu->tribute_points);
		out.WriteUInt32(0);
		out.WriteUInt32(emu->career_tribute_points);
		out.WriteUInt32(0);
		out.WriteUInt16(0);

		//u32 tribute_benefit_count
		out.WriteUInt32(5);

		//TributeBenefit tribute_benefits[tribute_benefit_count];
		for (int i = 0; i < 5; ++i) {
			/*
			s32 benefit_id;
			s32 benefit_tier;
			*/

			out.WriteUInt32(emu->tributes[i].tribute);
			out.WriteUInt32(emu->tributes[i].tier);
		}

		//u32 trophy_tribute_benefit_count;
		out.WriteUInt32(10);

		//TributeBenefit trophy_tribute_benefit[trophy_tribute_benefit_count];
		for (int i = 0; i < 10; ++i) {
			/*
			s32 benefit_id;
			s32 benefit_tier;
			*/

			out.WriteUInt32(0xFFFFFFFF);
			out.WriteUInt32(0);
		}
		const uint8_t task_data[137] = {
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00,
		};

		//u8 tasks[137]; //on live and on xac's capture from 12/28/23 these both are the same size;
		for (int i = 0; i < 137; i++)
		{
			out.WriteUInt8(task_data[i]);
		}

		/*
		u32 good_points_available;
		u32 good_points_earned;
		u32 bad_points_available;
		u32 bad_points_earned;
		*/

		out.WriteUInt32(emu->currentRadCrystals);
		out.WriteUInt32(emu->careerRadCrystals);
		out.WriteUInt32(emu->currentEbonCrystals);
		out.WriteUInt32(emu->careerEbonCrystals);

		/*
		u32 momentum_balance;
		u32 loyalty_reward_balance;
		u32 parcel_status;
		*/

		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);

		/*
		u32 vehicle_name_length;
		char vehicle_name[vehicle_name_length];
		*/

		out.WriteUInt32(64);
		for (int i = 0; i < 64; ++i) {
			out.WriteUInt8(0);
		}

		/*
		u8 super_pkill;
		u8 unclone;
		u8 dead;
		*/

		out.WriteUInt8(0);
		out.WriteUInt8(0);
		out.WriteUInt8(0);

		/*
		u32 ld_timer;
		u32 spell_interrupt_count;
		u8 autosplit;
		u8 tells_off;
		*/

		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt8(emu->autosplit);
		out.WriteUInt8(0);

		/*
		u8 gm_invis;
		u32 kill_me;
		u8 cheater_ld_flag;
		u8 norent;
		u8 corpse;
		u8 client_gm_flag_set;
		u32 mentor_pct;
		*/

		out.WriteUInt8(0);
		out.WriteUInt32(0);
		out.WriteUInt8(0);
		out.WriteUInt8(0);
		out.WriteUInt8(0);
		out.WriteUInt8(0);
		out.WriteUInt32(0);

		//RaidData raid;
		/*
		u32 main_assist1;
		u32 main_assist2;
		u32 main_assist3;
		char main_assist_name1[];
		char main_assist_name2[];
		char main_assist_name3[];
		u32 main_marker1;
		u32 main_marker2;
		u32 main_marker3;
		u32 master_looter;
		*/

		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt8(0);
		out.WriteUInt8(0);
		out.WriteUInt8(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);

		//u32 unique_player_id;
		out.WriteUInt32(emu->char_id);

		//LdonData ldon_data;
		/*
		u32 count;
		u32 ldon_categories[count];
		u32 ldon_points_available;
		*/

		out.WriteUInt32(6);
		out.WriteUInt32(0);
		out.WriteUInt32(emu->ldon_points_guk);
		out.WriteUInt32(emu->ldon_points_mir);
		out.WriteUInt32(emu->ldon_points_mmc);
		out.WriteUInt32(emu->ldon_points_ruj);
		out.WriteUInt32(emu->ldon_points_tak);
		out.WriteUInt32(emu->ldon_points_available);

		//u32 air_supply;
		out.WriteUInt32(emu->air_remaining);

		//PvPData pvp_data;
		/*
		u32 kills;
		u32 deaths;
		u32 current_points;
		u32 career_points;
		u32 best_kill_streak;
		u32 worst_death_streak;
		u32 current_kill_streak;
		*/

		out.WriteUInt32(emu->PVPKills);
		out.WriteUInt32(emu->PVPDeaths);
		out.WriteUInt32(emu->PVPCurrentPoints);
		out.WriteUInt32(emu->PVPCareerPoints);
		out.WriteUInt32(emu->PVPBestKillStreak);
		out.WriteUInt32(emu->PVPWorstDeathStreak);
		out.WriteUInt32(emu->PVPCurrentKillStreak);

		//PvPKill last_kill;
		/*
		char name[];
		u32 level;
		u32 unknown1; //not sure
		u32 unknown2; //not sure
		u32 race;
		u32 class;
		u32 zone;
		u32 time;
		u32 points;
		*/
		out.WriteString(emu->PVPLastKill.Name);
		out.WriteUInt32(emu->PVPLastKill.Level);
		out.WriteUInt32(0);
		out.WriteUInt32(0);
		out.WriteUInt32(emu->PVPLastKill.Race);
		out.WriteUInt32(emu->PVPLastKill.Class);
		out.WriteUInt32(emu->PVPLastKill.Zone);
		out.WriteUInt32(emu->PVPLastKill.Time);
		out.WriteUInt32(emu->PVPLastKill.Points);

		//PvPDeath last_death;
		/*
		char name[];
		u32 level;
		u32 race;
		u32 class;
		u32 zone;
		u32 time;
		u32 points;
		*/

		out.WriteString(emu->PVPLastDeath.Name);
		out.WriteUInt32(emu->PVPLastDeath.Level);
		out.WriteUInt32(emu->PVPLastDeath.Race);
		out.WriteUInt32(emu->PVPLastDeath.Class);
		out.WriteUInt32(emu->PVPLastDeath.Zone);
		out.WriteUInt32(emu->PVPLastDeath.Time);
		out.WriteUInt32(emu->PVPLastDeath.Points);

		/*
		u32 kills_in_past_24_hours;
		*/

		out.WriteUInt32(emu->PVPNumberOfKillsInLast24Hours);

		//u32 kill_list_count;
		out.WriteUInt32(50);

		//PvPKill kill_list[kill_list_count];
		for (int i = 0; i < 50; ++i) {
			/*
			char name[];
			u32 level;
			u32 unknown1; //not sure
			u32 unknown2; //not sure
			u32 race;
			u32 class;
			u32 zone;
			u32 time;
			u32 points;
			*/
			out.WriteString(emu->PVPRecentKills[i].Name);
			out.WriteUInt32(emu->PVPRecentKills[i].Level);
			out.WriteUInt32(0);
			out.WriteUInt32(0);
			out.WriteUInt32(emu->PVPRecentKills[i].Race);
			out.WriteUInt32(emu->PVPRecentKills[i].Class);
			out.WriteUInt32(emu->PVPRecentKills[i].Zone);
			out.WriteUInt32(emu->PVPRecentKills[i].Time);
			out.WriteUInt32(emu->PVPRecentKills[i].Points);
		}

		/*
		u32 pvp_infamy_level;
		u32 pvp_vitality;
		*/
		out.WriteUInt32(0);
		out.WriteUInt32(0);

		/*
		u32 cursor_krono;
		u32 krono;
		*/
		out.WriteUInt32(0);
		out.WriteUInt32(0);

		/*
		u8 autoconsent_group;
		u8 autoconsent_raid;
		u8 autoconsent_guild;
		u8 autoconsent_fellowship;
		*/

		out.WriteUInt8(emu->groupAutoconsent);
		out.WriteUInt8(emu->raidAutoconsent);
		out.WriteUInt8(emu->guildAutoconsent);
		out.WriteUInt8(1);

		/*
		u8 private_for_eq_players;
		u32 main_level;
		u8 show_helm;
		u32 downtime;
		*/
		out.WriteUInt8(1);
		out.WriteUInt32(emu->level);
		out.WriteUInt8(emu->showhelm);
		out.WriteUInt32(emu->RestTimer);

		//AltCurrency alt_currency;
		/*
		u32 alt_currency_str_length;
		u32 unknown1;
		char alt_currency_string[alt_currency_str_length];
		*/
		out.WriteUInt32(1);
		out.WriteUInt32(0);
		out.WriteUInt8(0x31);

		//u32 completed_event_subcomponent_count;
		out.WriteUInt32(0);
		//AchivementSubComponentData completed_event_subcomponents[completed_event_subcomponent_count];

		//u32 inprogress_event_subcomponent_count;
		out.WriteUInt32(0);
		//AchivementSubComponentData inprogress_event_subcomponents[inprogress_event_subcomponent_count];

		/*
		u64 merc_aa_exp;
		u32 merc_aa_points;
		u32 merc_aa_spent;
		*/
		out.WriteUInt64(0);
		out.WriteUInt32(0);
		out.WriteUInt32(0);

		//u32 starting_city_zone_id;
		//we don't actually support this yet
		out.WriteUInt32(394);

		/*
		u8 use_advanced_looting;
		u8 is_master_loot_candidate;
		*/

		// These are the only bytes the client reads for the advanced looting master switch on
		// login: FUN_1402c58b0 deserializes them into profile +0x7c98 / +0x7c99 and FUN_1402c01a0
		// copies them to PcClient +0x2434 / +0x2435. The zone fills them from character_loot_settings.
		out.WriteUInt8(emu->use_advanced_looting);
		out.WriteUInt8(emu->master_loot_candidate);

		//alchemy_bonus_list_count
		out.WriteUInt32(0);
		//AlchemyBonusSkillData alchemy_bonus_list[alchemy_bonus_list_count];

		//u32 persona_count;
		out.WriteUInt32(0);
		//PersonaEquipmentSet persona_equipment_set[persona_count];

		//u8 term;
		out.WriteUInt8(0);

		auto outapp = new EQApplicationPacket(OP_PlayerProfile, out.length());
		outapp->WriteData(out.buffer(), out.length());
		outapp->SetWritePosition(4);
		outapp->WriteUInt32(outapp->size - 9);
		CRC32::SetEQChecksum(outapp->pBuffer, outapp->size - 1, 8);
		dest->FastQueuePacket(&outapp, ack_req);
		delete in;
	}

	ENCODE(OP_RecipeAutoCombine)
	{
		ENCODE_LENGTH_EXACT(RecipeAutoCombine_Struct);
		SETUP_DIRECT_ENCODE(RecipeAutoCombine_Struct, structs::RecipeAutoCombine_Struct);

		OUT(object_type);
		OUT(some_id);
		eq->container_slot = ServerToLaurionSlot(emu->unknown1);
		structs::InventorySlot_Struct LaurionSlot;
		LaurionSlot.Type = 8;	// Observed
		LaurionSlot.Padding1 = 0;
		LaurionSlot.Slot = 0xffff;
		LaurionSlot.SubIndex = 0xffff;
		LaurionSlot.AugIndex = 0xffff;
		LaurionSlot.Padding2 = 0;
		eq->unknown_slot = LaurionSlot;
		OUT(recipe_id);
		OUT(reply_code);

		FINISH_ENCODE();
	}

	ENCODE(OP_RemoveBlockedBuffs) { ENCODE_FORWARD(OP_BlockedBuffs); }

	ENCODE(OP_RespondAA)
	{
		SETUP_DIRECT_ENCODE(AATable_Struct, structs::AATable_Struct);

		eq->aa_spent = emu->aa_spent;
		// These fields may need to be correctly populated at some point
		eq->aapoints_assigned[0] = emu->aa_spent;
		eq->aapoints_assigned[1] = 0;
		eq->aapoints_assigned[2] = 0;
		eq->aapoints_assigned[3] = 0;
		eq->aapoints_assigned[4] = 0;
		eq->aapoints_assigned[5] = 0;

		for (uint32 i = 0; i < MAX_PP_AA_ARRAY; ++i)
		{
			eq->aa_list[i].AA = emu->aa_list[i].AA;
			eq->aa_list[i].value = emu->aa_list[i].value;
			eq->aa_list[i].charges = emu->aa_list[i].charges;
			eq->aa_list[i].bUnknown0x0c = false;
		}

		FINISH_ENCODE();
	}

	ENCODE(OP_RequestClientZoneChange)
	{
		ENCODE_LENGTH_EXACT(RequestClientZoneChange_Struct);
		SETUP_DIRECT_ENCODE(RequestClientZoneChange_Struct, structs::RequestClientZoneChange_Struct);

		OUT(zone_id);
		OUT(instance_id);
		OUT(y);
		OUT(x);
		OUT(z);
		OUT(heading);
		eq->type = 0x0b;
		eq->unknown004 = 0xffffffff;
		eq->unknown172 = 0x0168b500;

		FINISH_ENCODE();
	}

	ENCODE(OP_ReadBook)
	{
		ENCODE_LENGTH_ATLEAST(BookText_Struct);
		SETUP_VAR_ENCODE(BookText_Struct);

		// The client parses the reply with the same reader it used for the request (FUN_1403731e0 /
		// FUN_14046b990): window, book type, target, ItemGlobalIndex, then the two button flags at
		// +19/+20 and the text at +21. EQEmu's canonical struct has single-byte window/type and a
		// trailing variable text, so the wire has to be rebuilt field by field.
		uint32 text_len = __packet->size - (sizeof(BookText_Struct) - 1);
		if (text_len > structs::BOOK_TEXT_SIZE) {
			text_len = structs::BOOK_TEXT_SIZE;
		}

		ALLOC_VAR_ENCODE(structs::BookRequest_Struct, 21 + text_len);

		eq->window = emu->window;
		eq->book_type = emu->type;
		eq->target_id = emu->target_id;

		// Rebuild the split ItemGlobalIndex from the canonical slot so the client can match the reply
		// to the book window it opened.
		structs::TypelessInventorySlot_Struct laurion_slot = ServerToLaurionTypelessSlot(emu->invslot, EQ::invtype::typePossessions);
		eq->top_slot = laurion_slot.Slot;
		eq->sub_slot = laurion_slot.SubIndex;
		eq->aug_index = static_cast<uint16>(laurion_slot.AugIndex);

		eq->unknown018 = 0;
		eq->can_scribe = emu->can_scribe;
		eq->can_cast = emu->can_cast;
		memcpy(eq->text, emu->booktext, text_len);

		FINISH_ENCODE();
	}

	ENCODE(OP_SendAATable)
	{
		EQApplicationPacket* in = *p;
		*p = nullptr;
		AARankInfo_Struct* emu = (AARankInfo_Struct*)in->pBuffer;

		std::vector<int32> skill;
		std::vector<int32> points;
		in->SetReadPosition(sizeof(AARankInfo_Struct) + emu->total_effects * sizeof(AARankEffect_Struct));
		for (auto i = 0; i < emu->total_prereqs; ++i) {
			skill.push_back(in->ReadUInt32());
			points.push_back(in->ReadUInt32());
		}

		SerializeBuffer buffer;

		/*
		s32 AbilityId;
		u8 ShowInAbilityWindow;
		s32 ShortName;
		s32 ShortName2;
		s32 Name;
		s32 Desc;
		*/

		buffer.WriteUInt32(emu->id);
		buffer.WriteUInt8(1);
		buffer.WriteInt32(emu->upper_hotkey_sid);
		buffer.WriteInt32(emu->lower_hotkey_sid);
		buffer.WriteInt32(emu->title_sid);
		buffer.WriteInt32(emu->desc_sid);

		/*
		s32 MinLevel;
		s32 Cost;
		s32 GroupID;
		s32 CurrentRank;
		*/
		buffer.WriteInt32(emu->level_req);
		buffer.WriteInt32(emu->cost);
		buffer.WriteUInt32(emu->seq);
		buffer.WriteUInt32(emu->current_level);

		/*
		u32 PrereqSkillCount;
		s32 PrereqSkills[PrereqSkillCount];
		u32 PrereqLevelCount;
		s32 PrereqLevels[PrereqLevelCount];
		*/

		if (emu->total_prereqs) {
			buffer.WriteUInt32(emu->total_prereqs);
			for (auto& e : skill)
				buffer.WriteInt32(e);
			buffer.WriteUInt32(emu->total_prereqs);
			for (auto& e : points)
				buffer.WriteInt32(e);
		}
		else {
			buffer.WriteUInt32(1);
			buffer.WriteUInt32(0);
			buffer.WriteUInt32(1);
			buffer.WriteUInt32(0);
		}

		/*
		u32 Type;
		s32 SpellId;
		*/
		buffer.WriteInt32(emu->type);
		buffer.WriteInt32(emu->spell);

		/*
		u32 TimerIdCount;
		s32 TimerIds[TimerIdCount];
		s32 ReuseTimer;
		u32 Classes;
		*/
		buffer.WriteInt32(1);
		buffer.WriteInt32(emu->spell_type);
		buffer.WriteInt32(emu->spell_refresh);
		buffer.WriteInt32(emu->classes);

		/*
		s32 MaxRank;
		s32 PrevAbilityId;
		s32 NextAbilityId;
		s32 TotalPoints;
		*/

		buffer.WriteInt32(emu->max_level);
		buffer.WriteInt32(emu->prev_id);
		buffer.WriteInt32(emu->next_id);
		buffer.WriteInt32(emu->total_cost);

		/*
		u8 bRefund;
		s32 QuestOnly;
		u8 bIgnoreDeLevel;
		*/
		buffer.WriteUInt8(0);
		buffer.WriteUInt32(emu->grant_only);
		buffer.WriteUInt8(0);

		/*
		s32 Charges;
		s32 Expansion;
		s32 Category;
		*/
		buffer.WriteUInt32(emu->charges);
		buffer.WriteInt32(emu->expansion);
		buffer.WriteInt32(emu->category);

		/*
		u8 bShroud;
		u8 bBetaOnly;
		u8 bResetOnDeath;
		u8 AutoGrant;
		*/
		buffer.WriteUInt8(0);
		buffer.WriteUInt8(0);
		buffer.WriteUInt8(0);
		buffer.WriteUInt8(0);

		/*
		s32 AutoGrantExpansion;
		s32 Unknown098;
		u8 Unknown09C;
		*/

		buffer.WriteInt32(emu->expansion);
		buffer.WriteInt32(0);
		buffer.WriteUInt8(0);

		//u32 TotalEffects;
		buffer.WriteUInt32(emu->total_effects);
		in->SetReadPosition(sizeof(AARankInfo_Struct));
		for (auto i = 0; i < emu->total_effects; ++i) {
			auto skill_id = in->ReadUInt32();
			auto base1 = in->ReadUInt32();
			auto base2 = in->ReadUInt32();
			auto slot = in->ReadUInt32();

			/*
			u32 effect_id;
			s64 base1;
			s64 base2;
			u32 slot;
			*/

			buffer.WriteUInt32(skill_id);
			buffer.WriteInt64(base1);
			buffer.WriteInt64(base2);
			buffer.WriteUInt32(slot);
		}

		auto outapp = new EQApplicationPacket(OP_SendAATable, buffer.size());
		outapp->WriteData(buffer.buffer(), buffer.size());
		dest->FastQueuePacket(&outapp, ack_req);

		delete in;
	}

	ENCODE(OP_SendCharInfo) {
		ENCODE_LENGTH_ATLEAST(CharacterSelect_Struct);
		SETUP_VAR_ENCODE(CharacterSelect_Struct);

		// Wire layout verified against the client parser (FUN_1401f0d90) and the 0x832
		// handler (FUN_140200f40):
		//   [uint32 count][N x variable-length entry][uint8 trailing flag -> DAT_140e3bee4]
		// Entry = null-terminated name + 23 byte head + 9 x 24 byte armor/tint + 52 byte tail.
		// The client stores entries at a 0x170 stride - that is storage, not the wire size.
		uint32 character_count = emu->CharCount;
		if (character_count > EQ::constants::CHARACTER_CREATION_LIMIT) {
			character_count = EQ::constants::CHARACTER_CREATION_LIMIT;
		}

		const size_t head_size  = 23;
		const size_t armor_size = 216;
		const size_t tail_size  = 52;

		CharacterSelectEntry_Struct* emu_cse = (CharacterSelectEntry_Struct*)nullptr;
		unsigned char* emu_ptr = __emu_buffer + sizeof(CharacterSelect_Struct);

		size_t names_length = 0;
		for (uint32 i = 0; i < character_count; ++i) {
			emu_cse = (CharacterSelectEntry_Struct*)emu_ptr;
			names_length += strlen(emu_cse->Name) + 1;	// the null terminator is on the wire
			emu_ptr += sizeof(CharacterSelectEntry_Struct);
		}

		size_t total_length = 4 + names_length
			+ character_count * (head_size + armor_size + tail_size)
			+ 1;	// trailing flag

		ALLOC_VAR_ENCODE(structs::CharacterSelect_Struct, total_length);

		unsigned char* eq_ptr = __packet->pBuffer + 4;
		auto put_u32 = [&](uint32 v) { memcpy(eq_ptr, &v, 4); eq_ptr += 4; };
		auto put_u16 = [&](uint16 v) { memcpy(eq_ptr, &v, 2); eq_ptr += 2; };
		auto put_u8  = [&](uint8 v)  { *eq_ptr++ = v; };

		eq->CharCount = character_count;

		emu_ptr = __emu_buffer + sizeof(CharacterSelect_Struct);
		for (uint32 counter = 0; counter < character_count; ++counter) {
			emu_cse = (CharacterSelectEntry_Struct*)emu_ptr;

			// name: null-terminated, variable length
			size_t name_len = strlen(emu_cse->Name);
			memcpy(eq_ptr, emu_cse->Name, name_len + 1);
			eq_ptr += name_len + 1;

			// head, wire order
			put_u32(emu_cse->Class);				// memory +0x40
			put_u32(emu_cse->Race);				// memory +0x44
			put_u8(emu_cse->Level);				// memory +0x48
			put_u32(emu_cse->ShroudClass);			// memory +0x4C
			put_u32(emu_cse->ShroudRace);			// memory +0x50
			put_u32(emu_cse->Zone);				// memory +0x54
			put_u8(emu_cse->Gender);				// memory +0x55
			put_u8(emu_cse->Face);				// memory +0x56

			// armor block: the client writes wire dwords [0,2,1,3,4] into ArmorProperties
			// and the 6th dword into Tint[9] - the two arrays are separate in memory, so the
			// wire order is permuted relative to EQ::textures::Texture_Struct.
			for (int slot = 0; slot < EQ::textures::materialCount; ++slot) {
				const EQ::textures::Texture_Struct& tex = emu_cse->Equip[slot];
				put_u32(tex.Material);				// ArmorProperties dword 0
				put_u32(tex.EliteModel);			// dword 2
				put_u32(tex.Unknown1);				// dword 1
				put_u32(tex.HerosForgeModel);			// dword 3
				put_u32(tex.Unknown2);				// dword 4
				put_u32(emu_cse->Equip[slot].Color);		// Tint[9]
			}

			// tail, wire order (memory offsets 0x135/0x136/0x161 are not read from the wire)
			put_u8(emu_cse->Unknown15);				// +0x134 TextureType
			put_u8(emu_cse->Unknown19);				// +0x137 HeadType
			put_u32(emu_cse->DrakkinTattoo);			// +0x138
			put_u32(emu_cse->DrakkinDetails);			// +0x13C
			put_u32(emu_cse->Deity);				// +0x140
			put_u32(emu_cse->PrimaryIDFile);			// +0x144
			put_u32(emu_cse->SecondaryIDFile);			// +0x148
			put_u8(emu_cse->HairColor);				// +0x14C
			put_u8(emu_cse->BeardColor);			// +0x14D
			put_u8(emu_cse->EyeColor1);				// +0x14E
			put_u8(emu_cse->EyeColor2);				// +0x14F
			put_u8(emu_cse->HairStyle);				// +0x150
			put_u8(emu_cse->Beard);				// +0x151
			put_u8(emu_cse->GoHome);				// +0x152
			put_u8(emu_cse->Tutorial);				// +0x153
			put_u32(emu_cse->DrakkinHeritage);			// +0x154
			put_u8(emu_cse->Unknown1);				// +0x158
			put_u8(emu_cse->Enabled);				// +0x159
			put_u32(emu_cse->LastLogin);			// +0x15C
			put_u8(emu_cse->Unknown2);				// +0x160
			put_u16(0);					// +0x162 bShrouded
			put_u8(0);					// +0x164
			for (int i = 0; i < 8; ++i) { put_u8(0); }		// +0x168 final block

			emu_ptr += sizeof(CharacterSelectEntry_Struct);
		}

		put_u8(0);						// trailing flag -> DAT_140e3bee4

		DumpPacket(__packet);

		FINISH_ENCODE();
	}

	ENCODE(OP_SendMaxCharacters) {
		ENCODE_LENGTH_EXACT(MaxCharacters_Struct);
		SETUP_DIRECT_ENCODE(MaxCharacters_Struct, structs::MaxCharacters_Struct);

		//OUT(max_chars);
		eq->max_chars = 8; //needs to be fixed
		eq->marketplace_chars = 0;
		eq->unknown008 = -1;
		eq->unknown00c = 196608;
		eq->unknown010 = 0;
		eq->unknown014 = 0;
		eq->unknown018 = 0;
		eq->unknown01c = 0;
		eq->unknown020 = -1;
		eq->unknown024 = 0;
		eq->unknown028 = 0;
		eq->unknown02c = 0;
		eq->unknown030 = 0;
		eq->unknown034 = 0;

		FINISH_ENCODE();
	}

	/*
	** OP_SendMembership (0x2aca) entries[] is the client's feature table: FreeToPlayClient::LimitData[33]
	** at +0x0C.  PCClient::GetGameFeature(i) reads it directly (vtable thunk FUN_1402c2420 ->
	** FUN_1401d4d30 = *(int *)(FreeToPlayClient__Instance() + 0xc + i * 4)), so whatever is sent here is
	** what every feature check in the client sees.
	**
	** The index -> meaning column below is the client's own name table, RestrictionInfo at 0x140aa24d0:
	** 33 records of {const char *name; bool plural; bool boolean;}, the all-zero record at 0x140aa24c0
	** being the Invalid/-1 slot.  A record whose boolean member is true is a yes/no permission and is
	** sent as 0 or 1; the rest are counts, and -1 means "no limit".  Full evidence:
	** docs/op_2aca_membership.md.
	*/
	static const int LAURION_FEATURE_LIMIT_COUNT = 33;

	// Used verbatim when Membership:FeatureLimits is empty, and per index when a value is malformed.
	static const int32_t kLaurionFeatureLimitDefaults[LAURION_FEATURE_LIMIT_COUNT] = {
		-1,  //  0 "AAs"                                        count
		-1,  //  1 "maximum level"                              count
		-1,  //  2 "character slots"                            count
		-1,  //  3 "ability to use this spell tier"             count (highest tier)
		-1,  //  4 "inventory slots"                            count; OP_SendFeatureList is the authoritative path
		-1,  //  5 "total platinum"                             count
		1,   //  6 "ability to send mail"                       [flag]
		1,   //  7 "ability to send parcels"                    [flag]
		1,   //  8 "ability to spend loyalty"                   [flag]
		-1,  //  9 "mercenary ranking cap"                      count (max merc tier)
		1,   // 10 "access to a number of guild hall functions" [flag]
		1,   // 11 "shared bank slot access"                    [flag]
		1,   // 12 "number of active quests"                    [flag]
		1,   // 13 "ability to create a guild"                  [flag]
		1,   // 14 "ability to sell in the bazaar"              [flag]
		1,   // 15 "ability to create buy lines in the bazaar"  [flag]
		1,   // 16 "ability to use global chat"                 [flag]
		1,   // 17 "ability to petition"                        [flag]
		1,   // 18 "advertising mode"                           count
		1,   // 19 "ability to use this item"                   [flag]
		-1,  // 20 "starting cities"                            count
		1,   // 21 "appearance slot use"                        count
		0,   // 22 "heroic character"                           [flag]; EQEmu has no heroic characters
		0,   // 23 "auto-grant AAs"                             [flag]; AA granting is server side
		0,   // 24 "mount key ring slots"                       count; base allowance, see OP_KeyRingSlots
		0,   // 25 "illusion key ring slots"                    count; idem
		0,   // 26 "familiar key ring slots"                    count; idem
		1,   // 27 "familiar auto leave"                        [flag]
		0,   // 28 "hero's forge key ring slots"                count; idem
		0,   // 29 "dragon's hoard slots"                       count; EQEmu has no Dragon's Hoard
		0,   // 30 "teleportation item key ring slots"          count; idem
		0,   // 31 "personal depot slots"                       count; EQEmu has no personal depot
		0   // 32 "name reservations"                          count; EQEmu has no name reservations
	};

	static void GetLaurionFeatureLimits(int32_t out[LAURION_FEATURE_LIMIT_COUNT])
	{
		for (int i = 0; i < LAURION_FEATURE_LIMIT_COUNT; ++i) {
			out[i] = kLaurionFeatureLimitDefaults[i];
		}

		const std::string rule = RuleS(Membership, FeatureLimits);
		if (rule.empty()) {
			return;
		}

		// Positional, not sparse: a partial list would silently shift every feature after the gap, and
		// these 33 values are not the same kind of quantity, so a wrong length is treated as a
		// configuration error rather than a prefix.
		std::vector<std::string> values = Strings::Split(rule, ',');
		if (static_cast<int>(values.size()) != LAURION_FEATURE_LIMIT_COUNT) {
			LogNetcode("Membership:FeatureLimits expects {} comma separated values, got {} - using the built-in defaults", LAURION_FEATURE_LIMIT_COUNT, values.size());
			return;
		}

		for (int i = 0; i < LAURION_FEATURE_LIMIT_COUNT; ++i) {
			std::string value = values[i];
			Strings::Trim(value);

			int32_t limit = kLaurionFeatureLimitDefaults[i];
			try {
				const long parsed = std::stol(value, nullptr, 0);
				limit = (parsed > INT32_MAX) ? INT32_MAX
						: (parsed < INT32_MIN) ? INT32_MIN
						: static_cast<int32_t>(parsed);
			}
			catch (...) {
				LogNetcode("Malformed Membership:FeatureLimits value [{}] at position {} - using the default {}", value, i, kLaurionFeatureLimitDefaults[i]);
			}

			out[i] = limit;
		}
	}

	ENCODE(OP_SendMembership) {
		ENCODE_LENGTH_EXACT(Membership_Struct);
		SETUP_DIRECT_ENCODE(Membership_Struct, structs::Membership_Struct);

		eq->membership = emu->membership;
		eq->races = emu->races;
		eq->classes = emu->classes;

		/*
		** entries[] is Membership:FeatureLimits, the client's FreeToPlayClient::LimitData[0..32].  The
		** index -> meaning table and the flag/count convention are on kLaurionFeatureLimitDefaults above.
		*/
		int32_t limits[LAURION_FEATURE_LIMIT_COUNT];
		GetLaurionFeatureLimits(limits);

		eq->entrysize = LAURION_FEATURE_LIMIT_COUNT;
		for (int i = 0; i < LAURION_FEATURE_LIMIT_COUNT; ++i) {
			eq->entries[i] = limits[i];
		}

		FINISH_ENCODE();
	}

	ENCODE(OP_SendMembershipDetails) {
		ENCODE_LENGTH_EXACT(Membership_Details_Struct);
		SETUP_DIRECT_ENCODE(Membership_Details_Struct, structs::Membership_Details_Struct);

		int32 settings[96][3] = {
			{ 0, 0, 250 }, { 1, 0, 1000 }, { 0, 1, -1 }, { 1, 1, -1 }, 
			{ 0, 2, 2 }, { 2, 0, -1 }, { 3, 0, -1 }, { 1, 2, 4 }, 
			{ 0, 3, 1 }, { 2, 1, -1 }, { 3, 1, -1 }, { 1, 3, 1 }, 
			{ 0, 4, -1 }, { 2, 2, -1 }, { 3, 2, -1 }, { 1, 4, -1 }, 
			{ 0, 5, -1 }, { 2, 3, -1 }, { 3, 3, -1 }, { 1, 5, -1 }, 
			{ 0, 6, 0 }, { 2, 4, -1 }, { 3, 4, -1 }, { 1, 6, 0 }, 
			{ 0, 7, 1 }, { 2, 5, -1 }, { 3, 5, -1 }, { 1, 7, 1 }, 
			{ 0, 8, 1 }, { 2, 6, 1 }, { 3, 6, 1 }, { 1, 8, 1 }, 
			{ 0, 9, 5 }, { 2, 7, 1 }, { 3, 7, 1 }, { 1, 9, 5 }, 
			{ 0, 10, 0 }, { 2, 8, 1 }, { 3, 8, 1 }, { 0, 11, -1 }, 
			{ 1, 10, 1 }, { 2, 9, -1 }, { 3, 9, -1 }, { 0, 12, -1 }, 
			{ 1, 11, -1 }, { 2, 10, 1 }, { 3, 10, 1 }, { 0, 13, 0 }, 
			{ 1, 12, -1 }, { 2, 11, -1 }, { 3, 11, -1 }, { 0, 14, 0 }, 
			{ 1, 13, 1 }, { 2, 12, -1 }, { 3, 12, -1 }, { 0, 15, 0 }, 
			{ 1, 14, 0 }, { 2, 13, 1 }, { 3, 13, 1 }, { 0, 16, 0 }, 
			{ 1, 15, 0 }, { 2, 14, 1 }, { 3, 14, 1 }, { 0, 17, 0 }, 
			{ 1, 16, 1 }, { 2, 15, 1 }, { 3, 15, 1 }, { 0, 18, 0 }, 
			{ 1, 17, 0 }, { 2, 16, 1 }, { 3, 16, 1 }, { 0, 19, 0 }, 
			{ 1, 18, 0 }, { 2, 17, 1 }, { 3, 17, 1 }, { 0, 20, 0 }, 
			{ 1, 19, 0 }, { 2, 18, 1 }, { 3, 18, 1 }, { 0, 21, 0 }, 
			{ 1, 20, 0 }, { 2, 19, -1 }, { 3, 19, -1 }, { 0, 22, 0 }, 
			{ 1, 21, 0 }, { 2, 20, -1 }, { 3, 20, -1 }, { 2, 21, 0 }, 
			{ 0, 23, 0 }, { 1, 22, 0 }, { 3, 21, 0 }, { 2, 22, 0 }, 
			{ 1, 23, 0 }, { 3, 22, 0 }, { 2, 23, 0 }, { 3, 23, 0 }
		};

		uint32 races[17][2] = {
			{ 1, 131071 },
			{ 333, 131071 },
			{ 90287, 131071 },
			{ 90289, 16 },
			{ 90290, 32 },
			{ 90291, 64 },
			{ 90292, 128 },
			{ 90293, 256 },
			{ 90294, 512 },
			{ 90295, 1024 },
			{ 90296, 2048 },
			{ 90297, 8192 },
			{ 90298, 16384 },
			{ 90299, 32768 },
			{ 90300, 65536 },
			{ 2012271, 131071 },
			{ 2012277, 131071 }
		};
		
		uint32 classes[17][2] = {
			{ 1, 131071 },
			{ 333, 131071 },
			{ 90287, 131071 },
			{ 90301, 8 },
			{ 90302, 16 },
			{ 90303, 32 },
			{ 90304, 64 },
			{ 90305, 128 },
			{ 90306, 256 },
			{ 90307, 1024 },
			{ 90308, 2048 },
			{ 90309, 8192 },
			{ 90310, 16384 },
			{ 90311, 32768 },
			{ 90312, 65536 },
			{ 2012271, 131071 },
			{ 2012277, 131071 }
		};

		eq->membership_setting_count = 96;

		for (int i = 0; i < 96; ++i) {
			eq->settings[i].setting_index = (int8)settings[i][0];
			eq->settings[i].setting_id = settings[i][1];
			eq->settings[i].setting_value = settings[i][2];
		}

		eq->class_entry_count = 17;
		for (int i = 0; i < 17; ++i) {
			eq->membership_classes[i].purchase_id = classes[i][0];
			eq->membership_classes[i].bitwise_entry = classes[i][1];
		}

		eq->race_entry_count = 17;
		for (int i = 0; i < 17; ++i) {
			eq->membership_races[i].purchase_id = races[i][0];
			eq->membership_races[i].bitwise_entry = races[i][1];
		}

		eq->exit_url_length = 0;

		FINISH_ENCODE();
	}	

	ENCODE(OP_SendZonepoints)
	{
		SETUP_VAR_ENCODE(ZonePoints);
		ALLOC_VAR_ENCODE(structs::ZonePoints, sizeof(structs::ZonePoints) + sizeof(structs::ZonePoint_Entry) * (emu->count + 1));

		eq->count = emu->count;
		for (uint32 i = 0; i < emu->count; ++i)
		{
			eq->zpe[i].iterator = emu->zpe[i].iterator;
			eq->zpe[i].x = emu->zpe[i].x;
			eq->zpe[i].y = emu->zpe[i].y;
			eq->zpe[i].z = emu->zpe[i].z;
			eq->zpe[i].heading = emu->zpe[i].heading;
			eq->zpe[i].zoneid = emu->zpe[i].zoneid;
			eq->zpe[i].zoneinstance = emu->zpe[i].zoneinstance;
		}

		FINISH_ENCODE();
	}

	// Find window (Ctrl+F) point of interest list, answered on OP_FindLocationsPOIs (docs/findwnd.md).
	// The emulator side uses fixed 255 byte strings, the wire uses NUL terminated strings with no length
	// prefix, so the outbound length has to be computed from the string contents. The client (FUN_1403af400)
	// copies min(strlen + 1, 0xFF) bytes per string, which caps what it can display at 254 characters.
	ENCODE(OP_FindLocationsPOIs)
	{
		SETUP_VAR_ENCODE(FindLocations);

		// bounded strlen: the client's reader copies at most 255 bytes per string (254 usable characters)
		const auto capped_len = [](const char *s) -> uint32 {
			uint32 len = 0;
			while (len < FIND_LOCATION_STRING_MAX && s[len]) {
				++len;
			}
			return len;
		};

		uint32 wire_size = sizeof(uint32);
		for (uint32 i = 0; i < emu->count; ++i) {
			wire_size += sizeof(uint32) + capped_len(emu->entry[i].description) + 1
				+ capped_len(emu->entry[i].category) + 1;
		}

		ALLOC_LEN_ENCODE(wire_size);
		uint8 *out = __packet->pBuffer;

		memcpy(out, &emu->count, sizeof(uint32));
		out += sizeof(uint32);
		for (uint32 i = 0; i < emu->count; ++i) {
			memcpy(out, &emu->entry[i].id, sizeof(uint32));
			out += sizeof(uint32);
			uint32 len = capped_len(emu->entry[i].description);
			memcpy(out, emu->entry[i].description, len);
			out[len] = 0;
			out += len + 1;
			len = capped_len(emu->entry[i].category);
			memcpy(out, emu->entry[i].category, len);
			out[len] = 0;
			out += len + 1;
		}

		FINISH_ENCODE();
	}

	// Find window (Ctrl+F) zone connections, answered on OP_FindLocationsConnections (docs/findwnd.md).
	// Fixed size records: uint32 count followed by count * 8 x uint32, identical on both sides.
	ENCODE(OP_FindLocationsConnections)
	{
		SETUP_VAR_ENCODE(FindZoneConnections);
		ALLOC_VAR_ENCODE(structs::FindZoneConnections, sizeof(structs::FindZoneConnections) + sizeof(structs::FindZoneConnection_Entry) * emu->count);

		eq->count = emu->count;
		for (uint32 i = 0; i < emu->count; ++i) {
			eq->entry[i].type = emu->entry[i].type;
			eq->entry[i].id = emu->entry[i].id;
			eq->entry[i].sub_id = emu->entry[i].sub_id;
			eq->entry[i].zone_id = emu->entry[i].zone_id;
			eq->entry[i].zone_identifier = emu->entry[i].zone_identifier;
			eq->entry[i].x = emu->entry[i].x;
			eq->entry[i].y = emu->entry[i].y;
			eq->entry[i].z = emu->entry[i].z;
		}

		FINISH_ENCODE();
	}

	// Find window (Ctrl+F) mystical path reply, OP_PlayerPathReply (docs/findwnd.md 6.4). The payload is nothing
	// but float3 waypoints and there is no header, the client counts them as payload size / 12, so the emulator
	// packet already is the wire packet and this encoder only validates and copies it. A zero size payload is
	// the deliberate "no path" answer: the client then prints "Mysterious forces prevent you from finding your
	// destination easily." and clears the goal, which is better feedback than saying nothing at all.
	ENCODE(OP_PlayerPathReply)
	{
		EQApplicationPacket *__packet = *p;
		*p = nullptr;
		unsigned char *__emu_buffer = __packet->pBuffer;

		const uint32 wire_size = __packet->size;

		if (wire_size % 12) {
			LogNetcode("Outbound OP_PlayerPathReply payload [{}] is not a whole number of float3 waypoints", wire_size);
			FAIL_ENCODE();
			return;
		}

		ALLOC_LEN_ENCODE(wire_size);
		if (wire_size) {
			memcpy(__packet->pBuffer, __emu_buffer, wire_size);
		}

		FINISH_ENCODE();
	}

	ENCODE(OP_ShopPlayerBuy)
	{
		ENCODE_LENGTH_EXACT(Merchant_Sell_Struct);
		SETUP_DIRECT_ENCODE(Merchant_Sell_Struct, structs::Merchant_Sell_Response_Struct);

		OUT(npcid);
		OUT(playerid);
		OUT(itemslot);
		OUT(quantity);
		OUT(price);

		FINISH_ENCODE();
	}

	ENCODE(OP_ShopPlayerSell)
	{
		ENCODE_LENGTH_EXACT(Merchant_Purchase_Struct);
		SETUP_DIRECT_ENCODE(Merchant_Purchase_Struct, structs::Merchant_Purchase_Response_Struct);

		// The client's sell ack has no npcid: the slot struct is at +0, quantity at +8, price at +16.
		eq->inventory_slot = ServerToLaurionTypelessSlot(emu->itemslot, EQ::invtype::typePossessions);
		eq->quantity = emu->quantity;
		eq->price = emu->price;

		FINISH_ENCODE();
	}

	ENCODE(OP_ShopRequest)
	{
		ENCODE_LENGTH_EXACT(MerchantClick_Struct);
		SETUP_DIRECT_ENCODE(MerchantClick_Struct, structs::MerchantClickResponse_Struct);

		// The Laurion client reads +4 as the action flag (0 = close, non-zero = open). It used to be
		// filled with player_id, which the zone always sets to 0 on open, so the client took the close
		// branch and the merchant window never appeared.
		// Client handler: ZonePacket__dispatchRecv case 0x840 -> FUN_1401d4fa0 -> FUN_14045c750.
		eq->npc_id      = emu->npc_id;   // must be the merchant's entity id for the spawn lookup
		eq->action      = emu->command;  // MerchantActions::Open = 1, MerchantActions::Close = 0
		eq->rate        = emu->rate;
		eq->tab_display = emu->tab_display;
		eq->unknown028  = 256;

		FINISH_ENCODE();
	}

	ENCODE(OP_SkillUpdate)
	{
		ENCODE_LENGTH_EXACT(SkillUpdate_Struct);
		SETUP_DIRECT_ENCODE(SkillUpdate_Struct, structs::SkillUpdate_Struct);

		OUT(skillId);
		OUT(value);
		eq->active = 1;

		FINISH_ENCODE();
	}

	ENCODE(OP_SpecialMesg)
	{
		EQApplicationPacket* in = *p;
		*p = nullptr;

		SerializeBuffer buf(in->size);
		buf.WriteInt8(in->ReadUInt8()); // speak mode
		buf.WriteInt8(in->ReadUInt8()); // journal mode
		buf.WriteInt8(in->ReadUInt8()); // language
		buf.WriteInt32(in->ReadUInt32()); // message type
		buf.WriteInt32(in->ReadUInt32()); // target spawn id

		std::string name;
		in->ReadString(name); // NPC names max out at 63 chars

		buf.WriteString(name);

		buf.WriteInt32(in->ReadUInt32()); // loc
		buf.WriteInt32(in->ReadUInt32());
		buf.WriteInt32(in->ReadUInt32());

		std::string old_message;
		std::string new_message;

		in->ReadString(old_message);

		ServerToLaurionConvertLinks(new_message, old_message);

		buf.WriteString(new_message);

		auto outapp = new EQApplicationPacket(OP_SpecialMesg, std::move(buf));

		dest->FastQueuePacket(&outapp, ack_req);
		delete in;
	}

	ENCODE(OP_SpawnAppearance)
	{
		EQApplicationPacket* in = *p;
		*p = nullptr;
	
		unsigned char* emu_buffer = in->pBuffer;
	
		SpawnAppearance_Struct* sas = (SpawnAppearance_Struct*)emu_buffer;
	
		if (sas->type != AppearanceType::Size)
		{
			//laurion struct is different than rof2's but the idea is the same
			//we will probably want to better implement Laurion's structure later
			auto outapp = new EQApplicationPacket(OP_SpawnAppearance, sizeof(structs::SpawnAppearance_Struct));
			structs::SpawnAppearance_Struct *eq = (structs::SpawnAppearance_Struct*)outapp->pBuffer;
	
			eq->spawn_id = sas->spawn_id;
			eq->type = ServerToLaurionSpawnAppearanceType(sas->type);
			eq->parameter = sas->parameter;
	
			dest->FastQueuePacket(&outapp, ack_req);
			delete in;
			return;
		}
	
		auto outapp = new EQApplicationPacket(OP_ChangeSize, sizeof(ChangeSize_Struct));
	
		ChangeSize_Struct* css = (ChangeSize_Struct*)outapp->pBuffer;
	
		css->EntityID = sas->spawn_id;
		css->Size = (float)sas->parameter;
		css->Unknown08 = 0;
		css->Unknown12 = 1.0f;
	
		dest->FastQueuePacket(&outapp, ack_req);
		delete in;
	}

	ENCODE(OP_SpawnDoor)
	{
		SETUP_VAR_ENCODE(Door_Struct);
		int door_count = __packet->size / sizeof(Door_Struct);
		int total_length = door_count * sizeof(structs::Door_Struct);
		ALLOC_VAR_ENCODE(structs::Door_Struct, total_length);

		int r;
		for (r = 0; r < door_count; r++) {
			strncpy(eq[r].name, emu[r].name, 32);
			eq[r].DefaultY = emu[r].yPos;
			eq[r].DefaultX = emu[r].xPos;
			eq[r].DefaultZ = emu[r].zPos;
			eq[r].DefaultHeading = emu[r].heading;
			eq[r].DefaultDoorAngle = emu[r].incline;
			eq[r].Y = emu[r].yPos;
			eq[r].X = emu[r].xPos;
			eq[r].Z = emu[r].zPos;
			eq[r].Heading = emu[r].heading;
			//there's a door angle here but im not sure if / what we set it to since ive literally never seen it as anything but 0 on live
			//based on pattern it probably is supposed to match the default angle?
			//I'm not 100% sure this is a float it might be a uint32
			eq[r].DoorAngle = emu[r].incline;
			eq[r].ScaleFactor = emu[r].size;
			eq[r].Id = emu[r].doorId;
			eq[r].Type = emu[r].opentype;
			eq[r].State = emu[r].state_at_spawn;
			eq[r].DefaultState = emu[r].invert_state;
			eq[r].Param = emu[r].door_param;
			eq[r].bVisible = 1;
			eq[r].bUsable = 1;
		}

		FINISH_ENCODE();
	}

	ENCODE(OP_Stun)
	{
		ENCODE_LENGTH_EXACT(Stun_Struct);
		SETUP_DIRECT_ENCODE(Stun_Struct, structs::Stun_Struct);

		OUT(duration);
		OUT(flag);
		eq->unknown005 = 163;
		eq->unknown006 = 67;

		FINISH_ENCODE();
	}

	ENCODE(OP_WearChange)
	{
		ENCODE_LENGTH_EXACT(WearChange_Struct);
		SETUP_DIRECT_ENCODE(WearChange_Struct, structs::WearChange_Struct);

		OUT(spawn_id);
		eq->wear_slot_id = emu->wear_slot_id;
		eq->armor_id = emu->material;
		eq->variation = emu->unknown06;
		eq->material = emu->elite_material;
		eq->new_armor_id = emu->hero_forge_model;
		eq->new_armor_type = emu->unknown18;
		eq->color = emu->color.Color;

		FINISH_ENCODE();
	}

	ENCODE(OP_ZoneChange)
	{
		ENCODE_LENGTH_EXACT(ZoneChange_Struct);
		SETUP_DIRECT_ENCODE(ZoneChange_Struct, structs::ZoneChange_Struct);

		memcpy(eq->char_name, emu->char_name, sizeof(emu->char_name));
		OUT(zoneID);
		OUT(instanceID);
		OUT(y);
		OUT(x);
		OUT(z)
			OUT(zone_reason);
		OUT(success);

		if (eq->success < 0)
			eq->success -= 1;

		FINISH_ENCODE();
	}

	ENCODE(OP_ZoneEntry) { ENCODE_FORWARD(OP_ZoneSpawns); }

	ENCODE(OP_ZonePlayerToBind)
	{
		SETUP_VAR_ENCODE(ZonePlayerToBind_Struct);
		ALLOC_LEN_ENCODE(sizeof(structs::ZonePlayerToBind_Struct) + strlen(emu->zone_name));

		__packet->SetWritePosition(0);
		__packet->WriteUInt16(emu->bind_zone_id);
		__packet->WriteUInt16(emu->bind_instance_id);
		__packet->WriteFloat(emu->x);
		__packet->WriteFloat(emu->y);
		__packet->WriteFloat(emu->z);
		__packet->WriteFloat(emu->heading);
		__packet->WriteString(emu->zone_name);
		__packet->WriteUInt32(60);
		__packet->WriteUInt32(0);
		__packet->WriteUInt32(51);
		__packet->WriteUInt32(41);

		FINISH_ENCODE();
	}

	ENCODE(OP_ZoneSpawns)
	{
		EQApplicationPacket* in = *p;
		*p = nullptr;

		//store away the emu struct
		unsigned char* __emu_buffer = in->pBuffer;
		Spawn_Struct* emu = (Spawn_Struct*)__emu_buffer;

		int entrycount = in->size / sizeof(Spawn_Struct);
		if (entrycount == 0 || (in->size % sizeof(Spawn_Struct)) != 0) {
			LogNetcode("[STRUCTS] Wrong size on outbound [{}]: Got [{}], expected multiple of [{}]", opcodes->EmuToName(in->GetOpcode()), in->size, sizeof(Spawn_Struct));
			delete in;
			return;
		}
		
		for (int i = 0; i < entrycount; i++, emu++) {
			SerializeBuffer buffer;

			auto SpawnSize = emu->size;
			if (!((emu->NPC == 0) || (emu->race <= Race::Gnome) || (emu->race == Race::Iksar) ||
				(emu->race == Race::VahShir) || (emu->race == Race::Froglok2) || (emu->race == Race::Drakkin))
				)
			{
				if (emu->size == 0)
				{
					emu->size = 6;
					SpawnSize = 6;
				}
			}

			if (SpawnSize == 0)
			{
				SpawnSize = 3;
			}

			/*
			char Name[];
			u32 SpawnId;
			u8 Level;
			float MeleeRadius;
			*/
			buffer.WriteString(emu->name);
			buffer.WriteUInt32(emu->spawnId);
			buffer.WriteUInt8(emu->level);
			if (emu->DestructibleObject) //bounding radius: we should consider supporting this officially in the future
			{
				buffer.WriteFloat(10.0f);
			}
			else
			{

				buffer.WriteFloat(SpawnSize - 0.7f);
			}

			/*
			EqGuid HashKey;
			*/
			// The client stores this as PlayerBase::HashKey (MacroQuest src/eqlib/PlayerClient.h:446) and
			// resolves spawns with PlayerManagerClient::GetPlayerFromHashKey. EQEmu has no real character
			// GUID, so the entity id is used as the hash key. It must match the value the server puts at
			// payload+0x18 of the fellowship invite prompt - see ENCODE(OP_GroupInvitePrompt).
			buffer.WriteUInt32(emu->spawnId); // CharacterGuid.Id - entity id, used as the hash key
			buffer.WriteUInt16(0); // CharacterGuid.WorldId
			buffer.WriteUInt16(0);

			/*
			u8 Type;
			ActorFlags Flags;
			*/
			buffer.WriteUInt8(emu->NPC);

			structs::Spawn_Struct_Bitfields flags;
			memset(&flags, 0, sizeof(structs::Spawn_Struct_Bitfields));

			flags.gender = emu->gender;
			flags.ispet = emu->is_pet;
			flags.afk = emu->afk;
			flags.anon = emu->anon;
			flags.gm = emu->gm;
			flags.sneak = 0;
			flags.lfg = emu->lfg;
			flags.invis = emu->invis; //we need to implement this
			flags.linkdead = 0; //on live I often see this as 1 for npcs, maybe consider adding this in the future
			flags.showhelm = emu->showhelm;
			flags.trader = emu->trader ? 1 : 0;
			flags.targetable = 1;
			flags.targetable_with_hotkey = emu->targetable_with_hotkey ? 1 : 0;
			flags.showname = emu->show_name;
			flags.buyer = emu->buyer ? 1 : 0;
			flags.title = strlen(emu->title) > 0 ? 1 : 0;
			flags.suffix = strlen(emu->suffix) > 0 ? 1 : 0;

			if (emu->DestructibleObject || emu->class_ == Class::LDoNTreasure) {
				flags.interactiveobject = 1;
			}

			//write flags
			//buffer.WriteStructure(flags);
			for (int j = 0; j < 5; ++j) {
				buffer.WriteUInt8(flags.raw[j]);
			}

			/*
			float EmitterScalingRadius;
			s32 DefaultEmitterID;
			*/

			//we don't support this atm; wouldn't be hard to add I don't think
			//RoF also supports this so might be worth implementing.
			buffer.WriteFloat(1.0f);
			buffer.WriteInt32(-1);

			if (emu->DestructibleObject || emu->class_ == Class::LDoNTreasure)
			{
				/*
				char InteractiveObjectModelName[];
				char InteractiveObjectName[];
				char InteractiveObjectOtherName[];
				*/

				buffer.WriteString(emu->DestructibleModel);
				buffer.WriteString(emu->DestructibleName2);
				buffer.WriteString(emu->DestructibleString);

				/*
				s32 CurrIOState;
				s32 ObjectAnimationID;
				*/
				buffer.WriteUInt32(emu->DestructibleAppearance);
				buffer.WriteUInt32(emu->DestructibleUnk1);

				/*
				s32 SoundId[10];
				*/
				buffer.WriteUInt32(emu->DestructibleID1);
				buffer.WriteUInt32(emu->DestructibleID2);
				buffer.WriteUInt32(emu->DestructibleID3);
				buffer.WriteUInt32(emu->DestructibleID4);
				buffer.WriteUInt32(emu->DestructibleUnk2);
				buffer.WriteUInt32(emu->DestructibleUnk3);
				buffer.WriteUInt32(emu->DestructibleUnk4);
				buffer.WriteUInt32(emu->DestructibleUnk5);
				buffer.WriteUInt32(emu->DestructibleUnk6);
				buffer.WriteUInt32(emu->DestructibleUnk7);
				/*
				s8 Collidable;
				s8 ObjectType;
				*/
				buffer.WriteUInt8(emu->DestructibleUnk8);
				buffer.WriteUInt8(emu->DestructibleUnk9);
			}

			/*
			u8 PropertyCount;
			u32 Properties[PropertyCount];
			*/
			// The client stores this list as a per-spawn property set (Character + 0x138) and
			// gates spells against it: CharacterZoneClient::CastSpell -> PlayerBase::HasProperty.
			// The property IDs it tests are the same numeric IDs as our BodyType IDs, but a single
			// body type can imply more than one property, so a 1:1 copy is not enough. Sending only
			// the body type makes the client reject the cast locally (eqstr 219 "This spell only
			// works on the undead.") and never send OP_CastSpell at all.
			// See BodyType::GetClientProperties and docs/mend-bones-undead-target.md.

			if (!emu->DestructibleObject)
			{
				const std::vector<uint8_t> properties = BodyType::GetClientProperties(emu->bodytype);

				buffer.WriteUInt8(static_cast<uint8_t>(properties.size()));
				for (const uint8_t &property : properties)
				{
					buffer.WriteUInt32(property);
				}
			}
			else
			{
				buffer.WriteUInt8(0);
			}

			/*
			u8 HPCurrentPct;
			*/
			buffer.WriteUInt8(emu->curHp);

			/*
			s8 HairColor;
			s8 FacialHairColor;
			s8 EyeColor1;
			s8 EyeColor2;
			s8 HairStyle;
			s8 FacialHair;
			s32 Heritage;
			s32 Tattoo;
			s32 Details;
			*/

			buffer.WriteUInt8(emu->haircolor);
			buffer.WriteUInt8(emu->beardcolor);
			buffer.WriteUInt8(emu->eyecolor1);
			buffer.WriteUInt8(emu->eyecolor2);
			buffer.WriteUInt8(emu->hairstyle);
			buffer.WriteUInt8(emu->beard);
			buffer.WriteUInt32(emu->drakkin_heritage);
			buffer.WriteUInt32(emu->drakkin_tattoo);
			buffer.WriteUInt32(emu->drakkin_details);

			/*
			s8 TextureType;
			s8 Material;
			s8 Variation;
			s8 HeadType;
			*/
			buffer.WriteUInt8(emu->equip_chest2);
			buffer.WriteUInt8(0);
			buffer.WriteUInt8(0);
			buffer.WriteUInt8(emu->helm);

			/*
			float Height;
			s8 FaceStyle;
			float MyWalkSpeed;
			float RunSpeed;
			s32 Race;
			*/

			buffer.WriteFloat(emu->size);
			buffer.WriteInt8(emu->face);
			buffer.WriteFloat(emu->walkspeed);
			buffer.WriteFloat(emu->runspeed);
			buffer.WriteInt32(emu->race);

			/*
			u8 HoldingAnimation;
			u32 Deity;
			EqGuid GuildID;
			u32 Class;
			*/

			buffer.WriteUInt8(0);
			buffer.WriteUInt32(emu->deity);
			if (emu->NPC) {
				buffer.WriteInt32(-1);
				buffer.WriteUInt32(0);
			}
			else { //guilds will probably need a ton of work
				buffer.WriteUInt32(emu->guildID);
				buffer.WriteUInt32(0);
			}
			buffer.WriteUInt32(emu->class_);

			/*
			u8 PvP;
			u8 StandState;
			u8 Light;
			u8 GravityBehavior;
			*/

			buffer.WriteUInt8(0);
			buffer.WriteUInt8(emu->StandState);
			buffer.WriteUInt8(emu->light);
			buffer.WriteUInt8(emu->flymode);

			/*
			char LastName[];
			*/
			buffer.WriteString(emu->lastName);

			/*
			u8 bGuildShowAnim;
			u8 bTempPet;
			u32 MasterID;
			u8 FindBits;
			*/

			buffer.WriteUInt8(emu->guild_show);
			buffer.WriteUInt8(0);
			buffer.WriteUInt32(emu->petOwnerId);
			buffer.WriteUInt8(0);

			/*
			u32 PlayerState;
			u32 NpcTintIndex;
			u32 PrimaryTintIndex;
			u32 SecondaryTintIndex;
			*/

			buffer.WriteUInt32(emu->PlayerState);
			buffer.WriteUInt32(0);
			buffer.WriteUInt32(0);
			buffer.WriteUInt32(0);

			/*
			u32 EncounterLockState;
			u64 LockID;
			*/

			buffer.WriteUInt32(0);
			buffer.WriteUInt64(0);

			//u32 SeeInvis[3];
			if (emu->NPC == 1) {
				buffer.WriteUInt32(0);
				buffer.WriteUInt32(0);
				buffer.WriteUInt32(0);
			}

			/*
			s32 Primary;
			s32 Secondary;
			*/
			buffer.WriteUInt32(0xffffffff);
			buffer.WriteUInt32(0xffffffff);

			if ((emu->NPC == 0) || (emu->race <= Race::Gnome) || (emu->race == Race::Iksar) ||
				(emu->race == Race::VahShir) || (emu->race == Race::Froglok2) || (emu->race == Race::Drakkin)
				)
			{
				/*
				u32 ArmorColor[9];
				*/
				for (int k = EQ::textures::textureBegin; k < EQ::textures::materialCount; ++k)
				{
					buffer.WriteUInt32(emu->equipment_tint.Slot[k].Color);
				}

				/*
				Armor Armor[9];
				*/
				for (int k = EQ::textures::textureBegin; k < EQ::textures::materialCount; k++) {
					buffer.WriteUInt32(emu->equipment.Slot[k].Material);
					buffer.WriteUInt32(emu->equipment.Slot[k].Unknown1);
					buffer.WriteUInt32(emu->equipment.Slot[k].EliteModel);
					buffer.WriteUInt32(emu->equipment.Slot[k].HerosForgeModel);
					buffer.WriteUInt32(emu->equipment.Slot[k].Unknown2);
				}
			}
			else
			{
				//Armor Armor[3];
				buffer.WriteUInt32(0);
				buffer.WriteUInt32(0);
				buffer.WriteUInt32(0);
				buffer.WriteUInt32(0);
				buffer.WriteUInt32(0);

				buffer.WriteUInt32(emu->equipment.Primary.Material);
				buffer.WriteUInt32(0);
				buffer.WriteUInt32(0);
				buffer.WriteUInt32(0);
				buffer.WriteUInt32(0);

				buffer.WriteUInt32(emu->equipment.Secondary.Material);
				buffer.WriteUInt32(0);
				buffer.WriteUInt32(0);
				buffer.WriteUInt32(0);
				buffer.WriteUInt32(0);
			}

			//u8 CPhysicsData[20];
			structs::Spawn_Struct_Position position;
			memset(&position, 0, sizeof(structs::Spawn_Struct_Position));

			position.y = emu->y;
			position.deltaZ = emu->deltaZ;
			position.deltaX = emu->deltaX;
			position.x = emu->x;
			position.heading = emu->heading;
			position.deltaHeading = emu->deltaHeading;
			position.z = emu->z;
			position.animation = emu->animation;
			position.deltaY = emu->deltaY;

			//buffer.WriteStructure(position);
			for (int j = 0; j < 5; ++j) {
				buffer.WriteUInt32(position.raw[j]);
			}

			/*
			if(Flags.title) {
				char Title[];
			}
			*/
			if (flags.title) {
				buffer.WriteString(emu->title);
			}

			/*
			if(Flags.suffix) {
				char Suffix[];
			}
			*/
			if (flags.suffix) {
				buffer.WriteString(emu->suffix);
			}

			/*
			u32 Unknown0x0164;
			s32 SplineID;
			*/

			buffer.WriteUInt32(0);
			buffer.WriteUInt32(0);

			/*
			u8 Mercenary;
			*/
			buffer.WriteUInt8(emu->IsMercenary);

			/*
				char realEstateItemGuid[];
				s32 RealEstateID;
				s32 RealEstateItemId;
			*/

			buffer.WriteString("0000000000000000");
			buffer.WriteInt32(-1);
			buffer.WriteInt32(-1);

			/*
			s32 MercId;
			s32 ContractorID;
			u32 Birthdate;
			u8 bAlwaysShowAura;
			*/

			buffer.WriteInt32(0);
			buffer.WriteInt32(0);
			buffer.WriteUInt32(0);
			buffer.WriteUInt8(0);

			/*
			u32 physicsEffectCount;
			PhysicsEffect physicsEffects[physicsEffectCount];
			*/
			buffer.WriteUInt32(0);

			//s32 SpawnStatus[6];
			buffer.WriteUInt32(0);
			buffer.WriteUInt32(0);
			buffer.WriteUInt32(0);
			buffer.WriteUInt32(0);
			buffer.WriteUInt32(0);
			buffer.WriteUInt32(0);

			if (flags.interactiveobject && emu->DestructibleUnk9 == 4) {
				/*
				s32 BannerIndex0;
				s32 BannerIndex1;
				s32 BannerTint0;
				s32 BannerTint1;
				*/
				buffer.WriteUInt32(0);
				buffer.WriteUInt32(0);
				buffer.WriteUInt32(0);
				buffer.WriteUInt32(0);
			}

			auto outapp = new EQApplicationPacket(OP_ZoneEntry, buffer.size());
			outapp->WriteData(buffer.buffer(), buffer.size());
			dest->FastQueuePacket(&outapp, ack_req);
		}

		delete in;
	}

	ENCODE(OP_GroupInvitePrompt) // 0x7e71 - fellowship invite prompt
	{
		// Handler FUN_1403aa570 reads only the u64 at payload+0x18 and looks the inviter up by hash
		// key. The value has to be the same one ENCODE(OP_ZoneSpawns) wrote for that spawn, which is
		// the entity id. Nothing before +0x18 is read by the handler.
		ENCODE_LENGTH_EXACT(GroupInvitePrompt_Struct);
		SETUP_DIRECT_ENCODE(GroupInvitePrompt_Struct, structs::GroupInvitePrompt_Struct);

		eq->inviter_guid.Id = emu->inviter_id;

		FINISH_ENCODE();
	}

	ENCODE(OP_GroupCancelInvite) // 0x1f25 - group invite response / declined invite
	{
		// EQEmu carries this as GroupCancel_Struct (129): name1, name2, toggle at +128. Laurion uses 156
		// bytes and puts the subcommand at +152, which is the only field its receive handler reads:
		// FUN_140282be0 switches on the byte at payload+152 (1 "%1 is currently considering joining another
		// group.", 2 "%1 is already in another group.", 3 "%1 rejects your offer to join the group.",
		// 4 raid variant, 5 raid group leader variant) and formats it with the name at payload+64.
		// Both client senders write the subcommand only at +152 and explicitly zero the low byte of the
		// dword at +128, and both leave a constant 0x100 in the u16 at +144, so mirror that.
		// Without this encoder the inviter's client gets a 129 byte packet, reads +152 outside the payload,
		// and prints nothing.
		ENCODE_LENGTH_EXACT(GroupCancel_Struct);
		SETUP_DIRECT_ENCODE(GroupCancel_Struct, structs::GroupCancelInvite_Struct);

		memcpy(eq->name1, emu->name1, sizeof(eq->name1));
		memcpy(eq->name2, emu->name2, sizeof(eq->name2));
		eq->subcommand = emu->toggle;
		eq->unknown0144 = 0x100;

		FINISH_ENCODE();
	}

	ENCODE(OP_KeyRingSlots) // 0x7d5b - key ring slot allowance
	{
		// FUN_1401fb9d0 copies the five u16 straight into pinstLocalPC->BaseKeyRingSlots[0..4]
		// (PCClient + 0x2EE8). That array is the base of the client's key ring capacity:
		// capacity = min(items_in_ring + BaseKeyRingSlots[ring], 250/250/350/500/250) (FUN_1402c2630,
		// PCClient vtable slot 0x128), and FUN_1402b69e0 rejects a move when that capacity is not above
		// the slots already used. With the array at zero - which is what a client that never received
		// this packet has - no key item can be added to any ring. The client reads the values back as
		// signed short, so the sender clamps them to 32767. Reverse notes: docs/op_7d5b_keyring_slots.md
		ENCODE_LENGTH_EXACT(KeyRingSlots_Struct);
		SETUP_DIRECT_ENCODE(KeyRingSlots_Struct, structs::KeyRingSlots_Struct);

		eq->mount_keyring_slots = emu->mount_keyring_slots;
		eq->illusion_keyring_slots = emu->illusion_keyring_slots;
		eq->familiar_keyring_slots = emu->familiar_keyring_slots;
		eq->heroforge_keyring_slots = emu->heroforge_keyring_slots;
		eq->teleportation_keyring_slots = emu->teleportation_keyring_slots;

		FINISH_ENCODE();
	}

	// 0x57b2 - the group roster. Client receive handler FUN_1402c2c40 (dispatcher branch 0x1401e5cb6,
	// reached by CMP opcode 0x57b2 at 0x1401e45a8) creates the CGroup object at pinstLocalPC + 0x2eb8 and
	// runs the variable length deserializer FUN_140637630. This is the packet that populates the group
	// window. Wire order is cursor based, strings are null terminated, and every read is guarded by the
	// remaining size:
	//   u32 group id            -> CGroupBase::m_id (+0x40)
	//   u32 member count
	//   char[] leader name      -> matched against each entry to pick CGroupBase::m_groupLeader (+0x38)
	//   per entry:
	//     u32 slot index        -> CGroupBase::m_groupMembers[slot]
	//     char[] name           -> CGroupMemberBase::Name (+0x08)
	//     u16 type              -> CGroupMemberBase::Type (+0x10), MQ EQP_PC = 0
	//     char[] owner name     -> CGroupMemberBase::OwnerName (+0x18)
	//     u32 level             -> CGroupMemberBase::Level (+0x20)
	//     5 x u8 role flags     -> CGroupMemberBase::bRoleStates[1..5] (+0x2c..+0x30)
	//     u32 offline flag      -> CGroupMemberBase::bIsOffline (+0x24)
	//     u64 online timestamp  -> CGroupMemberBase::OnlineTimestamp (+0x38)
	//     u32 unique player id  -> CGroupMemberBase::UniquePlayerID (+0x28)
	//
	// EQEmu builds the fixed size GroupUpdate_Struct / GroupUpdate2_Struct / GroupJoin_Struct, which share
	// the first 452 bytes: action, yourname, membername[5], leadersname. The group id, slot, type, level,
	// roles, offline flag, timestamp and unique player id are not carried by that struct, so they are
	// emitted as zero. The action is dropped: the Laurion deserializer has no action field, it rebuilds the
	// roster from the entries, so a disband is expressed as an empty roster.
	//
	// OP_GroupUpdate and OP_GroupUpdateB both map to 0x57b2, so both encoders emit this shape.
	ENCODE(OP_GroupUpdate)
	{
		EQApplicationPacket* in = *p;
		*p = nullptr;

		if (in->size < sizeof(GroupUpdate_Struct)) {
			LogNetcode("[STRUCTS] GroupUpdate too short: got [{}], expected at least [{}]",
				in->size, sizeof(GroupUpdate_Struct));
			delete in;
			return;
		}

		GroupUpdate_Struct* gu = (GroupUpdate_Struct*)in->pBuffer;

		// EQEmu's established group model (common/patches/sod.cpp, uf.cpp, rof.cpp, rof2.cpp): a leave or a
		// disband is sent as OP_GroupDisbandYou when the recipient is the departing member, and as
		// OP_GroupDisbandOther when the recipient is a remaining member. EQEmu sets yourname to the
		// recipient, so yourname == membername[0] identifies the departing member.
		//
		// Laurion's group handler FUN_1402c2930 matches that split exactly:
		//   0x7be0 -> prints eqstr 12001 "You have been removed from the group." then FUN_1402bff30 clears
		//            the group object. Nothing else in the payload is read.
		//   0x14cc -> FUN_140294e80(group, payload + 64) looks a member up by name and clears its slot
		//            through FUN_140637260. This is the only single member removal path, because the roster
		//            deserializer FUN_140637630 clears all six slots and rebuilds from its entries, so it
		//            can only add.
		// Both branches read the payload at +64, which is GroupGeneric_Struct::name2, so the packet is
		// EQEmu's GroupGeneric_Struct passed through unchanged.
		if (gu->action == groupActLeave || gu->action == groupActDisband) {
			bool recipient_is_departing = gu->action == groupActDisband ||
				std::string(gu->yourname) == std::string(gu->membername[0]);

			in->SetOpcode(recipient_is_departing ? OP_GroupDisbandYou : OP_GroupDisbandOther);

			SerializeBuffer buffer;
			for (uint32 i = 0; i < 64; ++i) buffer.WriteUInt8((uint8)gu->yourname[i]);       // name1, the recipient
			for (uint32 i = 0; i < 64; ++i) buffer.WriteUInt8((uint8)gu->membername[0][i]);   // name2, read at +64

			in->size = buffer.size();
			in->pBuffer = new unsigned char[buffer.size()];
			memcpy(in->pBuffer, buffer.buffer(), buffer.size());

			dest->FastQueuePacket(&in, ack_req);
			return;
		}

		uint32 count = 0;
		for (uint32 i = 0; i < 5; ++i) {
			if (gu->membername[i][0] != '\0') ++count;
		}

		// Everything that is not a leave or a disband would have to be a complete roster, and no EQEmu
		// group-update carrier can express one. GroupJoin_Struct - which is what almost every send site here
		// actually allocates - has a single membername[64]; reading membername[1..4] and leadersname out of it
		// reads leader_aas, unknown196 and NPCMarkerID. Worse, Laurion's deserializer FUN_140637630 clears all
		// six member slots before it installs anything, so a partial roster is not merely redundant: it deletes
		// the roster the client already had. That is what happened in the 2026-10-20 capture, where a
		// groupActAAUpdate from Group::SendLeadershipAAUpdate landed after the correct full roster and left the
		// group window with one entry and no bars.
		//
		// Group::SendLaurionRoster is the only packet that can carry a Laurion roster, and it goes out on
		// OP_GroupUpdateB. Leader AAs are not a Laurion concept at all - see the OP_GroupLeadershipAAUpdate
		// note in patch_Laurion.conf.
		LogNetcode(
			"[STRUCTS] Laurion group update action [{}] with [{}] member name(s) cannot express a full roster; "
			"dropped in favour of Group::SendLaurionRoster",
			gu->action,
			count);

		delete in;
		return;
	}

	// Client::SendGroupCreatePacket already writes the variable length shape, but with EQEmu's older field
	// widths: 3 role bytes instead of 5, a u32 timestamp instead of u64, and no unique player id. Re-read
	// it with EQEmu's widths and re-emit with the client's.
	ENCODE(OP_GroupUpdateB)
	{
		EQApplicationPacket* in = *p;
		*p = nullptr;

		if (in->size < 12) {
			LogNetcode("[STRUCTS] GroupUpdateB too short: got [{}], expected at least 12", in->size);
			delete in;
			return;
		}

		uint32 group_id = 0;
		uint32 count = 0;
		memcpy(&group_id, in->pBuffer, 4);
		memcpy(&count, in->pBuffer + 4, 4);

		if (count > MAX_GROUP_MEMBERS) {
			LogNetcode("[STRUCTS] GroupUpdateB count [{}] exceeds MAX_GROUP_MEMBERS [{}]", count, MAX_GROUP_MEMBERS);
			delete in;
			return;
		}

		struct Entry {
			uint32 slot;
			char   name[64];
			uint16 type;
			char   owner[64];
			uint32 level;
			uint8  roles[5];
			uint32 offline;
			uint64 timestamp;
			uint32 unique_id;
		};

		auto read_string = [&](uint32& pos, char* out) -> bool {
			uint32 len = 0;
			while (pos + len < in->size && in->pBuffer[pos + len] != 0) ++len;
			if (pos + len >= in->size) return false;
			if (len > 63) return false;
			memcpy(out, in->pBuffer + pos, len);
			out[len] = '\0';
			pos += len + 1;
			return true;
		};

		uint32 pos = 8;
		char leader[64];
		if (!read_string(pos, leader)) {
			LogNetcode("[STRUCTS] GroupUpdateB leader name is not null terminated within size [{}]", in->size);
			delete in;
			return;
		}

		Entry entries[MAX_GROUP_MEMBERS];
		for (uint32 i = 0; i < count; ++i) {
			Entry& e = entries[i];
			memset(&e, 0, sizeof(e));

			if (pos + 4 > in->size) {
				LogNetcode("[STRUCTS] GroupUpdateB entry [{}] slot truncated at size [{}]", i, in->size);
				delete in;
				return;
			}
			memcpy(&e.slot, in->pBuffer + pos, 4);
			pos += 4;

			if (!read_string(pos, e.name)) {
				LogNetcode("[STRUCTS] GroupUpdateB entry [{}] name truncated at size [{}]", i, in->size);
				delete in;
				return;
			}

			if (pos + 2 > in->size) {
				LogNetcode("[STRUCTS] GroupUpdateB entry [{}] type truncated at size [{}]", i, in->size);
				delete in;
				return;
			}
			memcpy(&e.type, in->pBuffer + pos, 2);
			pos += 2;

			if (!read_string(pos, e.owner)) {
				LogNetcode("[STRUCTS] GroupUpdateB entry [{}] owner name truncated at size [{}]", i, in->size);
				delete in;
				return;
			}

			if (pos + 4 > in->size) {
				LogNetcode("[STRUCTS] GroupUpdateB entry [{}] level truncated at size [{}]", i, in->size);
				delete in;
				return;
			}
			memcpy(&e.level, in->pBuffer + pos, 4);
			pos += 4;

			// EQEmu writes 3 role bytes, the client reads 5. Take what is there, zero the rest.
			for (uint32 r = 0; r < 5; ++r) {
				if (pos < in->size) {
					e.roles[r] = in->pBuffer[pos];
					pos += 1;
				}
			}

			if (pos + 4 > in->size) {
				LogNetcode("[STRUCTS] GroupUpdateB entry [{}] offline truncated at size [{}]", i, in->size);
				delete in;
				return;
			}
			memcpy(&e.offline, in->pBuffer + pos, 4);
			pos += 4;

			if (pos + 4 > in->size) {
				LogNetcode("[STRUCTS] GroupUpdateB entry [{}] timestamp truncated at size [{}]", i, in->size);
				delete in;
				return;
			}
			uint32 ts32 = 0;
			memcpy(&ts32, in->pBuffer + pos, 4);
			pos += 4;
			e.timestamp = ts32;

			// Optional tail. Group::SendLaurionRoster carries it; Client::SendGroupCreatePacket does not,
			// so it stays 0 for that path. This is the spawn id OP_ZoneEntry writes as the HashKey the client
			// resolves through PlayerManagerClient::GetPlayerFromHashKey - without it a roster entry cannot be
			// bound to a spawn.
			e.unique_id = 0;
			if (pos + 4 <= in->size) {
				memcpy(&e.unique_id, in->pBuffer + pos, 4);
				pos += 4;
			}
		}

		SerializeBuffer buffer;
		buffer.WriteUInt32(group_id);
		buffer.WriteUInt32(count);
		buffer.WriteString(leader);
		for (uint32 i = 0; i < count; ++i) {
			const Entry& e = entries[i];
			buffer.WriteUInt32(e.slot);
			buffer.WriteString(e.name);
			buffer.WriteUInt16(e.type);
			buffer.WriteString(e.owner);
			buffer.WriteUInt32(e.level);
			for (uint32 r = 0; r < 5; ++r) buffer.WriteUInt8(e.roles[r]);
			buffer.WriteUInt32(e.offline);
			buffer.WriteUInt64(e.timestamp);
			buffer.WriteUInt32(e.unique_id);
		}

		in->size = buffer.size();
		in->pBuffer = new unsigned char[buffer.size()];
		memcpy(in->pBuffer, buffer.buffer(), buffer.size());

		dest->FastQueuePacket(&in, ack_req);
	}

	// DECODE methods
	DECODE(OP_Animation)
	{
		DECODE_LENGTH_EXACT(structs::Animation_Struct);
		SETUP_DIRECT_DECODE(Animation_Struct, structs::Animation_Struct);

		IN(spawnid);
		IN(action);
		IN(speed);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ApplyPoison)
	{
		DECODE_LENGTH_EXACT(structs::ApplyPoison_Struct);
		SETUP_DIRECT_DECODE(ApplyPoison_Struct, structs::ApplyPoison_Struct);

		emu->inventorySlot = LaurionToServerTypelessSlot(eq->inventorySlot, invtype::typePossessions);
		IN(success);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_AugmentInfo)
	{
		DECODE_LENGTH_EXACT(structs::AugmentInfo_Struct);
		SETUP_DIRECT_DECODE(AugmentInfo_Struct, structs::AugmentInfo_Struct);

		IN(itemid);
		IN(window);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_AugmentItem)
	{
		DECODE_LENGTH_EXACT(structs::AugmentItem_Struct);
		SETUP_DIRECT_DECODE(AugmentItem_Struct, structs::AugmentItem_Struct);

		emu->container_slot = LaurionToServerSlot(eq->container_slot);
		emu->augment_slot = LaurionToServerSlot(eq->augment_slot);
		emu->container_index = eq->container_index;
		emu->augment_index = eq->augment_index;
		emu->dest_inst_id = eq->dest_inst_id;
		emu->augment_action = eq->augment_action;

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_BlockedBuffs)
	{
		DECODE_LENGTH_EXACT(structs::BlockedBuffs_Struct);
		SETUP_DIRECT_DECODE(BlockedBuffs_Struct, structs::BlockedBuffs_Struct);

		for (uint32 i = 0; i < BLOCKED_BUFF_COUNT; ++i)
			emu->SpellID[i] = eq->SpellID[i];

		IN(Count);
		IN(Pet);
		IN(Initialise);
		IN(Flags);

		FINISH_DIRECT_DECODE();
	}

	static_assert(sizeof(structs::CastSpell_Struct) == 39, "OP_0x325b CastSpell payload is 39 bytes");

	DECODE(OP_CastSpell)
	{
		DECODE_LENGTH_EXACT(structs::CastSpell_Struct);
		SETUP_DIRECT_DECODE(CastSpell_Struct, structs::CastSpell_Struct);

		const EQ::spells::CastingSlot server_slot =
			LaurionToServerCastingSlot(static_cast<spells::CastingSlot>(eq->slot));
		emu->slot = static_cast<uint32>(server_slot);

		IN(spell_id);
		// Handle_OP_CastSpell only consults inventoryslot for CastingSlot::Item casts: it looks the item up,
		// compares item->Click.Effect against spell_id, fires the click-cast quest events and burns a charge.
		// It was pinned to -1 here, so SupportsClickCasting(-1) was always false and every cast from an
		// inventory item fell into the "inventoryslot >= slotCursor" error branch - clicky items could not be
		// used. The encoder at ENCODE(OP_CastSpell) already converts the other direction, so the wire record
		// is real; only the decode was dropping it. For a gem cast the client leaves the record zeroed, and
		// translating zeros would invent a bag slot out of nothing, so keep 0xFFFF (normal cast) there.
		if (server_slot == EQ::spells::CastingSlot::Item) {
			emu->inventoryslot = LaurionToServerSlot(
				LaurionCastingInventorySlotToInventorySlot(eq->inventory_slot));
		}
		else {
			emu->inventoryslot = 0xFFFF;
		}
		IN(target_id);
		IN(x_pos);
		IN(y_pos);
		IN(z_pos);
		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ChannelMessage)
	{
		unsigned char* __eq_buffer = __packet->pBuffer;

		char* InBuffer = (char*)__eq_buffer;

		char Sender[64];
		char Target[64];

		VARSTRUCT_DECODE_STRING(Sender, InBuffer);
		VARSTRUCT_DECODE_STRING(Target, InBuffer);

		//packet seems the same as rof2 with 4 more empty bytes before language
		InBuffer += 8;

		uint32 Language = VARSTRUCT_DECODE_TYPE(uint32, InBuffer);
		uint32 Channel = VARSTRUCT_DECODE_TYPE(uint32, InBuffer);

		InBuffer += 5;

		uint32 Skill = VARSTRUCT_DECODE_TYPE(uint32, InBuffer);

		std::string old_message = InBuffer;
		std::string new_message;
		LaurionToServerConvertLinks(new_message, old_message);

		__packet->size = sizeof(ChannelMessage_Struct) + new_message.length() + 1;
		__packet->pBuffer = new unsigned char[__packet->size];
		ChannelMessage_Struct* emu = (ChannelMessage_Struct*)__packet->pBuffer;

		strn0cpy(emu->targetname, Target, sizeof(emu->targetname));
		strn0cpy(emu->sender, Sender, sizeof(emu->sender));
		emu->language = Language;
		emu->chan_num = Channel;
		emu->skill_in_language = Skill;
		strcpy(emu->message, new_message.c_str());

		delete[] __eq_buffer;
	}

	DECODE(OP_ClickDoor)
	{
		DECODE_LENGTH_EXACT(structs::ClickDoor_Struct);
		SETUP_DIRECT_DECODE(ClickDoor_Struct, structs::ClickDoor_Struct);

		IN(doorid);
		IN(player_id);

		FINISH_DIRECT_DECODE();
	}

	// Laurion widened the click packet from 8 to 12 bytes - see structs::ClickObject_Struct.
	// Without this decoder Handle_OP_ClickObject saw size 12 against an 8 byte ClickObject_Struct,
	// logged "Invalid size on ClickObject_Struct" and dropped the packet, so clicking a ground item
	// opened the inventory window but nothing ever landed on the cursor.
	DECODE(OP_ClickObject)
	{
		DECODE_LENGTH_EXACT(structs::ClickObject_Struct);
		SETUP_DIRECT_DECODE(ClickObject_Struct, structs::ClickObject_Struct);

		IN(drop_id);
		IN(player_id);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ClientUpdate)
	{
		// for some odd reason, there is an extra byte on the end of this on occasion..
		DECODE_LENGTH_ATLEAST(structs::PlayerPositionUpdateClient_Struct);
		SETUP_DIRECT_DECODE(PlayerPositionUpdateClient_Struct, structs::PlayerPositionUpdateClient_Struct);

		IN(spawn_id);
		IN(vehicle_id);
		IN(sequence);
		emu->x_pos = eq->position.x;
		emu->y_pos = eq->position.y;
		emu->z_pos = eq->position.z;
		emu->heading = eq->position.heading;
		emu->delta_x = eq->position.delta_x;
		emu->delta_y = eq->position.delta_y;
		emu->delta_z = eq->position.delta_z;
		emu->delta_heading = eq->position.delta_heading;
		emu->animation = eq->position.animation;

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_Consider)
	{
		DECODE_LENGTH_EXACT(structs::Consider_Struct);
		SETUP_DIRECT_DECODE(Consider_Struct, structs::Consider_Struct);

		IN(playerid);
		IN(targetid);
		IN(faction);
		IN(level);
		//emu->cur_hp = 1;
		//emu->max_hp = 2;
		//emu->pvpcon = 0;

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ConsiderCorpse) { DECODE_FORWARD(OP_Consider); }

	DECODE(OP_CharacterCreate)
	{
		// Incoming body is 168 bytes (opcode 0x6a3c), the emu struct is 92. Only the fields that
		// are directly evidenced by the client send site FUN_140380ed0 are mapped; the unmapped
		// appearance / unknown fields are left zero. See docs/OP_CharacterCreatePacket.md.
		DECODE_LENGTH_EXACT(structs::CharCreate_Struct);
		SETUP_DIRECT_DECODE(CharCreate_Struct, structs::CharCreate_Struct);

		IN(gender);
		IN(race);
		IN(class_);
		IN(deity);
		IN(start_zone);
		IN(STR);
		IN(STA);
		IN(AGI);
		IN(DEX);
		IN(WIS);
		IN(INT);
		IN(CHA);

		// The tutorial request is not at CharCreate_Struct::tutorial (offset 88) on this client.
		// Live capture 2026-10-13 (opcode 0x6a3c, 168-byte body, send site FUN_140380ed0):
		// body +160 = CCharacterCreation + 0x3874, the Tutorial checkbox. Verified against a male
		// dwarf berserker: +72 gender 0, +76 race 8, +80 class 16, +84 deity 396, +160 = 01.
		emu->tutorial = eq->unknown160;

		// char_name is not carried on the 0x6a3c path and the emu CharCreate_Struct has no name
		// field; the world handler takes the name from OP_ApproveName.

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_Damage)
	{
		DECODE_LENGTH_EXACT(structs::CombatDamage_Struct);
		SETUP_DIRECT_DECODE(CombatDamage_Struct, structs::CombatDamage_Struct);

		IN(target);
		IN(source);
		IN(type);
		IN(spellid);

		// structs::CombatDamage_Struct::damage is int64 and the client branches on its sign
		// (JNS at 0x140202EBB in FUN_140202DF0). The emu field is int32, so saturate instead of
		// wrapping - a wrapped negative would reach every other client as a huge positive hit.
		// The client itself clamps resulting HP to +/-10000000 (FUN_1400F7DE0), so this clamp is
		// not expected to trigger in normal play.
		int64 dmg = eq->damage;
		if (dmg > INT32_MAX) {
			LogNetcode("[OP_Damage] damage [{}] clamped to INT32_MAX", dmg);
			dmg = INT32_MAX;
		} else if (dmg < INT32_MIN) {
			LogNetcode("[OP_Damage] damage [{}] clamped to INT32_MIN", dmg);
			dmg = INT32_MIN;
		}
		emu->damage = static_cast<int32>(dmg);

		IN(force);
		IN(hit_heading);
		IN(hit_pitch);
		IN(special);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_DeleteItem)
	{
		DECODE_LENGTH_EXACT(structs::DeleteItem_Struct);
		SETUP_DIRECT_DECODE(DeleteItem_Struct, structs::DeleteItem_Struct);

		emu->from_slot = LaurionToServerSlot(eq->from_slot);
		emu->to_slot = LaurionToServerSlot(eq->to_slot);
		IN(number_in_stack);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_EnterWorld)
	{
		DECODE_LENGTH_EXACT(structs::EnterWorld_Struct);
		SETUP_DIRECT_DECODE(EnterWorld_Struct, structs::EnterWorld_Struct);

		memcpy(emu->name, eq->name, sizeof(emu->name));

		// Live capture 2026-10-13 (opcode 0x6691, 72-byte payload, send site FUN_140257f10):
		// the flags are single bytes, not dwords.
		//   +64 = CCharacterListWnd + 0x3bc -> tutorial request
		//   +65 = CCharacterListWnd + 0x3bd -> return-home request
		//   +66 = "either flag was set" byte
		//   +68..71 = DAT_140e3bf14, always 0xFFFFFFFF - a sentinel, not a flag
		// Reading unknown2 as a dword would make return_home = 0xFFFFFFFF and trip the GoHome
		// branch in HandleEnterWorldPacket before the tutorial check, disconnecting the client.
		emu->tutorial = eq->unknown1 & 0xFF;
		emu->return_home = (eq->unknown1 >> 8) & 0xFF;

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GMTraining)
	{
		DECODE_LENGTH_EXACT(structs::GMTrainee_Struct);
		SETUP_DIRECT_DECODE(GMTrainee_Struct, structs::GMTrainee_Struct);

		IN(npcid);
		IN(playerid);

		for (int i = 0; i < 100; ++i) {
			IN(skills[i]);
		}

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GroupCancelInvite)
	{
		// The client sends this from two places: FUN_140257290 when the invitee declines (subcommand 3,
		// after printing eqstr 12289 "You cancel the invitation to join %1's group.") and FUN_1402827f0 when
		// an invite arrives that the client will not accept (1 already invited, 4 in a raid, 5 raid group
		// leader). name1 = inviter, name2 = invitee, subcommand at +152.
		// Handle_OP_GroupCancelInvite requires exactly sizeof(GroupCancel_Struct) = 129 and forwards the
		// packet to the inviter, so normalise 156 -> 129 here and let ENCODE(OP_GroupCancelInvite) rebuild
		// the Laurion shape on the way out to the inviter's client.
		DECODE_LENGTH_EXACT(structs::GroupCancelInvite_Struct);
		SETUP_DIRECT_DECODE(GroupCancel_Struct, structs::GroupCancelInvite_Struct);

		memcpy(emu->name1, eq->name1, sizeof(emu->name1));
		memcpy(emu->name2, eq->name2, sizeof(emu->name2));
		emu->toggle = (uint8)eq->subcommand;

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GroupDisband)
	{
		// Laurion sends 152 payload bytes for 0x78ef (two names + 24 byte tail), not the 128 byte
		// GroupGeneric_Struct. Accept the Laurion layout and normalise down to the emu struct so
		// Handle_OP_GroupDisband sees the size it expects. Without this the disband request is rejected and
		// Group::DisbandGroup() never runs, so the client never receives the clear.
		DECODE_LENGTH_EXACT(structs::GroupDisband_Struct);
		SETUP_DIRECT_DECODE(GroupGeneric_Struct, structs::GroupDisband_Struct);

		memcpy(emu->name1, eq->name1, sizeof(emu->name1));
		memcpy(emu->name2, eq->name2, sizeof(emu->name2));

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GroupInvite)
	{
		// Laurion sends 152 payload bytes here (two names + 24 byte tail), not the 128 byte
		// GroupGeneric_Struct. Accept the Laurion layout and normalise the packet down to the
		// 128 byte emu struct so Handle_OP_GroupInvite2 sees the size it expects.
		DECODE_LENGTH_EXACT(structs::GroupInvite_Struct);
		SETUP_DIRECT_DECODE(GroupGeneric_Struct, structs::GroupInvite_Struct);

		memcpy(emu->name1, eq->invitee_name, sizeof(emu->name1));
		memcpy(emu->name2, eq->inviter_name, sizeof(emu->name2));

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GroupInvite2)
	{
		DECODE_FORWARD(OP_GroupInvite);
	}

	DECODE(OP_MemorizeSpell)
	{
		DECODE_LENGTH_EXACT(structs::MemorizeSpell_Struct);
		SETUP_DIRECT_DECODE(MemorizeSpell_Struct, structs::MemorizeSpell_Struct);

		// Translate the Laurion wire action back into EQEmu's semantic vocabulary so
		// Client::OPMemorizeSpell() sees the intent instead of the wire number. Without this the
		// client's memorize (wire 2) is read as memSpellForget and every memorize request unmemorizes
		// the slot it was meant to fill.
		emu->scribing = LaurionSpellAction::FromWire(eq->action);

		IN(slot);
		IN(spell_id);
		IN(reduction);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ItemVerifyRequest)
	{
		DECODE_LENGTH_EXACT(structs::ItemVerifyRequest_Struct);
		SETUP_DIRECT_DECODE(ItemVerifyRequest_Struct, structs::ItemVerifyRequest_Struct);

		// Laurion sends the whole InventorySlot_Struct (12 bytes) where EQEmu's canonical struct has a
		// single int32 slot id. Collapse it back into EQEmu's RoF2-based slot space.
		emu->slot = static_cast<int32>(LaurionToServerSlot(eq->inventory_slot));

		IN(target);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_BookButton)
	{
		DECODE_LENGTH_EXACT(structs::BookButton_Struct);
		SETUP_DIRECT_DECODE(BookButton_Struct, structs::BookButton_Struct);

		// BookWindow sends the full ItemGlobalIndex (top slot, sub slot, aug index) where EQEmu's
		// canonical struct has a single int16.
		structs::TypelessInventorySlot_Struct laurion_slot{};
		laurion_slot.Slot = eq->top_slot;
		laurion_slot.SubIndex = eq->sub_slot;
		laurion_slot.AugIndex = static_cast<int16>(eq->aug_index);
		laurion_slot.Padding = 0;
		emu->invslot = LaurionToServerTypelessSlot(laurion_slot, EQ::invtype::typePossessions);
		emu->target_id = eq->target_id;

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ReadBook)
	{
		DECODE_LENGTH_EXACT(structs::BookRequest_Struct);
		SETUP_DIRECT_DECODE(BookRequest_Struct, structs::BookRequest_Struct);

		// EQEmu's canonical struct is 28 bytes (single-byte window and type, book name capped at 20),
		// so map the wide Laurion fields back into it.
		emu->window = static_cast<uint8>(eq->window);
		emu->type = static_cast<uint8>(eq->book_type);
		emu->target_id = eq->target_id;

		structs::TypelessInventorySlot_Struct laurion_slot{};
		laurion_slot.Slot = eq->top_slot;
		laurion_slot.SubIndex = eq->sub_slot;
		laurion_slot.AugIndex = static_cast<int16>(eq->aug_index);
		laurion_slot.Padding = 0;
		emu->invslot = LaurionToServerTypelessSlot(laurion_slot, EQ::invtype::typePossessions);

		// +19/+20 are the book window's Scribe and Cast Spell button flags. EQEmu's canonical
		// BookRequest_Struct has no room for them, so they are only carried on the reply
		// (BookText_Struct) that Client::ReadBook() builds.
		strn0cpy(emu->txtfile, eq->text, sizeof(emu->txtfile));

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_MoveItem)
	{
		DECODE_LENGTH_EXACT(structs::MoveItem_Struct);
		SETUP_DIRECT_DECODE(MoveItem_Struct, structs::MoveItem_Struct);

		LogNetcodeDetail("Laurion::DECODE(OP_MoveItem)");

		emu->from_slot = LaurionToServerSlot(eq->from_slot);
		emu->to_slot = LaurionToServerSlot(eq->to_slot);
		IN(number_in_stack);

		FINISH_DIRECT_DECODE();
	}

	// Variable length: count + count * 36.  The wire entry layout matches EQEmu's
	// MultiMoveItemSub_Struct, so the packet is translated in place - only the slot numbering differs.
	// Without this decode the handler receives the client's own slot numbers (cursor = 35, general
	// slots 23-34), which do not line up with EQEmu's canonical model and every inventory move fails.
	DECODE(OP_MoveMultipleItems)
	{
		DECODE_LENGTH_ATLEAST(structs::MultiMoveItem_Struct);

		structs::MultiMoveItem_Struct* eq = (structs::MultiMoveItem_Struct*)__packet->pBuffer;
		uint32 count = eq->count;
		uint32 expected = sizeof(structs::MultiMoveItem_Struct) + count * sizeof(structs::MultiMoveItemSub_Struct);

		if (__packet->size != expected) {
			LogNetcode("Laurion::DECODE(OP_MoveMultipleItems): size [{}] does not match count [{}] (expected [{}])", __packet->size, count, expected);
			__packet->SetOpcode(OP_Unknown);
			return;
		}

		LogNetcodeDetail("Laurion::DECODE(OP_MoveMultipleItems) count [{}]", count);

		for (uint32 i = 0; i < count; i++) {
			if (eq->moves[i].from_slot.Type == invtype::typePossessions) {
				eq->moves[i].from_slot.Slot = LaurionGeneralSlotToServer(eq->moves[i].from_slot.Slot);
			}
			if (eq->moves[i].to_slot.Type == invtype::typePossessions) {
				eq->moves[i].to_slot.Slot = LaurionGeneralSlotToServer(eq->moves[i].to_slot.Slot);
			}
		}
	}

	DECODE(OP_RemoveBlockedBuffs) { DECODE_FORWARD(OP_BlockedBuffs); }

	DECODE(OP_SetServerFilter)
	{
		DECODE_LENGTH_EXACT(structs::SetServerFilter_Struct);
		SETUP_DIRECT_DECODE(SetServerFilter_Struct, structs::SetServerFilter_Struct);

		int r;
		for (r = 0; r < 29; r++) {
			// Size 68 in Laurion
			IN(filters[r]);
		}

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ShopPlayerBuy)
	{
		DECODE_LENGTH_EXACT(structs::Merchant_Sell_Request_Struct);
		SETUP_DIRECT_DECODE(Merchant_Sell_Struct, structs::Merchant_Sell_Request_Struct);

		IN(npcid);
		IN(playerid);
		// eq->itemslot is the client's 64 bit ItemBase::MerchantSlot; the merchant list slot always
		// fits in the low 32 bits, the high half is zero.
		emu->itemslot = static_cast<uint32>(eq->itemslot);
		IN(quantity);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ShopPlayerSell)
	{
		DECODE_LENGTH_EXACT(structs::Merchant_Purchase_Request_Struct);
		SETUP_DIRECT_DECODE(Merchant_Purchase_Struct, structs::Merchant_Purchase_Request_Struct);

		IN(npcid);
		emu->itemslot = LaurionToServerTypelessSlot(eq->inventory_slot, invtype::typePossessions);
		IN(quantity);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ShopSendParcel)
	{
		DECODE_LENGTH_EXACT(structs::Parcel_Request_Struct);
		SETUP_DIRECT_DECODE(Parcel_Struct, structs::Parcel_Request_Struct);

		IN(npc_id);
		// Rebuild the full slot struct from the split fields so the flat canonical item_slot is right
		// for bag contents and for Laurion's two extra general slots.
		structs::TypelessInventorySlot_Struct laurion_slot{};
		laurion_slot.Slot = eq->slot;
		laurion_slot.SubIndex = eq->sub;
		laurion_slot.AugIndex = eq->aug;
		laurion_slot.Padding = 0;
		emu->item_slot = LaurionToServerTypelessSlot(laurion_slot, invtype::typePossessions);
		IN(quantity);
		IN(money_flag);
		IN_str(send_to);
		IN_str(note);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ShopItem)
	{
		DECODE_LENGTH_EXACT(structs::MerchantSellItem_Request_Struct);
		SETUP_DIRECT_DECODE(MerchantSellItem_Struct, structs::MerchantSellItem_Request_Struct);

		IN(npcid);
		// The client only carries slot + subindex here, so rebuild the full struct before converting.
		structs::TypelessInventorySlot_Struct laurion_slot{};
		laurion_slot.Slot = eq->slot;
		laurion_slot.SubIndex = eq->subindex;
		laurion_slot.AugIndex = -1;
		laurion_slot.Padding = 0;
		emu->itemslot = LaurionToServerTypelessSlot(laurion_slot, invtype::typePossessions);
		IN(item_id);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ShopSellItemBulk)
	{
		DECODE_LENGTH_EXACT(structs::MerchantSellItemBulk_Request_Struct);
		SETUP_DIRECT_DECODE(MerchantSellItemBulk_Struct, structs::MerchantSellItemBulk_Request_Struct);

		IN(slot_index);
		IN(npcid);
		IN(container0d4);
		IN(container0d8);
		IN(padding);
		IN(item_id);
		IN(price);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ShopRequestItem)
	{
		DECODE_LENGTH_EXACT(structs::MerchantRequestItem_Request_Struct);
		SETUP_DIRECT_DECODE(MerchantRequestItem_Struct, structs::MerchantRequestItem_Request_Struct);

		IN(ldtype);
		IN(spawn_id);
		IN(item_id);
		IN(quantity);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ShopRequest)
	{
		DECODE_LENGTH_EXACT(structs::MerchantClickRequest_Struct);
		SETUP_DIRECT_DECODE(MerchantClick_Struct, structs::MerchantClickRequest_Struct);

		IN(npc_id);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_SpawnAppearance) {
		DECODE_LENGTH_EXACT(structs::SpawnAppearance_Struct);
		SETUP_DIRECT_DECODE(SpawnAppearance_Struct, structs::SpawnAppearance_Struct);
		static_assert(sizeof(structs::SpawnAppearance_Struct) == 24, "OP_0x4eb0 SpawnAppearance payload is 24 bytes");

		IN(spawn_id);
		// eq->type is the low 16 bits only. The client builds spawn_id+type+parameter with a single
		// 16 byte MOVUPS off its stack frame, so the two bytes above the type are stack residue.
		// Reading them as part of the type made every packet that happened to have a dirty stack slot
		// log as "unmapped Laurion type [1952645156]" when the real type was 36.
		emu->type = LaurionToServerSpawnAppearanceType(eq->type, eq->parameter);
		IN(parameter);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_TradeSkillCombine)
	{
		DECODE_LENGTH_EXACT(structs::NewCombine_Struct);
		SETUP_DIRECT_DECODE(NewCombine_Struct, structs::NewCombine_Struct);

		emu->container_slot = LaurionToServerSlot(eq->container_slot);
		emu->guildtribute_slot = LaurionToServerSlot(eq->guildtribute_slot); // this should only return INVALID_INDEX until implemented

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_WearChange)
	{
		DECODE_LENGTH_EXACT(structs::WearChange_Struct);
		SETUP_DIRECT_DECODE(WearChange_Struct, structs::WearChange_Struct);

		IN(spawn_id);
		emu->wear_slot_id = eq->wear_slot_id;
		emu->material = eq->armor_id;
		emu->unknown06 = eq->variation;
		emu->elite_material = eq->material;
		emu->hero_forge_model = eq->new_armor_id;
		emu->unknown18 = eq->new_armor_type;
		emu->color.Color = eq->color;

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ZoneEntry)
	{
		DECODE_LENGTH_EXACT(structs::ClientZoneEntry_Struct);
		SETUP_DIRECT_DECODE(ClientZoneEntry_Struct, structs::ClientZoneEntry_Struct);

		memcpy(emu->char_name, eq->char_name, sizeof(emu->char_name));

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ZoneChange)
	{
		DECODE_LENGTH_EXACT(structs::ZoneChange_Struct);
		SETUP_DIRECT_DECODE(ZoneChange_Struct, structs::ZoneChange_Struct);

		memcpy(emu->char_name, eq->char_name, sizeof(emu->char_name));
		IN(zoneID);
		IN(instanceID);
		IN(y);
		IN(x);
		IN(z)
			IN(zone_reason);
		IN(success);

		FINISH_DIRECT_DECODE();
	}

	// ---------------------------------------------------------------------------
	// SoF x64 zone opcodes (client -> server). Layouts come from the client send
	// sites in eqgame.exe (Ghidra SoF project); see utils/patches/patch_Laurion.conf.
	// ---------------------------------------------------------------------------

	DECODE(OP_ZoneConnectRequest) // 0x6d2d
	{
		DECODE_LENGTH_EXACT(structs::ZoneConnectRequest_Struct);
		SETUP_DIRECT_DECODE(ZoneConnectRequest_Struct, structs::ZoneConnectRequest_Struct);

		IN(flag);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_LootItem) // 0x0856
	{
		// Laurion widened slot_id to a uint32, which shifts everything after it by two bytes.  EQEmu's
		// auto_loot lives at wire offset 12 (CLootWnd__RequestLootSlot param_3 - 1 for a loot window click
		// and for LootAll, 0 when the client is happy to leave the item on the cursor).  Laurion's extra
		// uint32 at offset 16 is the requested stack quantity, which EQEmu has no room for and ignores.
		DECODE_LENGTH_EXACT(structs::LootingItem_Struct);
		SETUP_DIRECT_DECODE(LootingItem_Struct, structs::LootingItem_Struct);

		IN(lootee);
		IN(looter);
		// The client echoes back the main_slot it was handed for the row, which is a corpse container index
		// (0..CORPSE_SIZE-1), not EQEmu's canonical corpse id.
		emu->slot_id = static_cast<uint16>(LaurionToServerCorpseMainSlot(eq->slot_id));
		emu->unknown3[0] = 0;
		emu->unknown3[1] = 0;
		IN(auto_loot);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ItemLinkResponse) // 0x609c
	{
		// CItemDisplayWnd::SendItemLink (FUN_14040fbf0) sends the item stats request on the same opcode
		// the client reads the reply on. EQEmu resolves 0x609c to OP_ItemLinkResponse, so this decoder is
		// what sees the incoming request, and Handle_OP_ItemLinkResponse then expects the fixed size
		// LDONItemViewRequest_Struct. Rebuild that from the variable length wire form.
		DECODE_LENGTH_ATLEAST(structs::ItemDisplayRequest_Struct);

		unsigned char* __eq_buffer = __packet->pBuffer;
		uint32 wire_size = __packet->size;
		char* InBuffer = (char*)__eq_buffer;

		uint32 item_number = VARSTRUCT_DECODE_TYPE(uint32, InBuffer);
		uint32 maker_id = VARSTRUCT_DECODE_TYPE(uint32, InBuffer);
		uint32 name_length = VARSTRUCT_DECODE_TYPE(uint32, InBuffer);

		__packet->size = sizeof(LDONItemViewRequest_Struct);
		__packet->pBuffer = new unsigned char[__packet->size] {};
		LDONItemViewRequest_Struct* emu = (LDONItemViewRequest_Struct*)__packet->pBuffer;

		// The name is length prefixed and not null terminated, so clamp it to what actually arrived.
		if (name_length > wire_size - 12)
			name_length = wire_size - 12;
		if (name_length > sizeof(emu->item_name) - 1)
			name_length = sizeof(emu->item_name) - 1;

		emu->item_id = item_number;
		memcpy(emu->unknown004, &maker_id, sizeof(emu->unknown004));
		memcpy(emu->item_name, InBuffer, name_length);

		FINISH_DIRECT_DECODE();
	}

	// AdvLoot inspect request. CAdvancedLootWnd alt+right-click on the item-name column of a loot list:
	// FUN_1400A6370 -> FUN_1400A6250 -> FUN_14046C5C0(DAT_140d83170 /*ItemDisplayWnd singleton*/,
	// AdvLoot row record +0x00 /*item id*/), then Show(bShow=1, bFocus=1) on that window through vtable
	// slot +0x1C0. The client arms the window locally, so it opens either way; what was missing is the
	// server reply that fills it. EQEmu had no mapping for 0x6159 (patch_Laurion.conf left it 0x0000) and
	// no decoder, so the request was dropped as OP_Unknown. Rebuild the fixed ItemViewRequest_Struct that
	// Handle_OP_ItemLinkClick already expects, with the augments cleared: the AdvLoot row record carries no
	// augment data, and copying the wire body verbatim would hand 0xFFFFFFFF to CreateItem as augment 2.
	DECODE(OP_ItemLinkClick) // 0x6159
	{
		DECODE_LENGTH_EXACT(structs::ItemDisplayWndRequest_Struct);
		SETUP_DIRECT_DECODE(ItemViewRequest_Struct, structs::ItemDisplayWndRequest_Struct);
		MEMSET_IN(ItemViewRequest_Struct);

		emu->item_id = eq->item_id;

		FINISH_DIRECT_DECODE();
	}

	// AdvLoot inspect request (the other half of the inspect surface). CAdvancedLootWnd alt+left-click on
	// the item-name column: FUN_1400A4020 checks column == 0, reads the row's item data, resolves the
	// AdvLoot record at index * 0x98 + this+8, builds an item link string through FUN_1405607B0 and calls
	// FUN_14040FA60, which sends wire opcode 0x0497 with an 80 byte body. EQEmu had no mapping for 0x0497
	// so it was dropped as OP_Unknown. The first 72 bytes of that body are exactly what the client's own
	// 0x609c item-view reply decoder expects, so truncate to LDONItemViewRequest_Struct and reuse
	// Handle_OP_ItemLinkResponse, which does CreateItem(item_id) and sends ItemPacketViewLink.
	DECODE(OP_ItemLinkInspect) // 0x0497
	{
		DECODE_LENGTH_EXACT(structs::ItemLinkInspect_Struct);
		SETUP_DIRECT_DECODE(LDONItemViewRequest_Struct, structs::ItemLinkInspect_Struct);

		emu->item_id = eq->item_id;

		FINISH_DIRECT_DECODE();
	}

	// Saylink click. CChatWindow__WndNotification (FUN_1403948d0) handles WndNotify 0x1b, the link-click
	// event, parses the 90 character link data with FUN_1402357e0 into an 84 byte record, ORs the keyboard
	// modifier flags into its +0x20 dword and sends it. Three other window handlers (FUN_140482580,
	// FUN_14048a080, FUN_1404f3340) emit the same record, and 0x1e67 has no receive case anywhere, so the
	// client expects no reply on it - it is the "the player clicked this link" packet.
	//
	// The record's first 36 bytes are ItemViewRequest_Struct field for field (item_id, augments[6],
	// link_hash, then the click flags where EQEmu has unknown028), so rebuild that struct here and let
	// Handle_OP_ItemLinkClick do the work. That handler is what makes the silent quest links run: a link
	// built by Saylink::Silent carries SAYLINK_ITEM_ID with the saylink row id in augments[1] (augments[0]
	// for non silent), the handler reads the stored phrase out of the saylink table and hands it to
	// ChannelMessageReceived on the say channel, which dispatches it as a # command. That is the whole
	// reason saylinks are the way to run server commands on this client: InterpretCmd (0x14026322c) throws
	// away any chat line that starts with '#' before it reaches a send site.
	DECODE(OP_SayLinkClick) // 0x1e67
	{
		DECODE_LENGTH_EXACT(structs::SayLinkClick_Struct);
		SETUP_DIRECT_DECODE(ItemViewRequest_Struct, structs::SayLinkClick_Struct);
		MEMSET_IN(ItemViewRequest_Struct);

		emu->item_id    = eq->item_id;
		emu->link_hash  = eq->hash;
		emu->unknown028 = eq->click_flags;
		for (int i = 0; i < 6; ++i) {
			emu->augments[i] = eq->augments[i];
		}

		FINISH_DIRECT_DECODE();
	}

	static_assert(sizeof(structs::Consume_Struct) == 20, "OP_0x5ef7 Consume payload is 20 bytes");

	DECODE(OP_Consume) // 0x5ef7
	{
		// Laurion widened the packet to 20 bytes. The first twelve bytes are the twelve byte
		// InventorySlot_Struct (Type, Padding1, Slot, SubIndex, AugIndex, Padding2) that identifies the item,
		// followed by uint32 auto_consumed (0xFFFFFFFF) and uint32 type.
		// The previous model had auto_consumed at +0x0A and type at +0x0E, which came to 18 bytes: the size
		// gate rejected every packet, the opcode became OP_Unknown, and Handle_OP_Consume never ran - the
		// client ate and drank locally while hunger/thirst only ever moved through OP_Stamina.
		//
		// Two client code paths build this packet, and they encode the type field differently:
		//   FUN_1402ca370  auto eat/drink. Scans possessions slots 0x17..0x22 (the twelve general slots),
		//                  opens bags, and matches item type 0xe when its param_2 selector is 0 and 0xf
		//                  when it is 1. It sends that selector straight into the type field: 0 = food,
		//                  1 = water. This is the path that fires on the hunger/thirst timer, so it is what
		//                  the server sees in normal play.
		//   FUN_1401d4020  right-click / use the item. Writes 0x100 for food and 0x101 for water.
		// EQEmu's Consume_Struct is 16 bytes and expects 0x01 = food, 0x02 = water, so map both encodings.
		DECODE_LENGTH_EXACT(structs::Consume_Struct);
		SETUP_DIRECT_DECODE(Consume_Struct, structs::Consume_Struct);

		emu->slot = LaurionToServerSlot(eq->slot);
		emu->auto_consumed = eq->auto_consumed;
		emu->c_unknown1[0] = 0;
		emu->c_unknown1[1] = 0;
		emu->c_unknown1[2] = 0;
		emu->c_unknown1[3] = 0;
		// Map only the values the client actually sends. The previous form folded every other value into
		// food, which would have credited hunger for an unknown item type.
		switch (eq->type) {
			case 0x00000000:  // FUN_1402ca370, param_2 == 0, item type 0xe
			case 0x00000100:  // FUN_1401d4020, food
				emu->type = 0x01;
				break;
			case 0x00000001:  // FUN_1402ca370, param_2 == 1, item type 0xf
			case 0x00000101:  // FUN_1401d4020, water
				emu->type = 0x02;
				break;
			default:
				LogNetcode("OP_Consume: unrecognized type value, type:[{:08x}]", eq->type);
				emu->type = 0;
				break;
		}
		emu->unknown13[0] = 0;
		emu->unknown13[1] = 0;
		emu->unknown13[2] = 0;

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_SkillRankReport) // 0x3fe5
	{
		DECODE_LENGTH_EXACT(structs::SkillRankReport_Struct);
		SETUP_DIRECT_DECODE(SkillRankReport_Struct, structs::SkillRankReport_Struct);

		IN(announce_flags);
		IN(skill_a);
		IN(skill_b);
		IN(skill_c);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_TributeInfo) // 0x3c49
	{
		DECODE_LENGTH_EXACT(structs::TributeRequest_Struct);
		SETUP_DIRECT_DECODE(TributeRequest_Struct, structs::TributeRequest_Struct);

		IN(request_type);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ClientScreenSize) // 0x51a5
	{
		DECODE_LENGTH_EXACT(structs::ClientScreenSize_Struct);
		SETUP_DIRECT_DECODE(ClientScreenSize_Struct, structs::ClientScreenSize_Struct);

		IN(width);
		IN(height);
		IN(flag);
		memcpy(emu->pad, eq->pad, sizeof(emu->pad));

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_ClientStats) // 0x706a, periodic (every 30s) client performance report
	{
		DECODE_LENGTH_EXACT(structs::ClientStats_Struct);
		SETUP_DIRECT_DECODE(ClientStats_Struct, structs::ClientStats_Struct);

		memcpy(emu->counters, eq->counters, sizeof(emu->counters));
		IN(value);
		IN(unknown);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_HoardItemAdd) // 0x229c
	{
		DECODE_LENGTH_EXACT(structs::HoardItem_Struct);
		SETUP_DIRECT_DECODE(HoardItem_Struct, structs::HoardItem_Struct);

		IN(action);
		IN(slot);

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_HoardItemRemove) // 0x6d0f
	{
		DECODE_LENGTH_EXACT(structs::HoardItemRemove_Struct);
		SETUP_DIRECT_DECODE(HoardItemRemove_Struct, structs::HoardItemRemove_Struct);

		IN(action);
		IN(slot);
		IN(unknown);

		FINISH_DIRECT_DECODE();
	}

	// OP_AdvLoot (0x3175) is variable length with a uint16 sub-command at payload offset 0.
	// Do not decode it into a fixed struct - PassDecoder leaves the payload raw so
	// Client::Handle_OP_AdvLoot can walk it (see advloot.md section 3.1).


	//Naive version but should work well enough for now

	/* ---- Client -> server decoders for the LS wire layouts (opcodes/batch_*.md) ---- */

	static void ReadWireString(const unsigned char*& p, const unsigned char* end, char* out, size_t out_size) {
		size_t n = 0;
		while (p + n < end && p[n] != '\0') n++;
		size_t copy = n < out_size - 1 ? n : out_size - 1;
		memcpy(out, p, copy);
		out[copy] = '\0';
		p += n + 1;
	}

	DECODE(OP_GroupRoles)
	{
		DECODE_LENGTH_EXACT(structs::GroupRole_Struct);
		SETUP_DIRECT_DECODE(GroupRole_Struct, structs::GroupRole_Struct);

		strn0cpy(emu->Name1, eq->Name1, sizeof(emu->Name1));
		strn0cpy(emu->Name2, eq->Name2, sizeof(emu->Name2));
		IN(Unknown128);
		IN(Unknown132);
		IN(Unknown136);
		IN(RoleNumber);
		// EQEmu reads Toggle at +144, this client sends it at +148
		emu->Toggle = eq->Toggle;

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GuildMemberRankAltBanker)
	{
		DECODE_LENGTH_EXACT(structs::GuildMemberRankAltBanker_Struct);
		SETUP_DIRECT_DECODE(GuildManageBanker_Struct, structs::GuildMemberRankAltBanker_Struct);

		strn0cpy(emu->member, eq->Member, sizeof(emu->member));
		emu->enabled = eq->Flags;

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GuildMemberPublicNote)
	{
		DECODE_LENGTH_EXACT(structs::GuildMemberPublicNote_Struct);
		SETUP_DIRECT_DECODE(GuildUpdate_PublicNote, structs::GuildMemberPublicNote_Struct);

		emu->unknown0 = (uint32) eq->GuildID;
		strn0cpy(emu->name, eq->LocalName, sizeof(emu->name));
		strn0cpy(emu->target, eq->TargetName, sizeof(emu->target));
		strn0cpy(emu->note, eq->Note, sizeof(emu->note));

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_SummonCorpse)
	{
		DECODE_LENGTH_EXACT(structs::SummonCorpse_Struct);
		SETUP_DIRECT_DECODE(SummonCorpse_Struct, structs::SummonCorpse_Struct);

		strn0cpy(emu->charname, eq->Name, sizeof(emu->charname));

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_CrystalCreate)
	{
		DECODE_LENGTH_EXACT(structs::CrystalCreate_Struct);
		SETUP_DIRECT_DECODE(CrystalReclaim_Struct, structs::CrystalCreate_Struct);

		// the client uses 0 for radiant and non zero for ebon, EQEmu expects 4 / 5
		emu->type = eq->Type == 0 ? CrystalReclaimTypes::Radiant : CrystalReclaimTypes::Ebon;
		emu->amount = eq->Quantity;

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_RaidInvite)
	{
		// variable length: action, null terminated name, dword, null terminated name, dword, optional text
		unsigned char* __eq_buffer = __packet->pBuffer;
		const unsigned char* p = __eq_buffer;
		const unsigned char* end = __packet->pBuffer + __packet->size;

		if (end - p < 5) { __packet->SetOpcode(OP_Unknown); return; }

		uint32 action = *(const uint32*)p; p += 4;

		char player_name[64] = { 0 };
		ReadWireString(p, end, player_name, sizeof(player_name));

		uint32 unknown1 = 0;
		if (end - p >= 4) { unknown1 = *(const uint32*)p; p += 4; }

		char leader_name[64] = { 0 };
		ReadWireString(p, end, leader_name, sizeof(leader_name));

		uint32 parameter = 0;
		if (end - p >= 4) { parameter = *(const uint32*)p; p += 4; }

		__packet->size = sizeof(RaidGeneral_Struct);
		__packet->pBuffer = new unsigned char[__packet->size] {};
		RaidGeneral_Struct* emu = (RaidGeneral_Struct*)__packet->pBuffer;

		emu->action = action;
		strn0cpy(emu->player_name, player_name, sizeof(emu->player_name));
		emu->unknown1 = unknown1;
		strn0cpy(emu->leader_name, leader_name, sizeof(emu->leader_name));
		emu->parameter = parameter;

		delete[] __eq_buffer;
	}

	DECODE(OP_RaidDelegateAbility)
	{
		// variable length: seven dwords then a null terminated name
		unsigned char* __eq_buffer = __packet->pBuffer;
		const unsigned char* p = __eq_buffer;
		const unsigned char* end = __packet->pBuffer + __packet->size;

		if (end - p < 28) { __packet->SetOpcode(OP_Unknown); return; }

		uint32 ability = *(const uint32*)p; p += 4;
		uint32 member_number = *(const uint32*)p; p += 4;
		uint32 action = *(const uint32*)p; p += 4;
		uint32 unknown012 = *(const uint32*)p; p += 4;
		uint32 unknown016 = *(const uint32*)p; p += 4;
		uint32 entity_id = *(const uint32*)p; p += 4;
		uint32 unknown024 = *(const uint32*)p; p += 4;

		char name[64] = { 0 };
		ReadWireString(p, end, name, sizeof(name));

		__packet->size = sizeof(DelegateAbility_Struct);
		__packet->pBuffer = new unsigned char[__packet->size] {};
		DelegateAbility_Struct* emu = (DelegateAbility_Struct*)__packet->pBuffer;

		// this client numbers them 0 master looter / 1 main assist / 2 mark npc,
		// EQEmu's enum is RaidDelegateMainAssist 3 and RaidDelegateMainMarker 4
		if (ability == 1) ability = 3;
		else if (ability == 2) ability = 4;

		emu->DelegateAbility = ability;
		emu->MemberNumber = member_number;
		emu->Action = action;
		emu->Unknown012 = unknown012;
		emu->Unknown016 = unknown016;
		emu->EntityID = entity_id;
		emu->Unknown024 = unknown024;
		strn0cpy(emu->Name, name, sizeof(emu->Name));

		delete[] __eq_buffer;
	}

	DECODE(OP_GMLastName)
	{
		DECODE_LENGTH_EXACT(structs::GMLastName_Struct);
		SETUP_DIRECT_DECODE(GMLastName_Struct, structs::GMLastName_Struct);

		strn0cpy(emu->name, eq->Name, sizeof(emu->name));
		strn0cpy(emu->gmname, eq->GMName, sizeof(emu->gmname));
		strn0cpy(emu->lastname, eq->LastName, sizeof(emu->lastname));
		emu->unknown[0] = eq->Unknown192;
		// the client only sends 3 bytes of EQEmu's uint16 unknown[4]

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GuildInvite)
	{
		DECODE_LENGTH_EXACT(structs::GuildCommand_Struct);
		SETUP_DIRECT_DECODE(GuildCommand_Struct, structs::GuildCommand_Struct);

		strn0cpy(emu->othername, eq->OtherName, sizeof(emu->othername));
		strn0cpy(emu->myname, eq->MyName, sizeof(emu->myname));
		emu->guildeqid = (uint16) eq->GuildEqID;
		emu->officer = eq->Officer;

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GuildRemove)
	{
		DECODE_LENGTH_EXACT(structs::GuildCommand_Struct);
		SETUP_DIRECT_DECODE(GuildCommand_Struct, structs::GuildCommand_Struct);

		strn0cpy(emu->othername, eq->OtherName, sizeof(emu->othername));
		strn0cpy(emu->myname, eq->MyName, sizeof(emu->myname));
		emu->guildeqid = (uint16) eq->GuildEqID;
		emu->officer = eq->Officer;

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GuildStatus)
	{
		DECODE_LENGTH_EXACT(structs::GuildStatus_Struct);
		SETUP_DIRECT_DECODE(GuildStatus_Struct, structs::GuildStatus_Struct);

		strn0cpy(emu->Name, eq->Name, sizeof(emu->Name));
		// EQEmu's tail is 72 bytes, this client sends 80

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GuildInviteAccept)
	{
		DECODE_LENGTH_EXACT(structs::GuildInviteAccept_Struct);
		SETUP_DIRECT_DECODE(GuildInviteAccept_Struct, structs::GuildInviteAccept_Struct);

		strn0cpy(emu->inviter, eq->Inviter, sizeof(emu->inviter));
		strn0cpy(emu->new_member, eq->NewMember, sizeof(emu->new_member));
		emu->response = eq->Response;
		emu->guild_id = (uint32) eq->GuildID;

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GuildManageStatus)
	{
		// Rank change record. The client's layout is 82 bytes (u64 key, rank, name, flags) while
		// EQEmu's GuildManageStatus_Struct is 76 bytes with no flags field, so normalise down.
		DECODE_LENGTH_EXACT(structs::GuildManageStatus_Struct);
		SETUP_DIRECT_DECODE(GuildManageStatus_Struct, structs::GuildManageStatus_Struct);

		emu->guild_id = (uint32) eq->GuildGuid;
		emu->oldrank  = 0;
		emu->newrank  = eq->Rank;
		strn0cpy(emu->name, eq->Name, sizeof(emu->name));

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_SetGuildMOTD)
	{
		// /guildmotd <text> (FUN_140218e40, command table entry 0x140cfbf10) sends the 656 byte
		// record documented in Laurion::structs::GuildMOTD_Struct. EQEmu's GuildMOTD_Struct is 648
		// bytes with a u32 at +0, the name at +4 and the text at +0x88, so the size and every offset
		// differ. The client leaves the author field zero - Handle_OP_SetGuildMOTD records GetName().
		DECODE_LENGTH_EXACT(structs::GuildMOTD_Struct);
		SETUP_DIRECT_DECODE(GuildMOTD_Struct, structs::GuildMOTD_Struct);

		emu->unknown0   = 0;
		strn0cpy(emu->name, eq->Name, sizeof(emu->name));
		strn0cpy(emu->setby_name, eq->SetbyName, sizeof(emu->setby_name));
		emu->unknown132 = 0;
		strn0cpy(emu->motd, eq->Motd, sizeof(emu->motd));

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GetGuildMOTD)
	{
		// /getguildmotd (FUN_1402190e0, command table entry 0x140cfbf30): guid + name, everything
		// else zero. Normalised for the same reason as above; Handle_OP_GetGuildMOTD only uses the
		// packet as a trigger.
		DECODE_LENGTH_EXACT(structs::GuildMOTD_Struct);
		SETUP_DIRECT_DECODE(GuildMOTD_Struct, structs::GuildMOTD_Struct);

		emu->unknown0   = 0;
		strn0cpy(emu->name, eq->Name, sizeof(emu->name));
		emu->unknown132 = 0;

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GroupFollow)
	{
		// The group invite accept, sent by FUN_140260420 when the invitee clicks FOLLOW in the group
		// window (also reachable from /follow and the hot button). Laurion sends 156 payload bytes (two
		// names plus a 28 byte tail), not the 128 byte GroupGeneric_Struct, so normalise down to the emu
		// struct. Handle_OP_GroupFollow2 only consumes the two names.
		//
		// FUN_140260420 picks the opcode from the one shot flag DAT_140eb0b9c, which FUN_1402827f0 sets
		// only when the invite arrived on 0x1d90. So 0x8de answers an invite from a player who was not
		// yet in a group (the group is being formed) and 0x70b7 answers an invite from a player already
		// in a group. Same function, same layout, so both names share this decoder.
		DECODE_LENGTH_EXACT(structs::GroupFollowAccept_Struct);
		SETUP_DIRECT_DECODE(GroupGeneric_Struct, structs::GroupFollowAccept_Struct);

		memcpy(emu->name1, eq->inviter_name, sizeof(emu->name1));
		memcpy(emu->name2, eq->invitee_name, sizeof(emu->name2));

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GroupFollow2)
	{
		DECODE_FORWARD(OP_GroupFollow);
	}

	DECODE(OP_GroupAutoFollow)
	{
		// /follow <name> - the auto follow request, single null terminated name. This is a different
		// send site (FUN_14020ee10) from the group window accept, so it needs its own opcode name.
		DECODE_LENGTH_EXACT(structs::GroupFollow_Struct);
		SETUP_DIRECT_DECODE(GroupGeneric_Struct, structs::GroupFollow_Struct);

		strn0cpy(emu->name1, eq->Name, sizeof(emu->name1));
		// name2 is filled by the handler

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GroupMakeLeader)
	{
		DECODE_LENGTH_EXACT(structs::GroupMakeLeader_Struct);
		SETUP_DIRECT_DECODE(GroupMakeLeader_Struct, structs::GroupMakeLeader_Struct);

		emu->Unknown000 = eq->Unknown000;
		strn0cpy(emu->CurrentLeader, eq->CurrentLeader, sizeof(emu->CurrentLeader));
		strn0cpy(emu->NewLeader, eq->NewLeader, sizeof(emu->NewLeader));

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_PetCommands)
	{
		DECODE_LENGTH_EXACT(structs::PetCommands_Struct);
		SETUP_DIRECT_DECODE(PetCommand_Struct, structs::PetCommands_Struct);

		emu->command = eq->Command;
		// Target holds an entity id for the commands that act on something, and holds the equipment
		// slot for /pet inventory destroy, where it is a slot number rather than an entity.
		emu->target = eq->Target;
		// the trailing 4 bytes have no EQEmu equivalent

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_TributeItem)
	{
		DECODE_LENGTH_EXACT(structs::TributeItem_Struct);
		SETUP_DIRECT_DECODE(TributeItem_Struct, structs::TributeItem_Struct);

		// positional mapping against the client's own reader (batch_21):
		// +0 item index, +8 count, +16 spawn id, +24 value
		emu->slot = (uint32) eq->ItemField0;
		emu->quantity = eq->Quantity;
		emu->tribute_master_id = eq->SpawnID;

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_LFGuild)
	{
		if (__packet->size < 4) { __packet->SetOpcode(OP_Unknown); return; }

		uint32 command = *(uint32*)__packet->pBuffer;

		if (command == 3 && __packet->size == sizeof(structs::LFGuild_SearchPlayer_Struct)) {
			SETUP_DIRECT_DECODE(LFGuild_SearchPlayer_Struct, structs::LFGuild_SearchPlayer_Struct);

			emu->Command = eq->Command;
			emu->FromLevel = eq->FromLevel;
			emu->ToLevel = eq->ToLevel;
			emu->MinAA = eq->MinAA;
			emu->Classes = eq->Classes;
			emu->TimeZone = eq->TimeZone;

			FINISH_DIRECT_DECODE();
		} else if (command == 4 && __packet->size == sizeof(structs::LFGuild_SearchGuild_Struct)) {
			SETUP_DIRECT_DECODE(LFGuild_SearchGuild_Struct, structs::LFGuild_SearchGuild_Struct);

			emu->Command = eq->Command;
			emu->Level = eq->Level;
			emu->AAPoints = eq->AAPoints;
			emu->Class = eq->Class;
			emu->TimeZone = eq->TimeZone;

			FINISH_DIRECT_DECODE();
		}
		// subcommands 0 and 1 carry a runtime record block that cannot be described statically
	}

	DECODE(OP_WhoAllRequest) // 0x2a09 - /who and /who all
	{
		DECODE_LENGTH_EXACT(structs::WhoAllRequest_Struct);
		SETUP_DIRECT_DECODE(Who_All_Struct, structs::WhoAllRequest_Struct);

		// field names differ between the base struct and the Laurion wire struct
		strncpy(emu->whom, eq->Name, sizeof(emu->whom));
		emu->whom[sizeof(emu->whom) - 1] = '\0';

		emu->wrace   = eq->Race;
		emu->wclass  = eq->Class;
		emu->lvllow  = eq->LevelLow;
		emu->lvlhigh = eq->LevelHigh;
		emu->gmlookup = eq->GmLookup;

		// Laurion keeps the filter mode in its own field and the guild id as a 64 bit value.
		// The base struct folds both into guildid, so rebuild it here.
		if (eq->LookupType == 0xFFFFFFFF) {
			emu->guildid = 0xFFFFFFFF;             // no guild filter
		} else if (eq->LookupType == 0xFFFFFFFA) { // guild
			emu->guildid = static_cast<uint32>(eq->GuildID);
		} else {
			emu->guildid = eq->LookupType;          // LFG / trader / buyer specials
		}

		emu->type = eq->Type;

		FINISH_DIRECT_DECODE();
	}

	ENCODE(OP_WhoAllResponse) // 0x6404 - /who all reply
	{
		EQApplicationPacket* in = *p;
		*p = nullptr;

		WhoAllReturnStruct* emu = (WhoAllReturnStruct*)in->pBuffer;
		uint32 entry_count = emu->playercount;
		uint32 old_size = in->size;

		unsigned char* old_buffer = in->pBuffer;

		// Laurion carries one extra dword per player entry (the status bitmask at +4), so the
		// outgoing packet is 4 bytes larger per entry. The 64 byte header is unchanged.
		in->size = old_size + 4 * entry_count;
		in->pBuffer = new unsigned char[in->size];
		memset(in->pBuffer, 0, in->size);

		memcpy(in->pBuffer, old_buffer, sizeof(WhoAllReturnStruct));

		char* src = (char*)old_buffer + sizeof(WhoAllReturnStruct);
		char* end = (char*)old_buffer + old_size;
		char* OutBuffer = (char*)in->pBuffer + sizeof(WhoAllReturnStruct);

		for (uint32 i = 0; i < entry_count && src < end; ++i) {
			uint32 FormatStringID = VARSTRUCT_DECODE_TYPE(uint32, src);
			uint32 PidStringID    = VARSTRUCT_DECODE_TYPE(uint32, src);
			char Name[128];       VARSTRUCT_DECODE_STRING(Name, src);
			uint32 RankStringID   = VARSTRUCT_DECODE_TYPE(uint32, src);
			char Guild[128];      VARSTRUCT_DECODE_STRING(Guild, src);
			uint32 Field0         = VARSTRUCT_DECODE_TYPE(uint32, src);
			uint32 Field1         = VARSTRUCT_DECODE_TYPE(uint32, src);
			uint32 ZoneStringID   = VARSTRUCT_DECODE_TYPE(uint32, src);
			uint32 Zone           = VARSTRUCT_DECODE_TYPE(uint32, src);
			uint32 ClassID         = VARSTRUCT_DECODE_TYPE(uint32, src);
			uint32 Level          = VARSTRUCT_DECODE_TYPE(uint32, src);
			uint32 RaceID          = VARSTRUCT_DECODE_TYPE(uint32, src);
			char Account[128];    VARSTRUCT_DECODE_STRING(Account, src);
			uint32 Field100       = VARSTRUCT_DECODE_TYPE(uint32, src);

			// ZoneWho writes the status bitmask into Unknown80[1] for Laurion clients. -1 means the
			// sender had no status information for this entry (the world server path, which does not
			// track AFK / linkdead / trader state), so it encodes as "no flags".
			uint32 Flags = (Field1 == 0xFFFFFFFF) ? 0 : Field1;

			VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, FormatStringID);
			VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, Flags);
			VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, PidStringID);
			VARSTRUCT_ENCODE_STRING(OutBuffer, Name);
			VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, RankStringID);
			VARSTRUCT_ENCODE_STRING(OutBuffer, Guild);
			VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, Field0);
			// Field1 only carried the bitmask; the client prints it as a number when it is not -1,
			// so emit -1 here to keep the flags string clean.
			VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, 0xFFFFFFFF);
			VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, ZoneStringID);
			VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, Zone);
			// Laurion has an extra string here, but only when Flags & structs::WHOAF_EXTRA.
			// Nothing in the base struct can fill it, so that bit is never set.
			VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, ClassID);
			VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, Level);
			VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, RaceID);
			VARSTRUCT_ENCODE_STRING(OutBuffer, Account);
			VARSTRUCT_ENCODE_TYPE(uint32, OutBuffer, Field100);
		}

		delete[] old_buffer;
		dest->FastQueuePacket(&in, ack_req);
	}

	// Targeting, aggro meter and extended target encoders.
	//
	// The zone code builds these packets straight into the Laurion wire layout with
	// EQApplicationPacket::WriteUInt32 / WriteUInt8 / WriteString, so these encoders are layout
	// validators: they enforce the field order the client handlers read and drop a malformed packet
	// instead of delivering it as garbage. Handler addresses are the branches of
	// ZonePacket__dispatchRecv (0x1401d5140) in eqgame.exe at image base 0x140000000.

	// 0x639c - target of target. Client branch 0x1401d9449 writes
	// [pinstLocalPC + 0x2850 + 0xF88] (MQ2 PlayerClient::TargetOfTarget) from payload dword 0.
	// Client::SetHoTT() is the only producer, and this is the only opcode the client actually reads.
	ENCODE(OP_TargetHoTT) {
		ENCODE_LENGTH_EXACT(structs::TargetHoTT_Struct);
		SETUP_DIRECT_ENCODE(structs::TargetHoTT_Struct, structs::TargetHoTT_Struct);

		OUT(TargetOfTarget);

		FINISH_ENCODE();
	}

	// 0x5b2d - pet's target. Client branch 0x1401d7274 stores payload dword 0 as the pet's Target
	// pointer (or clears it when 0). The Pet Info window reads that field via
	// __GetGaugeValueFromEQ case 0x29, so this is the only packet that can populate the window.
	// NPC::SetTarget() is the only producer.
	ENCODE(OP_PetHoTT) {
		ENCODE_LENGTH_EXACT(structs::PetHoTT_Struct);
		SETUP_DIRECT_ENCODE(structs::PetHoTT_Struct, structs::PetHoTT_Struct);

		OUT(PetTarget);

		FINISH_ENCODE();
	}

	// 0x5479 - aggro meter lock + target id. Client handler FUN_1400ac650 writes payload[0] to
	// AggroMeterManagerClient + 0x1E0 (AggroLockID) and payload[4] to + 0x1E4 (AggroTargetID).
	ENCODE(OP_AggroMeterTargetInfo) {
		ENCODE_LENGTH_EXACT(structs::AggroMeterTargetInfo_Struct);
		SETUP_DIRECT_ENCODE(structs::AggroMeterTargetInfo_Struct, structs::AggroMeterTargetInfo_Struct);

		OUT(LockID);
		OUT(TargetID);

		FINISH_ENCODE();
	}

	// 0x68eb - aggro meter update. Client handler FUN_1400ac650 (shared with 0x5479) reads:
	//   u8 flag; when flag != 0 it reads u32 secondary_id -> AggroMeterManagerClient + 0x1E8
	//   u8 count; then count x (u8 type, u16 percent), each writing a u16 at +8 + type * 0x10
	// The client aborts on any type > 0x1D, so AggroMeter::AggroTypes must stay inside [0, 29].
	ENCODE(OP_AggroMeterUpdate) {
		EQApplicationPacket* in = *p;
		*p = nullptr;

		if (in->size < 2) {
			LogNetcode("[STRUCTS] AggroMeterUpdate too short: got [{}], expected at least 2", in->size);
			delete in;
			return;
		}

		uint8 flag = in->pBuffer[0];
		uint32 pos = flag ? 5 : 1;
		if (pos >= in->size) {
			LogNetcode("[STRUCTS] AggroMeterUpdate truncated after flag: got [{}], flag [{}]", in->size, flag);
			delete in;
			return;
		}

		uint8 count = in->pBuffer[pos];
		if (pos + 1 + static_cast<uint32>(count) * 3 > in->size) {
			LogNetcode("[STRUCTS] AggroMeterUpdate entry count [{}] does not fit in size [{}]", count, in->size);
			delete in;
			return;
		}

		for (uint32 i = 0; i < count; ++i) {
			uint8 type = in->pBuffer[pos + 1 + i * 3];
			if (type > 0x1D) {
				LogNetcode("[STRUCTS] AggroMeterUpdate type [{}] outside client range [0, 29]", type);
				delete in;
				return;
			}
		}

		dest->FastQueuePacket(&in, ack_req);
	}

	// 0x123c - extended target list. Client handler FUN_14028ff00 reads:
	//   u32 max_slots (resizes the list), u32 count,
	//   count x (u32 slot, u8 status, u32 spawn_id, null terminated name)
	// The client slot record is 0x4C bytes: u32 xTargetType, u32 status, u32 spawn_id, char name[64].
	// Client::SendXTargetPacket() and SendXTargetUpdates() already emit this order; the check below
	// keeps a truncated name from being read past the end of the buffer.
	ENCODE(OP_XTargetResponse) {
		EQApplicationPacket* in = *p;
		*p = nullptr;

		if (in->size < 8) {
			LogNetcode("[STRUCTS] XTargetResponse too short: got [{}], expected at least 8", in->size);
			delete in;
			return;
		}

		uint32 max_slots = 0;
		uint32 count = 0;
		memcpy(&max_slots, in->pBuffer, 4);
		memcpy(&count, in->pBuffer + 4, 4);

		uint32 pos = 8;
		for (uint32 i = 0; i < count; ++i) {
			if (pos + 9 > in->size) {
				LogNetcode("[STRUCTS] XTargetResponse entry [{}] truncated at size [{}]", i, in->size);
				delete in;
				return;
			}

			uint32 slot = 0;
			memcpy(&slot, in->pBuffer + pos, 4);
			if (slot >= max_slots) {
				LogNetcode("[STRUCTS] XTargetResponse slot [{}] >= max_slots [{}]", slot, max_slots);
				delete in;
				return;
			}

			pos += 4;              // slot
			pos += 1;              // status
			pos += 4;              // spawn id

			uint32 name_len = 0;
			while (pos + name_len < in->size && in->pBuffer[pos + name_len] != 0) ++name_len;
			if (name_len > 63) {
				LogNetcode("[STRUCTS] XTargetResponse name length [{}] exceeds client slot name [64]", name_len);
				delete in;
				return;
			}
			pos += name_len + 1;   // name + null
		}

		dest->FastQueuePacket(&in, ack_req);
	}

	// 0x5c3e - extended target slot assignment. Client send site FUN_14028fb90 (the slot setter, reached
	// from __ExecuteCmd and from the 0x0ede receive branch). Variable length:
	//   u32 flag (constant 1), u32 slot, u32 type, null terminated name
	// The type is the XTargetType enum (MQ XTargetTypes 0..26). This is the only packet that tells the
	// server what a slot IS, so it is what makes MyPetTarget (24) - the slot the Pet window reads - work.
	// Client::Handle_OP_XTargetRequest already reads this exact field order.
	ENCODE(OP_XTargetRequest) {
		EQApplicationPacket* in = *p;
		*p = nullptr;

		// Mirrors XTARGET_HARDCAP (zone/client.h) = 30. The client's slot vector is resized by whatever
		// max_slots the server sends (FUN_140691bc0 has no cap); 30 is the client's own target-set limit.
		const uint32 xtarget_hardcap = 30;

		if (in->size < 12) {
			LogNetcode("[STRUCTS] XTargetRequest too short: got [{}], expected at least 12", in->size);
			delete in;
			return;
		}

		uint32 flag = 0;
		memcpy(&flag, in->pBuffer, 4);
		if (flag != 1) {
			LogNetcode("[STRUCTS] XTargetRequest flag [{}] is not the constant 1", flag);
			delete in;
			return;
		}

		uint32 slot = 0;
		memcpy(&slot, in->pBuffer + 4, 4);
		if (slot >= xtarget_hardcap) {
			LogNetcode("[STRUCTS] XTargetRequest slot [{}] >= hardcap [{}]", slot, xtarget_hardcap);
			delete in;
			return;
		}

		uint32 type = 0;
		memcpy(&type, in->pBuffer + 8, 4);
		if (type > 26) {
			LogNetcode("[STRUCTS] XTargetRequest type [{}] outside XTargetType range 0..26", type);
			delete in;
			return;
		}

		uint32 name_len = 0;
		while (name_len < 64 && 12 + name_len < in->size && in->pBuffer[12 + name_len] != 0) ++name_len;
		if (name_len >= 64) {
			LogNetcode("[STRUCTS] XTargetRequest name length [{}] exceeds client slot name [64]", name_len);
			delete in;
			return;
		}

		if (in->size != 12 + name_len + 1) {
			LogNetcode("[STRUCTS] XTargetRequest size [{}] != 12 + name [{}] + null", in->size, name_len);
			delete in;
			return;
		}

		dest->FastQueuePacket(&in, ack_req);
	}

	// 0x0ede - single extended target slot update, used in both directions. Client send site
	// FUN_140229f50 (/xtarget set <slot> <name>) and client receive branch 0x1401e02cb share this opcode
	// and layout: u32 spawn_id, u32 slot. It carries no type, so it can only ever fill a PC/NPC slot.
	ENCODE(OP_0x0ede) {
		ENCODE_LENGTH_EXACT(structs::XTargetSlotUpdate_Struct);
		SETUP_DIRECT_ENCODE(structs::XTargetSlotUpdate_Struct, structs::XTargetSlotUpdate_Struct);

		OUT(SpawnID);
		OUT(Slot);

		FINISH_ENCODE();
	}

	int ExtractIDFile(const std::string& input) {
		std::string number;
		for (char ch : input) {
			if (std::isdigit(static_cast<unsigned char>(ch))) {
				number += ch;
			}
		}

		if (number.empty()) {
			return 0;
		}

		return std::stoi(number);
	}

	// Helper Functions
	void SerializeItemDefinition(SerializeBuffer& buffer, const EQ::ItemData* item) {
		//u8 Type;
		buffer.WriteUInt8(item->ItemClass);
		//char Name[];
		buffer.WriteString(item->Name);

		//char LoreName[];
		buffer.WriteString(item->Lore);

		//we need to parse id file
		//s32 IDFile;
		int32 idfile = ExtractIDFile(item->IDFile);
		buffer.WriteUInt32(idfile);

		//s32 IDFile2;
		buffer.WriteUInt32(0); //unsupported atm

		/*
		ibs.id = item->ID;
		ibs.weight = item->Weight;
		ibs.norent = item->NoRent;
		ibs.nodrop = item->NoDrop;
		ibs.attune = item->Attuneable;
		ibs.size = item->Size;
		ibs.slots = item->Slots;
		ibs.price = item->Price;
		ibs.icon = item->Icon;
		*/

		//s32 ItemNumber;
		buffer.WriteUInt32(item->ID);
		
		//s32 Weight;
		buffer.WriteInt32(item->Weight);
		
		//bool NoRent;
		buffer.WriteUInt8(item->NoRent);

		//bool IsDroppable;
		buffer.WriteUInt8(item->NoDrop);

		//bool Attuneable;
		buffer.WriteUInt8(item->Attuneable);

		//u8 Size;
		buffer.WriteUInt8(item->Size);

		//u32 EquipSlots;
		buffer.WriteUInt32(item->Slots);

		//u32 Cost;
		buffer.WriteUInt32(item->Price);

		//s32 IconNumber;
		buffer.WriteInt32(item->Icon);

		//bool eGMRequirement;
		buffer.WriteUInt8(0); //unsupported atm

		//bool Tradeskills;
		buffer.WriteUInt8(0); //unsupported atm

		//s8 SvCold;
		//s8 SvDisease;
		//s8 SvPoison;
		//s8 SvMagic;
		//s8 SvFire;
		//s8 SvCorruption;
		buffer.WriteInt8(item->CR);
		buffer.WriteInt8(item->DR);
		buffer.WriteInt8(item->PR);
		buffer.WriteInt8(item->MR);
		buffer.WriteInt8(item->FR);
		buffer.WriteInt8(item->SVCorruption);
		
		//s8 STR;
		//s8 STA;
		//s8 AGI;
		//s8 DEX;
		//s8 CHA;
		//s8 INT;
		//s8 WIS;
		buffer.WriteInt8(item->AStr);
		buffer.WriteInt8(item->ASta);
		buffer.WriteInt8(item->AAgi);
		buffer.WriteInt8(item->ADex);
		buffer.WriteInt8(item->ACha);
		buffer.WriteInt8(item->AInt);
		buffer.WriteInt8(item->AWis);

		//s32 HP;
		//s32 Mana;
		//s32 Endurance;
		//s32 AC;
		buffer.WriteInt32(item->HP);
		buffer.WriteInt32(item->Mana);
		buffer.WriteInt32(item->Endur);
		buffer.WriteInt32(item->AC);

		//s32 HPRegen;
		//s32 ManaRegen;
		//s32 EnduranceRegen;
		buffer.WriteInt32(item->Regen);
		buffer.WriteInt32(item->ManaRegen);
		buffer.WriteInt32(item->EnduranceRegen);
		
		//u32 Classes;
		//u32 Races;
		//u32 Deity;
		buffer.WriteUInt32(item->Classes);
		buffer.WriteUInt32(item->Races);
		buffer.WriteUInt32(item->Deity);

		//u32 SkillModValue;
		//u32 SkillModMax;
		//s32 SkillModType;
		//s32 SkillModBonus;
		buffer.WriteInt32(item->SkillModValue);
		buffer.WriteUInt32(item->SkillModMax);
		buffer.WriteInt32(item->SkillModType);
		buffer.WriteInt32(0); //unsupported atm
		
		//s32 BaneDMGRace;
		//s32 BaneDMGBodyType;
		//s32 BaneDMGRaceValue;
		//s32 BaneDMGBodyTypeValue;
		buffer.WriteInt32(item->BaneDmgRace);
		buffer.WriteInt32(item->BaneDmgBody);
		buffer.WriteInt32(item->BaneDmgRaceAmt);
		buffer.WriteInt32(item->BaneDmgAmt);

		//bool Magic;
		buffer.WriteUInt8(item->Magic);

		//s32 FoodDuration;
		buffer.WriteInt32(item->CastTime_);

		//s32 RequiredLevel;
		buffer.WriteInt32(item->ReqLevel > 125 ? 125 : item->ReqLevel);

		//s32 RecommendedLevel;
		buffer.WriteInt32(item->RecLevel > 125 ? 125 : item->RecLevel);

		//s32 InstrumentType;
		//s32 InstrumentMod;
		buffer.WriteInt32(item->BardType);
		buffer.WriteInt32(item->BardValue);

		//u8 Light;
		buffer.WriteUInt8(item->Light);

		//u8 Delay;
		buffer.WriteUInt8(item->Delay);

		//u8 ElementalFlag;
		//u8 ElementalDamage;
		buffer.WriteUInt8(item->ElemDmgType);
		buffer.WriteUInt8(item->ElemDmgAmt);

		//u8 Range;
		buffer.WriteUInt8(item->Range);

		//u32 Damage;
		buffer.WriteUInt32(item->Damage);

		//u32 MaterialTintIndex;
		//u32 Prestige;
		buffer.WriteUInt32(item->Color);
		buffer.WriteUInt32(0); //unsupported atm
		
		//u8 ItemClass;
		buffer.WriteUInt8(item->ItemType);

		//ArmorProperties properties;
		//s32 Type;
		//s32 Material;
		//s32 Variation;
		//s32 NewArmorId;
		//s32 NewArmorType;
		buffer.WriteUInt32(item->Material);
		buffer.WriteUInt32(0); //unsupported atm
		buffer.WriteUInt32(item->EliteMaterial);
		buffer.WriteUInt32(item->HerosForgeModel);
		buffer.WriteUInt32(0); //unsupported atm
		
		//float MerchantGreedMod;
		buffer.WriteFloat(item->SellRate);

		//s32 DmgBonusSkill;
		//s32 DmgBonusValue;
		buffer.WriteInt32(item->ExtraDmgSkill);
		buffer.WriteInt32(item->ExtraDmgAmt);

		//s32 ScriptID;
		//char CharmFile[];
		buffer.WriteUInt32(item->CharmFileID);
		buffer.WriteString(item->CharmFile);

		//s32 AugType;
		//u32 AugSkinTypeMask;
		//u32 AugRestrictions;
		buffer.WriteUInt32(item->AugType);
		buffer.WriteInt32(-1); //unsupported atm
		buffer.WriteUInt32(item->AugRestrict);

		//ItemAugmentationSocket AugData[6];
		for (int j = 0; j < 6; ++j) {
			/*
			s32 Type;
			bool Visible;
			bool Infusible;
			*/

			buffer.WriteInt32(item->AugSlotType[j]);
			buffer.WriteUInt8(item->AugSlotVisible[j]);
			buffer.WriteUInt8(item->AugSlotUnk2[j]); //not entirely supported atm
		}
		
		//s32 LDType;
		//s32 LDTheme;
		//s32 LDCost;
		//s32 PointBuyBackPercent;
		//s32 NeedAdventureCompleted;
		buffer.WriteUInt32(item->PointType);
		buffer.WriteUInt32(item->LDoNTheme);
		buffer.WriteUInt32(item->LDoNPrice);
		buffer.WriteUInt32(item->LDoNSellBackRate);
		buffer.WriteUInt32(item->LDoNSold);

		//u8 ContainerType;
		//u8 Slots;
		//u8 SizeCapacity;
		//u8 WeightReduction;
		buffer.WriteUInt8(item->BagType);
		buffer.WriteUInt8(item->BagSlots);
		buffer.WriteUInt8(item->BagSize);
		buffer.WriteUInt8(item->BagWR);

		//u8 BookType;
		//u8 BookLang;
		//char BookFile[];
		buffer.WriteUInt8(item->Book);
		buffer.WriteUInt8(item->BookType); //doesn't match the name for eqlib
		buffer.WriteString(item->Filename);

		//s32 Lore;
		buffer.WriteInt32(item->LoreGroup);

		//bool Artifact;
		buffer.WriteUInt8(item->ArtifactFlag);

		//s32 Favor;
		buffer.WriteUInt32(item->Favor);

		//bool bIsFVNoDrop;
		buffer.WriteUInt8(item->FVNoDrop);

		//s32 Attack;
		//s32 Haste;
		buffer.WriteInt32(item->Attack);
		buffer.WriteInt32(item->Haste);

		//s32 GuildFavor;
		buffer.WriteUInt32(item->GuildFavor);

		//s32 SolventItemID;
		buffer.WriteUInt32(item->AugDistiller);
		
		//s32 AnimationOverride;
		buffer.WriteInt32(-1); //unsupported atm

		//u32 PaletteTintIndex;
		buffer.WriteInt32(0); //unsupported atm

		//bool bNoPetGive;
		buffer.WriteUInt8(item->NoPet);

		//bool bSomeProfile;
		buffer.WriteUInt8(0); //unsupported atm

		//u32 StackSize;
		buffer.WriteUInt32(item->ID == PARCEL_MONEY_ITEM_ID ? 0x7FFFFFFF : ((item->Stackable ? item->StackSize : 0)));
		
		//bool bNoStorage;
		buffer.WriteUInt8(item->NoTransfer);

		//bool Expendable;
		uint8 expendable = item->ExpendableArrow;

		if (item->ItemType == EQ::item::ItemTypeFishingPole && item->SubType == 0) {
			expendable = 1;
		}

		buffer.WriteUInt8(expendable);

		//u8 SpellDataSkillMask[78];
		for (int j = 0; j < 78; ++j) {
			buffer.WriteUInt8(0); //unsure what this is exactly
		}

		/* SpellData:
			s32 SpellId;
			u8 RequiredLevel;
			u8 EffectType;
			s32 EffectiveCasterLevel;
			s32 MaxCharges;
			s32 CastTime;
			s32 RecastTime;
			s32 RecastType;
			s32 ProcRate;
			char OverrideName[];
			s32 OverrideDesc;
		*/

		//SpellData SpellDataClicky;
		buffer.WriteInt32(item->Click.Effect);
		buffer.WriteUInt8(item->Click.Level2);
		buffer.WriteUInt8(item->Click.Type);
		buffer.WriteInt32(item->Click.Level);
		buffer.WriteInt32(item->MaxCharges);
		buffer.WriteInt32(item->CastTime);
		buffer.WriteInt32(item->RecastDelay);
		buffer.WriteInt32(item->RecastType);
		buffer.WriteInt32(0); //unsupported atm
		if (strlen(item->ClickName) > 0) {
			buffer.WriteString(item->ClickName);
		}
		else {
			buffer.WriteString("");
		}
		buffer.WriteInt32(0); //unsupported atm

		//SpellData SpellDataProc;
		buffer.WriteInt32(item->Proc.Effect);
		buffer.WriteUInt8(item->Proc.Level2);
		buffer.WriteUInt8(item->Proc.Type);
		buffer.WriteInt32(item->Proc.Level);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0); //unsupported atm; even live sets this to 0 for procs
		if (strlen(item->ProcName) > 0) {
			buffer.WriteString(item->ProcName);
		}
		else {
			buffer.WriteString("");
		}
		buffer.WriteInt32(0); //unsupported atm

		//SpellData SpellDataWorn;
		buffer.WriteInt32(item->Worn.Effect);
		buffer.WriteUInt8(item->Worn.Level2);
		buffer.WriteUInt8(item->Worn.Type);
		buffer.WriteInt32(item->Worn.Level);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		if (strlen(item->WornName) > 0) {
			buffer.WriteString(item->WornName);
		}
		else {
			buffer.WriteString("");
		}
		buffer.WriteInt32(0); //unsupported atm

		//SpellData SpellDataFocus;
		buffer.WriteInt32(item->Focus.Effect);
		buffer.WriteUInt8(item->Focus.Level2);
		buffer.WriteUInt8(item->Focus.Type);
		buffer.WriteInt32(item->Focus.Level);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		if (strlen(item->FocusName) > 0) {
			buffer.WriteString(item->FocusName);
		}
		else {
			buffer.WriteString("");
		}
		buffer.WriteInt32(0); //unsupported atm

		//SpellData SpellDataScroll;
		buffer.WriteInt32(item->Scroll.Effect);
		buffer.WriteUInt8(item->Scroll.Level2);
		buffer.WriteUInt8(item->Scroll.Type);
		buffer.WriteInt32(item->Scroll.Level);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		if (strlen(item->ScrollName) > 0) {
			buffer.WriteString(item->ScrollName);
		}
		else {
			buffer.WriteString("");
		}
		buffer.WriteInt32(0); //unsupported atm

		//SpellData SpellDataFocus2; //unsupported atm
		buffer.WriteInt32(-1);
		buffer.WriteUInt8(0);
		buffer.WriteUInt8(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteString("");
		buffer.WriteInt32(0);

		//SpellData SpellDataBlessing; //unsupported atm
		buffer.WriteInt32(-1);
		buffer.WriteUInt8(0);
		buffer.WriteUInt8(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteInt32(0);
		buffer.WriteString("");
		buffer.WriteInt32(0);		
		
		//s32 RightClickScriptID;
		buffer.WriteInt32(0); //unsupported atm

		//bool QuestItem;
		buffer.WriteInt8(item->QuestItemFlag);

		//s32 MaxPower;
		buffer.WriteInt32(0); //unsupported atm

		//s32 Purity;
		buffer.WriteInt32(item->Purity);

		//s32 BackstabDamage;
		buffer.WriteInt32(item->BackstabDmg);

		//s32 HeroicSTR;
		//s32 HeroicINT;
		//s32 HeroicWIS;
		//s32 HeroicAGI;
		//s32 HeroicDEX;
		//s32 HeroicSTA;
		//s32 HeroicCHA;
		buffer.WriteInt32(item->HeroicStr);
		buffer.WriteInt32(item->HeroicInt);
		buffer.WriteInt32(item->HeroicWis);
		buffer.WriteInt32(item->HeroicAgi);
		buffer.WriteInt32(item->HeroicDex);
		buffer.WriteInt32(item->HeroicSta);
		buffer.WriteInt32(item->HeroicCha);

		//s32 HealAmount;
		//s32 SpellDamage;
		//s32 Clairvoyance;
		buffer.WriteInt32(item->HealAmt);
		buffer.WriteInt32(item->SpellDmg);
		buffer.WriteInt32(item->Clairvoyance);

		//s32 SubClass;
		buffer.WriteInt32(item->SubType);

		//bool bLoginRegReqItem;
		buffer.WriteUInt8(0); //unsupported atm

		//s32 ItemLaunchScriptID;
		buffer.WriteInt32(0); //unsupported atm

		//bool Heirloom;
		buffer.WriteUInt8(0); //unsupported atm

		//s32 Placeable;
		buffer.WriteInt32(0); //unsupported atm

		//bool bPlaceableIgnoreCollisions;
		buffer.WriteUInt8(0);

		//s32 PlacementType;
		buffer.WriteInt32(0); //unsupported atm

		//s32 RealEstateDefID;
		buffer.WriteInt32(0); //unsupported atm

		//float PlaceableScaleRangeMin;
		//float PlaceableScaleRangeMax;
		buffer.WriteFloat(0.0f); //unsupported atm
		buffer.WriteFloat(0.0f); //unsupported atm

		//s32 RealEstateUpkeepID;
		buffer.WriteInt32(0); //unsupported atm

		//s32 MaxPerRealEstate;
		buffer.WriteInt32(-1); //unsupported atm

		//char HousepetFileName[];
		buffer.WriteString(""); //unsupported atm

		//bool bInteractiveObject;
		buffer.WriteUInt8(0); //unsupported atm

		//s32 TrophyBenefitID;
		buffer.WriteInt32(-1); //unsupported atm

		//bool bDisablePlacementRotation;
		//bool bDisableFreePlacement;
		buffer.WriteUInt8(0); //unsupported atm
		buffer.WriteUInt8(0); //unsupported atm

		//s32 NpcRespawnInterval;
		buffer.WriteInt32(0); //unsupported atm

		//float PlaceableDefScale;
		//float PlaceableDefHeading;
		//float PlaceableDefPitch;
		//float PlaceableDefRoll;
		buffer.WriteFloat(0.0f);
		buffer.WriteFloat(0.0f);
		buffer.WriteFloat(0.0f);
		buffer.WriteFloat(0.0f);

		//u8 SocketSubClassCount;
		//s32 SocketSubClass[SocketSubClassCount];
		buffer.WriteUInt8(0); //unsupported atm
		
		//bool Collectible;
		buffer.WriteUInt8(0); //unsupported atm

		//bool NoDestroy;
		buffer.WriteUInt8(0); //unsupported atm
		
		//bool bNoNPC;
		buffer.WriteUInt8(0); //unsupported atm

		//bool NoZone;
		buffer.WriteUInt8(0); //unsupported atm

		//s32 MakerId;
		buffer.WriteInt32(0); //unsupported atm

		//bool NoGround;
		buffer.WriteUInt8(0); //unsupported atm

		//bool bNoLoot;
		buffer.WriteUInt8(0); //unsupported atm

		//bool MarketPlace;
		buffer.WriteUInt8(0); //unsupported atm

		//bool bFreeSlot;
		buffer.WriteUInt8(0); //unsupported atm

		//bool bAutoUse;
		buffer.WriteUInt8(0); //unsupported atm

		//s32 Unknown0x0e4;
		buffer.WriteInt32(-1); //unsupported atm

		//s32 MinLuck;
		//s32 MaxLuck;
		buffer.WriteUInt32(0); //unsupported atm
		buffer.WriteUInt32(0); //unsupported atm

		//s32 LoreEquipped;
		buffer.WriteUInt32(0); //unsupported atm
	}

	void SerializeItem(SerializeBuffer& buffer, const EQ::ItemInstance* inst, int16 slot_id_in, uint8 depth, ItemPacketType packet_type) {
		const EQ::ItemData* item = inst->GetUnscaledItem();

		//char ItemGUID[];
		auto item_guid = fmt::format("{:016}", inst->GetSerialNumber());
		buffer.WriteString(item_guid);

		//u32 StackCount;
		auto stacksize =
			item->ID == PARCEL_MONEY_ITEM_ID ? inst->GetPrice() : (inst->IsStackable() ? ((inst->GetCharges() > 1000)
				? 0xFFFFFFFF : inst->GetCharges()) : 1);
		buffer.WriteUInt32(stacksize);

		structs::InventorySlot_Struct slot_id{};
		// Canonical ids 23..35 are shared by the general slots and the corpse container (CORPSE_BEGIN is
		// slotGeneral1), so the slot number alone cannot tell the two apart - only the packet type can.
		// Routing every possession item through the corpse conversion made all general slots resolve to a
		// corpse container index, which is why the inventory window showed nothing and the food/water check
		// failed. AdvLoot rows do live in 23..56, so the corpse branch is keyed on the packet type: loot, and
		// the profile insert types when the slot is inside the canonical corpse range. The tradeskill combine
		// slot is 1000, so it never collides.
		if (packet_type == ItemPacketLoot ||
			((packet_type == ItemPacketWorldContainer || packet_type == ItemPacketViewLink) && IsLaurionCorpseSlot(slot_id_in))) {
			slot_id = ServerToLaurionCorpseSlot(slot_id_in);
		}
		else {
			slot_id = ServerToLaurionSlot(slot_id_in);
		}

		//u32 slot_type;
		buffer.WriteUInt32(inst->GetMerchantSlot() ? invtype::typeMerchant : slot_id.Type);
		//s16 main_slot;
		//s16 sub_slot;
		//s16 aug_slot;
		buffer.WriteInt16(inst->GetMerchantSlot() ? inst->GetMerchantSlot() : slot_id.Slot);
		buffer.WriteInt16(inst->GetMerchantSlot() ? 0xffff : slot_id.SubIndex);
		buffer.WriteInt16(inst->GetMerchantSlot() ? 0xffff : slot_id.AugIndex);

		//u64 price;
		buffer.WriteUInt64(inst->GetPrice());

		//u32 MerchantQuantity;
		buffer.WriteUInt32(inst->GetMerchantSlot() ? inst->GetMerchantCount() : 1);

		//u32 ScriptIndex;
		buffer.WriteUInt32(inst->IsScaling() ? (inst->GetExp() / 100) : 0);

		//u64 MerchantSlot;
		buffer.WriteUInt64(inst->GetMerchantSlot() ? inst->GetMerchantSlot() : inst->GetSerialNumber());

		//u32 LastCastTime;
		buffer.WriteUInt32(inst->GetRecastTimestamp());

		//s32 Charges;
		auto charges = (inst->IsStackable() ? (item->MaxCharges ? 1 : 0) : ((inst->GetCharges() > 254)
			? -1
			: inst->GetCharges()));

		buffer.WriteInt32(charges);

		//s32 NoDropFlag;
		buffer.WriteInt32(inst->IsAttuned() ? 1 : 0);

		//s32 Power;
		buffer.WriteInt32(0);

		//s32 AugFlag;
		buffer.WriteInt32(0);

		//bool bConvertable;
		buffer.WriteInt8(0);

		//u32 ConvertItemNameLength;
		buffer.WriteInt32(0);

		//char ConvertItemName[ConvertItemNameLength];

		//u32 ConvertItemID;
		buffer.WriteInt32(0);

		//u32 Open;
		buffer.WriteInt32(0);

		//bool EvolvingItem;
		buffer.WriteInt8(item->EvolvingItem);

		//EvoData evoData;
		if (item->EvolvingItem > 0) {
			//s32 GroupId;
			buffer.WriteInt32(0);

			//s32 EvolvingCurrentLevel;
			buffer.WriteInt32(item->EvolvingLevel);

			//double EvolvingExpPct;
			buffer.WriteDouble(0.0);
			
			//s32 EvolvingMaxLevel;
			buffer.WriteInt32(item->EvolvingMax);

			//s32 LastEquipped;
			buffer.WriteInt32(0);
		}

		uint32 ornamentation_icon = (inst->GetOrnamentationIcon() ? inst->GetOrnamentationIcon() : 0);
		uint32 hero_model = 0;

		//s32 ActorTag1;
		//s32 ActorTag2;
		if (inst->GetOrnamentationIDFile()) {
			hero_model = inst->GetOrnamentHeroModel(EQ::InventoryProfile::CalcMaterialFromSlot(slot_id_in));

			buffer.WriteInt32(inst->GetOrnamentationIDFile());
			buffer.WriteInt32(inst->GetOrnamentationIDFile());
		}
		else {
			buffer.WriteInt32(0);
			buffer.WriteInt32(0);
		}

		//s32 OrnamentationIcon;
		//s32 ArmorType;
		//s32 NewArmorID;
		//u32 Tint;
		buffer.WriteInt32(ornamentation_icon);
		buffer.WriteInt32(-1);
		buffer.WriteInt32(hero_model);
		buffer.WriteInt32(0);

		//bool bCopied;
		buffer.WriteUInt8(0);

		//s32 RealEstateID;
		buffer.WriteInt32(-1);
		//s32 RespawnTime;
		buffer.WriteInt32(0);

		//ItemDefinition Item;
		SerializeItemDefinition(buffer, item);

		//u32 RealEstateArrayCount;
		buffer.WriteInt32(0);
		//s32 RealEstateArray[RealEstateArrayCount];
		
		//bool bRealEstateItemPlaceable;
		buffer.WriteInt8(0);

		//u32 SubContentSize;
		uint32 subitem_count = 0;

		int16 SubSlotNumber = EQ::invbag::SLOT_INVALID;
		
		if (EQ::invslot::IsGeneralSlot(EQ::versions::ClientVersion::Laurion, slot_id_in))
			SubSlotNumber = EQ::invbag::GeneralBagSlotId(EQ::versions::ClientVersion::Laurion, slot_id_in, 0);
		else if (slot_id_in == EQ::invslot::slotCursor)
			SubSlotNumber = EQ::invbag::CURSOR_BAG_BEGIN;
		else if (slot_id_in <= EQ::invslot::BANK_END && slot_id_in >= EQ::invslot::BANK_BEGIN)
			SubSlotNumber = EQ::invbag::BANK_BAGS_BEGIN + ((slot_id_in - EQ::invslot::BANK_BEGIN) * EQ::invbag::SLOT_COUNT);
		else if (slot_id_in <= EQ::invslot::SHARED_BANK_END && slot_id_in >= EQ::invslot::SHARED_BANK_BEGIN)
			SubSlotNumber = EQ::invbag::SHARED_BANK_BAGS_BEGIN + ((slot_id_in - EQ::invslot::SHARED_BANK_BEGIN) * EQ::invbag::SLOT_COUNT);
		else
			SubSlotNumber = slot_id_in; // not sure if this is the best way to handle this..leaving for now

		if (SubSlotNumber != EQ::invbag::SLOT_INVALID) {
			for (uint32 index = EQ::invbag::SLOT_BEGIN; index <= EQ::invbag::SLOT_END; ++index) {
				EQ::ItemInstance* sub = inst->GetItem(index);
				if (!sub)
					continue;

				++subitem_count;
			}

			buffer.WriteUInt32(subitem_count);

			for (uint32 index = EQ::invbag::SLOT_BEGIN; index <= EQ::invbag::SLOT_END; ++index) {
				EQ::ItemInstance* sub = inst->GetItem(index);
				if (!sub)
					continue;

				buffer.WriteUInt32(index);

				SerializeItem(buffer, sub, SubSlotNumber, (depth + 1), packet_type);
			}
		}

		//bool bCollected;
		buffer.WriteInt8(0); //unsupported atm
		//u64 DontKnow;
		buffer.WriteUInt64(0); //unsupported atm
		//s32 Luck;
		buffer.WriteInt32(0); //unsupported atm
	}

	// A link in a message string is '\x12' <etag digit> <data> <display text> '\x12'.
	//
	// The Laurion client only renders an item tag as a link when the data block is exactly the length
	// its own table says it is. __ConvertItemTags (0x140230AE0) takes TagSizes[etag] - 3 characters as
	// the data and everything after that as the display text, and the parser FUN_1402357e0 returns
	// failure unless the data is exactly 90 characters:
	//
	//   %05X item_id
	//   6 x (%05X socket + %05X socket_luck)                    = 60
	//   %1d is_evolving, %04X evolve_group, %02X evolve_level   = 7
	//   %05X ornament_icon, %05X luck, %08X hash
	//
	// 90 data chars + the etag digit = the 91 char body EQEmu calls SAY_LINK_BODY_SIZE for Laurion.
	// Every other tag (player, spell, achievement, faction, dialog, command) is variable length and is
	// passed through untouched.
	//
	// EQEmu's canonical body is the RoF2 layout (56 chars). SayLinkEngine writes the canonical form,
	// the encoder expands it to Laurion and the decoder shrinks Laurion back to canonical, so the rest
	// of the server only ever sees one layout. The length is checked rather than assumed: when the
	// engine is configured to emit Laurion directly, expanding a segment a second time swallows the
	// display text and the client prints the raw hex - that is the mangling this fixes.

	// Converts one link segment (etag digit + data + display text, without the surrounding '\x12') to
	// `target`. The source layout is read from which fixed data block width the segment fills, so a link
	// that is already in the target form is passed through instead of converted a second time, and a link
	// whose display text starts with a hex character is still attributed to the right layout.
	static inline void ConvertLinkSegment(std::string& message_out, const std::string& segment, EQ::versions::ClientVersion target)
	{
		// A '\x12' whose next character is not a digit is not a link. The client indexes its per-etag size
		// table with that character and the table only covers '0' - '9', so a stray or empty segment makes
		// it read a pointer as a length and copy out of bounds. Drop the delimiter instead of passing it
		// through, so the text is still delivered but never reaches the client as a malformed link.
		if (segment.empty() || !EQ::saylink::IsLinkEtag(segment[0])) {
			LogSaylinkDetail("Laurion segment [{}] is not a link (etag [{}]) - delimiter dropped", segment, segment.empty() ? '\0' : segment[0]);
			message_out.append(segment);
			return;
		}

		message_out.push_back('\x12');

		const char etag = segment[0];
		const size_t laurion_data = EQ::constants::GetSayLinkBodySize(EQ::versions::ClientVersion::Laurion) - 1;   // 90
		const size_t canonical_data = EQ::constants::GetSayLinkBodySize(EQ::versions::ClientVersion::RoF2) - 1;     // 55

		// Only the item tag is length based (client TagSizes: '0' = 93, '1' and '2' = 3, the rest 0).
		if (etag != '0') {
			message_out.append(segment);
			message_out.push_back('\x12');
			return;
		}

		size_t data_length = EQ::saylink::DetectLinkDataWidth(segment);
		EQ::versions::ClientVersion source;

		if (data_length == laurion_data) {
			source = EQ::versions::ClientVersion::Laurion;
		} else if (data_length == canonical_data) {
			source = EQ::versions::ClientVersion::RoF2;
		} else {
			LogSaylinkDetail("Laurion link segment [{}] data length [{}] is not a known layout - passed through unchanged", segment, data_length);
			message_out.append(segment);
			message_out.push_back('\x12');
			return;
		}

		const size_t target_data = EQ::constants::GetSayLinkBodySize(target) - 1;
		if (data_length == target_data) {
			message_out.append(segment);
			message_out.push_back('\x12');
			return;
		}

		EQ::SayLinkBody_Struct body;
		if (!EQ::saylink::DegenerateLinkBody(body, segment.substr(0, data_length + 1), source)) {
			LogSaylinkDetail("Laurion link segment [{}] data length [{}] could not be parsed", segment, data_length);
			message_out.append(segment);
			message_out.push_back('\x12');
			return;
		}

		std::string converted;
		if (!EQ::saylink::GenerateLinkBody(converted, body, target)) {
			message_out.append(segment);
			message_out.push_back('\x12');
			return;
		}

		message_out.append(converted);
		message_out.append(segment.substr(data_length + 1));
		message_out.push_back('\x12');
	}

	static inline void ServerToLaurionConvertLinks(std::string& message_out, const std::string& message_in)
	{
		if (message_in.find('\x12') == std::string::npos) {
			message_out = message_in;
			return;
		}

		auto segments = Strings::Split(message_in, '\x12');
		for (size_t segment_iter = 0; segment_iter < segments.size(); ++segment_iter) {
			if (segment_iter & 1) {
				ConvertLinkSegment(message_out, segments[segment_iter], EQ::versions::ClientVersion::Laurion);
			}
			else {
				message_out.append(segments[segment_iter]);
			}
		}
	}

	// Incoming links arrive in the Laurion layout. Normalise them to the server canonical form so the
	// code that reads a link (#giveitem, #summonitem, Client::GarbleMessage, DialogueWindow) sees one
	// layout.
	static inline void LaurionToServerConvertLinks(std::string& message_out, const std::string& message_in)
	{
		if (message_in.find('\x12') == std::string::npos) {
			message_out = message_in;
			return;
		}

		auto segments = Strings::Split(message_in, '\x12');
		for (size_t segment_iter = 0; segment_iter < segments.size(); ++segment_iter) {
			if (segment_iter & 1) {
				ConvertLinkSegment(message_out, segments[segment_iter], EQ::versions::ClientVersion::RoF2);
			}
			else {
				message_out.append(segments[segment_iter]);
			}
		}
	}

	static inline uint32 ServerToLaurionSpawnAppearanceType(uint32 server_type) {
		switch (server_type)
		{
		case AppearanceType::WhoLevel:
			return structs::LaurionAppearance::WhoLevel;
		case AppearanceType::MaxHealth:
			return structs::LaurionAppearance::MaxHealth;
		case AppearanceType::Invisibility:
			return structs::LaurionAppearance::Invisibility;
		case AppearanceType::PVP:
			return structs::LaurionAppearance::PVP;
		case AppearanceType::Light:
			return structs::LaurionAppearance::Light;
		case AppearanceType::Animation:
			return structs::LaurionAppearance::Animation;
		case AppearanceType::Sneak:
			return structs::LaurionAppearance::Sneak;
		case AppearanceType::SpawnID:
			return structs::LaurionAppearance::SpawnID;
		case AppearanceType::Health:
			return structs::LaurionAppearance::Health;
		case AppearanceType::Linkdead:
			return structs::LaurionAppearance::Linkdead;
		case AppearanceType::FlyMode:
			return structs::LaurionAppearance::FlyMode;
		case AppearanceType::GM:
			return structs::LaurionAppearance::GM;
		case AppearanceType::Anonymous:
			return structs::LaurionAppearance::Anonymous;
		// AppearanceType::GuildID has no Laurion appearance field - the client learns a spawn's
		// guild from the guild packets. Field 14 is the rank, so GuildRank is what maps here.
		case AppearanceType::GuildRank:
			return structs::LaurionAppearance::GuildRank;
		case AppearanceType::AFK:
			return structs::LaurionAppearance::AFK;
		case AppearanceType::Pet:
			return structs::LaurionAppearance::Pet;
		case AppearanceType::Summoned:
			return structs::LaurionAppearance::Summoned;
		case AppearanceType::SetType:
			return structs::LaurionAppearance::NPCName;
		case AppearanceType::CancelSneakHide:
			return structs::LaurionAppearance::CancelSneakHide;
		case AppearanceType::AreaHealthRegen:
			return structs::LaurionAppearance::AreaHealthRegen;
		case AppearanceType::AreaManaRegen:
			return structs::LaurionAppearance::AreaManaRegen;
		case AppearanceType::AreaEnduranceRegen:
			return structs::LaurionAppearance::AreaEnduranceRegen;
		case AppearanceType::FreezeBeneficialBuffs:
			return structs::LaurionAppearance::FreezeBeneficialBuffs;
		case AppearanceType::NPCTintIndex:
			return structs::LaurionAppearance::NPCTintIndex;
		case AppearanceType::ShowHelm:
			return structs::LaurionAppearance::ShowHelm;
		case AppearanceType::DamageState:
			return structs::LaurionAppearance::DamageState;
		case AppearanceType::TextureType:
			return structs::LaurionAppearance::TextureType;
		case AppearanceType::GuildShow:
			return structs::LaurionAppearance::GuildShow;
		case AppearanceType::OfflineMode:
			return structs::LaurionAppearance::OfflineMode;
		default:
			return structs::LaurionAppearance::None;
		}
	}

	static inline uint32 LaurionToServerSpawnAppearanceType(uint32 laurion_type, uint32 laurion_parameter) {
		switch (laurion_type)
		{
		case structs::LaurionAppearance::WhoLevel:
			return AppearanceType::WhoLevel;
		case structs::LaurionAppearance::MaxHealth:
			return AppearanceType::MaxHealth;
		case structs::LaurionAppearance::Invisibility:
			return AppearanceType::Invisibility;
		case structs::LaurionAppearance::PVP:
			return AppearanceType::PVP;
		case structs::LaurionAppearance::Light:
			return AppearanceType::Light;
		case structs::LaurionAppearance::Animation:
			return AppearanceType::Animation;
		case structs::LaurionAppearance::Sneak:
			return AppearanceType::Sneak;
		case structs::LaurionAppearance::SpawnID:
			return AppearanceType::SpawnID;
		case structs::LaurionAppearance::Health:
			return AppearanceType::Health;
		case structs::LaurionAppearance::Linkdead:
			return AppearanceType::Linkdead;
		case structs::LaurionAppearance::FlyMode:
			return AppearanceType::FlyMode;
		case structs::LaurionAppearance::GM:
			return AppearanceType::GM;
		case structs::LaurionAppearance::Anonymous:
			return AppearanceType::Anonymous;
		case structs::LaurionAppearance::GuildRank:
			return AppearanceType::GuildRank;
		case structs::LaurionAppearance::AFK:
			return AppearanceType::AFK;
		case structs::LaurionAppearance::Pet:
			return AppearanceType::Pet;
		case structs::LaurionAppearance::Summoned:
			return AppearanceType::Summoned;
		case structs::LaurionAppearance::SetType:
			return AppearanceType::NPCName;
		case structs::LaurionAppearance::CancelSneakHide:
			return AppearanceType::CancelSneakHide;
		case structs::LaurionAppearance::AreaHealthRegen:
			return AppearanceType::AreaHealthRegen;
		case structs::LaurionAppearance::AreaManaRegen:
			return AppearanceType::AreaManaRegen;
		case structs::LaurionAppearance::AreaEnduranceRegen:
			return AppearanceType::AreaEnduranceRegen;
		case structs::LaurionAppearance::FreezeBeneficialBuffs:
			return AppearanceType::FreezeBeneficialBuffs;
		case structs::LaurionAppearance::NPCTintIndex:
			return AppearanceType::NPCTintIndex;
		case structs::LaurionAppearance::ShowHelm:
			return AppearanceType::ShowHelm;
		case structs::LaurionAppearance::DamageState:
			return AppearanceType::DamageState;
		case structs::LaurionAppearance::TextureType:
			return AppearanceType::TextureType;
		case structs::LaurionAppearance::GuildShow:
			return AppearanceType::GuildShow;
		case structs::LaurionAppearance::OfflineMode:
			return AppearanceType::OfflineMode;
		case structs::LaurionAppearance::Unknown18:
			// Loot Settings window "Auto Split Coin" checkbox (FUN_14027e210 sends type 0x12).
			// EQEmu's canonical type for this is AppearanceType::Split, handled in
			// Client::Handle_OP_SpawnAppearance via m_pp.autosplit.
			return AppearanceType::Split;
		case structs::LaurionAppearance::Unknown40:
			// Client Loot Settings window sends this for the "Use Advanced Looting" checkbox
			return AppearanceType::AdvLootSettings;
		case structs::LaurionAppearance::Unknown41:
			// Client Loot Settings window sends this for the "Master Loot Candidate" checkbox
			return AppearanceType::MasterLootCandidate;
		default:
			// Keep the raw Laurion type visible: without this every unmapped type collapses to
			// AppearanceType::Die and the log shows "Unknown SpawnAppearance type: 0x0000".
			LogLootFilters("SpawnAppearance: unmapped Laurion type [{}] value [{}]", laurion_type, laurion_parameter);
			return AppearanceType::Die;
		}
	}

	// Every InventorySlot_Struct we hand to the client has two 16-bit holes in it (Padding1 at +2 and
	// Padding2 at +10). The client does not treat them as spare bytes: it reads the slot record as three
	// dwords and validates the *whole first dword* (Type | Padding1 << 16) before it will resolve the slot.
	// CInventory::GetItemByGlobalIndex1 / FUN_140656da0 gate on "(dword0 < 0x2a) && Slot != -1", so a single
	// stray non-zero byte in Padding1 makes the client refuse to look the slot up at all. That is what the
	// scroll-learn bug looked like from the player's side: the server scribed the spell and sent the cursor
	// clear (OP_MoveItem from {possessions, 35} to invalid), the client received it, and then refused to
	// remove the item from its cursor because from_slot's first dword was not exactly 0 - the scroll stayed
	// on the cursor and desynced into a corrupted item when it was dragged back. ALLOC_VAR_ENCODE zeroes the
	// outgoing buffer, but assigning a by-value struct over it re-introduces whatever the local held, so the
	// helpers that build these records have to value-initialise them.
	static inline structs::InventorySlot_Struct ServerToLaurionSlot(uint32 server_slot)
	{
		structs::InventorySlot_Struct LaurionSlot{};
		LaurionSlot.Type = invtype::TYPE_INVALID;
		LaurionSlot.Slot = invslot::SLOT_INVALID;
		LaurionSlot.SubIndex = invbag::SLOT_INVALID;
		LaurionSlot.AugIndex = invaug::SOCKET_INVALID;

		uint32 TempSlot = EQ::invslot::SLOT_INVALID;

		if (server_slot < invtype::POSSESSIONS_SIZE) {
			LaurionSlot.Type = invtype::typePossessions;
			LaurionSlot.Slot = ServerGeneralSlotToLaurion(static_cast<int16>(server_slot));
		}

		else if (
			EQ::invbag::IsGeneralBagSlot(EQ::versions::ClientVersion::Laurion, server_slot) ||
			(server_slot >= EQ::invbag::CURSOR_BAG_BEGIN && server_slot <= EQ::invbag::CURSOR_BAG_END)
		) {
			LaurionSlot.Type = invtype::typePossessions;
			LaurionSlot.Slot = ServerGeneralSlotToLaurion(static_cast<int16>(EQ::invbag::GeneralBagParentSlot(EQ::versions::ClientVersion::Laurion, server_slot)));
			LaurionSlot.SubIndex = static_cast<int16>(EQ::invbag::GeneralBagIndex(EQ::versions::ClientVersion::Laurion, server_slot));
		}

		else if (server_slot <= EQ::invslot::TRIBUTE_END && server_slot >= EQ::invslot::TRIBUTE_BEGIN) {
			LaurionSlot.Type = invtype::typeTribute;
			LaurionSlot.Slot = server_slot - EQ::invslot::TRIBUTE_BEGIN;
		}

		else if (server_slot <= EQ::invslot::GUILD_TRIBUTE_END && server_slot >= EQ::invslot::GUILD_TRIBUTE_BEGIN) {
			LaurionSlot.Type = invtype::typeGuildTribute;
			LaurionSlot.Slot = server_slot - EQ::invslot::GUILD_TRIBUTE_BEGIN;
		}

		else if (server_slot == EQ::invslot::SLOT_TRADESKILL_EXPERIMENT_COMBINE) {
			LaurionSlot.Type = invtype::typeWorld;
		}

		else if (server_slot <= EQ::invslot::BANK_END && server_slot >= EQ::invslot::BANK_BEGIN) {
			LaurionSlot.Type = invtype::typeBank;
			LaurionSlot.Slot = server_slot - EQ::invslot::BANK_BEGIN;
		}

		else if (server_slot <= EQ::invbag::BANK_BAGS_END && server_slot >= EQ::invbag::BANK_BAGS_BEGIN) {
			TempSlot = server_slot - EQ::invbag::BANK_BAGS_BEGIN;

			LaurionSlot.Type = invtype::typeBank;
			LaurionSlot.Slot = TempSlot / EQ::invbag::SLOT_COUNT;
			LaurionSlot.SubIndex = TempSlot - (LaurionSlot.Slot * EQ::invbag::SLOT_COUNT);
		}

		else if (server_slot <= EQ::invslot::SHARED_BANK_END && server_slot >= EQ::invslot::SHARED_BANK_BEGIN) {
			LaurionSlot.Type = invtype::typeSharedBank;
			LaurionSlot.Slot = server_slot - EQ::invslot::SHARED_BANK_BEGIN;
		}

		else if (server_slot <= EQ::invbag::SHARED_BANK_BAGS_END && server_slot >= EQ::invbag::SHARED_BANK_BAGS_BEGIN) {
			TempSlot = server_slot - EQ::invbag::SHARED_BANK_BAGS_BEGIN;

			LaurionSlot.Type = invtype::typeSharedBank;
			LaurionSlot.Slot = TempSlot / EQ::invbag::SLOT_COUNT;
			LaurionSlot.SubIndex = TempSlot - (LaurionSlot.Slot * EQ::invbag::SLOT_COUNT);
		}

		else if (server_slot <= EQ::invslot::TRADE_END && server_slot >= EQ::invslot::TRADE_BEGIN) {
			LaurionSlot.Type = invtype::typeTrade;
			LaurionSlot.Slot = server_slot - EQ::invslot::TRADE_BEGIN;
		}

		else if (server_slot <= EQ::invbag::TRADE_BAGS_END && server_slot >= EQ::invbag::TRADE_BAGS_BEGIN) {
			TempSlot = server_slot - EQ::invbag::TRADE_BAGS_BEGIN;

			LaurionSlot.Type = invtype::typeTrade;
			LaurionSlot.Slot = TempSlot / EQ::invbag::SLOT_COUNT;
			LaurionSlot.SubIndex = TempSlot - (LaurionSlot.Slot * EQ::invbag::SLOT_COUNT);
		}

		else if (server_slot <= EQ::invslot::WORLD_END && server_slot >= EQ::invslot::WORLD_BEGIN) {
			LaurionSlot.Type = invtype::typeWorld;
			LaurionSlot.Slot = server_slot - EQ::invslot::WORLD_BEGIN;
		}

		LogNetcodeDetail("Convert Server Slot {} to Laurion Slot [{}, {}, {}, {}]",
			server_slot, LaurionSlot.Type, LaurionSlot.Slot, LaurionSlot.SubIndex, LaurionSlot.AugIndex);

		return LaurionSlot;
	}

	static inline structs::InventorySlot_Struct ServerToLaurionCorpseSlot(uint32 server_corpse_slot)
	{
		structs::InventorySlot_Struct LaurionSlot{};
		LaurionSlot.Type = invtype::TYPE_INVALID;
		LaurionSlot.Slot = ServerToLaurionCorpseMainSlot(server_corpse_slot);
		LaurionSlot.SubIndex = invbag::SLOT_INVALID;
		LaurionSlot.AugIndex = invaug::SOCKET_INVALID;

		if (LaurionSlot.Slot != invslot::SLOT_INVALID)
			LaurionSlot.Type = invtype::typeCorpse;

		LogNetcodeDetail("Convert Server Corpse Slot {} to Laurion Corpse Slot [{}, {}, {}, {}]",
			server_corpse_slot, LaurionSlot.Type, LaurionSlot.Slot, LaurionSlot.SubIndex, LaurionSlot.AugIndex);

		return LaurionSlot;
	}
	
	static inline uint32 ServerToLaurionCorpseMainSlot(uint32 server_corpse_slot)
	{
		uint32 LaurionSlot = invslot::SLOT_INVALID;

		if (server_corpse_slot <= EQ::invslot::CORPSE_END && server_corpse_slot >= EQ::invslot::CORPSE_BEGIN) {
			// FUN_140657530 validates a global index as container < 0x2a && slot < container size, and the
			// client's corpse container is CORPSE_SIZE (36) slots indexed from 0. EQEmu's canonical ids have to
			// be rebased - anything from 36 up is rejected, so those loot rows can never be resolved.
			LaurionSlot = server_corpse_slot - EQ::invslot::CORPSE_BEGIN;
		}

		LogNetcode("Convert Server Corpse Slot [{}] to Laurion Corpse Main Slot [{}]", server_corpse_slot, LaurionSlot);

		return LaurionSlot;
	}

	static inline structs::TypelessInventorySlot_Struct ServerToLaurionTypelessSlot(uint32 server_slot, int16 server_type)
	{
		structs::TypelessInventorySlot_Struct LaurionSlot{};
		LaurionSlot.Slot = invslot::SLOT_INVALID;
		LaurionSlot.SubIndex = invbag::SLOT_INVALID;
		LaurionSlot.AugIndex = invaug::SOCKET_INVALID;

		if (server_type == EQ::invtype::typePossessions) {
			// Must use Laurion's POSSESSIONS_SIZE (36), not EQ::invtype::POSSESSIONS_SIZE (RoF2 = 34).
			// Canonical ids 34 and 35 carry Laurion's extra general slots and sit outside the RoF2 size, so
			// the RoF2 constant made every sell in those slots return SLOT_INVALID. The client's sell ack
			// handler prints eqstr 12062 ("I am not interested in buying that") when the slot is -1, which is
			// exactly the symptom: coins credited by SendMoneyUpdate, item never removed from the model.
			if (server_slot < invtype::POSSESSIONS_SIZE) {
				// Canonical ids 34 and 35 carry Laurion's two extra general slots; the client numbers them
				// 33 and 34, so the reverse direction has to remap them back or the client highlights the
				// wrong slot when the sell ack removes the item.
				LaurionSlot.Slot = ServerGeneralSlotToLaurion(server_slot);
			}

			else if (EQ::invbag::IsGeneralBagSlot(EQ::versions::ClientVersion::Laurion, server_slot)) {
				LaurionSlot.Slot = ServerGeneralSlotToLaurion(EQ::invbag::GeneralBagParentSlot(EQ::versions::ClientVersion::Laurion, server_slot));
				LaurionSlot.SubIndex = EQ::invbag::GeneralBagIndex(EQ::versions::ClientVersion::Laurion, server_slot);
			}
		}

		LogNetcodeDetail("Convert Server Slot {} to Laurion Typeless Slot [{}, {}, {}] (implied type: {})",
			server_slot, LaurionSlot.Slot, LaurionSlot.SubIndex, LaurionSlot.AugIndex, server_type);

		return LaurionSlot;
	}

	static inline uint32 LaurionToServerSlot(structs::InventorySlot_Struct laurion_slot)
	{
		if (laurion_slot.AugIndex < invaug::SOCKET_INVALID || laurion_slot.AugIndex >= invaug::SOCKET_COUNT) {
			LogNetcodeDetail("Convert Laurion Slot [{}, {}, {}, {}] to Server Slot {}",
				laurion_slot.Type, laurion_slot.Slot, laurion_slot.SubIndex, laurion_slot.AugIndex, EQ::invslot::SLOT_INVALID);

			return EQ::invslot::SLOT_INVALID;
		}

		uint32 server_slot = EQ::invslot::SLOT_INVALID;
		uint32 temp_slot = invslot::SLOT_INVALID;

		switch (laurion_slot.Type) {
		case invtype::typePossessions: {
			if (laurion_slot.Slot >= invslot::POSSESSIONS_BEGIN &&
				laurion_slot.Slot <= EQ::invslot::PossessionsEnd(EQ::versions::ClientVersion::Laurion)) {
				if (laurion_slot.SubIndex == invbag::SLOT_INVALID) {
					server_slot = LaurionGeneralSlotToServer(laurion_slot.Slot);
				}

				else if (laurion_slot.SubIndex >= invbag::SLOT_BEGIN && laurion_slot.SubIndex <= invbag::SLOT_END) {
					if (laurion_slot.Slot < invslot::GENERAL_BEGIN)
						return EQ::invslot::SLOT_INVALID;

					// Bag sub-slots are addressed with the server's container stride, not the client's
					// visible bag count, so the parent slot has to go through the general-slot mapping first.
					int16 server_parent = LaurionGeneralSlotToServer(laurion_slot.Slot);
					if (server_parent == EQ::invslot::SLOT_INVALID)
						return EQ::invslot::SLOT_INVALID;

					server_slot = EQ::invbag::GeneralBagSlotId(EQ::versions::ClientVersion::Laurion, server_parent, static_cast<uint8>(laurion_slot.SubIndex));
				}
			}

			break;
		}
		case invtype::typeBank: {
			if (laurion_slot.Slot >= invslot::SLOT_BEGIN && laurion_slot.Slot < invtype::BANK_SIZE) {
				if (laurion_slot.SubIndex == invbag::SLOT_INVALID) {
					server_slot = EQ::invslot::BANK_BEGIN + laurion_slot.Slot;
				}

				else if (laurion_slot.SubIndex >= invbag::SLOT_BEGIN && laurion_slot.SubIndex <= invbag::SLOT_END) {
					temp_slot = laurion_slot.Slot * EQ::invbag::SLOT_COUNT;
					server_slot = EQ::invbag::BANK_BAGS_BEGIN + temp_slot + laurion_slot.SubIndex;
				}
			}

			break;
		}
		case invtype::typeSharedBank: {
			if (laurion_slot.Slot >= invslot::SLOT_BEGIN && laurion_slot.Slot < invtype::SHARED_BANK_SIZE) {
				if (laurion_slot.SubIndex == invbag::SLOT_INVALID) {
					server_slot = EQ::invslot::SHARED_BANK_BEGIN + laurion_slot.Slot;
				}

				else if (laurion_slot.SubIndex >= invbag::SLOT_BEGIN && laurion_slot.SubIndex <= invbag::SLOT_END) {
					temp_slot = laurion_slot.Slot * EQ::invbag::SLOT_COUNT;
					server_slot = EQ::invbag::SHARED_BANK_BAGS_BEGIN + temp_slot + laurion_slot.SubIndex;
				}
			}

			break;
		}
		case invtype::typeTrade: {
			if (laurion_slot.Slot >= invslot::SLOT_BEGIN && laurion_slot.Slot < invtype::TRADE_SIZE) {
				if (laurion_slot.SubIndex == invbag::SLOT_INVALID) {
					server_slot = EQ::invslot::TRADE_BEGIN + laurion_slot.Slot;
				}

				else if (laurion_slot.SubIndex >= invbag::SLOT_BEGIN && laurion_slot.SubIndex <= invbag::SLOT_END) {
					temp_slot = laurion_slot.Slot * EQ::invbag::SLOT_COUNT;
					server_slot = EQ::invbag::TRADE_BAGS_BEGIN + temp_slot + laurion_slot.SubIndex;
				}
			}

			break;
		}
		case invtype::typeWorld: {
			if (laurion_slot.Slot >= invslot::SLOT_BEGIN && laurion_slot.Slot < invtype::WORLD_SIZE) {
				server_slot = EQ::invslot::WORLD_BEGIN + laurion_slot.Slot;
			}

			else if (laurion_slot.Slot == invslot::SLOT_INVALID) {
				server_slot = EQ::invslot::SLOT_TRADESKILL_EXPERIMENT_COMBINE;
			}

			break;
		}
		case invtype::typeLimbo: {
			if (laurion_slot.Slot >= invslot::SLOT_BEGIN && laurion_slot.Slot < invtype::LIMBO_SIZE) {
				server_slot = EQ::invslot::slotCursor;
			}

			break;
		}
		case invtype::typeTribute: {
			if (laurion_slot.Slot >= invslot::SLOT_BEGIN && laurion_slot.Slot < invtype::TRIBUTE_SIZE) {
				server_slot = EQ::invslot::TRIBUTE_BEGIN + laurion_slot.Slot;
			}

			break;
		}
		case invtype::typeGuildTribute: {
			if (laurion_slot.Slot >= invslot::SLOT_BEGIN && laurion_slot.Slot < invtype::GUILD_TRIBUTE_SIZE) {
				server_slot = EQ::invslot::GUILD_TRIBUTE_BEGIN + laurion_slot.Slot;
			}

			break;
		}
		case invtype::typeCorpse: {
			if (laurion_slot.Slot >= invslot::CORPSE_BEGIN && laurion_slot.Slot <= invslot::CORPSE_END) {
				server_slot = laurion_slot.Slot;
			}

			break;
		}
		default: {

			break;
		}
		}

		LogNetcodeDetail("Convert Laurion Slot [{}, {}, {}, {}] to Server Slot {}",
			laurion_slot.Type, laurion_slot.Slot, laurion_slot.SubIndex, laurion_slot.AugIndex, server_slot);

		return server_slot;
	}

	static inline uint32 LaurionToServerCorpseSlot(structs::InventorySlot_Struct laurion_corpse_slot)
	{
		uint32 ServerSlot = EQ::invslot::SLOT_INVALID;

		if (laurion_corpse_slot.Type != invtype::typeCorpse || laurion_corpse_slot.SubIndex != invbag::SLOT_INVALID || laurion_corpse_slot.AugIndex != invaug::SOCKET_INVALID) {
			ServerSlot = EQ::invslot::SLOT_INVALID;
		}

		else {
			ServerSlot = LaurionToServerCorpseMainSlot(laurion_corpse_slot.Slot);
		}

		LogNetcodeDetail("Convert Laurion Slot [{}, {}, {}, {}] to Server Slot {}",
			laurion_corpse_slot.Type, laurion_corpse_slot.Slot, laurion_corpse_slot.SubIndex, laurion_corpse_slot.AugIndex, ServerSlot);

		return ServerSlot;
	}

	static inline uint32 LaurionToServerCorpseMainSlot(uint32 laurion_corpse_slot)
	{
		uint32 ServerSlot = EQ::invslot::SLOT_INVALID;

		if (laurion_corpse_slot < static_cast<uint32>(invtype::CORPSE_SIZE)) {
			ServerSlot = laurion_corpse_slot + EQ::invslot::CORPSE_BEGIN;
		}

		LogNetcode("Convert Laurion Corpse Main Slot [{}] to Server Corpse Slot [{}]", laurion_corpse_slot, ServerSlot);

		return ServerSlot;
	}

	static inline uint32 LaurionToServerTypelessSlot(structs::TypelessInventorySlot_Struct laurion_slot, int16 laurion_type)
	{
		if (laurion_slot.AugIndex < invaug::SOCKET_INVALID || laurion_slot.AugIndex >= invaug::SOCKET_COUNT) {
			LogNetcodeDetail("Convert Laurion Typeless Slot [{}, {}, {}] (implied type: {}) to Server Slot {}",
				laurion_slot.Slot, laurion_slot.SubIndex, laurion_slot.AugIndex, laurion_type, EQ::invslot::SLOT_INVALID);

			return EQ::invslot::SLOT_INVALID;
		}

		uint32 ServerSlot = EQ::invslot::SLOT_INVALID;
		uint32 TempSlot = invslot::SLOT_INVALID;

		switch (laurion_type) {
		case invtype::typePossessions: {
			// Laurion numbers the inventory window as 23 equipment slots (0-22), 12 general slots (client
			// 23-34) and a cursor at client slot 35. The canonical RoF2 range this function is otherwise
			// written against stops at 33, so client slots 34 and 35 used to be rejected outright.
			if (laurion_slot.Slot >= invslot::POSSESSIONS_BEGIN &&
				laurion_slot.Slot <= EQ::invslot::PossessionsEnd(EQ::versions::ClientVersion::Laurion)) {
				if (laurion_slot.SubIndex == invbag::SLOT_INVALID) {
					// Remap Laurion's two extra general slots onto the canonical ids EQEmu stores them on.
					ServerSlot = LaurionGeneralSlotToServer(laurion_slot.Slot);
				}

				else if (laurion_slot.SubIndex >= invbag::SLOT_BEGIN && laurion_slot.SubIndex <= invbag::SLOT_END) {
					if (laurion_slot.Slot < invslot::GENERAL_BEGIN)
						return EQ::invslot::SLOT_INVALID;

					ServerSlot = EQ::invbag::GeneralBagSlotId(EQ::versions::ClientVersion::Laurion, LaurionGeneralSlotToServer(laurion_slot.Slot), static_cast<uint8>(laurion_slot.SubIndex));
				}
			}

			break;
		}
		case invtype::typeBank: {
			if (laurion_slot.Slot >= invslot::SLOT_BEGIN && laurion_slot.Slot < invtype::BANK_SIZE) {
				if (laurion_slot.SubIndex == invbag::SLOT_INVALID) {
					ServerSlot = EQ::invslot::BANK_BEGIN + laurion_slot.Slot;
				}

				else if (laurion_slot.SubIndex >= invbag::SLOT_BEGIN && laurion_slot.SubIndex <= invbag::SLOT_END) {
					TempSlot = laurion_slot.Slot * EQ::invbag::SLOT_COUNT;
					ServerSlot = EQ::invbag::BANK_BAGS_BEGIN + TempSlot + laurion_slot.SubIndex;
				}
			}

			break;
		}
		case invtype::typeSharedBank: {
			if (laurion_slot.Slot >= invslot::SLOT_BEGIN && laurion_slot.Slot < invtype::SHARED_BANK_SIZE) {
				if (laurion_slot.SubIndex == invbag::SLOT_INVALID) {
					ServerSlot = EQ::invslot::SHARED_BANK_BEGIN + laurion_slot.Slot;
				}

				else if (laurion_slot.SubIndex >= invbag::SLOT_BEGIN && laurion_slot.SubIndex <= invbag::SLOT_END) {
					TempSlot = laurion_slot.Slot * EQ::invbag::SLOT_COUNT;
					ServerSlot = EQ::invbag::SHARED_BANK_BAGS_BEGIN + TempSlot + laurion_slot.SubIndex;
				}
			}

			break;
		}
		case invtype::typeTrade: {
			if (laurion_slot.Slot >= invslot::SLOT_BEGIN && laurion_slot.Slot < invtype::TRADE_SIZE) {
				if (laurion_slot.SubIndex == invbag::SLOT_INVALID) {
					ServerSlot = EQ::invslot::TRADE_BEGIN + laurion_slot.Slot;
				}

				else if (laurion_slot.SubIndex >= invbag::SLOT_BEGIN && laurion_slot.SubIndex <= invbag::SLOT_END) {
					TempSlot = laurion_slot.Slot * EQ::invbag::SLOT_COUNT;
					ServerSlot = EQ::invbag::TRADE_BAGS_BEGIN + TempSlot + laurion_slot.SubIndex;
				}
			}

			break;
		}
		case invtype::typeWorld: {
			if (laurion_slot.Slot >= invslot::SLOT_BEGIN && laurion_slot.Slot < invtype::WORLD_SIZE) {
				ServerSlot = EQ::invslot::WORLD_BEGIN + laurion_slot.Slot;
			}

			else if (laurion_slot.Slot == invslot::SLOT_INVALID) {
				ServerSlot = EQ::invslot::SLOT_TRADESKILL_EXPERIMENT_COMBINE;
			}

			break;
		}
		case invtype::typeLimbo: {
			if (laurion_slot.Slot >= invslot::SLOT_BEGIN && laurion_slot.Slot < invtype::LIMBO_SIZE) {
				ServerSlot = EQ::invslot::slotCursor;
			}

			break;
		}
		case invtype::typeTribute: {
			if (laurion_slot.Slot >= invslot::SLOT_BEGIN && laurion_slot.Slot < invtype::TRIBUTE_SIZE) {
				ServerSlot = EQ::invslot::TRIBUTE_BEGIN + laurion_slot.Slot;
			}

			break;
		}
		case invtype::typeGuildTribute: {
			if (laurion_slot.Slot >= invslot::SLOT_BEGIN && laurion_slot.Slot < invtype::GUILD_TRIBUTE_SIZE) {
				ServerSlot = EQ::invslot::GUILD_TRIBUTE_BEGIN + laurion_slot.Slot;
			}

			break;
		}
		case invtype::typeCorpse: {
			if (laurion_slot.Slot >= invslot::CORPSE_BEGIN && laurion_slot.Slot <= invslot::CORPSE_END) {
				ServerSlot = laurion_slot.Slot;
			}

			break;
		}
		default: {

			break;
		}
		}

		LogNetcodeDetail("Convert Laurion Typeless Slot [{}, {}, {}] (implied type: {}) to Server Slot {}",
			laurion_slot.Slot, laurion_slot.SubIndex, laurion_slot.AugIndex, laurion_type, ServerSlot);

		return ServerSlot;
	}

	static inline structs::InventorySlot_Struct LaurionCastingInventorySlotToInventorySlot(structs::CastSpellInventorySlot_Struct laurion_slot) {
		structs::InventorySlot_Struct ret{};
		ret.Type = laurion_slot.type;
		ret.Slot = laurion_slot.slot;
		ret.SubIndex = laurion_slot.sub_index;
		ret.AugIndex = laurion_slot.aug_index;
		return ret;
	}

	static inline structs::CastSpellInventorySlot_Struct LaurionInventorySlotToCastingInventorySlot(structs::InventorySlot_Struct laurion_slot) {
		structs::CastSpellInventorySlot_Struct ret{};
		ret.type = laurion_slot.Type;
		ret.slot = laurion_slot.Slot;
		ret.sub_index = laurion_slot.SubIndex;
		ret.aug_index = laurion_slot.AugIndex;
		return ret;
	}

	static item::ItemPacketType ServerToLaurionItemPacketType(ItemPacketType server_type) {
		switch (server_type) {
		case ItemPacketType::ItemPacketMerchant:
			return item::ItemPacketType::ItemPacketMerchant;
		case ItemPacketType::ItemPacketTradeView:
			return item::ItemPacketType::ItemPacketTradeView;
		case ItemPacketType::ItemPacketLoot:
			return item::ItemPacketType::ItemPacketLoot;
		case ItemPacketType::ItemPacketTrade:
			return item::ItemPacketType::ItemPacketTrade;
		case ItemPacketType::ItemPacketCharInventory:
			return item::ItemPacketType::ItemPacketCharInventory;
		case ItemPacketType::ItemPacketLimbo:
			return item::ItemPacketType::ItemPacketLimbo;
		case ItemPacketType::ItemPacketWorldContainer:
			return item::ItemPacketType::ItemPacketWorldContainer;
		case ItemPacketType::ItemPacketTributeItem:
			return item::ItemPacketType::ItemPacketTributeItem;
		case ItemPacketType::ItemPacketGuildTribute:
			return item::ItemPacketType::ItemPacketGuildTribute;
		case ItemPacketType::ItemPacketCharmUpdate:
			return item::ItemPacketType::ItemPacketCharmUpdate;
		case ItemPacketType::ItemPacketRecovery:
			// Canonical 0x71 -> Laurion 0x72. Without this case the packet falls through to
			// ItemPacketInvalid and ENCODE(OP_ItemPacket) drops it, so the Recover page never fills.
			return item::ItemPacketType::ItemPacketRecovery;
		case ItemPacketType::ItemPacketParcel:
			return item::ItemPacketType::ItemPacketParcel;
		default:
			return item::ItemPacketType::ItemPacketInvalid;
		}
	}

	//This stuff isn't right because they for one removed potion belt
	//This will probably be enough to get casting working for now though
	static inline spells::CastingSlot ServerToLaurionCastingSlot(EQ::spells::CastingSlot slot) {
		switch (slot) {
		case EQ::spells::CastingSlot::Gem1:
			return spells::CastingSlot::Gem1;
		case EQ::spells::CastingSlot::Gem2:
			return spells::CastingSlot::Gem2;
		case EQ::spells::CastingSlot::Gem3:
			return spells::CastingSlot::Gem3;
		case EQ::spells::CastingSlot::Gem4:
			return spells::CastingSlot::Gem4;
		case EQ::spells::CastingSlot::Gem5:
			return spells::CastingSlot::Gem5;
		case EQ::spells::CastingSlot::Gem6:
			return spells::CastingSlot::Gem6;
		case EQ::spells::CastingSlot::Gem7:
			return spells::CastingSlot::Gem7;
		case EQ::spells::CastingSlot::Gem8:
			return spells::CastingSlot::Gem8;
		case EQ::spells::CastingSlot::Gem9:
			return spells::CastingSlot::Gem9;
		case EQ::spells::CastingSlot::Gem10:
			return spells::CastingSlot::Gem10;
		case EQ::spells::CastingSlot::Gem11:
			return spells::CastingSlot::Gem11;
		case EQ::spells::CastingSlot::Gem12:
			return spells::CastingSlot::Gem12;
		case EQ::spells::CastingSlot::Item:
		case EQ::spells::CastingSlot::PotionBelt:
			return spells::CastingSlot::Item;
		case EQ::spells::CastingSlot::Discipline:
			return spells::CastingSlot::Discipline;
		case EQ::spells::CastingSlot::AltAbility:
			return spells::CastingSlot::AltAbility;
		default: // we shouldn't have any issues with other slots ... just return something
			return spells::CastingSlot::Discipline;
		}
	}

	static inline EQ::spells::CastingSlot LaurionToServerCastingSlot(spells::CastingSlot slot) {
		switch (slot) {
		case spells::CastingSlot::Gem1:
			return EQ::spells::CastingSlot::Gem1;
		case spells::CastingSlot::Gem2:
			return EQ::spells::CastingSlot::Gem2;
		case spells::CastingSlot::Gem3:
			return EQ::spells::CastingSlot::Gem3;
		case spells::CastingSlot::Gem4:
			return EQ::spells::CastingSlot::Gem4;
		case spells::CastingSlot::Gem5:
			return EQ::spells::CastingSlot::Gem5;
		case spells::CastingSlot::Gem6:
			return EQ::spells::CastingSlot::Gem6;
		case spells::CastingSlot::Gem7:
			return EQ::spells::CastingSlot::Gem7;
		case spells::CastingSlot::Gem8:
			return EQ::spells::CastingSlot::Gem8;
		case spells::CastingSlot::Gem9:
			return EQ::spells::CastingSlot::Gem9;
		case spells::CastingSlot::Gem10:
			return EQ::spells::CastingSlot::Gem10;
		case spells::CastingSlot::Gem11:
			return EQ::spells::CastingSlot::Gem11;
		case spells::CastingSlot::Gem12:
			return EQ::spells::CastingSlot::Gem12;
		case spells::CastingSlot::Discipline:
			return EQ::spells::CastingSlot::Discipline;
		case spells::CastingSlot::Item:
			return EQ::spells::CastingSlot::Item;
		case spells::CastingSlot::AltAbility:
			return EQ::spells::CastingSlot::AltAbility;
		default: // we shouldn't have any issues with other slots ... just return something
			return EQ::spells::CastingSlot::Discipline;
		}
	}

	//Laurion has the same # of long buffs as rof2, but 10 more short buffs
	static inline int ServerToLaurionBuffSlot(int index)
	{
		// we're a disc
		if (index >= EQ::spells::LONG_BUFFS + EQ::spells::SHORT_BUFFS)
			return index - EQ::spells::LONG_BUFFS - EQ::spells::SHORT_BUFFS +
			spells::LONG_BUFFS + spells::SHORT_BUFFS;
		// we're a song
		if (index >= EQ::spells::LONG_BUFFS)
			return index - EQ::spells::LONG_BUFFS + spells::LONG_BUFFS;
		// we're a normal buff
		return index; // as long as we guard against bad slots server side, we should be fine
	}

	static inline int LaurionToServerBuffSlot(int index)
	{
		// we're a disc
		if (index >= spells::LONG_BUFFS + spells::SHORT_BUFFS)
			return index - spells::LONG_BUFFS - spells::SHORT_BUFFS + EQ::spells::LONG_BUFFS +
			EQ::spells::SHORT_BUFFS;
		// we're a song
		if (index >= spells::LONG_BUFFS)
			return index - spells::LONG_BUFFS + EQ::spells::LONG_BUFFS;
		// we're a normal buff
		return index; // as long as we guard against bad slots server side, we should be fine
	}
} /*Laurion*/
