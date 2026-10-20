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

#include "common/patches/laurion_limits.h"
#include "zone/npc.h"

class Client;
class Corpse;
class Group;
class Mob;
class Raid;
struct MercTemplate;
struct NPCType;
struct NewSpawn_Struct;

namespace EQ
{
	struct ItemData;
}

constexpr int MAXMERCS = 11;
#define TANK 1
#define HEALER 2
#define MELEEDPS 9
#define CASTERDPS 12

#define NO_MERC_ID 0
#define MERC_STATE_NORMAL 5
#define MERC_STATE_SUSPENDED 1

// Laurion (SoF x64) protocol constants. These are the values the client's own packet arms compare
// against, so they are deliberately kept separate from the RoF MERC_STATE_* pair above.

// MercenaryClientData+0x04, written by FUN_1402a9190 from the 0x4c40 state push and from the state
// notification opcodes. See docs/mercenary.md 4.6.
//
// FUN_1402ac790 is the client's own classifier for this field:
//   state == 3 || state < 0 || state >= 6  ->  returns false ("mercenary is in the world")
//   state in {0, 1, 2, 4, 5}               ->  returns true  ("no active mercenary")
// __GetLabelFromEQ refuses to paint any LABELTYPE_MERCENARY_* label (ids 338..358, 404..412) while
// that classifier is true, so 3 is the only value in 0..5 that makes the Merc tab show numbers.
// 5 was wrong here before: it is in the "no active mercenary" group, which is why every stat read
// as 0 even though 0x1f38 carried real values.
enum LaurionMercState : uint32_t {
	LaurionMercStateNone      = 0, // 0x4cff
	LaurionMercStateReady     = 1, // 0x6c4b, and a successful 0x72b6 switch
	LaurionMercStateSuspended = 2, // 0x4815, unless the state was already 0, 1, 4 or 5
	LaurionMercStateActive    = 3, // the only 0..5 value the client treats as "mercenary in world"
	LaurionMercStateDead      = 4, // 0x0d70
	LaurionMercStateReserved5 = 5, // grouped with 0/1/2/4 by FUN_1402ac790; nothing sets it
	LaurionMercStateTransient = 6, // 0x4ec9, 0x184e, 0x421c; also the value the 0x4c40 arm uses to
	                              // mean "do not treat this as a corrupt transition"
};

// dbstr_us.txt category 36 (eMercenaryAbilityType): the mercenary role, carried in
// MercenaryRecord::type and MercenaryAAAbilityRecord::aa_type. The enum lives in
// common/patches/laurion_limits.h next to the other Laurion wire constants.
//
// Note this is a different numbering from EQEmu's TANK / HEALER / MELEEDPS / CASTERDPS constants,
// which are merc_subtypes.class_id values.
constexpr int32_t LaurionMercRoleGeneral   = Laurion::MercRoleGeneral;
constexpr int32_t LaurionMercRoleTank      = Laurion::MercRoleTank;
constexpr int32_t LaurionMercRoleHealer    = Laurion::MercRoleHealer;
constexpr int32_t LaurionMercRoleMeleeDPS  = Laurion::MercRoleMeleeDPS;
constexpr int32_t LaurionMercRoleCasterDPS = Laurion::MercRoleCasterDPS;

// Map EQEmu's merc_subtypes.class_id role onto the dbstr category 36 id the client expects.
inline int32_t LaurionMercRoleForClassID(uint32 class_id)
{
	switch (class_id) {
		case TANK:      return Laurion::MercRoleTank;
		case HEALER:    return Laurion::MercRoleHealer;
		case MELEEDPS:  return Laurion::MercRoleMeleeDPS;
		case CASTERDPS: return Laurion::MercRoleCasterDPS;
		default:        return Laurion::MercRoleGeneral;
	}
}

// Clamp a persisted stance id to the stance list of a mercenary template. Defined in zone/merc.cpp;
// the reason the client needs the record to be self-consistent is spelled out there and in
// docs/mercenary.md 15.8.
int32_t ClampLaurionStance(uint32 merc_template_id, int32_t stance_id);

