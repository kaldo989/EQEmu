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

#include "zone/advloot_session.h"

#include "zone/client.h"
#include "zone/corpse.h"
#include "zone/entity.h"
#include "zone/groups.h"
#include "zone/raids.h"

#include "common/eq_constants.h"
#include "common/eqemu_logsys.h"
#include "common/random.h"
#include "common/rulesys.h"
#include "common/strings.h"
#include "zone/string_ids.h"
#include "zone/zonedb.h"

#include <algorithm>
#include <set>

namespace AdvLoot {

// ---------------------------------------------------------------- rendering

// The group record for one viewer, rebuilt from the corpse's current loot list. The loot-rights flag and
// the filter bits are per viewer, so this cannot be broadcast.
//
// AddCorpse (FUN_14009e310) walks the nested item records and calls the row builder twice per item:
//   entry +0x14 non-zero -> Personal Loot List row with that quantity
//   entry +0x0C non-zero -> Corpse (shared) Loot List row with that quantity
// Solo awards the whole pile to the viewer first (RenderGroupFor), because FUN_1400a5d10 keeps ADLW_CLLWnd
// hidden unless the player is in a group or a raid - the shared store still exists (window +0x320 / store
// +0x3E8), it is just not drawn, so a unit left in the shared pool would be invisible and unlootable.
// Group and raid push the shared pool, because that is the pool a 0x0e transaction resolves and the one that
// has to be synchronised across clients; whatever this viewer has already been awarded rides in +0x14.
//
// One nested record per distinct item id: the client walks the corpse node's item array for a matching u64
// and Corpse::GetLootQuantityByItemID already treats an item id as unique, so two loot entries of the same
// item are one row with the charges summed by that lookup rather than two rows that fight over one pool.
CorpseGroup Session::BuildGroupFor(Client *c) const
{
	CorpseGroup group;

	group.corpse_key       = corpse->GetID();
	group.npc_name         = corpse->GetOwnerName();
	group.decay_expires_in = corpse->GetDecayTime() / 1000;   // seconds - FUN_14014f5f0 packs value * 1000 + clock
	group.present_at_kill  = corpse->IsKillCreditLooter(c->CharacterID()) ? 1 : 0;

	const bool solo = !c->HasGroup() && !c->HasRaid();

	std::set<uint32_t> added;

	for (const auto &i : corpse->GetLootItems()) {
		const auto *item = database.GetItem(i->item_id);
		if (!item || !added.insert(item->ID).second) {
			continue;
		}

		Filter entry;   // the nested item record (see AdvLoot::Filter in zone/advloot.h)
		entry.item_id         = item->ID;
		entry.filter_bits     = c->AdvLootFilterBits(item->ID);
		entry.icon            = item->Icon;
		entry.name            = item->Name;

		entry.personal_qty = PersonalQty(c->CharacterID(), item->ID);
		entry.shared_qty   = solo ? 0 : SharedQty(item->ID);

		// AddCorpse only builds a row for a pool that is non-zero, so a record with nothing in either pool
		// draws nothing and would make the pending-row count disagree with the rows on screen.
		if (!entry.personal_qty && !entry.shared_qty) {
			continue;
		}

		entry.decay_expires_in = group.decay_expires_in;
		entry.present_at_kill  = group.present_at_kill;
		entry.corpse_key       = group.corpse_key;

		group.items.push_back(entry);
	}

	return group;
}

// Build the record for a viewer and, in solo, establish their personal pool first: a solo looter has no
// shared row to act on, so everything still on the corpse is theirs until they hand it back.
CorpseGroup Session::RenderGroupFor(Client *c)
{
	if (c && !c->HasGroup() && !c->HasRaid()) {
		std::set<uint32_t> seen;

		for (const auto &i : corpse->GetLootItems()) {
			if (!seen.insert(i->item_id).second) {
				continue;
			}

			SetAward(c->CharacterID(), i->item_id,
				static_cast<uint16_t>(corpse->GetLootQuantityByItemID(i->item_id)));
		}
	}

	return BuildGroupFor(c);
}

// Push this corpse to one viewer and record that they now have rows for it. A corpse with nothing left is
// not resent: the caller drops it from the window instead of leaving a stale row behind.
void Session::Render(Client *c)
{
	if (!c || !c->advloot_enabled) {
		return;
	}

	const CorpseGroup group = RenderGroupFor(c);

	if (group.items.empty()) {
		ClearPendingRowsFor(c->CharacterID());
		return;
	}

	LogLootFilters(
		"[{}] advloot corpse [{}] [{}] item(s) [{}], kill credit [{}], group name [{}], decay_expires_in [{}]s",
		c->GetCleanName(),
		corpse->GetID(),
		group.items.size(),
		group.present_at_kill ? "unlocked" : "locked",
		group.present_at_kill ? "yes" : "no",
		group.npc_name,
		group.decay_expires_in
	);

	SetPendingRows(c->CharacterID(), static_cast<uint32_t>(group.items.size()));
	c->advloot_last_corpse_id = static_cast<uint16_t>(corpse->GetID());

	const uint16_t corpse_id = static_cast<uint16_t>(corpse->GetID());
	if (std::find(c->advloot_corpses.begin(), c->advloot_corpses.end(), corpse_id) == c->advloot_corpses.end()) {
		c->advloot_corpses.push_back(corpse_id);
	}

	c->SendAdvLootGroup(group);

	// Rule R2: this 0x03 just wiped the viewer's ask table and reset their ask counter, so anything still
	// outstanding on this corpse has to follow it. Order matters - the 0x0a records go after the rebuild.
	ReissueAsks(c);
}

// The answer to a refused legacy right-click. Corpse::CanPlayerLoot asks the session whether this viewer
// still has unresolved rows and, if so, calls this. Render() already registers the corpse on the client and
// sets the pending-row count, so the only thing left is the line that explains why the legacy window stayed
// closed - eqstr 11306, "This corpse is using advanced looting...".
void Session::ExplainRowsTo(Client *c)
{
	if (!c) {
		return;
	}

	Render(c);
	c->MessageString(Chat::Yellow, AdvLoot::StringId::AdvLootCorpse);
}

// The kill-time push: every Advanced-Loot client in the zone, whether or not they had kill credit (the
// non-credit ones get locked rows). Returns the clients it rendered to, which is also the set the corpse
// cash is split across.
std::vector<Client *> Session::RenderAll()
{
	std::vector<Client *> looters;

	for (const auto &entry : entity_list.GetClientList()) {
		Client *c = entry.second;
		if (!c || !c->advloot_enabled) {
			continue;
		}

		looters.push_back(c);
		Render(c);
	}

	return looters;
}

// ---------------------------------------------------------------- mode and authority

const char *AuthorityName(uint32_t authority)
{
	switch (authority) {
		case AuthorityNone:         return "locked";
		case AuthorityEveryone:     return "every member";
		case AuthorityMasterLooter: return "master looter only";
		case AuthorityFreeGrab:     return "free grab";
		default:                    return "unknown";
	}
}

const char *Session::ModeName() const
{
	return AdvLoot::ModeName(mode);
}

// The construct this corpse belongs to is the one its kill-credit list lives in: a raid beats a group, and
// a group beats solo. Corpse::SetKillCreditLooters already expanded the killer's raid or group into the
// list, so any member of it identifies the same construct.
void Session::Open()
{
	mode      = AdvLoot::ModeSolo;
	authority = AuthorityMasterLooter;   // solo: the player is the authority over their own rows

	for (uint32_t character_id : corpse->GetKillCreditLooters()) {
		Client *c = entity_list.GetClientByCharID(character_id);
		if (!c || !c->advloot_enabled) {
			continue;
		}

		if (Raid *raid = c->GetRaid()) {
			mode = AdvLoot::ModeRaid;

			switch (raid->GetLootType()) {
				case RaidLootType::EntireRaid:
					authority = AuthorityFreeGrab;
					break;
				default:
					authority = AuthorityMasterLooter;
					break;
			}

			break;
		}

		if (Group *group = c->GetGroup()) {
			mode = AdvLoot::ModeGroup;
			// GetMasterLooter() falls back to the leader, so an empty name is the only signal that nobody has
			// been named and the rule is "every member may act".
			authority = group->GetMasterLooterName().empty() ? AuthorityEveryone : AuthorityMasterLooter;
		}
	}

	LogLootFilters(
		"advloot session opened corpse [{}] mode [{}] authority [{}] kill credit [{}]",
		corpse->GetID(),
		ModeName(),
		AuthorityName(authority),
		corpse->GetKillCreditLooters().size()
	);
}

void Session::RefreshAuthority()
{
	Open();
}

// Read live from the viewer's current raid or group. A raid join, a disband or a /advloot mol therefore
// takes effect on the next packet rather than needing a re-render.
uint32_t Session::AuthorityFor(Client *c) const
{
	if (!c) {
		return AuthorityNone;
	}

	// No kill credit -> the group record's first byte was 0 for this viewer, their LootDetails entries read
	// as Locked, and FUN_1400a9cc0 refuses the row outright (eqstr 11321).
	if (!corpse->IsKillCreditLooter(c->CharacterID())) {
		return AuthorityNone;
	}

	if (Raid *raid = c->GetRaid()) {
		switch (raid->GetLootType()) {
			case RaidLootType::EntireRaid:
				return AuthorityFreeGrab;

			case RaidLootType::LeaderAndGroupLeadersOnly:
				if (raid->IsGroupLeader(c)) {
					return AuthorityEveryone;
				}
				break;

			case RaidLootType::LeaderSelected:
				if (raid->IsRaidLooter(c)) {
					return AuthorityEveryone;
				}
				break;

			default:
				break;
		}

		return raid->GetMasterLooter() == c ? AuthorityMasterLooter : AuthorityNone;
	}

	if (Group *group = c->GetGroup()) {
		if (group->GetMasterLooterName().empty()) {
			return AuthorityEveryone;
		}

		return group->GetMasterLooter() == c ? AuthorityMasterLooter : AuthorityNone;
	}

	return AuthorityMasterLooter;
}

// The viewer the mode list marks MasterLooter, for logs and for /advloot output.
Client *Session::MasterLooter() const
{
	for (uint32_t character_id : corpse->GetKillCreditLooters()) {
		Client *c = entity_list.GetClientByCharID(character_id);
		if (!c) {
			continue;
		}

		if (Raid *raid = c->GetRaid()) {
			return raid->GetMasterLooter();
		}

		if (Group *group = c->GetGroup()) {
			return group->GetMasterLooter();
		}

		return c;
	}

	return nullptr;
}

// ---------------------------------------------------------------- pools

// Units of this item that are still physically on the corpse. Corpse::GetLootQuantityByItemID reports the
// first loot entry's charges, which is the same quantity the group record pushes.
static uint16_t CorpseQty(Corpse *corpse, uint32_t item_id)
{
	return static_cast<uint16_t>(corpse->GetLootQuantityByItemID(item_id));
}

uint16_t Session::PersonalQty(uint32_t character_id, uint32_t item_id) const
{
	auto item = awards.find(item_id);
	if (item == awards.end()) {
		return 0;
	}

	auto holder = item->second.find(character_id);
	return holder == item->second.end() ? 0 : holder->second;
}

// What is on the corpse minus what has already been handed out. Clamped at zero: two solo viewers can each
// be shown the same pile, and whoever acts first takes it.
uint16_t Session::SharedQty(uint32_t item_id) const
{
	int32_t shared = CorpseQty(corpse, item_id);

	auto item = awards.find(item_id);
	if (item != awards.end()) {
		for (const auto &holder : item->second) {
			shared -= holder.second;
		}
	}

	return shared > 0 ? static_cast<uint16_t>(shared) : 0;
}

// State the personal pool for one viewer at one quantity. Used by the solo render, which restates the whole
// pool from what is left on the corpse, so it sets rather than adds.
void Session::SetAward(uint32_t character_id, uint32_t item_id, uint16_t quantity)
{
	if (!quantity) {
		auto item = awards.find(item_id);
		if (item != awards.end()) {
			item->second.erase(character_id);
			if (item->second.empty()) {
				awards.erase(item);
			}
		}
		return;
	}

	awards[item_id][character_id] = quantity;
}

// Move units from the shared pool into this viewer's personal pool - what subcmd 0x0e flag 0 does on the
// client (FUN_140151c60 case 0xe: entry+0x14 += quantity, entry+0x0C -= quantity). Returns the units
// actually moved, which is the quantity the 0x0e has to carry for the two copies to agree.
uint16_t Session::Award(Client *c, uint32_t item_id, uint16_t quantity)
{
	if (!c || !quantity) {
		return 0;
	}

	const uint16_t available = SharedQty(item_id);
	const uint16_t moved = quantity > available ? available : quantity;

	if (!moved) {
		return 0;
	}

	awards[item_id][c->CharacterID()] += moved;

	LogLootFilters(
		"[{}] award item [{}] corpse [{}] [{}] unit(s) - shared [{}] personal [{}]",
		c->GetCleanName(),
		item_id,
		corpse->GetID(),
		moved,
		SharedQty(item_id),
		PersonalQty(c->CharacterID(), item_id)
	);

	return moved;
}

void Session::ConsumeAward(uint32_t character_id, uint32_t item_id, uint16_t quantity)
{
	auto item = awards.find(item_id);
	if (item == awards.end()) {
		return;
	}

	auto holder = item->second.find(character_id);
	if (holder == item->second.end()) {
		return;
	}

	holder->second = holder->second > quantity ? holder->second - quantity : 0;
	if (!holder->second) {
		item->second.erase(holder);
		if (item->second.empty()) {
			awards.erase(item);
		}
	}
}

// The item is no longer anyone's to take: it stays on the corpse for someone else.
void Session::ClearAwards(uint32_t item_id)
{
	awards.erase(item_id);
}

// ---------------------------------------------------------------- delivery

// The subcmd 0x08 gate. FUN_1400a9cc0 is only reached from a Personal Loot List row, so a 0x08 is a claim
// on the requester's personal pool, never a claim on the corpse. Answering one for a unit that was never
// awarded is what lets a stale or forged row loot an item out of the shared pool in front of the group.
//
// Corpse::AdvLootTransaction stays the delivery primitive - it still runs CanPlayerLoot, the loot-slot
// lookup and LootCorpseItem with all of Corpse::GetItem's lore / no-trade / status checks - and the subcmd
// 0x09 answer is built from the quantity this returns, because FUN_140151810 only erases the client's corpse
// item entry when that quantity equals the entry's personal pool and its shared pool is empty.
Session::Delivery Session::Deliver(Client *c, uint32_t item_id, uint16_t requested_qty)
{
	Delivery out;

	out.character_id = c ? c->CharacterID() : 0;
	out.item_id      = item_id;

	if (!c) {
		out.reason = "no client";
		last_delivery = out;
		return out;
	}

	const uint16_t personal = PersonalQty(c->CharacterID(), item_id);

	if (!personal) {
		out.action = AdvLoot::ActionNotAllowed;
		out.reason = "nothing in the requester's personal pool";

		LogLootFilters(
			"[{}] deliver item [{}] corpse [{}] refused - [{}] (shared [{}])",
			c->GetCleanName(),
			item_id,
			corpse->GetID(),
			out.reason,
			SharedQty(item_id)
		);

		last_delivery = out;
		return out;
	}

	out.quantity = requested_qty && requested_qty < personal ? requested_qty : personal;

	// Decide the answer before the transaction runs: AdvLootTransaction removes the loot slot, so asking
	// whether the item is still on the corpse afterwards would report every success as a refusal.
	uint16_t action;
	const char *reason;

	if (!CorpseQty(corpse, item_id)) {
		action = AdvLoot::ActionNotAllowed;      // stale row, nothing left on the corpse
		reason = "nothing left on the corpse";
	} else if (!corpse->CanPlayerLoot(c->CharacterID())) {
		action = AdvLoot::ActionNotAllowed;      // AdvLootTransaction prints eqstr 11321
		reason = "no loot rights";
	} else if (corpse->IsBeingLooted() && !corpse->IsBeingLootedBy(c)) {
		// Reported by the legacy loot-error packet; 0x09 has no "someone else is looting" code, so send the
		// silent form, which still performs the cleanup.
		action = AdvLoot::ActionNone;
		reason = "being looted by someone else";
	} else {
		action = AdvLoot::ActionLooted;
		reason = "looted";
	}

	out.action    = action;
	out.reason    = reason;
	out.delivered = action == AdvLoot::ActionLooted;

	corpse->AdvLootTransaction(c, item_id, out.quantity);

	if (out.delivered) {
		ConsumeAward(c->CharacterID(), item_id, out.quantity);
	}

	LogLootFilters(
		"[{}] deliver item [{}] corpse [{}] qty [{}] personal [{}] shared [{}] -> [{}]",
		c->GetCleanName(),
		item_id,
		corpse->GetID(),
		out.quantity,
		personal,
		SharedQty(item_id),
		reason
	);

	last_delivery = out;

	return out;
}

// ---------------------------------------------------------------- rows

Roll *Session::FindRoll(uint32_t item_id)
{
	auto it = rolls.find(item_id);
	return it == rolls.end() ? nullptr : &it->second;
}

Roll &Session::EnsureRoll(uint32_t item_id)
{
	auto it = rolls.find(item_id);
	if (it != rolls.end()) {
		return it->second;
	}

	Roll roll;
	roll.corpse_key = corpse->GetID();
	roll.item_id    = item_id;

	const auto *item = database.GetItem(item_id);
	if (item) {
		roll.item_name = item->Name;
	}

	it = rolls.insert(std::make_pair(item_id, roll)).first;

	LogLootFilters("advloot roll created item [{}] corpse [{}]", item_id, corpse->GetID());

	return it->second;
}

// ---------------------------------------------------------------- ask window

void Session::Arm(Roll &roll)
{
	Arm(roll, static_cast<uint32_t>(RuleI(Loot, AdvLootAskTimerSeconds)));
}

// Arm the window with a caller-supplied window instead of the rule value. Subcmd 0x0a carries the seconds
// the initiating client has left on its own countdown (FUN_140085c80 writes record+0x18 - clock), so the
// server expires at the same moment the client's row does. A zero or absurd value falls back to the rule.
//
// The client's countdown is an absolute deadline: the receive case adds its own clock back and
// CAdvancedLootWnd::DoAdvLootAction (FUN_1400aa1b0) stores the result at record+0x88. ask_deadline is the
// server's copy of that, which is what ReissueAsks converts back into relative seconds.
void Session::Arm(Roll &roll, uint32_t seconds)
{
	const uint32_t rule_seconds = static_cast<uint32_t>(RuleI(Loot, AdvLootAskTimerSeconds));

	if (seconds == 0 || seconds > rule_seconds) {
		seconds = rule_seconds;
	}

	roll.armed        = true;
	roll.ask_deadline = Timer::GetCurrentTime() + seconds * 1000;
	roll.ask_timer.SetTimer(seconds * 1000);

	// An ask the server started on its own - AdvLootAutoStartTimer arming at the first opt-in, with no 0x0a
	// on the wire - has no client-allocated key, so there is nothing to re-issue later. Record the shape of
	// the window anyway so a 0x0a that arrives afterwards fills the key in.
	if (!roll.has_ask) {
		roll.ask       = ItemUpdate{};
		roll.ask.state = roll.state ? roll.state : AdvLoot::StateAsk;
		roll.ask.flag  = 0;
	}

	roll.ask.seconds = seconds;

	LogLootFilters(
		"[{}] advloot roll armed item [{}] corpse [{}] window [{}]s (rule [{}]s) ask_key [{}]{}",
		roll.item_name,
		roll.item_id,
		corpse->GetID(),
		seconds,
		rule_seconds,
		roll.ask.ask_key,
		roll.ask.ask_key ? "" : " (no client key recorded yet)"
	);
}

// Chat line to every participant on the row. The client's own AdvLoot window shows the same state, so
// using the string ids from eqstr_us.txt 11300-11323 keeps the wording identical to the client.
void Session::ChatToRoll(const Roll &roll, uint32_t string_id,
	const char *arg1, const char *arg2, const char *arg3, const char *arg4, const char *arg5)
{
	for (const auto &participant : roll.participants) {
		Client *client = entity_list.GetClientByCharID(participant.character_id);
		if (!client) {
			continue;
		}
		client->MessageString(Chat::Yellow, string_id, arg1, arg2, arg3, arg4, arg5);
	}
}

// Apply a Manage Loot ND / GD tick to the row without touching the ask window. The name comes straight off the
// wire (FUN_1400a81b0 copies the ML_PlayerList row's text into the entry), so it is matched by name rather
// than by the sender - the master looter is the one who sends it, not the one who was answered for.
void Session::SetResponse(uint32_t item_id, const std::string &name, uint32_t choice)
{
	if (name.empty()) {
		return;
	}

	auto it = rolls.find(item_id);
	if (it == rolls.end()) {
		// A cleared answer for a row this corpse never had is noise; a real answer means the master looter
		// answered before anyone opted in, so create the row.
		if (choice == AdvLoot::ChoiceNone) {
			return;
		}
		it = rolls.find(EnsureRoll(item_id).item_id);
	}

	Roll &roll = it->second;

	for (auto &participant : roll.participants) {
		if (participant.name == name) {
			participant.choice = choice;
			LogLootFilters(
				"advloot response item [{}] corpse [{}] looter [{}] -> [{}]",
				item_id,
				corpse->GetID(),
				name,
				AdvLoot::ChoiceName(choice)
			);
			RefreshAskState(roll);
			return;
		}
	}

	if (choice == AdvLoot::ChoiceNone) {
		return;
	}

	AdvLoot::Participant participant;
	participant.name         = name;
	participant.choice       = choice;
	participant.opt_in_order = ++opt_in_counter;

	if (Client *client = entity_list.GetClientByName(name.c_str())) {
		participant.character_id = client->CharacterID();
	}

	roll.participants.push_back(participant);

	LogLootFilters(
		"advloot response item [{}] corpse [{}] looter [{}] added as [{}]",
		item_id,
		corpse->GetID(),
		name,
		AdvLoot::ChoiceName(choice)
	);

	RefreshAskState(roll);
}

Roll &Session::RecordChoice(Client *c, uint32_t item_id, uint32_t choice)
{
	Roll &roll = EnsureRoll(item_id);

	AdvLoot::Participant *participant = nullptr;
	for (auto &p : roll.participants) {
		if (p.character_id == c->CharacterID()) {
			participant = &p;
			break;
		}
	}

	if (!participant) {
		AdvLoot::Participant p;
		p.character_id = c->CharacterID();
		p.name         = c->GetCleanName();
		p.choice       = choice;
		p.opt_in_order = ++opt_in_counter;
		roll.participants.push_back(p);
		participant = &roll.participants.back();
	} else {
		participant->choice = choice;
	}

	// Solo never arms a timer - there is no competition, so the choice resolves immediately.
	if (!c->HasGroup() && !c->HasRaid()) {
		Resolve(roll);
		return roll;
	}

	// No competition yet: the first participant on a row in a group is not a roll, so award it now.
	// A roll only exists once a second Need/Greed participant appears on the same row.
	if (roll.participants.size() == 1) {
		Resolve(roll);
		return roll;
	}

	// Decision 1: the master looter arms the window by moving the row to AskAutoRoll (subcmd 0x11
	// state 2). The rule lets the first opt-in arm it when the master looter has not, so a group whose
	// master looter never acts still resolves.
	if (!roll.armed && RuleB(Loot, AdvLootAutoStartTimer)) {
		Arm(roll);
	}

	RefreshAskState(roll);

	return roll;
}

void Session::Tick()
{
	for (auto &entry : rolls) {
		Roll &roll = entry.second;
		if (!roll.armed) {
			continue;
		}

		if (roll.ask_timer.Check()) {
			Resolve(roll);
		}
	}
}

void Session::Resolve(Roll &roll)
{
	std::vector<AdvLoot::Participant *> need;
	std::vector<AdvLoot::Participant *> greed;

	for (auto &p : roll.participants) {
		if (p.choice == AdvLoot::ChoiceNeed) {
			need.push_back(&p);
		} else if (p.choice == AdvLoot::ChoiceGreed) {
			greed.push_back(&p);
		}
	}

	// Need tier beats Greed. With only Greed present, the highest roll wins.
	std::vector<AdvLoot::Participant *> &tier = need.empty() ? greed : need;

	roll.armed = false;
	roll.ask_timer.Disable();

	// The ask is over whichever branch this takes from here, and a stale record would put a finished
	// countdown back on the window after the next 0x03 rebuild.
	ClearAsk(roll);

	uint32 quantity = corpse->GetLootQuantityByItemID(roll.item_id);
	if (!quantity) {
		quantity = 1;
	}

	char count_str[32];
	char roll_str[32];

	if (tier.empty()) {
		// Nothing claimed: the item stays on the corpse and the row drops.
		roll.state = AdvLoot::StateRemoved;

		const char *count = ConvertArray(static_cast<int64>(quantity), count_str);

		// The participants list is empty in this branch, so the line goes to the players who had rows on
		// this corpse.
		for (const auto &entry : views) {
			Client *client = entity_list.GetClientByCharID(entry.first);
			if (client) {
				client->MessageString(Chat::Yellow, AdvLoot::StringId::NoOneInterested, count, roll.item_name.c_str());
			}
		}

		LogLootFilters(
			"advloot resolve item [{}] corpse [{}] - no Need or Greed participant, left on corpse",
			roll.item_id,
			corpse->GetID()
		);

		return;
	}

	// Rolls are drawn at resolution, one per participant.
	for (auto *p : tier) {
		p->roll = EQ::Random::Instance()->Int(1, 1000);
	}

	AdvLoot::Participant *winner = tier[0];
	for (auto *p : tier) {
		if (p->roll > winner->roll) {
			winner = p;
		} else if (p->roll == winner->roll && p->opt_in_order < winner->opt_in_order) {
			// Decision 2: earliest opt-in wins a tie. No re-roll - the client stores one roll value per
			// participant and has no packet path that clears it.
			winner = p;
		}
	}

	Client *winner_client = entity_list.GetClientByCharID(winner->character_id);
	if (!winner_client) {
		// Decision 4 edge case: a participant who disconnected before expiry is dropped. If the tier
		// empties, the item is left on the corpse.
		LogLootFilters(
			"advloot winner [{}] for item [{}] is not online - item left on corpse",
			winner->name,
			roll.item_id
		);
		roll.state = AdvLoot::StateRemoved;
		return;
	}

	roll.state = AdvLoot::StateAskCompleted;

	if (tier.size() > 1) {
		for (auto *p : tier) {
			Client *client = entity_list.GetClientByCharID(p->character_id);
			if (!client) {
				continue;
			}

			char participant_roll[32];
			client->MessageString(Chat::Yellow, AdvLoot::StringId::RolledOn,
				p->name.c_str(),
				ConvertArray(static_cast<int64>(p->roll), participant_roll),
				roll.item_name.c_str());
		}
	}

	ChatToRoll(roll, AdvLoot::StringId::WonRoll,
		winner->name.c_str(),
		need.empty() ? "Greed" : "Need",
		ConvertArray(static_cast<int64>(tier.size()), count_str),
		roll.item_name.c_str(),
		ConvertArray(static_cast<int64>(winner->roll), roll_str));

	LogLootFilters(
		"advloot resolve item [{}] corpse [{}] tier [{}] winner [{}] roll [{}]",
		roll.item_id,
		corpse->GetID(),
		need.empty() ? "Greed" : "Need",
		winner->name,
		winner->roll
	);

	// The client's CAdvancedLootWnd::DoAdvLootAction only fires when the assignee is non-zero, so the
	// winner is shown by the state 4 packet with the assignee set. Send it before the delivery so the
	// row and the chat line agree.
	EQApplicationPacket *state_packet = AdvLoot::BuildStatePacket(roll.item_id, winner->character_id, AdvLoot::StateAskCompleted, 0);
	if (state_packet) {
		for (const auto &participant : roll.participants) {
			Client *client = entity_list.GetClientByCharID(participant.character_id);
			if (client) {
				client->QueuePacket(state_packet);
			}
		}
		delete state_packet;
	}

	Award(winner_client, roll.item_id, static_cast<uint16_t>(quantity));
	Deliver(winner_client, roll.item_id, static_cast<uint16_t>(quantity));
}

void Session::Assign(Roll &roll, Client *target)
{
	// Decision 3: assignment is an override that resolves immediately and cancels the timer.
	roll.armed = false;
	roll.ask_timer.Disable();
	roll.state = AdvLoot::StateAskCompleted;
	ClearAsk(roll);

	uint32 quantity = corpse->GetLootQuantityByItemID(roll.item_id);
	if (!quantity) {
		quantity = 1;
	}

	char count_str[32];

	ChatToRoll(roll, AdvLoot::StringId::GivenTo,
		roll.item_name.c_str(),
		ConvertArray(static_cast<int64>(quantity), count_str),
		corpse->corpse_name,
		target->GetCleanName());

	LogLootFilters(
		"advloot assignment item [{}] corpse [{}] to [{}] - timer cancelled",
		roll.item_id,
		corpse->GetID(),
		target->GetCleanName()
	);

	Award(target, roll.item_id, static_cast<uint16_t>(quantity));
	Deliver(target, roll.item_id, static_cast<uint16_t>(quantity));
}

// ML_LeaveOnCorpse: the master looter (or the player the row belongs to) has decided the item stays on the
// corpse. Cancel any running ask, mark the row removed, tell the participants with the client's own wording
// (eqstr 11313 "--%1 left %2 %3 on %4.--"), and drop the row from the window of everyone who had one.
void Session::LeaveOnCorpse(Roll &roll, Client *who)
{
	roll.armed = false;
	roll.ask_timer.Disable();
	roll.state = AdvLoot::StateRemoved;
	ClearAsk(roll);

	// The units go back to the shared pool for whoever takes them next.
	ClearAwards(roll.item_id);

	uint32 quantity = corpse->GetLootQuantityByItemID(roll.item_id);
	if (!quantity) {
		quantity = 1;
	}

	char count_str[32];

	ChatToRoll(roll, AdvLoot::StringId::LeftOnCorpse,
		who ? who->GetCleanName() : "",
		ConvertArray(static_cast<int64>(quantity), count_str),
		roll.item_name.c_str(),
		corpse->corpse_name);

	LogLootFilters(
		"advloot leave on corpse item [{}] corpse [{}] by [{}] - [{}] unit(s) left, window cancelled",
		roll.item_id,
		corpse->GetID(),
		who ? who->GetCleanName() : "?",
		quantity
	);

	// The row is still on the corpse, so only the window representation changes: FUN_140154a90 with a
	// non-zero flag removes the matching row without a full rebuild.
	if (who) {
		who->SendAdvLootRowRemoved(roll.item_id, roll.item_name);
	}
}

// ---------------------------------------------------------------- viewers

View &Session::ViewFor(uint32_t character_id)
{
	View &view = views[character_id];
	view.character_id = character_id;
	return view;
}

bool Session::HasView(uint32_t character_id) const
{
	return views.find(character_id) != views.end();
}

// A looter who is no longer able to take part must not stay in the running. Resolve() draws a roll for every
// participant and only checks whether the *winner* is online, so an offline looter used to win a row and then
// have the item left on the corpse even though somebody who could collect it was standing right there.
//
// This drops them from the running without touching units already awarded to them: dying on a pull should not
// re-open an item the master looter already gave them.
void Session::DropFromArmedRolls(uint32_t character_id, const char *reason)
{
	for (auto &entry : rolls) {
		Roll &roll = entry.second;

		const size_t before = roll.participants.size();

		roll.participants.erase(
			std::remove_if(
				roll.participants.begin(),
				roll.participants.end(),
				[character_id](const AdvLoot::Participant &p) { return p.character_id == character_id; }
			),
			roll.participants.end()
		);

		if (roll.participants.size() == before) {
			continue;
		}

		LogLootFilters(
			"advloot drop looter [{}] from item [{}] corpse [{}] - {}",
			character_id,
			roll.item_id,
			corpse->GetID(),
			reason
		);

		if (roll.participants.empty()) {
			// Nobody left to decide. Close the window where it stands rather than calling Resolve(): that
			// branch prints 11315 to every viewer, and viewers who just lost their rows should not be told a
			// roll happened.
			if (roll.armed) {
				roll.armed = false;
				roll.ask_timer.Disable();
			}

			ClearAsk(roll);
			roll.state = AdvLoot::StateRemoved;
			continue;
		}

		// Removing somebody can be what completes the ask - the last unanswered participant was them.
		RefreshAskState(roll);
	}
}

// The corpse is no longer an Advanced Loot corpse. Everything that was decided but not yet carried away goes
// back to the shared pool, which is the promise eqstr 11315 already makes the player.
void Session::Close(const char *reason)
{
	if (rolls.empty() && views.empty() && awards.empty()) {
		return;
	}

	LogLootFilters(
		"advloot session closed corpse [{}] - {} ({} row(s), {} viewer(s), {} item(s) awarded)",
		corpse->GetID(),
		reason,
		rolls.size(),
		views.size(),
		awards.size()
	);

	for (auto &entry : rolls) {
		Roll &roll = entry.second;

		roll.armed = false;
		roll.ask_timer.Disable();
		ClearAsk(roll);
		roll.state = AdvLoot::StateRemoved;
	}

	rolls.clear();
	awards.clear();
	views.clear();
}

void Session::ForgetViewer(uint32_t character_id)
{
	// Their rows are gone, so they cannot answer anything any more either.
	DropFromArmedRolls(character_id, "viewer left the zone or disconnected");

	views.erase(character_id);

	// Their personal units return to the shared pool: the corpse still holds them, nobody else has been
	// given them.
	for (auto item = awards.begin(); item != awards.end(); ) {
		item->second.erase(character_id);
		if (item->second.empty()) {
			item = awards.erase(item);
		} else {
			++item;
		}
	}
}

std::vector<uint32_t> Session::ViewerIDs() const
{
	std::vector<uint32_t> ids;
	ids.reserve(views.size());
	for (const auto &entry : views) {
		ids.push_back(entry.first);
	}
	return ids;
}

void Session::SetPendingRows(uint32_t character_id, uint32_t count)
{
	ViewFor(character_id).pending_rows = count;
}

void Session::ConsumePendingRow(uint32_t character_id)
{
	auto it = views.find(character_id);
	if (it == views.end()) {
		return;
	}

	if (it->second.pending_rows > 0) {
		--it->second.pending_rows;
	}
}

uint32_t Session::PendingRows(uint32_t character_id) const
{
	auto it = views.find(character_id);
	return it == views.end() ? 0 : it->second.pending_rows;
}

bool Session::HasPendingRows(uint32_t character_id) const
{
	return PendingRows(character_id) > 0;
}

void Session::ClearPendingRows()
{
	views.clear();
}

void Session::ClearPendingRowsFor(uint32_t character_id)
{
	views.erase(character_id);
}

// ---------------------------------------------------------------- ask keys

void Session::RememberAskKey(uint32_t character_id, uint32_t ask_key, uint32_t corpse_key, uint32_t item_id)
{
	if (!ask_key) {
		return;
	}

	View &view = ViewFor(character_id);
	AskGroup &group = view.ask_groups[ask_key];

	if (group.corpse_key != corpse_key || group.item_id != item_id) {
		Client *client = entity_list.GetClientByCharID(character_id);
		LogLootFilters(
			"[{}] ask group [{}] -> corpse [{}] item [{}]",
			client ? client->GetCleanName() : "?",
			ask_key,
			corpse_key,
			item_id
		);
	}

	group.corpse_key = corpse_key;
	group.item_id    = item_id;
}

uint32_t Session::FindAskKey(uint32_t character_id, uint32_t corpse_key, uint32_t item_id) const
{
	auto it = views.find(character_id);
	if (it == views.end()) {
		return 0;
	}

	for (const auto &entry : it->second.ask_groups) {
		if (entry.second.corpse_key == corpse_key && entry.second.item_id == item_id) {
			return entry.first;
		}
	}

	return 0;
}

bool Session::FindAskGroup(uint32_t character_id, uint32_t ask_key, AskGroup *out) const
{
	auto it = views.find(character_id);
	if (it == views.end()) {
		return false;
	}

	auto group = it->second.ask_groups.find(ask_key);
	if (group == it->second.ask_groups.end()) {
		return false;
	}

	if (out) {
		*out = group->second;
	}

	return true;
}

// ---------------------------------------------------------------- ask lifecycle

// The 0x0a the client just sent is the authoritative shape of this row's ask: its key, its state, the window
// it has left, and the LootDetails entries it covers. Everything the server re-issues later is built from
// this, and the key is echoed unchanged (rule R3) because FUN_140153400 installs the record at
// manager+0x148[ask_key % 0x14] and FUN_1400a0480 binds it to row+0x60.
void Session::RecordAsk(Roll &roll, const ItemUpdate &update)
{
	roll.ask     = update;
	roll.has_ask = update.ask_key != 0;

	if (roll.ask.seconds) {
		roll.ask_deadline = Timer::GetCurrentTime() + roll.ask.seconds * 1000;
	}

	LogLootFilters(
		"advloot ask recorded item [{}] corpse [{}] ask_key [{}] state [{}] seconds [{}] entries [{}]",
		roll.item_id,
		corpse->GetID(),
		update.ask_key,
		AdvLoot::StateName(update.state),
		update.seconds,
		update.entries.size()
	);
}

// The ask is over: the row resolved, was assigned, or was left on the corpse. There is nothing left to
// re-issue, and a stale record would put a finished countdown back on the window after the next rebuild.
void Session::ClearAsk(Roll &roll)
{
	if (!roll.has_ask) {
		return;
	}

	roll.ask          = ItemUpdate{};
	roll.has_ask      = false;
	roll.ask_deadline = 0;
}

// Rule R2. A 0x03 rebuild clears the client's corpse hash, its ask table (manager+0x128) and its ask counter
// (manager+0x1e8 -> 0) before it re-adds the rows, so an ask that is still outstanding is gone from the
// window unless the server puts it back. Re-issuing re-installs the record and re-binds row+0x60, which is
// what 0x0e, 0x360e/0x0d and 0x12 all match against - and it is how a player who joined mid-ask sees the
// same countdown as everyone else.
//
// Sent to this viewer only, and only after the 0x03 that made the rebuild.
void Session::ReissueAsks(Client *c)
{
	if (!c || !RuleB(Loot, AdvLootReissueAsks)) {
		return;
	}

	const uint32_t now = Timer::GetCurrentTime();

	for (const auto &entry : rolls) {
		const Roll &roll = entry.second;

		if (!roll.has_ask || !roll.ask.ask_key) {
			continue;
		}

		ItemUpdate ask = roll.ask;

		if (ask.state == AdvLoot::StateAskCompleted || ask.state == AdvLoot::StateFixedAskCompleted) {
			// Completed but not yet resolved: the record still has to exist for the Manage Loot window and for
			// 0x12, but there is no countdown left to run.
			ask.seconds = 0;
		}
		else if (roll.ask_deadline > now) {
			// Round up so the client never shows a shorter countdown than the server is still waiting for.
			ask.seconds = (roll.ask_deadline - now + 999) / 1000;
		}
		else {
			// The window has run out; Tick() resolves the row on this same clock.
			continue;
		}

		EQApplicationPacket *app = AdvLoot::BuildItemUpdatePacket(ask);
		if (!app) {
			continue;
		}

		LogLootFilters(
			"[{}] re-issuing ask item [{}] corpse [{}] ask_key [{}] state [{}] [{}]s left",
			c->GetCleanName(),
			roll.item_id,
			corpse->GetID(),
			ask.ask_key,
			AdvLoot::StateName(ask.state),
			ask.seconds
		);

		c->QueuePacket(app);
		delete app;
	}
}

// The client moves a row to state 4 Ask Completed once every entry on it has an answer, and 4 is the only
// state ML_RollButton (0x12) fires from. The server cannot see the client's countdown, but it can see the
// answers, so it derives the same state and tells the participants. The window is not resolved early - the
// timer keeps running, exactly as the row keeps counting down on the client.
void Session::RefreshAskState(Roll &roll)
{
	if (!roll.armed || roll.participants.empty()) {
		return;
	}

	for (const auto &p : roll.participants) {
		// Never is an answer - the looter declined. Only an unanswered row is incomplete, and eqstr 11319
		// ("The ask has already completed. Your selection has been changed from %1 back to %2.") shows the
		// client refuses to reopen one either, so this only ever advances.
		if (p.choice == AdvLoot::ChoiceNone) {
			return;
		}
	}

	if (roll.state == AdvLoot::StateAskCompleted) {
		return;
	}

	roll.state     = AdvLoot::StateAskCompleted;
	roll.ask.state = AdvLoot::StateAskCompleted;

	LogLootFilters(
		"advloot ask completed item [{}] corpse [{}] ask_key [{}] - [{}] participant(s) all answered",
		roll.item_id,
		corpse->GetID(),
		roll.ask.ask_key,
		roll.participants.size()
	);

	EQApplicationPacket *app = AdvLoot::BuildStatePacket(roll.item_id, 0, AdvLoot::StateAskCompleted, 0);
	if (!app) {
		return;
	}

	for (const auto &p : roll.participants) {
		Client *client = entity_list.GetClientByCharID(p.character_id);
		if (client) {
			client->QueuePacket(app);
		}
	}

	delete app;
}

} // namespace AdvLoot
