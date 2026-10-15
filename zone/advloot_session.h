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

#include "zone/advloot.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class Corpse;
class Client;

// The per-corpse half of Advanced Loot. See docs/advlootdesignV2.md section 4.
//
// One Session exists for each corpse that at least one Advanced-Loot-enabled player killed. It owns
// everything the loot state machine has to remember between packets: the row per item, the ask window and
// its participants, and what each viewer was shown. Corpse keeps loot storage and the legacy looting path;
// Client keeps only a weak list of the corpses it has rows for.
//
// Step 2 moved the state here; Step 3 moved the render path in, so Session::Render / RenderAll are now the
// only producers of a subcmd 0x03 loot group. Step 4 moves delivery (Corpse::AdvLootTransaction).
namespace AdvLoot {

	// One (corpse, item) row as a particular client addresses it. The client allocates the ask key itself
	// (FUN_1401501c0 from the counter at LootFiltersManager+0x1e8) and the server only ever learns it from
	// subcmd 0x0B, so it is stored per viewer rather than per corpse.
	struct AskGroup {
		uint32_t corpse_key = 0;
		uint32_t item_id    = 0;
	};

	// What this looter currently has on this corpse: how many rows the server rendered for them, and the
	// ask keys that client allocated for those rows.
	struct View {
		uint32_t character_id = 0;
		uint32_t pending_rows = 0;
		std::map<uint32_t, AskGroup> ask_groups;
	};

	// Who may decide a row on this corpse. Resolved per viewer, because one corpse can be viewed by a solo
	// player, a grouped one and a raided one at the same time. docs/advlootdesignV2.md 1.3.
	enum Authority : uint32_t {
		AuthorityNone         = 0,   // no kill credit: the viewer's rows are Locked and they may not act
		AuthorityEveryone     = 1,   // every kill-credit member may ask, give, roll or leave on corpse
		AuthorityMasterLooter = 2,   // only the construct's master looter may act
		AuthorityFreeGrab     = 3,   // every member may act, and may take a unit without a roll
	};

	const char *AuthorityName(uint32_t authority);

	class Session {
	public:
		explicit Session(Corpse *corpse)
			: corpse(corpse)
		{
		}

		Corpse *const corpse;

		// ---- rendering ----
		//
		// The only place a subcmd 0x03 loot group record is built. Everything else that puts rows on the
		// client's window goes through Render / RenderAll, which is what keeps the two panes consistent:
		// FUN_140153970 clears the corpse hash, the ask table and both panes before re-adding, so a row can
		// only be created or removed by a group record that reflects the whole corpse.
		//
		// The record is per viewer and cannot be broadcast: the loot-rights byte (group record +0x146c, which
		// FUN_14009c0a0 turns into the LootDetails Locked flag) and the filter bits come from this viewer.
		CorpseGroup BuildGroupFor(Client *c) const;
		CorpseGroup RenderGroupFor(Client *c);
		void        Render(Client *c);
		std::vector<Client *> RenderAll();

		// The answer to a refused legacy right-click. The guard itself is a session query - HasPendingRows()
		// on this viewer - and everything the corpse has to do about it lives here: re-push this corpse's rows
		// and say why the legacy window stayed closed. Registering the corpse on the client is Render()'s job,
		// the same as it is for the kill-time push, so Corpse no longer duplicates it.
		void        ExplainRowsTo(Client *c);

		// ---- mode and authority ----
		//
		// Derived at kill time from the kill-credit list and the construct that list belongs to. The mode is
		// the display value the client's 0x13 list already carries; the authority is the rule the server gates
		// the decision sub-commands with. AuthorityFor() re-reads the viewer's current raid or group, so a
		// join, a disband or a /advloot mol takes effect on the next packet without a re-render.
		void        Open();
		void        RefreshAuthority();
		uint32_t    Mode() const { return mode; }
		const char *ModeName() const;
		uint32_t    AuthorityFor(Client *c) const;
		bool        MayAct(Client *c) const { return AuthorityFor(c) != AuthorityNone; }
		bool        MayFreeGrab(Client *c) const { return AuthorityFor(c) == AuthorityFreeGrab; }
		Client     *MasterLooter() const;

		// ---- pools and delivery ----
		//
		// The client keeps two pools per corpse item record and the server has to know both, because the two
		// request sub-commands address different ones: 0x0e acts on the shared pool (+0x0C) and 0x08 acts on
		// the personal pool (+0x14). The corpse's loot list is the ground truth for what physically exists;
		// `awards` records who has been *given* what, which is what makes a 0x08 legal or a refusal.
		//
		//   shared_qty(item)   = loot list total - sum(awards[item][*])
		//   personal_qty(who)  = awards[item][who]
		//
		// In solo the whole pile is awarded to the viewer at render time, because FUN_1400a5d10 hides the
		// Shared pane unless the player is grouped - a solo looter has no shared row to act on.
		uint16_t PersonalQty(uint32_t character_id, uint32_t item_id) const;
		uint16_t SharedQty(uint32_t item_id) const;
		void     SetAward(uint32_t character_id, uint32_t item_id, uint16_t quantity);
		uint16_t Award(Client *c, uint32_t item_id, uint16_t quantity);
		void     ConsumeAward(uint32_t character_id, uint32_t item_id, uint16_t quantity);
		void     ClearAwards(uint32_t item_id);