// The stance a mercenary of this template starts on, from merc_stance_entries.isdefault. Returns 0 when the
// template has no stance list, meaning the caller should leave the stance alone. Migration 9336 explains why
// the default is the archetype's working stance and not Passive.
int32_t LaurionDefaultStance(uint32 merc_template_id);

// 0x72b6 reply codes, from the switch in FUN_1402ae8e0. Every non-zero value has its own localized
// client string, so the server sends a number and never text alongside it.
enum LaurionSwitchResult : uint32_t {
	LaurionSwitchOk              = 0,
	LaurionSwitchNotFastRegen    = 1,
	LaurionSwitchCannotUnsuspend = 2,
	LaurionSwitchLostSlotAccess  = 3,
	LaurionSwitchCurrentMercDead = 4,
	LaurionSwitchNotAllowedNow   = 5,
};

// 0x50e6 action codes, from the switch in FUN_1401a5fa0. 5 and 6 have no arm at all and must never
// be sent.
enum LaurionAAAction : uint32_t {
	LaurionAAActionSetPool     = 0,
	LaurionAAActionSetCounters = 1,
	LaurionAAActionGrant       = 2,
	LaurionAAActionClear       = 3,
	LaurionAAActionRefundAll   = 4,
	LaurionAAActionGrantAlt    = 7,
	LaurionAAActionClearAlt    = 8,
};

// 0x5193 reply codes, from FUN_1402ae5f0. The client owns the whole vocabulary (0 through 0x15) and
// prints its own localized line for each one, so a Laurion mercenary failure is a number and never
// server-side text. Client::SendMercResponsePackets already maps EQEmu's internal response codes
// onto exactly these values for RoF and later, which is why the hire path needs no Laurion branch.
enum LaurionHireResult : int32_t {
	LaurionHireOk                = 0,  // success: UI event 0x12 and timeRemaining <- mgr+0x278
	LaurionHireInsufficientFunds = 1,  // "You do not have enough funds to make that purchase!"
	LaurionHireNoSuchMerc        = 2,  // "Mercenary does not exist!"
	LaurionHireSpawnFailed       = 3,  // "Mercenary failed to spawn!"
	LaurionHireNoRaids           = 4,  // "Mercenaries are not allowed in raids!"
	LaurionHireRequestPending    = 5,  // "You already have a pending mercenary purchase!"
	LaurionHireMaxMercs          = 6,  // "You have the maximum number of mercenaries..."
	LaurionHireGroupFull         = 7,  // "You can not purchase a mercenary because your group is full!"
	LaurionHireInCombat          = 8,  // "You can not purchase a mercenary because you are in combat!"
	LaurionHireRecentDismiss     = 9,  // "You have recently dismissed a mercenary and must wait..."
	LaurionHireCreateError       = 10, // "An error occurred created your mercenary!"
	LaurionHireUpkeepCharged     = 11, // FUN_1402ac7e0(mgr, 0): "You have been charged a mercenary
	                                   // upkeep cost of %d plat, and %d gold and your mercenary upkeep
	                                   // cost timer has been reset to %s."
	LaurionHireUpkeepChargedAlt  = 12, // FUN_1402ac7e0(mgr, 1): same line with "%d %s" for the alt
	                                   // currency, named from db_str category 17 or 18
	LaurionHireRanOutOfFunds     = 13, // "You ran out of funds..." + "Your mercenary has been
	                                   // suspended!" + UI event 2
	LaurionHireUpkeepWaived      = 14, // FUN_1402aca20(): "Your mercenary waived an upkeep cost of..."
	LaurionHireAboutToSuspend    = 15, // "Your mercenary is about to be suspended due to insufficient
	                                   // funds!"
	LaurionHireNoLiaison         = 16, // "There is no mercenary liaison nearby!"
	LaurionHireTooFar            = 17, // "You are too far from the liaison!"
	LaurionHireNotQualified      = 18, // "You do not meet the requirements for that mercenary!"
	LaurionHireCannotInteract    = 19, // "You are unable to interact with the liaison!"
	LaurionHireMembershipLow     = 20, // "You do not have a high enough membership level to purchase
	                                   // this mercenary!"
	LaurionHireAllAccessRequired = 21, // "Your purchase has failed because this mercenary requires an
	                                   // All Access membership!"
};
#define NOT_SUSPENDED_TIME 0

