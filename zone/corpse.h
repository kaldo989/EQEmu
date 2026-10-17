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

#include "common/loot.h"
#include "common/repositories/character_corpses_repository.h"
#include "zone/advloot_session.h"
#include "zone/client.h"
#include "zone/mob.h"

#include <map>
#include <memory>
#include <vector>

class EQApplicationPacket;
class Group;
class NPC;
class Raid;

struct ExtraAttackOptions;
struct NPCType;

namespace EQ {
	class ItemInstance;
}

#define MAX_LOOTERS 72

class Corpse : public Mob {
public:

	static void SendEndLootErrorPacket(Client *client);
	static void SendLootReqErrorPacket(Client *client, LootResponse response = LootResponse::NotAtThisTime);

	Corpse(
		NPC *npc,
		LootItems *item_list,
		uint32 npc_type_id,
		const NPCType **npc_type_data,
		uint32 decay_time = 600000
	);
	Corpse(Client *c, int32 rez_exp, KilledByTypes killed_by = KilledByTypes::Killed_NPC);
	Corpse(
		uint32 corpse_id,
		uint32 character_id,
		const char *character_name,
		LootItems *item_list,
		uint32 copper,
		uint32 silver,
		uint32 gold,
		uint32 platinum,
		const glm::vec4 &position,
		float size,
		uint8 gender,
		uint16 race,
		uint8 class_,
		uint8 deity,
		uint8 level,
		uint8 texture,
		uint8 helm_texture,
		uint32 rez_exp,
		uint32 gm_rez_exp,
		KilledByTypes killed_by,
		bool is_rezzable,
		uint32 rez_remaining_time,
		bool was_at_graveyard = false
	);

	~Corpse();

	/* Corpse: General */
	virtual bool Death(
		Mob *killer_mob,
		int64 damage,
		uint16 spell_id,
		EQ::skills::SkillType attack_skill,
		KilledByTypes killed_by = KilledByTypes::Killed_NPC,
		bool is_buff_tic = false
	) { return true; }

	virtual void Damage(
		Mob *from,
		int64 damage,
		uint16 spell_id,
		EQ::skills::SkillType attack_skill,
		bool avoidable = true,
		int8 buffslot = -1,
		bool iBuffTic = false,
		eSpecialAttacks special = eSpecialAttacks::None
	) { }

	bool Attack(
		Mob *other, int Hand = EQ::invslot::slotPrimary, bool FromRiposte = false, bool IsStrikethrough = true,
		bool IsFromSpell = false, ExtraAttackOptions *opts = nullptr
	) override
	{
		return false;
	}

	virtual bool HasRaid() { return false; }
	virtual bool HasGroup() { return false; }
	virtual Raid *GetRaid() { return nullptr; }
	virtual Group *GetGroup() { return nullptr; }
	inline uint32 GetCorpseDBID() { return m_corpse_db_id; }
	inline char *GetOwnerName() { return corpse_name; }
	bool IsEmpty() const;
	bool IsCorpse() const { return true; }
	bool IsPlayerCorpse() const { return m_is_player_corpse; }
	bool IsNPCCorpse() const { return !m_is_player_corpse; }
	bool IsBecomeNPCCorpse() const { return m_become_npc; }
	virtual void DepopNPCCorpse();
	virtual void DepopPlayerCorpse();
	bool Process();
	bool Save();

	uint32 GetCharID() { return m_character_id; }

	uint32 SetCharID(uint32 iCharID)
	{
		if (IsPlayerCorpse()) {
			return (m_character_id = iCharID);
		}
		return 0xFFFFFFFF;
	};

	uint32 GetDecayTime()
	{
		if (!m_corpse_decay_timer.Enabled()) {
			return 0xFFFFFFFF;
		}
		else {
			return m_corpse_decay_timer.GetRemainingTime();
		}
	}
	uint32 GetRezTime()
	{
		if (!m_corpse_rezzable_timer.Enabled()) {
			return 0;
		}
		else {
			return m_corpse_rezzable_timer.GetRemainingTime();
		}
	}

	void ResetDecayTimer();
	void SetDecayTimer(uint32 decay_time);
	void SetConsentGroupID(uint32 group_id) { if (IsPlayerCorpse()) { m_consented_group_id = group_id; }}
	void SetConsentRaidID(uint32 raid_id) { if (IsPlayerCorpse()) { m_consented_raid_id = raid_id; }}
	void SetConsentGuildID(uint32 guild_id) { if (IsPlayerCorpse()) { m_consented_guild_id = guild_id; }}
	void AddConsentName(const std::string& consent_player_name);
	void RemoveConsentName(const std::string& consent_player_name);
	void SendWorldSpawnPlayerCorpseInZone(uint32_t zone_id);
	bool MovePlayerCorpseToGraveyard();
	bool MovePlayerCorpseToNonInstance();