		// The answer to a subcmd 0x08. `quantity` is what goes into both the 0x0e reply and the 0x09 answer:
		// it has to be the requester's personal pool, because FUN_140151810 only erases the client's corpse
		// item entry when the reported quantity equals that pool and the shared pool is empty.
		struct Delivery {
			uint32_t character_id = 0;
			uint32_t item_id      = 0;
			uint16_t action       = AdvLoot::ActionNone;
			uint16_t quantity     = 0;
			bool     delivered    = false;
			const char *reason    = "";
		};

		Delivery Deliver(Client *c, uint32_t item_id, uint16_t requested_qty);

		// The delivery this session performed most recently. A roll that resolves immediately inside
		// RecordChoice delivers from inside the session, and the caller that is answering the 0x08 still has
		// to report the quantity that moved.
		const Delivery &LastDelivery() const { return last_delivery; }

		// Rows, one per item id. A Roll is created the moment anything decides about that item - an opt-in,
		// an ask, a response with a name - and lives until the corpse does.
		Roll *FindRoll(uint32_t item_id);
		Roll &EnsureRoll(uint32_t item_id);
		const std::map<uint32_t, Roll> &Rolls() const { return rolls; }

		void SetResponse(uint32_t item_id, const std::string &name, uint32_t choice);
		Roll &RecordChoice(Client *c, uint32_t item_id, uint32_t choice);
		void Arm(Roll &roll);
		void Arm(Roll &roll, uint32_t seconds);
		void Tick();
		void Resolve(Roll &roll);
		void Assign(Roll &roll, Client *target);
		void LeaveOnCorpse(Roll &roll, Client *who);
		void ChatToRoll(const Roll &roll, uint32_t string_id,
			const char *arg1 = nullptr, const char *arg2 = nullptr, const char *arg3 = nullptr,
			const char *arg4 = nullptr, const char *arg5 = nullptr);

		// Viewers. A viewer exists from the moment rows are rendered for them and is dropped when they log
		// out, leave the zone, or the corpse stops having rows.
		View &ViewFor(uint32_t character_id);
		bool HasView(uint32_t character_id) const;
		void ForgetViewer(uint32_t character_id);
		std::vector<uint32_t> ViewerIDs() const;

		// Drop one character from every in-flight ask on this corpse, leaving what they were already awarded
		// alone. This is the death case: a looter who is not alive must not win a roll they cannot collect, but
		// dying does not hand an item that was already given to them to somebody else. ForgetViewer is the
		// stronger form - it calls this and also returns their awarded units to the shared pool.
		void DropFromArmedRolls(uint32_t character_id, const char *reason);

		// The corpse has stopped being an Advanced Loot corpse: it unlocked, decayed, was resurrected or went
		// to the graveyard. Close every window and put every unit that was awarded but not yet picked up back in
		// the shared pool - which is exactly what eqstr 11315 promises the player: "These items can be randomed
		// again or will be available to everyone after the corpse unlocks."
		void Close(const char *reason);

		void SetPendingRows(uint32_t character_id, uint32_t count);
		void ConsumePendingRow(uint32_t character_id);
		uint32_t PendingRows(uint32_t character_id) const;
		bool HasPendingRows(uint32_t character_id) const;
		void ClearPendingRows();
		void ClearPendingRowsFor(uint32_t character_id);

		// Ask keys, per viewer. RememberAskKey is the forward of the old Client::RememberAdvLootAskGroup;
		// FindAskKey is its reverse, used when the server has to synthesise a subcmd 0x0e for a row the
		// client asked about with a 0x08.
		void RememberAskKey(uint32_t character_id, uint32_t ask_key, uint32_t corpse_key, uint32_t item_id);
		uint32_t FindAskKey(uint32_t character_id, uint32_t corpse_key, uint32_t item_id) const;
		bool FindAskGroup(uint32_t character_id, uint32_t ask_key, AskGroup *out) const;

		// ---- ask lifecycle ----
		//
		// The client owns the ask key and the countdown; the server keeps a copy of the record so it can put
		// the ask back after a rebuild and know when the window closes. See rule R2 in docs/advlootdesignV2.md
		// 1.4 and Step 6 in 12.
		void RecordAsk(Roll &roll, const ItemUpdate &update);
		void ClearAsk(Roll &roll);
		void ReissueAsks(Client *c);
		// Derive the client's Ask Completed state: it moves a row to state 4 when every entry on it has an
		// answer, and the server can see the answers even though it cannot see the client's countdown.
		void RefreshAskState(Roll &roll);

	private:
		std::map<uint32_t, Roll>  rolls;
		std::map<uint32_t, View>  views;
		uint32_t                  opt_in_counter = 0;   // monotonic, the tie-break order

		// item_id -> (character_id -> units in that viewer's personal pool)
		std::map<uint32_t, std::map<uint32_t, uint16_t>> awards;

		Delivery last_delivery;

		uint32_t    mode      = AdvLoot::ModeSolo;
		uint32_t    authority = AuthorityEveryone;
	};

} // namespace AdvLoot