const int MercAISpellRange = 100; // TODO: Write a method that calcs what the merc's spell range is based on spell, equipment, AA, whatever and replace this

struct MercSpell {
	uint16 spellid; // <= 0 = no spell
	uint32 type; // 0 = never, must be one (and only one) of the defined values
	int16 stance; // 0 = all, + = only this stance, - = all except this stance
	int16 slot;
	uint16 proc_chance;
	uint32 time_cancast; // when we can cast this spell next
};

struct MercTimer {
	uint16 timerid; // EndurTimerIndex
	uint8 timertype; // 1 = spell, 2 = disc
	uint16 spellid; // <= 0 = no spell
	uint32 time_cancast; // when we can cast this spell next
};

class Merc : public NPC {
public:
	Merc(const NPCType* d, float x, float y, float z, float heading);
	virtual ~Merc();

	//abstract virtual function implementations requird by base abstract class
	virtual bool Death(Mob* killer_mob, int64 damage, uint16 spell_id, EQ::skills::SkillType attack_skill, uint8 killed_by = 0, bool is_buff_tic = false);
	virtual void Damage(Mob* from, int64 damage, uint16 spell_id, EQ::skills::SkillType attack_skill, bool avoidable = true, int8 buffslot = -1, bool iBuffTic = false, eSpecialAttacks special = eSpecialAttacks::None);
	virtual bool Attack(Mob* other, int Hand = EQ::invslot::slotPrimary, bool FromRiposte = false, bool IsStrikethrough = false,
	bool IsFromSpell = false, ExtraAttackOptions *opts = nullptr);
	virtual bool HasRaid() { return false; }
	virtual bool HasGroup() { return (GetGroup() ? true : false); }
	virtual Raid* GetRaid() { return 0; }
	virtual Group* GetGroup() { return entity_list.GetGroupByMob(this); }

	// Mob AI Virtual Override Methods
	virtual void AI_Start(int32 iMoveDelay = 0);
	virtual void AI_Stop();
	virtual void AI_Process();

	//virtual bool AICastSpell(Mob* tar, int8 iChance, uint32 iSpellTypes);
	virtual bool AICastSpell(int8 iChance, uint32 iSpellTypes);
	virtual bool AIDoSpellCast(uint16 spellid, Mob* tar, int32 mana_cost, uint32* oDontDoAgainBefore = 0);
	virtual bool AI_EngagedCastCheck();
	//virtual bool AI_PursueCastCheck();
	virtual bool AI_IdleCastCheck();

	virtual bool Process();

	// Static Merc Group Methods
	static bool AddMercToGroup(Merc* merc, Group* group);
	static bool RemoveMercFromGroup(Merc* merc, Group* group);
	void ProcessClientZoneChange(Client* mercOwner);
	static void MercGroupSay(Mob *speaker, const char *msg, ...);
	Corpse* GetGroupMemberCorpse();