	void Delete();
	void Bury();
	void CalcCorpseName();
	void LoadPlayerCorpseDecayTime(uint32 dbid);

	/* Corpse: Items */
	uint32 GetWornItem(int16 equip_slot) const;
	LootItem *GetItem(uint16 lootslot, LootItem **bag_item_data = 0);
	void SetPlayerKillItemID(int32 pk_item_id) { m_player_kill_item = pk_item_id; }
	int32 GetPlayerKillItem() { return m_player_kill_item; }
	void RemoveItem(uint16 lootslot);
	void RemoveItem(LootItem *item_data);
	void RemoveItemByID(uint32 item_id, int quantity = 1);
	void AddItem(
		uint32 itemnum,
		uint16 charges,
		int16 slot = 0,
		uint32 aug1 = 0,
		uint32 aug2 = 0,
		uint32 aug3 = 0,
		uint32 aug4 = 0,
		uint32 aug5 = 0,
		uint32 aug6 = 0,
		bool attuned = false,
		const std::string &custom_data = std::string(),
		uint32 ornamenticon = 0,
		uint32 ornamentidfile = 0,
		uint32 ornament_hero_model = 0
	);

	/* Corpse: Coin */
	void SetCash(uint32 in_copper, uint32 in_silver, uint32 in_gold, uint32 in_platinum);
	void RemoveCash();
	uint32 GetCopper() { return m_copper; }
	uint32 GetSilver() { return m_silver; }
	uint32 GetGold() { return m_gold; }
	uint32 GetPlatinum() { return m_platinum; }

	/* Corpse: Resurrection */
	bool IsRezzed() { return m_rez; }
	void IsRezzed(bool in_rez) { m_rez = in_rez; }
	void CastRezz(uint16 spell_id, Mob *caster);
	void CompleteResurrection(bool timer_expired = false);
	bool IsRezzable() { return m_is_rezzable; }
	void SetRezTimer(bool initial_timer = false);

	/* Corpse: Loot */
	void QueryLoot(Client *to);
	bool HasItem(uint32 item_id);
	uint32 CountItem(uint32 item_id);
	uint32 GetItemIDBySlot(uint16 loot_slot);
	uint16 GetFirstLootSlotByItemID(uint32 item_id);
	// Units of this item still on the corpse. The same item id can occupy more than one loot slot - a loot
	// drop that rolled it twice, or a stack that was split when the corpse was built - so this sums every
	// matching entry rather than reporting the first one. Anything that then acts on a single slot (the
	// AdvLoot transaction) has to walk the entries itself or it strands the later slots.
	uint32 GetLootQuantityByItemID(uint32 item_id);
	std::vector<int> GetLootList();
	inline const LootItems &GetLootItems() { return m_item_list; }
	// LootItem::lootslot is assigned in exactly one place today: MakeLootRequestPackets(), which runs when a
	// client opens the corpse through OP_LootRequest. Corpse::AddLoot() never sets it, so on a corpse nobody
	// has opened it is indeterminate, and MakeLootRequestPackets() also leaves any entry it cannot show at
	// 0xFFFF. Both GetItem(lootslot) and RemoveItem(lootslot) return the FIRST entry that matches, so two
	// entries sharing a value make a slot lookup resolve to the wrong item. Give every entry a distinct value,
	// touching only the ambiguous ones so a client that currently holds the corpse open keeps the mapping it
	// was already sent. See the 2026-10-17 10:38 run in docs/advlootpcap.md §15.2.
	int NormalizeLootSlots();
	// respect_cooldown is the 10 ms m_loot_cooldown_timer gate that rate-limits client OP_LootItem spam. An
	// AdvLoot delivery that spans several loot slots is one server-side action, so only its first slot is
	// held to the timer.
	void LootCorpseItem(Client *c, const EQApplicationPacket *app, bool allow_cursor_fallback = true, bool respect_cooldown = true);
	void EndLoot(Client *c, const EQApplicationPacket *app);
	void MakeLootRequestPackets(Client *c, const EQApplicationPacket *app);
	void AllowPlayerLoot(Mob *them, uint8 slot);
	void AddLooter(Mob *who);

	// Loot rights follow the kill credit: the client's "was present when the enemy died" state is
	// driven by this list. Populate it from the client that receives the experience for the kill
	// plus that client's group or raid. Anyone outside the list gets locked rows.
	void SetKillCreditLooters(Client *killer);
	bool IsKillCreditLooter(uint32 character_id) const;

	// The kill-credit list itself. AdvLoot::Session derives its mode and its loot authority from it.
	std::vector<uint32_t> GetKillCreditLooters() const
	{
		std::vector<uint32_t> looters;

		for (int looter: m_allowed_looters) {
			if (looter) {
				looters.push_back(static_cast<uint32_t>(looter));
			}
		}

		return looters;
	}
	void SendAdvLootCorpseRows();

