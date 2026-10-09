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

#include "../eq_packet_structs.h"
#include "../misc_functions.h"
#include "../strings.h"
#include "../inventory_profile.h"
#include "laurion_structs.h"
#include "../rulesys.h"
#include "../path_manager.h"
#include "../classes.h"
#include "../races.h"
#include "../raid.h"

#include <iostream>
#include <sstream>
#include <numeric>
#include <cassert>
#include <cinttypes>
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
			Log(Logs::General, Logs::Netcode, "[STRUCTS] Wrong size on outbound %s: Got %d, expected multiple of %d",
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
		Log(Logs::Detail, Logs::Netcode, "Laurion::ENCODE(OP_DeleteCharge)");

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

		Log(Logs::Detail, Logs::Netcode, "Laurion::ENCODE(OP_MoveItem)");

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
		*/

		out.WriteInt32(emu->guild_id);
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

	ENCODE(OP_SendMembership) {
		ENCODE_LENGTH_EXACT(Membership_Struct);
		SETUP_DIRECT_ENCODE(Membership_Struct, structs::Membership_Struct);

		eq->membership = emu->membership;
		eq->races = emu->races;
		eq->classes = emu->classes;
		eq->entrysize = 33;
		eq->entries[0] = -1;  // Max AA Restriction
		eq->entries[1] = -1;  // Max Level Restriction
		eq->entries[2] = -1;  // Max Char Slots per Account (not used by client?)
		eq->entries[3] = -1;  // SpellTier
		eq->entries[4] = -1;  // Main Inventory Size
		eq->entries[5] = -1;  // Max Platinum per level
		eq->entries[6] = 1;   // Send Mail
		eq->entries[7] = 1;   // Use Parcels
		eq->entries[8] = 1;   // Loyalty
		eq->entries[9] = -1;  // Merc Tiers
		eq->entries[10] = 1;  // Housing
		eq->entries[11] = -1; // Shared Bank Slots
		eq->entries[12] = -1; // Max Journal Quests
		eq->entries[13] = 1;  // CreateGuild
		eq->entries[14] = 1;  // Bazaar
		eq->entries[15] = 1;  // Barter
		eq->entries[16] = 1;  // Chat
		eq->entries[17] = 1;  // Petition
		eq->entries[18] = 1;  // Advertising
		eq->entries[19] = -1; // UseItem
		eq->entries[20] = -1; // StartingCity
		eq->entries[21] = 1;  // Ornament
		eq->entries[22] = 0;  // HeroicCharacter
		eq->entries[23] = 0;  // AutoGrantAA
		eq->entries[24] = 0;  // MountKeyRingSlots
		eq->entries[25] = 0;  // IllusionKeyRingSlots
		eq->entries[26] = 0;  // FamiliarKeyRingSlots
		eq->entries[27] = 0;  // FamiliarAutoLeave
		eq->entries[28] = 0;  // HeroForgeKeyRingSlots
		eq->entries[29] = 0;  // DragonHoardSlots
		eq->entries[30] = 0;  // TeleportKeyRingSlots
		eq->entries[31] = 0;  // PersonalDepotSlots
		eq->entries[32] = 0;

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
			buffer.WriteUInt32(0); // CharacterGuid.Id - not available on server side
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
			//We don't actually support multiple body types yet, but we should consider it in the future

			if (!emu->DestructibleObject)
			{
				buffer.WriteUInt8(1);
				buffer.WriteUInt32(emu->bodytype);
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

	DECODE(OP_CastSpell)
	{
		DECODE_LENGTH_EXACT(structs::CastSpell_Struct);
		SETUP_DIRECT_DECODE(CastSpell_Struct, structs::CastSpell_Struct);

		emu->slot = static_cast<uint32>(LaurionToServerCastingSlot(static_cast<spells::CastingSlot>(eq->slot)));

		IN(spell_id);
		emu->inventoryslot = -1;
		IN(target_id);
		IN(y_pos);
		IN(x_pos);
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
		strn0cpy(emu->sender, Target, sizeof(emu->sender));
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
		emu->return_home = 0;
		emu->tutorial = 0;

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

	DECODE(OP_GroupDisband)
	{
		DECODE_LENGTH_EXACT(structs::GroupGeneric_Struct);
		SETUP_DIRECT_DECODE(GroupGeneric_Struct, structs::GroupGeneric_Struct);

		memcpy(emu->name1, eq->name1, sizeof(emu->name1));
		memcpy(emu->name2, eq->name2, sizeof(emu->name2));

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GroupInvite)
	{
		DECODE_LENGTH_EXACT(structs::GroupGeneric_Struct);
		SETUP_DIRECT_DECODE(GroupGeneric_Struct, structs::GroupGeneric_Struct);

		memcpy(emu->name1, eq->name1, sizeof(emu->name1));
		memcpy(emu->name2, eq->name2, sizeof(emu->name2));

		FINISH_DIRECT_DECODE();
	}

	DECODE(OP_GroupInvite2)
	{
		DECODE_FORWARD(OP_GroupInvite);
	}

	DECODE(OP_MoveItem)
	{
		DECODE_LENGTH_EXACT(structs::MoveItem_Struct);
		SETUP_DIRECT_DECODE(MoveItem_Struct, structs::MoveItem_Struct);

		Log(Logs::Detail, Logs::Netcode, "Laurion::DECODE(OP_MoveItem)");

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

		Log(Logs::Detail, Logs::Netcode, "Laurion::DECODE(OP_MoveMultipleItems) count [{}]", count);

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

		IN(spawn_id);
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

	DECODE(OP_Consume) // 0x5ef7
	{
		// Laurion widened the packet to 20 bytes: slot, item global index, uint16, auto_consumed and a
		// uint32 type (0x100 = food, 0x101 = water).  EQEmu's Consume_Struct is 16 bytes and expects
		// type 0x01 / 0x02, so map the Laurion values back.
		DECODE_LENGTH_EXACT(structs::Consume_Struct);
		SETUP_DIRECT_DECODE(Consume_Struct, structs::Consume_Struct);

		IN(slot);
		emu->auto_consumed = eq->auto_consumed;
		emu->c_unknown1[0] = 0;
		emu->c_unknown1[1] = 0;
		emu->c_unknown1[2] = 0;
		emu->c_unknown1[3] = 0;
		emu->type = (eq->type == 0x101) ? 0x02 : 0x01;
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

	DECODE(OP_GroupFollow)
	{
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

	static inline void ServerToLaurionConvertLinks(std::string& message_out, const std::string& message_in)
	{
		if (message_in.find('\x12') == std::string::npos) {
			message_out = message_in;
			return;
		}

		auto segments = Strings::Split(message_in, '\x12');
		for (size_t segment_iter = 0; segment_iter < segments.size(); ++segment_iter) {
			if (segment_iter & 1) {
				auto etag = std::stoi(segments[segment_iter].substr(0, 1));

				switch (etag) {
				case 0:
				{
					size_t index = 1;
					auto item_id = segments[segment_iter].substr(index, 5);
					index += 5;
					
					auto aug1 = segments[segment_iter].substr(index, 5);
					index += 5;
					
					auto aug2 = segments[segment_iter].substr(index, 5);
					index += 5;
					
					auto aug3 = segments[segment_iter].substr(index, 5);
					index += 5;
					
					auto aug4 = segments[segment_iter].substr(index, 5);
					index += 5;
					
					auto aug5 = segments[segment_iter].substr(index, 5);
					index += 5;
					
					auto aug6 = segments[segment_iter].substr(index, 5);
					index += 5;
					
					auto is_evolving = segments[segment_iter].substr(index, 1);
					index += 1;
					
					auto evolutionGroup = segments[segment_iter].substr(index, 4);
					index += 4;
					
					auto evolutionLevel = segments[segment_iter].substr(index, 2);
					index += 2;
					
					auto ornamentationIconID = segments[segment_iter].substr(index, 5);
					index += 5;
					
					auto itemHash = segments[segment_iter].substr(index, 8);
					index += 8;
					
					auto text = segments[segment_iter].substr(index);
					
					message_out.push_back('\x12');
					message_out.push_back('0'); //etag item
					message_out.append(item_id);
					message_out.append(aug1);
					message_out.append("00000");
					message_out.append(aug2);
					message_out.append("00000");
					message_out.append(aug3);
					message_out.append("00000");
					message_out.append(aug4);
					message_out.append("00000");
					message_out.append(aug5);
					message_out.append("00000");
					message_out.append(aug6);
					message_out.append("00000");
					message_out.append(is_evolving);
					message_out.append(evolutionGroup);
					message_out.append(evolutionLevel);
					message_out.append(ornamentationIconID);
					message_out.append("00000");
					message_out.append(itemHash);
					message_out.append(text);
					message_out.push_back('\x12');

					break;
				}
				default:
					//unsupported etag right now; just pass it as is
					message_out.append(segments[segment_iter]);
					break;
				}
			}
			else {
				message_out.append(segments[segment_iter]);
			}
		}
	}

	static inline void LaurionToServerConvertLinks(std::string& message_out, const std::string& message_in) {
		message_out = message_in;
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
		case AppearanceType::GuildID:
			return structs::LaurionAppearance::GuildID;
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
		case structs::LaurionAppearance::GuildID:
			return AppearanceType::GuildID;
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

	static inline structs::InventorySlot_Struct ServerToLaurionSlot(uint32 server_slot)
	{
		structs::InventorySlot_Struct LaurionSlot;
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

		Log(Logs::Detail, Logs::Netcode, "Convert Server Slot %i to Laurion Slot [%i, %i, %i, %i]",
			server_slot, LaurionSlot.Type, LaurionSlot.Slot, LaurionSlot.SubIndex, LaurionSlot.AugIndex);

		return LaurionSlot;
	}

	static inline structs::InventorySlot_Struct ServerToLaurionCorpseSlot(uint32 server_corpse_slot)
	{
		structs::InventorySlot_Struct LaurionSlot;
		LaurionSlot.Type = invtype::TYPE_INVALID;
		LaurionSlot.Slot = ServerToLaurionCorpseMainSlot(server_corpse_slot);
		LaurionSlot.SubIndex = invbag::SLOT_INVALID;
		LaurionSlot.AugIndex = invaug::SOCKET_INVALID;

		if (LaurionSlot.Slot != invslot::SLOT_INVALID)
			LaurionSlot.Type = invtype::typeCorpse;

		Log(Logs::Detail, Logs::Netcode, "Convert Server Corpse Slot %i to Laurion Corpse Slot [%i, %i, %i, %i]",
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
		structs::TypelessInventorySlot_Struct LaurionSlot;
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

		Log(Logs::Detail, Logs::Netcode, "Convert Server Slot %i to Laurion Typeless Slot [%i, %i, %i] (implied type: %i)",
			server_slot, LaurionSlot.Slot, LaurionSlot.SubIndex, LaurionSlot.AugIndex, server_type);

		return LaurionSlot;
	}

	static inline uint32 LaurionToServerSlot(structs::InventorySlot_Struct laurion_slot)
	{
		if (laurion_slot.AugIndex < invaug::SOCKET_INVALID || laurion_slot.AugIndex >= invaug::SOCKET_COUNT) {
			Log(Logs::Detail, Logs::Netcode, "Convert Laurion Slot [%i, %i, %i, %i] to Server Slot %i",
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

		Log(Logs::Detail, Logs::Netcode, "Convert Laurion Slot [%i, %i, %i, %i] to Server Slot %i",
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

		Log(Logs::Detail, Logs::Netcode, "Convert Laurion Slot [%i, %i, %i, %i] to Server Slot %i",
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
			Log(Logs::Detail, Logs::Netcode, "Convert Laurion Typeless Slot [%i, %i, %i] (implied type: %i) to Server Slot %i",
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

		Log(Logs::Detail, Logs::Netcode, "Convert Laurion Typeless Slot [%i, %i, %i] (implied type: %i) to Server Slot %i",
			laurion_slot.Slot, laurion_slot.SubIndex, laurion_slot.AugIndex, laurion_type, ServerSlot);

		return ServerSlot;
	}

	static inline structs::InventorySlot_Struct LaurionCastingInventorySlotToInventorySlot(structs::CastSpellInventorySlot_Struct laurion_slot) {
		structs::InventorySlot_Struct ret;
		ret.Type = laurion_slot.type;
		ret.Slot = laurion_slot.slot;
		ret.SubIndex = laurion_slot.sub_index;
		ret.AugIndex = laurion_slot.aug_index;
		return ret;
	}

	static inline structs::CastSpellInventorySlot_Struct LaurionInventorySlotToCastingInventorySlot(structs::InventorySlot_Struct laurion_slot) {
		structs::CastSpellInventorySlot_Struct ret;
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