	// Merc Spell Casting Methods
	int8 GetChanceToCastBySpellType(uint32 spellType);
	void SetSpellRecastTimer(uint16 timer_id, uint16 spellid, uint32 recast_delay);
	void SetDisciplineRecastTimer(uint16 timer_id, uint16 spellid, uint32 recast_delay);
	void SetSpellTimeCanCast(uint16 spellid, uint32 recast_delay);
	static int32 GetSpellRecastTimer(Merc *caster, uint16 timer_id);
	static bool CheckSpellRecastTimers(Merc *caster, uint16 spellid);
	static int32 GetDisciplineRecastTimer(Merc *caster, uint16 timer_id);
	static bool CheckDisciplineRecastTimers(Merc *caster, uint16 spell_id, uint16 timer_id);
	static int32 GetDisciplineRemainingTime(Merc *caster, uint16 timer_id);
	static std::list<MercSpell> GetMercSpellsForSpellEffect(Merc* caster, int spellEffect);
	static std::list<MercSpell> GetMercSpellsForSpellEffectAndTargetType(Merc* caster, int spellEffect, SpellTargetType targetType);
	static std::list<MercSpell> GetMercSpellsBySpellType(Merc* caster, uint32 spellType);
	static MercSpell GetFirstMercSpellBySpellType(Merc* caster, uint32 spellType);
	static MercSpell GetFirstMercSpellForSingleTargetHeal(Merc* caster);
	static MercSpell GetMercSpellBySpellID(Merc* caster, uint16 spellid);
	static MercSpell GetBestMercSpellForVeryFastHeal(Merc* caster);
	static MercSpell GetBestMercSpellForFastHeal(Merc* caster);
	static MercSpell GetBestMercSpellForHealOverTime(Merc* caster);
	static MercSpell GetBestMercSpellForPercentageHeal(Merc* caster);
	static MercSpell GetBestMercSpellForRegularSingleTargetHeal(Merc* caster);
	static MercSpell GetBestMercSpellForGroupHealOverTime(Merc* caster);
	static MercSpell GetBestMercSpellForGroupCompleteHeal(Merc* caster);
	static MercSpell GetBestMercSpellForGroupHeal(Merc* caster);
	static MercSpell GetBestMercSpellForAETaunt(Merc* caster);
	static MercSpell GetBestMercSpellForTaunt(Merc* caster);
	static MercSpell GetBestMercSpellForHate(Merc* caster);
	static MercSpell GetBestMercSpellForCure(Merc* caster, Mob* target);
	static MercSpell GetBestMercSpellForStun(Merc* caster);
	static MercSpell GetBestMercSpellForAENuke(Merc* caster, Mob* target);
	static MercSpell GetBestMercSpellForTargetedAENuke(Merc* caster, Mob* target);
	static MercSpell GetBestMercSpellForPBAENuke(Merc* caster, Mob* target);
	static MercSpell GetBestMercSpellForAERainNuke(Merc* caster, Mob* target);
	static MercSpell GetBestMercSpellForNuke(Merc* caster);
	static MercSpell GetBestMercSpellForNukeByTargetResists(Merc* caster, Mob* target);
	static bool CheckAENuke(Merc* caster, Mob* tar, uint16 spell_id, uint8 &numTargets);
	static bool GetNeedsCured(Mob *tar);
	bool HasOrMayGetAggro();
	bool UseDiscipline(int32 spell_id, int32 target);

	bool IsMerc() const override { return true; }
	bool IsOfClientBotMerc() const override { return true; }

	virtual void FillSpawnStruct(NewSpawn_Struct* ns, Mob* ForWho);
	static Merc* LoadMercenary(Client *c, MercTemplate* merc_template, uint32 merchant_id, bool updateFromDB = false);
	void UpdateMercInfo(Client *c);
	void UpdateMercStats(Client *c, bool setmax = false);
	void UpdateMercAppearance();
	virtual void UpdateEquipmentLight();
	void AddItem(uint8 slot, uint32 item_id);
	static const char *GetRandomName();
	bool Spawn(Client *owner);
	bool Suspend();
	bool Unsuspend(bool setMaxStats);
	bool MercJoinClientGroup();
	void Zone();
	virtual void Depop();
	virtual bool Save();
	bool GetDepop() { return p_depop; }

	bool IsDead() { return GetHP() < 0;};
	bool IsMedding() { return _medding; };
	bool IsSuspended() { return _suspended; };

	void Signal(int signal_id);
	void SendPayload(int payload_id, std::string payload_value);