	// True while the corpse still has something to show. Once the items and the cash are gone the corpse
	// is dropped from the window instead of leaving a stale row behind.
	bool HasAdvLootItems() const { return !m_item_list.empty(); }

	// AdvLoot rows are the loot UI for kill-credit players, so the server has to know which rows a
	// client still has unresolved. While a client has pending rows its legacy right-click is refused:
	// running both paths on the same corpse would take an item twice and pay the corpse cash twice.
	// The lockout is released when every row for that client is resolved, or when the corpse unlock
	// timer (NPC/CorpseUnlockTimer) expires and the corpse becomes public to everyone.
	//
	// These are forwarders onto the corpse's AdvLoot::Session. The state lives there (docs/advlootdesignV2
	// section 4); Corpse keeps loot storage, the render path and the delivery path.
	AdvLoot::Session& AdvLootSession();
	AdvLoot::Session* GetAdvLootSession() const { return m_advloot_session.get(); }

	void SetAdvLootPendingRows(uint32_t character_id, uint32_t count);
	void ConsumeAdvLootPendingRow(uint32_t character_id);
	uint32_t GetAdvLootPendingRows(uint32_t character_id) const;
	bool HasPendingAdvLootRows(uint32_t character_id) const;
	void ClearAdvLootPendingRows();
	void ClearAdvLootPendingRowsFor(uint32_t character_id);
	void ForgetAdvLootViewer(uint32_t character_id);

	// The corpse stopped being an Advanced Loot corpse - it unlocked, decayed, was resurrected or moved to the
	// graveyard. Windows close and anything awarded but not yet picked up returns to the shared pool.
	void CloseAdvLootSession(const char *reason);

	// A viewer died. They leave every in-flight ask on this corpse, but the units already awarded to them
	// stay awarded.
	void AdvLootViewerDied(uint32_t character_id);

	// The AdvLoot window has no coin row, so the kill-time delivery has to settle the corpse cash
	// itself: group/raid split when auto-split is on, otherwise split across the kill-credit looters.
	void DistributeAdvLootCash(const std::vector<Client *> &looters);

	// The client's green loot button sends AdvLoot subcmd 0x08 without a legacy OP_LootRequest, so
	// there is no looting session established. This sets the looting state from the kill-credit list
	// and then reuses LootCorpseItem so all the existing checks (lore, cooldown, lock, bag limits)
	// still apply.
	//
	// One AdvLoot row covers every loot slot holding this item id, so a delivery can span several slots and
	// a single stack can be split when the request is smaller than the stack. Returns the units that
	// actually left the corpse: the caller must only retire awards it really paid for.
	uint16_t AdvLootTransaction(Client *c, uint32 item_id, uint16_t quantity);

	// Advanced Loot roll state, owned by the corpse's AdvLoot::Session. One Roll per item on this corpse.
	// The client cannot report a player's Need/Greed choice over the wire (no sub-command carries it -
	// 0x11 is the Ask/AutoRoll state only), so the choice is recorded from /advloot need|greed|never
	// and pushed back to the window through subcmd 0x0b.
	AdvLoot::Roll* GetAdvLootRoll(uint32_t item_id);
	// Every roll this corpse owns, keyed by item id. A Manage Loot ask-response request (0x360e / 0x0D)
	// only carries the client's own ask-group id, so when that id has not been learned from a subcmd 0x0B
	// the server has to pick the roll out of this map instead.
	const std::map<uint32_t, AdvLoot::Roll>& GetAdvLootRolls() const;
	AdvLoot::Roll& RecordAdvLootChoice(Client *c, uint32_t item_id, uint32_t choice);
	// Create the roll for an item without recording a participant. Subcmd 0x0a is how the client tells us it
	// has moved a row into Ask / Ask auto Roll / Free Grab, and at that point the server may not have any
	// participant yet - RecordAdvLootChoice would resolve an empty row immediately, which is not what an Ask
	// is. This only makes sure the row exists so the state and the ask window have somewhere to live.
	AdvLoot::Roll& EnsureAdvLootRoll(uint32_t item_id);
	// Record a looter's Need / Greed / Never answer without arming or resolving the row. Subcmd 0x0b is how the
	// ND and GD checkboxes in Manage Loot reach the server (FUN_1400a4d80 -> FUN_1400a81b0 with action 3 = Need,
	// 2 = Greed, 0 = cleared), and a tick is not an opt-in: the master looter answers on behalf of people who
	// never clicked anything. RecordAdvLootChoice arms, and with a single participant even resolves, the row -
	// which is not what ticking a box means.
	void SetAdvLootResponse(uint32_t item_id, const std::string &name, uint32_t choice);
	void ArmAdvLootRoll(AdvLoot::Roll& roll);
	// Arm the window with a deadline the client supplied instead of the rule value. Subcmd 0x0a and 0x11 both
	// carry the remaining **milliseconds** (FUN_140085c80 and FUN_140085ec0 write record+0x18 - clock), and the
	// client renders its countdown bar from that, so the server has to expire at the same moment or the two
	// disagree.
	void ArmAdvLootRoll(AdvLoot::Roll& roll, uint32_t remaining_ms);
	void ProcessAdvLootRolls();
	void ResolveAdvLootRoll(AdvLoot::Roll& roll);
	void AssignAdvLootRoll(AdvLoot::Roll& roll, Client *target);
	// ML_LeaveOnCorpse (subcmd 0x0e, flag 1): the item stays on the corpse and the row is dropped. This is a
	// decision, not a no-op - it has to cancel any running ask window and tell the other members, or their
	// rows keep counting down for a claim nobody is making.
	void LeaveAdvLootRollOnCorpse(AdvLoot::Roll& roll, Client *who);

private:
	std::unique_ptr<AdvLoot::Session> m_advloot_session;   // rows, ask windows and per-viewer state

	void AdvLootChatToRoll(const AdvLoot::Roll& roll, uint32_t string_id,
		const char* arg1, const char* arg2 = nullptr, const char* arg3 = nullptr,
		const char* arg4 = nullptr, const char* arg5 = nullptr);

public:
	uint32 CountItems();
	bool CanPlayerLoot(int character_id);

	inline void Lock() { m_is_locked = true; }
	inline void UnLock() { m_is_locked = false; }
	inline bool IsLocked() { return m_is_locked; }
	inline void ResetLooter()
	{
		m_being_looted_by_entity_id = 0xFFFFFFFF;
		m_loot_request_type         = LootRequestType::Forbidden;
	}
	inline bool IsBeingLooted() { return (m_being_looted_by_entity_id != 0xFFFFFFFF); }
	inline bool IsBeingLootedBy(Client *c) { return m_being_looted_by_entity_id == c->GetID(); }

	/* Mob */
	void FillSpawnStruct(NewSpawn_Struct *ns, Mob *ForWho);
	bool Summon(Client *c, bool spell, bool CheckDistance);
	void Spawn();

	char corpse_name[64];
	uint32 GetEquippedItemFromTextureSlot(uint8 material_slot) const;
	uint32 GetEquipmentColor(uint8 material_slot) const;
	inline int64 GetRezExp() { return m_rezzed_experience; }
	inline int64 GetGMRezExp() { return m_gm_rezzed_experience; }
	uint8 GetKilledBy() { return m_killed_by_type; }
	uint32 GetRemainingRezTime() { return m_remaining_rez_time; }

	virtual void UpdateEquipmentLight();

	void CheckIsOwnerOnline();
	void SetOwnerOnline(bool value) { m_is_owner_online = value; }
	bool GetOwnerOnline() { return m_is_owner_online; }

	static Corpse *LoadCharacterCorpse(
		const CharacterCorpsesRepository::CharacterCorpses &cc,
		const glm::vec4 &position
	);

	void SyncEntityVariablesToCorpseDB();

protected:
	void MoveItemToCorpse(Client *client, EQ::ItemInstance *inst, int16 equipSlot, std::list<uint32> &removedList);

private:
	bool                     m_is_player_corpse;
	bool                     m_is_corpse_changed;
	bool                     m_is_locked;
	int32                    m_player_kill_item;
	uint32                   m_corpse_db_id;
	uint32                   m_character_id;
	uint32                   m_consented_group_id = 0;
	uint32                   m_consented_raid_id  = 0;
	uint32                   m_consented_guild_id = 0;
	LootItems                m_item_list;
	uint32                   m_copper;
	uint32                   m_silver;
	uint32                   m_gold;
	uint32                   m_platinum;
	bool                     m_player_corpse_depop;
	uint32                   m_being_looted_by_entity_id;
	uint64                   m_rezzed_experience;
	uint64                   m_gm_rezzed_experience;
	uint64                   m_gm_exp;
	bool                     m_rez;
	bool                     m_become_npc;
	int                      m_allowed_looters[MAX_LOOTERS];
	Timer                    m_corpse_decay_timer;
	Timer                    m_corpse_rezzable_timer;
	Timer                    m_corpse_delay_timer;
	Timer                    m_corpse_graveyard_timer;
	Timer                    m_loot_cooldown_timer;
	Timer                    m_check_owner_online_timer;
	Timer                    m_check_rezzable_timer;
	uint8                    m_killed_by_type;
	bool                     m_is_rezzable;
	EQ::TintProfile          m_item_tint;
	uint32                   m_remaining_rez_time;
	bool                     m_is_owner_online;
	std::vector<std::string> m_consented_player_names;
	LootRequestType          m_loot_request_type;
	uint32                   m_account_id;
};