	static uint32 CalcPurchaseCost( uint32 templateID , uint8 level, uint8 currency_type = 0);
	static uint32 CalcUpkeepCost( uint32 templateID , uint8 level, uint8 currency_type = 0);

	// "GET" Class Methods
	virtual Mob* GetOwner();
	Client* GetMercenaryOwner();
	virtual Mob* GetOwnerOrSelf();
	uint32 GetMercenaryID() { return _MercID; }
	uint32 GetMercenaryCharacterID( ) { return owner_char_id; }
	uint32 GetMercenaryTemplateID() { return _MercTemplateID; }
	uint32 GetMercenaryType() { return _MercType; }
	uint32 GetMercenarySubType() { return _MercSubType; }
	uint8 GetProficiencyID() { return _ProficiencyID; }
	uint8 GetTierID() { return _TierID; }
	uint32 GetCostFormula() { return _CostFormula; }
	uint32 GetMercNameType() { return _NameType; }
	uint8 GetStance() { return _currentStance; }
	int GetHatedCount() { return _hatedCount; }

	inline const uint8 GetClientVersion() const { return _OwnerClientVersion; }

	virtual void SetTarget(Mob* mob);
	bool HasSkill(EQ::skills::SkillType skill_id) const;
	bool CanHaveSkill(EQ::skills::SkillType skill_id) const;
	uint16 MaxSkill(EQ::skills::SkillType skillid, uint16 class_, uint16 level) const;
	inline uint16 MaxSkill(EQ::skills::SkillType skillid) const { return MaxSkill(skillid, GetClass(), GetLevel()); }
	virtual void DoClassAttacks(Mob *target);
	void CheckHateList();
	bool CheckTaunt();
	bool CheckAETaunt();
	bool CheckConfidence();
	bool TryHide();

	// stat functions
	virtual void ScaleStats(int scalepercent, bool setmax = false);
	virtual void CalcBonuses();
	int64 GetEndurance() const {return cur_end;} //This gets our current endurance
	int64 GetMaxEndurance() const {return max_end;} //This gets our endurance from the last CalcMaxEndurance() call
	inline uint8 GetEndurancePercent() { return (uint8)((float)cur_end / (float)max_end * 100.0f); }
	inline virtual int32 GetATK() const { return ATK; }
	inline virtual int32 GetATKBonus() const { return itembonuses.ATK + spellbonuses.ATK; }
	int32 GetRawACNoShield(int &shield_ac) const;

	// The Merc tab's Combat HP / Combat Mana / Combat Endurance Regen rows (0x1f38 keys 29..34).
	// The client's own tooltip for the player-side equivalents says it plainly:
	// "The number of hit points you will regenerate while in combat" (IWS_CurrentCombatHPRegen,
	// EQUI_Inventory.xml) -- so these are the in-combat regen rates, the same Calc* values
	// Merc::Process() adds on its six second tic, not the RestRegen* fast-regen bonus, which has no
	// row on the window.
	//
	// None of the Mob accessors are usable here. Mob::GetEnduranceRegen() is a stub that returns 0
	// ("not implemented", zone/attack.cpp), and Mob::GetHPRegen() / Mob::GetManaRegen() return the
	// raw npc_types columns without the item, spell, AA and group-lead bonuses that Calc* applies or
	// the class gate that zeroes mana regen for a mercenary with no mana pool.
	int64 GetCombatHPRegen() { return CalcHPRegen(); }
	int64 GetCombatManaRegen() { return CalcManaRegen(false); }
	int64 GetCombatEnduranceRegen() { return CalcEnduranceRegen(); }

	// The Merc tab's Mitigation / Evasion / Offense / Accuracy rows (0x1f38 keys 6..13).
	//
	// The Merc tab is the mercenary's copy of the player's Inventory window. EQUI_Inventory.xml labels
	// the two paired rows IWM_ArmorClassScreen and IWM_AttackScreen, and their tooltips name the four
	// terms outright:
	//   "Mitigation and Evasion components of your mercenary's Armor Class"
	//   "Mitigation: Chance to take less melee damage"
	//   "Evasion: Chance to avoid melee damage"
	//   "Offense and Accuracy components of your mercenary's melee attacks"
	//   "Offense: Chance to deal more damage"
	//   "Accuracy: Chance to land a hit"
	// Those are the same four numbers Mob::CheckHitChance() and the mitigation roll use when the
	// mercenary actually fights, and zone/mob_info.cpp already publishes them under the names
	// "mitigation_ac", "total_defense", "offense" and "tohit". See the block comment on the
	// definitions in merc.cpp for why reporting them is the only honest way to fill these rows.
	int32 GetDisplayMitigation();
	int32 GetDisplayEvasion();
	int32 GetDisplayOffense();
	int32 GetDisplayAccuracy();

	// The melee skill this mercenary actually swings with: NPCType::prim_melee_type, from
	// merc_weaponinfo.prim_melee_type, because Merc::Attack() forwards to NPC::Attack() which sets the
	// attack skill from GetPrimSkill(). See the block comment on the definitions in merc.cpp.
	EQ::skills::SkillType GetMeleeSkill() const;

	inline virtual int32 GetSTR() const { return STR; }
	inline virtual int32 GetSTA() const { return STA; }
	inline virtual int32 GetDEX() const { return DEX; }
	inline virtual int32 GetAGI() const { return AGI; }
	inline virtual int32 GetINT() const { return INT; }
	inline virtual int32 GetWIS() const { return WIS; }
	inline virtual int32 GetCHA() const { return CHA; }
	inline virtual int32 GetMR() const { return MR; }
	inline virtual int32 GetFR() const { return FR; }
	inline virtual int32 GetDR() const { return DR; }
	inline virtual int32 GetPR() const { return PR; }
	inline virtual int32 GetCR() const { return CR; }
	inline virtual int32 GetCorrup() const { return Corrup; }

	inline virtual int32 GetHeroicSTR() const { return itembonuses.HeroicSTR; }
	inline virtual int32 GetHeroicSTA() const { return itembonuses.HeroicSTA; }
	inline virtual int32 GetHeroicDEX() const { return itembonuses.HeroicDEX; }
	inline virtual int32 GetHeroicAGI() const { return itembonuses.HeroicAGI; }
	inline virtual int32 GetHeroicINT() const { return itembonuses.HeroicINT; }
	inline virtual int32 GetHeroicWIS() const { return itembonuses.HeroicWIS; }
	inline virtual int32 GetHeroicCHA() const { return itembonuses.HeroicCHA; }
	inline virtual int32 GetHeroicMR() const { return itembonuses.HeroicMR; }
	inline virtual int32 GetHeroicFR() const { return itembonuses.HeroicFR; }
	inline virtual int32 GetHeroicDR() const { return itembonuses.HeroicDR; }
	inline virtual int32 GetHeroicPR() const { return itembonuses.HeroicPR; }
	inline virtual int32 GetHeroicCR() const { return itembonuses.HeroicCR; }
	inline virtual int32 GetHeroicCorrup() const { return itembonuses.HeroicCorrup; }
	// Mod2
	inline virtual int32 GetShielding() const { return itembonuses.MeleeMitigation; }
	inline virtual int32 GetSpellShield() const { return itembonuses.SpellShield; }
	inline virtual int32 GetDoTShield() const { return itembonuses.DoTShielding; }
	inline virtual int32 GetStunResist() const { return itembonuses.StunResist; }
	inline virtual int32 GetStrikeThrough() const { return itembonuses.StrikeThrough; }
	inline virtual int32 GetAvoidance() const { return itembonuses.AvoidMeleeChance; }
	// The Merc tab's Accuracy row (0x1f38 key 13, label 345), and the mercenary's to-hit rating.
	//
	// Client and Bot both define this as `itembonuses.HitChance` alone, because a player has no base
	// accuracy rating -- mod2 accuracy is the whole story for them. A mercenary does have one:
	// merc_stats.Accuracy, which ZoneDatabase::GetMercenaryType() puts into NPCType::accuracy_rating
	// and NPC::NPC() copies into accuracy_rating, where Mob::compute_tohit() adds it under IsNPC().
	// Merc is an NPC (class Merc : public NPC), so the combat half was already wired; what was missing
	// was the display half, which showed only the mod2 bonus and therefore always read 0.
	//
	// This mirrors the paired Offense row (key 11), which is GetATK() + GetATKBonus(): base value plus
	// bonuses.
	inline virtual int32 GetAccuracy() const { return accuracy_rating + itembonuses.HitChance; }
	inline virtual int32 GetCombatEffects() const { return itembonuses.ProcChance; }
	inline virtual int32 GetDS() const { return itembonuses.DamageShield; }
	// Mod3
	inline int32 GetHealAmt() const override { return itembonuses.HealAmt; }
	inline int32 GetSpellDmg() const override { return itembonuses.SpellDmg; }
	inline virtual int32 GetClair() const { return itembonuses.Clairvoyance; }
	inline virtual int32 GetDSMit() const { return itembonuses.DSMitigation; }

	inline virtual int32 GetSingMod() const { return itembonuses.singingMod; }
	inline virtual int32 GetBrassMod() const { return itembonuses.brassMod; }
	inline virtual int32 GetPercMod() const { return itembonuses.percussionMod; }
	inline virtual int32 GetStringMod() const { return itembonuses.stringedMod; }
	inline virtual int32 GetWindMod() const { return itembonuses.windMod; }

	inline virtual int32 GetDelayDeath() const { return aabonuses.DelayDeath + spellbonuses.DelayDeath + itembonuses.DelayDeath; }

	// "SET" Class Methods
	void SetMercData (uint32 templateID );
	void SetMercID( uint32 mercID ) { _MercID = mercID; }
	void SetMercCharacterID( uint32 ownerID ) { owner_char_id = ownerID; }
	void SetMercTemplateID( uint32 templateID ) { _MercTemplateID = templateID; }
	void SetMercType( uint32 type ) { _MercType = type; }
	void SetMercSubType( uint32 subtype ) { _MercSubType = subtype; }
	void SetProficiencyID( uint8 proficiency_id ) { _ProficiencyID = proficiency_id; }
	void SetTierID( uint8 tier_id ) { _TierID = tier_id; }
	void SetCostFormula( uint8 costformula ) { _CostFormula = costformula; }
	void SetMercNameType( uint8 nametype ) { _NameType = nametype; }
	void SetClientVersion(uint8 clientVersion) { _OwnerClientVersion = clientVersion; }
	void SetSuspended(bool suspended) { _suspended = suspended; }
	void SetStance(uint8 stance_id) { _currentStance = stance_id; }
	void SetHatedCount( int count ) { _hatedCount = count; }

	void Sit();
	void Stand();
	bool IsSitting() const override;
	bool IsStanding();

	// Merc-specific functions
	bool IsMercCaster() { return (GetClass() == HEALER || GetClass() == CASTERDPS); }
	bool IsMercCasterCombatRange(Mob *target);
	virtual float GetMaxMeleeRangeToTarget(Mob* target);
	virtual void MercMeditate(bool isSitting);
	bool FindTarget();

protected:
	int64 GetFocusEffect(focusType type, uint16 spell_id, bool from_buff_tic = false);

	std::vector<MercSpell> merc_spells;
	std::map<uint32,MercTimer> timers;

	Timer evade_timer; // can be moved to pTimers at some point

	uint16 skills[EQ::skills::HIGHEST_SKILL + 1];
	uint32 equipment[EQ::invslot::EQUIPMENT_COUNT]; //this is an array of item IDs
	uint32 d_melee_texture1; //this is an item Material value
	uint32 d_melee_texture2; //this is an item Material value (offhand)
	uint8 prim_melee_type; //Sets the Primary Weapon attack message and animation
	uint8 sec_melee_type; //Sets the Secondary Weapon attack message and animation

private:

	int32 CalcAC();
	int32 CalcATK();
	//int CalcHaste();

	int32 CalcSTR();
	int32 CalcSTA();
	int32 CalcDEX();
	int32 CalcAGI();
	int32 CalcINT();
	int32 CalcWIS();
	int32 CalcCHA();

	int32 CalcMR();
	int32 CalcFR();
	int32 CalcDR();
	int32 CalcPR();
	int32 CalcCR();
	int32 CalcCorrup();
	int64 CalcMaxHP();
	int64 CalcBaseHP();
	int64 CalcHPRegen();
	int64 CalcHPRegenCap();
	int64 CalcMaxMana();
	int64 CalcBaseMana();
	// fade_sit_modifiers is false only for the Merc tab stat readout (GetCombatManaRegen): the
	// sitting branch of CalcManaRegen() calls Mob::BuffFadeBySitModifier(), which cancels buffs, and
	// a packet encoder must not have that side effect. The AI tic keeps the default true.
	int64 CalcManaRegen(bool fade_sit_modifiers = true);
	int64 CalcBaseManaRegen();
	int64 CalcManaRegenCap();
	void CalcMaxEndurance(); //This calculates the maximum endurance we can have
	int64 CalcBaseEndurance(); //Calculates Base End
	int64 CalcEnduranceRegen(); //Calculates endurance regen used in DoEnduranceRegen()
	int64 CalcEnduranceRegenCap();
	void SetEndurance(int32 newEnd); //This sets the current endurance to the new value
	void DoEnduranceUpkeep(); //does the endurance upkeep
	void CalcRestState();

	int GroupLeadershipAAHealthEnhancement();
	int GroupLeadershipAAManaEnhancement();
	int GroupLeadershipAAHealthRegeneration();
	int GroupLeadershipAAOffenseEnhancement();

	float GetDefaultSize();

	bool LoadMercenarySpells();
	bool CheckStance(int16 stance);
	std::vector<MercSpell> GetMercSpells() { return merc_spells; }

	// Private "base stats" Members
	int32 base_mana;
	int32 base_end;
	int32 _baseAC;
	uint32 _baseSTR;
	uint32 _baseSTA;
	uint32 _baseDEX;
	uint32 _baseAGI;
	uint32 _baseINT;
	uint32 _baseWIS;
	uint32 _baseCHA;
	uint32 _baseATK;
	int32  _baseAccuracy;   // merc_stats.Accuracy -> NPCType::accuracy_rating, before Mercs:ScaleRate
	int32  _baseAvoidance;  // merc_stats.Avoidance -> NPCType::avoidance_rating, before Mercs:ScaleRate
	uint32 _baseRace; // Necessary to preserve the race otherwise mercs get their race updated in the db when they get an illusion.
	uint8 _baseGender; // Merc gender. Necessary to preserve the original value otherwise it can be changed by illusions.
	uint32 _baseMR;
	uint32 _baseCR;
	uint32 _baseDR;
	uint32 _baseFR;
	uint32 _basePR;
	uint32 _baseCorrup;
	uint32 RestRegenHP;
	uint32 RestRegenMana;
	uint32 RestRegenEndurance;

	uint32 _MercID;
	uint32 _MercTemplateID;
	uint32 _MercType;
	uint32 _MercSubType;
	uint8 _ProficiencyID;
	uint8 _TierID;
	uint8 _CostFormula;
	uint8 _NameType;
	uint8 _OwnerClientVersion;
	uint8 _currentStance;

	EQ::InventoryProfile m_inv;
	int64 max_end;
	int64 cur_end;
	bool _medding;
	bool _suspended;
	bool p_depop;
	bool _check_confidence;
	bool _lost_confidence;
	int _hatedCount;
	uint32 owner_char_id;
	Timer endupkeep_timer;
	Timer rest_timer;
	Timer confidence_timer;
	Timer check_target_timer;
};
