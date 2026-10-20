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

// Laurion (SoF x64) mercenary protocol layer.
//
// Everything here speaks the packet shapes the Laurion client actually parses. The RoF/RoF2
// mercenary code in merc.cpp is left alone; the Laurion paths are separate functions that the
// existing call sites dispatch to, so an older client never sees a Laurion-shaped packet and a
// Laurion client never sees a RoF-shaped one.
//
// Two dispatch paths exist on the client and they are not interchangeable:
//
//   * FUN_1402af060(mgr, opcode, payload, size) is CMercenaryClientManager's dispatcher. It owns
//     the lists, the shop, the state machine, assist, transfer and the record dump.
//   * ZonePacket__dispatchRecv arms 0x0516, 0x3e8c, 0x50e6 and 0x78e8 at 0x1401e37ea,
//     0x1401e41aa, 0x1401e44b7 and 0x1401e4a49 and routes them into FUN_1401a5fa0, which is the
//     MercenaryAlternateAdvancementManagerClient. Mercenary AA never reaches the mercenary manager.
//
// Payload shapes were read out of the dispatcher arms themselves; the offsets are recorded in the
// comments next to each function and in docs/mercenary.md section 4.

#include "merc.h"

#include "client.h"
#include "entity.h"
#include "zonedb.h"
#include "zone.h"

#include "common/patches/laurion_structs.h"
#include "common/repositories/merc_aa_ability_repository.h"
#include "common/rulesys.h"
#include "common/repositories/merc_aa_currency_repository.h"
#include "common/repositories/merc_aa_prereqs_repository.h"
#include "common/repositories/merc_aa_rank_repository.h"
#include "common/repositories/player_merc_aa_repository.h"

#include <algorithm>
#include <map>
#include <vector>

namespace {

// MercenaryClientData+0x04. The LaurionMercState / LaurionSwitchResult / LaurionAAAction /
// LaurionHireResult enums these values come from live in zone/merc.h so that the packet handlers in
// client_packet.cpp can name the codes they reply with.

uint32_t LaurionStateForClient(Client *c)
{
	const MercInfo &info = c->GetMercInfo();

	if (!info.mercid) {
		return LaurionMercStateNone;
	}

	if (c->GetMerc()) {
		return LaurionMercStateActive;
	}

	if (info.IsSuspended) {
		return LaurionMercStateSuspended;
	}

	return LaurionMercStateReady;
}

} // namespace

// ---------------------------------------------------------------------------
// Active mercenary state
// ---------------------------------------------------------------------------

// OP_MercenaryAssign (Laurion 0x4c40), 24 bytes, every field dword aligned.
//
// Disassembly 0x1402af701..0x1402af740:
//   MOVZX EDX, byte  ptr [R15]        -> FUN_1402a9170 -> MercenaryClientData+0x00
//   MOV   EDX, dword ptr [R15 + 0x0C] -> FUN_1402a9190 -> MercenaryClientData+0x04
//   MOV   RDX, qword ptr [R15 + 0x10] -> FUN_140081e70 -> MercenaryClientData+0x08
//   MOV   EAX, dword ptr [R15 + 0x04] -> mgr+0x278
//   MOV   EAX, dword ptr [R15 + 0x08] -> mgr+0x27C
// then FUN_1402ac550(mgr), which raises UI event 0x13 and, for state 0, event 5, or for state
// 1/2/4/5, event 2 -- that is what repaints the Manage window's buttons.
//
// The client also inspects the transition before applying it (0x1402af6a0..0x1402af6db): if it
// had no mercenary and this one says it does, or its own state was 3 or outside 0..5 while State
// is not 6, it sends OP_MercenaryTimerRequest (0x7172) back. If it had a mercenary, this says it
// has none, and State == 6, it runs its own local dismiss. Sending a coherent state is therefore
// what keeps the client from spamming re-sync requests.
void Client::SendMercAssignStateLaurion()
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	const MercInfo &info = GetMercInfo();

	MercenaryAssignLaurion_Struct s{};
	s.HasMercenary   = GetMerc() ? 1u : 0u;
	s.InitTimeMS     = static_cast<uint32>(RuleI(Mercs, UpkeepIntervalMS));
	s.WarningTimeMS  = static_cast<uint32>(RuleI(Mercs, SuspendIntervalMS));
	s.State          = LaurionStateForClient(this);
	s.RestorationTime = 0;

	// RestorationTime is the unix time the mercenary becomes usable again. The client compares it
	// against its own clock to grey out the Unsuspend button, so a suspended mercenary must carry
	// the same value EQEmu already stores for the pTimerMercSuspend cooldown.
	if (!info.mercid) {
		s.RestorationTime = 0;
	} else if (info.IsSuspended && !GetMerc()) {
		s.RestorationTime = static_cast<uint64>(info.SuspendedTime);
	}

	auto outapp = new EQApplicationPacket(OP_MercenaryAssign, sizeof(MercenaryAssignLaurion_Struct));
	outapp->WriteUInt32(s.HasMercenary);
	outapp->WriteUInt32(s.InitTimeMS);
	outapp->WriteUInt32(s.WarningTimeMS);
	outapp->WriteUInt32(s.State);
	outapp->WriteUInt64(s.RestorationTime);

	FastQueuePacket(&outapp);

	Log(Logs::Detail, Logs::Mercenaries,
		"Sent Laurion mercenary state push: has %u, state %u, restoration %llu, init %u ms, warn %u ms. Client: %s.",
		s.HasMercenary, s.State, static_cast<unsigned long long>(s.RestorationTime),
		s.InitTimeMS, s.WarningTimeMS, GetName());
}

// OP_MercenarySpawnLink (Laurion 0x33e0).
//
// Arm at 0x1402af3a0:
//   spawn = SpawnManager->GetSpawn(payload[1]); if (spawn) spawn->0x1348 = payload[0];
//   spawn = SpawnManager->GetSpawn(payload[0]); if (!spawn) return; spawn->0x134C = payload[1];
//   FUN_1402e2050(spawn);
//
// This is what makes the mercenary NPC *yours* in the client's eyes: the owner gets a back-pointer
// to its mercenary and the mercenary gets its master. Without it there is no pet bar, the Manage
// window cannot follow the mercenary, and the client's own assist/selection logic has nothing to
// key on. It has to be sent after the spawn exists, so it is called from SpawnMerc.
void Client::SendMercenarySpawnLinkLaurion(Merc *merc)
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	if (!merc || !merc->GetID()) {
		return;
	}

	auto outapp = new EQApplicationPacket(OP_MercenarySpawnLink, sizeof(MercenarySpawnLink_Struct));
	outapp->WriteSInt32(static_cast<int32>(merc->GetID()));
	outapp->WriteSInt32(static_cast<int32>(GetID()));
	FastQueuePacket(&outapp);

	Log(Logs::Detail, Logs::Mercenaries,
		"Sent Laurion mercenary spawn link: merc %u -> owner %u. Client: %s.",
		merc->GetID(), GetID(), GetName());
}

// OP_MercenarySpawnId (Laurion 0x3296): mgr+0x274. The arm at 0x1402af1a0 zeroes mgr+0x274 first
// and only overwrites it when a payload is present, so a zero length packet is a valid "no
// mercenary in the world" message.
void Client::SendMercenarySpawnIdLaurion(uint32 spawn_id)
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	auto outapp = new EQApplicationPacket(OP_MercenarySpawnId, spawn_id ? 4 : 0);
	if (spawn_id) {
		outapp->WriteSInt32(static_cast<int32>(spawn_id));
	}
	FastQueuePacket(&outapp);
}

// ---------------------------------------------------------------------------
// State notifications
// ---------------------------------------------------------------------------

// OP_MercenaryTimer (Laurion 0x6f85): i32 time_remaining -> MercenaryInfo+0x24.
// Arm at 0x1402af935: MOV EDX, dword [R8] / CALL FUN_1402a91a0, then mgr+0x280 = GetTickCount()
// and UI event 0x13. This is the upkeep tick that drives the Manage window's TimeLeftText.
void Client::SendMercenaryTimerLaurion(int32 time_remaining)
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	auto outapp = new EQApplicationPacket(OP_MercenaryTimer, 4);
	outapp->WriteSInt32(time_remaining);
	FastQueuePacket(&outapp);
}

// OP_MercenarySuspendResponse (Laurion 0x4815), opcode only.
// Arm at 0x1402af6d0: unless the state is already 0, 1, 4 or 5 it becomes 2, then
// "Your mercenary has been suspended." and UI event 6.
void Client::SendMercenarySuspendedLaurion()
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	auto outapp = new EQApplicationPacket(OP_MercenarySuspendResponse, 0);
	FastQueuePacket(&outapp);
}

// OP_MercenaryUnsuspend (Laurion 0x184e), opcode only. Arm: state <- 6,
// MercenaryInfo.unsuspendedTime <- Util__FastTime(), UI event 3.
void Client::SendMercenaryUnsuspendLaurion()
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	auto outapp = new EQApplicationPacket(OP_MercenaryUnsuspend, 0);
	FastQueuePacket(&outapp);
}

// OP_MercenaryUnsuspendResponse (Laurion 0x421c): u8 use_alt_currency.
// Arm at 0x1402af2d6: state <- 6, unsuspendedTime <- now, then FUN_1402ac830(second_manager,
// payload[0]) prints the upkeep charge line out of the MercenaryInfo the server already sent:
//   payload[0] == 0 -> "You have been charged a mercenary upkeep cost of %d plat, and %d gold and
//                       your mercenary upkeep cost timer has been reset to %s."
//                       plat = upkeepCost / 10, gold = upkeepCost % 10
//   payload[0] != 0 -> "... %d %s ..." using altUpkeepCost and the dbstr name of altCurrencyId
// This is why the cost fields are in 1/10 plat units on the wire.
//
// Note that 0x5193 reason 11 and reason 12 reach the exact same FUN_1402ac7e0 -> FUN_1402ac830 pair,
// so the upkeep tick in UpdateMercTimer uses those and must not also send this packet, or the player
// reads the identical sentence twice. This sender exists for flows that charge upkeep without going
// through a hire response, and for the unsuspend-with-charge path.
void Client::SendMercenaryUpkeepChargedLaurion(bool use_alt_currency)
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	auto outapp = new EQApplicationPacket(OP_MercenaryUnsuspendResponse, 1);
	outapp->WriteUInt8(use_alt_currency ? 1 : 0);
	FastQueuePacket(&outapp);
}

// OP_MercenaryStateDead (Laurion 0x0d70), opcode only. Arm at 0x1402af0da: state <- 4,
// restorationTime <- 0, UI event 2.
void Client::SendMercenaryDeadLaurion()
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	auto outapp = new EQApplicationPacket(OP_MercenaryStateDead, 0);
	FastQueuePacket(&outapp);
}

// OP_MercenaryStateNone (Laurion 0x4cff): i64 restorationTime. Arm: state <- 0, UI event 5.
void Client::SendMercenaryStateNoneLaurion(int64 restoration_time)
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	auto outapp = new EQApplicationPacket(OP_MercenaryStateNone, 8);
	outapp->WriteUInt64(static_cast<uint64>(restoration_time));
	FastQueuePacket(&outapp);
}

// OP_MercenaryStateReady (Laurion 0x6c4b): i64 restorationTime. Arm at 0x1402af984: state <- 1,
// then, only when a payload is present, restorationTime <- payload as a qword. UI event 2.
void Client::SendMercenaryStateReadyLaurion(int64 restoration_time)
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	auto outapp = new EQApplicationPacket(OP_MercenaryStateReady, 8);
	outapp->WriteUInt64(static_cast<uint64>(restoration_time));
	FastQueuePacket(&outapp);
}

// OP_MercenaryClearTimers (Laurion 0x06ed), opcode only. Arm at 0x1402af12c: unsuspendedTime and
// restorationTime both <- Util__FastTime(), then "Your mercenary timers have all been cleared".
void Client::SendMercenaryClearTimersLaurion()
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	auto outapp = new EQApplicationPacket(OP_MercenaryClearTimers, 0);
	FastQueuePacket(&outapp);
}

// OP_MercenaryCostUpdate (Laurion 0x4135), 4 x i32 in 1/10 plat units.
// Arm at 0x1402af33e loads payload+4, +8 and +0xC into EBX, EDI and ESI and writes them to
// MercenaryInfo +0x0C, +0x14, +0x10 and +0x18 in that order -- so the wire order is
// initial, altInitial, upkeep, altUpkeep, which is not the struct's field order. Then
// FUN_1402a90c0 and UI event 0x1A.
//
// This is the cheap way to reprice a mercenary after a level up or an AA purchase without
// replacing the whole owned list, which is a full clear-and-rebuild on the client.
void Client::SendMercenaryCostUpdateLaurion()
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	const MercInfo &info = GetMercInfo();
	if (!info.mercid) {
		return;
	}

	// Alt costs are zero on Laurion: see LaurionAltCurrencySupported() in zone/merc.cpp. A
	// non-zero currency id here makes FUN_1402ac830 look up currency 19 in a container the client
	// never received and print "Unknown DB String 0-17" in the cost line.
	MercenaryCostUpdate_Struct s{};
	s.InitialCost     = Merc::CalcPurchaseCost(info.MercTemplateID, GetLevel(), 0);
	s.AltInitialCost  = 0;
	s.UpkeepCost      = Merc::CalcUpkeepCost(info.MercTemplateID, GetLevel(), 0);
	s.AltUpkeepCost   = 0;

	auto outapp = new EQApplicationPacket(OP_MercenaryCostUpdate, sizeof(MercenaryCostUpdate_Struct));
	outapp->WriteUInt32(s.InitialCost);
	outapp->WriteUInt32(s.AltInitialCost);
	outapp->WriteUInt32(s.UpkeepCost);
	outapp->WriteUInt32(s.AltUpkeepCost);
	FastQueuePacket(&outapp);
}

// ---------------------------------------------------------------------------
// Selection and switching
// ---------------------------------------------------------------------------

// OP_MercenarySelect (Laurion 0xb8f3): i32 index into the owned list.
// Arm at 0x1402afa48: reads payload[0] only when size >= 4, ignores it when it is negative or
// >= the owned count, sets mgr+0x268 (currMercenaryIndex), copies list[index] (0xE0 per record)
// into MercenaryClientData with FUN_1402a90e0, rebuilds the stance list with FUN_1402b1e20 and
// then runs FUN_1402ac550. The client never sends this opcode: it is how the server tells the
// client which owned mercenary is now the active one.
void Client::SendMercenarySelectLaurion(int32 index)
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	auto outapp = new EQApplicationPacket(OP_MercenarySelect, 4);
	outapp->WriteSInt32(index);
	FastQueuePacket(&outapp);
}

// OP_MercenarySwitch result (Laurion 0x72b6), 12 bytes. FUN_1402ae8e0:
//   result 0 -> if remaining != 0: state <- 1 and restorationTime <- now + remaining
//   result 1 -> "You cannot switch mercenaries because you are not in fast regeneration mode!"
//   result 2 -> remaining == 0: "cannot unsuspend the selected mercenary!"
//               otherwise:     "cannot unsuspend ... for another %s!" with remaining formatted
//   result 3 -> "you have lost access to that slot."
//   result 4 -> "while your current mercenary is dead!"
//   result 5 -> "You cannot switch mercenaries right now."
// Any non-zero result also raises UI event 0xC.
void Client::SendMercenarySwitchResultLaurion(uint32 suspend_remaining_ms, uint32 result)
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	auto outapp = new EQApplicationPacket(OP_MercenarySwitch, sizeof(MercenarySwitchResult_Struct));
	outapp->WriteUInt32(0);
	outapp->WriteUInt32(suspend_remaining_ms);
	outapp->WriteUInt32(result);
	FastQueuePacket(&outapp);

	Log(Logs::Detail, Logs::Mercenaries,
		"Sent Laurion switch result %u (remaining %u ms) to %s.", result, suspend_remaining_ms, GetName());
}

// ---------------------------------------------------------------------------
// Assist
// ---------------------------------------------------------------------------

// OP_MercenaryDataUpdateRequest (Laurion 0x0b22) is bidirectional, and the two directions are not
// the same shape. Client->server is the 8-byte stance pair. The server->client arm at 0x1402af12c
// reads only the first dword when a payload is present (EAX = 0 when the packet is empty) and raises
// UI event 0x10 with it through FUN_1402ab7f0, which is what the stance combo box listens for.
// 4 bytes: i32 stance_id.
void Client::SendMercenaryStanceNotificationLaurion(int32 stance_id)
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	auto outapp = new EQApplicationPacket(OP_MercenaryDataUpdateRequest, 4);
	outapp->WriteSInt32(stance_id);
	FastQueuePacket(&outapp);
}

// OP_MercenaryAssistModeResult (Laurion 0x4be9): i32 value, -1 means "unset".
// Arm at 0x1402af76c: prints string 0x37ad from the legacy StringTable and raises UI event 0x17
// with the value, which is what the AssistModeCheckbox reads back.
void Client::SendMercenaryAssistModeLaurion(int32 value)
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	auto outapp = new EQApplicationPacket(OP_MercenaryAssistModeResult, 4);
	outapp->WriteSInt32(value);
	FastQueuePacket(&outapp);
}

// OP_MercenaryAssistMode (Laurion 0x2d14) [Client->Server]: u8 on/off.
// FUN_1402b18c0 sets MercenaryInfo.autoAssist (MercenaryClientData+0x50) locally, raises UI event
// 0x15 and sends the byte. Driven by the Manage window's AssistModeCheckbox (FUN_1404504d0) and by
// "/assistmerc on|off" (FUN_140228c00, which also prints "Mercenary auto-assist is now on/off.").
void Client::Handle_OP_MercenaryAssistMode(const EQApplicationPacket *app)
{
	if (app->size != 1) {
		LogDebug("Size mismatch in OP_MercenaryAssistMode expected [1] got [{}]", app->size);
		return;
	}

	if (!RuleB(Mercs, AllowMercs)) {
		return;
	}

	const bool enabled = app->pBuffer[0] != 0;

	// The flag belongs to the owned mercenary, not to the spawn, so it survives a suspend and a
	// zone change. Persist it with the mercenary row.
	GetMercInfo().AutoAssist = enabled;

	if (Merc *merc = GetMerc()) {
		merc->Save();
	}

	Log(Logs::General, Logs::Mercenaries, "Mercenary auto-assist set to %s for %s.",
		enabled ? "on" : "off", GetName());

	// Echo it back so the checkbox and any other listener converge on the server's answer.
	SendMercenaryAssistModeLaurion(enabled ? 1 : 0);

	// Turning assist on with a mercenary already in the world assists immediately, which matches
	// what the client does locally for "/assistmerc on".
	if (enabled) {
		AssistMercLaurion();
	}
}

// OP_MercenaryCallAssist (Laurion 0x0183) [Client->Server]: u8 0x30.
// FUN_1402a9f20, reached from the Manage window's CallForAssistButton and from "/assistmerc" with
// no argument (FUN_140228c00). The client checks FUN_1402ac4f0 first and prints "You have no
// mercenary to assist you!" locally when there is none, so a request here always means the player
// has a mercenary.
void Client::Handle_OP_MercenaryCallAssist(const EQApplicationPacket *app)
{
	if (app->size != 1) {
		LogDebug("Size mismatch in OP_MercenaryCallAssist expected [1] got [{}]", app->size);
		return;
	}

	if (!RuleB(Mercs, AllowMercs)) {
		return;
	}

	AssistMercLaurion();
}

// Shared body for both assist entry points. The mercenary takes the owner's current target, which
// is what the client's own assist display expects to see happen.
void Client::AssistMercLaurion()
{
	Merc *merc = GetMerc();
	if (!merc) {
		return;
	}

	Mob *owner_target = GetTarget();

	if (!owner_target || owner_target == this || !owner_target->IsNPC()) {
		return;
	}

	// Same range guard Handle_OP_Assist uses, so a mercenary cannot be used to target-scan.
	if (DistanceSquared(m_Position, owner_target->GetPosition()) > TARGETING_RANGE * TARGETING_RANGE) {
		return;
	}

	merc->SetTarget(owner_target);

	Log(Logs::Detail, Logs::Mercenaries, "Mercenary %s assisting %s against %s.",
		merc->GetName(), GetName(), owner_target->GetName());
}

// ---------------------------------------------------------------------------
// Transfer
// ---------------------------------------------------------------------------

// OP_MercenaryTransferResult (Laurion 0x452c) [Client->Server].
// FUN_1402ad4e0's else branch: when the client already holds a pending offer at mgr+0x2D8 and a
// second one arrives, it echoes the stored offer back on 0x452c with Result forced to 4 (denied).
// The buffer is the same 28 byte MercenaryTransfer_Struct it received, so the server must read it
// as such rather than as a small result code.
void Client::Handle_OP_MercenaryTransferResult(const EQApplicationPacket *app)
{
	if (app->size != sizeof(MercenaryTransfer_Struct)) {
		LogDebug("Size mismatch in OP_MercenaryTransferResult expected [{}] got [{}]",
			sizeof(MercenaryTransfer_Struct), app->size);
		return;
	}

	const auto *tr = reinterpret_cast<const MercenaryTransfer_Struct *>(app->pBuffer);

	// Result 4 is the client's own "already pending" denial. Anything else would mean the client
	// accepted an offer, which the server has no UI for yet; log it rather than inventing state.
	Log(Logs::General, Logs::Mercenaries,
		"Mercenary transfer reply from %s: source %d, target %d, result %d. Mercenary transfer is not implemented server-side yet.",
		GetName(), tr->SourceSpawnID, tr->TargetSpawnID, tr->Result);
}

// ---------------------------------------------------------------------------
// Mercenary record save/load
// ---------------------------------------------------------------------------

// OP_MercenaryRecordDumpReply (Laurion 0x5783) [Client->Server].
// FUN_1402ad900 builds this from the client's own mercenary list: for each entry it extracts the
// text between "RecordId=" and "!" from a saved record blob, resolves it through
// FUN_1402abee0, and logs "Index: %d - Record Id: %d - List Name: %s\n - Save String: %s\n".
// The reply is that accumulated text. Mercenary loadout save/load has no server-side storage yet,
// so this records what the client sent instead of discarding it.
void Client::Handle_OP_MercenaryRecordDumpReply(const EQApplicationPacket *app)
{
	if (!app->size) {
		return;
	}

	std::string dump;
	dump.assign(reinterpret_cast<const char *>(app->pBuffer), app->size);
	while (!dump.empty() && dump.back() == '\0') {
		dump.pop_back();
	}

	Log(Logs::Detail, Logs::Mercenaries, "Mercenary record dump from {} ({} bytes):\n{}",
		GetName(), app->size, dump);
}

// ---------------------------------------------------------------------------
// Mercenary alternate advancement: purchase, mutation, persistence
// ---------------------------------------------------------------------------

// The client's group model, which the purchase rules have to match:
//   * 0x78e8 sends one record per rank, all sharing group_id, distinguished by rank_index (f5).
//   * FUN_1401a71f0 keeps exactly one granted ability per group and only accepts a grant whose
//     rank_index is higher than the one already held.
//   * CAltAbilityData__GetMercCurrentRank returns that held record's rank_index.
//   * CAltAbilityData__GetMercMaxRank returns the group record's member counter, which
//     FUN_1401a4ff0 increments once per member -- so the maximum rank is the group size, not f3.
// m_merc_aa_ranks is keyed by ability id because that is what 0x0516 and 0x50e6 carry; the group
// is looked up through the zone's definition cache.
uint32 Client::GetMercenaryAAGroupRank(int32 group_id) const
{
	uint32 best = 0;

	for (const auto &kv : m_merc_aa_ranks) {
		const auto *def = zone->FindMercenaryAAAbility(kv.first);
		if (!def || def->group_id != group_id) {
			continue;
		}
		best = std::max(best, kv.second);
	}

	return best;
}

// Drop every granted ability in a group. Used when a rank is bought (the lower ranks are implied)
// or when a family is refunded.
void Client::ClearMercenaryAAGroup(int32 group_id)
{
	for (auto it = m_merc_aa_ranks.begin(); it != m_merc_aa_ranks.end();) {
		const auto *def = zone->FindMercenaryAAAbility(it->first);
		if (def && def->group_id == group_id) {
			it = m_merc_aa_ranks.erase(it);
		} else {
			++it;
		}
	}
}

// Persist m_merc_aa_ranks and m_merc_aa_currency.
//
// player_merc_aa is rewritten wholesale per character: the table's logical key is
// (char_id, ability_id) but the repository's PrimaryKey() is char_id alone, so FindOne/UpdateOne
// cannot address a single ability reliably. Delete-then-insert is the shape that cannot drift.
bool Client::SaveMercenaryAA()
{
	const std::string char_filter = fmt::format("`char_id` = {}", CharacterID());

	if (!PlayerMercAaRepository::DeleteWhere(database, char_filter)) {
		// Zero rows deleted is normal for a character that has never had mercenary AA.
		if (PlayerMercAaRepository::Count(database, char_filter)) {
			LogError("Failed to clear mercenary AA rows for char_id {}.", CharacterID());
			return false;
		}
	}

	if (!m_merc_aa_ranks.empty()) {
		std::vector<PlayerMercAaRepository::PlayerMercAa> rows;
		rows.reserve(m_merc_aa_ranks.size());

		for (const auto &kv : m_merc_aa_ranks) {
			auto e = PlayerMercAaRepository::NewEntity();
			e.char_id   = CharacterID();
			e.ability_id = kv.first;
			e.rank_     = kv.second;
			e.aa_value  = kv.second;
			rows.push_back(e);
		}

		if (PlayerMercAaRepository::InsertMany(database, rows) <= 0) {
			LogError("Failed to save mercenary AA ranks for char_id {}.", CharacterID());
			return false;
		}
	}

	auto currency = MercAaCurrencyRepository::FindOne(database, CharacterID());
	if (currency.char_id) {
		currency.aa_exp_pct          = m_merc_aa_currency.aa_exp_pct;
		currency.aa_points           = m_merc_aa_currency.aa_points;
		currency.aa_points_assigned  = m_merc_aa_currency.aa_points_assigned;

		if (!MercAaCurrencyRepository::UpdateOne(database, currency)) {
			LogError("Failed to update mercenary AA currency for char_id {}.", CharacterID());
			return false;
		}
	} else {
		auto e = MercAaCurrencyRepository::NewEntity();
		e.char_id             = CharacterID();
		e.aa_exp_pct          = m_merc_aa_currency.aa_exp_pct;
		e.aa_points           = m_merc_aa_currency.aa_points;
		e.aa_points_assigned  = m_merc_aa_currency.aa_points_assigned;

		MercAaCurrencyRepository::InsertOne(database, e);
	}

	return true;
}

// OP_MercenaryAAAction (Laurion 0x50e6), 20 bytes. The only mercenary AA packet with a real
// sub-command. FUN_1401a5fa0's switch:
//   0  Arg2 -> player+0x2730 (point pool)
//   1  Arg2 -> pool, Arg0 -> +0x2738 (assigned), Arg1 -> +0x2750 (available)
//   2  FUN_14061e0c0(player, Arg0, Arg1) then FUN_1401a71f0(mgr, Arg0)
//   3  FUN_14062f920(player, Arg0) then FUN_1401a69c0(mgr, Arg0)
//   4  FUN_1401a5690(player) then FUN_1401a69c0(mgr, Arg0)
//   7  same as 2, 8 same as 3
//   5 and 6 have no arm at all, so they are never sent.
//
// A grant arriving here is dropped unless the ability is already in the 0x78e8 definition
// container, which is why the definition list is sent on zone-in before anything else.
void Client::SendMercenaryAAActionLaurion(uint32 arg0, uint32 arg1, uint64 arg2, uint32 action)
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	auto outapp = new EQApplicationPacket(OP_MercenaryAAAction, sizeof(MercenaryAAAction_Struct));
	outapp->WriteUInt32(arg0);
	outapp->WriteUInt32(arg1);
	outapp->WriteUInt64(arg2);
	outapp->WriteUInt32(action);
	FastQueuePacket(&outapp);
}

// Refresh the three AA counters the AA window reads: pool, assigned, available.
void Client::SendMercenaryAACountersLaurion()
{
	const uint32 assigned = static_cast<uint32>(std::max(0, m_merc_aa_currency.aa_points_assigned));
	const uint32 available = static_cast<uint32>(std::max(0,
		m_merc_aa_currency.aa_points - m_merc_aa_currency.aa_points_assigned));
	const uint64 pool = static_cast<uint64>(static_cast<uint32>(std::max(0, m_merc_aa_currency.aa_points)));

	SendMercenaryAAActionLaurion(assigned, available, pool, LaurionAAActionSetCounters);
}

// OP_MercenaryAABuy (Laurion 0x3861) [Client->Server], 20 bytes.
//
// FUN_1401a6db0 builds it from the AA window's selection: AbilityID is
// CListWnd__GetItemData(AAW_MercAAList), i.e. field f0 of the 0x78e8 record, and ModeOrRank is 2
// for a normal purchase. The client does no cost or prerequisite checking of its own -- it sends
// the request and waits for 0x50e6 -- so every rule lives here.
void Client::Handle_OP_MercenaryAABuy(const EQApplicationPacket *app)
{
	if (app->size != sizeof(MercenaryAABuy_Struct)) {
		LogDebug("Size mismatch in OP_MercenaryAABuy expected [{}] got [{}]",
			sizeof(MercenaryAABuy_Struct), app->size);
		return;
	}

	const auto *buy = reinterpret_cast<const MercenaryAABuy_Struct *>(app->pBuffer);

	const uint32 ability_id = buy->AbilityID;

	const auto *def = zone->FindMercenaryAAAbility(ability_id);
	if (!def) {
		// The client can only offer ids it received in 0x78e8, so reaching here means either a
		// hacked packet or a definition list that is out of step with the table.
		LogError("Mercenary AA purchase for unknown ability id {} from {}. Not applying.", ability_id, GetName());
		return;
	}

	if (def->group_id < 0) {
		LogError("merc_aa_ability {} has group_id {}; the client's group model cannot rank it.",
			ability_id, def->group_id);
		return;
	}

	// Maximum rank is the number of enabled definitions sharing the group, because that is what
	// FUN_1401a4ff0 counts and what CAltAbilityData__GetMercMaxRank returns.
	const int32 max_rank = zone->MercenaryAAGroupSize(def->group_id);

	const uint32 current_rank = GetMercenaryAAGroupRank(def->group_id);

	if (current_rank >= static_cast<uint32>(max_rank)) {
		Log(Logs::General, Logs::Mercenaries,
			"Mercenary AA purchase refused for %s: group %d is already at rank %u of %d.",
			GetName(), def->group_id, current_rank, max_rank);
		SendMercenaryAACountersLaurion();
		return;
	}

	// FUN_1401a71f0 ignores a grant whose rank_index is not strictly higher than the held one, so
	// the server has to enforce the same ladder or the two sides disagree about the current rank.
	if (static_cast<uint32>(def->rank_index) != current_rank + 1) {
		Log(Logs::General, Logs::Mercenaries,
			"Mercenary AA purchase refused for %s: ability %u is rank %d but the group is at rank %u. Ranks must be bought in order.",
			GetName(), ability_id, def->rank_index, current_rank);
		SendMercenaryAACountersLaurion();
		return;
	}

	// Prerequisites, mirroring aa_rank_prereqs: the referenced ability must already be held at the
	// requested rank.
	for (const auto &pre : MercAaPrereqsRepository::GetWhere(database,
			fmt::format("`ability_id` = {} AND `rank_index` = {}", ability_id, def->rank_index))) {
		const auto *req = zone->FindMercenaryAAAbility(pre.req_ability_id);
		if (!req) {
			LogError("merc_aa_prereqs for ability {} rank {} names unknown ability {}.",
				ability_id, def->rank_index, pre.req_ability_id);
			SendMercenaryAACountersLaurion();
			return;
		}

		if (GetMercenaryAAGroupRank(req->group_id) < pre.req_rank) {
			Log(Logs::General, Logs::Mercenaries,
				"Mercenary AA purchase refused for %s: ability %u rank %d requires ability %u rank %u.",
				GetName(), ability_id, def->rank_index, pre.req_ability_id, pre.req_rank);
			SendMercenaryAACountersLaurion();
			return;
		}
	}

	const int32 cost = std::max(0, def->cost);
	const int32 available = m_merc_aa_currency.aa_points - m_merc_aa_currency.aa_points_assigned;

	if (cost > available) {
		// The client has no localized "not enough AA points" string for mercenary AA -- it only
		// reacts to 0x50e6 -- so the correct reply is to re-send the counters unchanged, which is
		// what makes the AA window's gauge stop moving.
		Log(Logs::General, Logs::Mercenaries,
			"Mercenary AA purchase refused for %s: ability %u costs %d points, %d available.",
			GetName(), ability_id, cost, available);
		SendMercenaryAACountersLaurion();
		return;
	}

	// Apply. The lower ranks in the group are implied by the higher one and the client only keeps
	// one entry per group, so drop them before inserting.
	ClearMercenaryAAGroup(def->group_id);
	SetMercenaryAARank(ability_id, static_cast<uint32>(def->rank_index));

	m_merc_aa_currency.aa_points_assigned += cost;

	if (!SaveMercenaryAA()) {
		LogError("Mercenary AA purchase applied in memory but could not be saved for {}.", GetName());
	}

	// Counters first, then the grant: action 1 rewrites pool/assigned/available, and action 2 is
	// what actually lights up the row in AAW_MercAAList.
	SendMercenaryAACountersLaurion();
	SendMercenaryAAActionLaurion(ability_id, static_cast<uint32>(def->rank_index),
		static_cast<uint64>(static_cast<uint32>(std::max(0, m_merc_aa_currency.aa_points))),
		LaurionAAActionGrant);

	// The Merc tab reads AA points out of the 0x1f38 stat table (keys 1 and 2), so refresh it too.
	SendMercenaryStats();

	Log(Logs::General, Logs::Mercenaries,
		"Mercenary AA: %s bought ability %u (group %d) at rank %d for %d points, %d/%d spent.",
		GetName(), ability_id, def->group_id, def->rank_index, cost,
		m_merc_aa_currency.aa_points_assigned, m_merc_aa_currency.aa_points);
}

// OP_MercenaryAAAction with action 3: clear one rank and give the points back. Reached from the
// AA window's refund path and from quest scripting.
void Client::ClearMercenaryAAAbilityLaurion(uint32 ability_id)
{
	const auto *def = zone->FindMercenaryAAAbility(ability_id);
	if (!def) {
		return;
	}

	const int32 refunded = std::max(0, def->cost);

	ClearMercenaryAAGroup(def->group_id);
	m_merc_aa_currency.aa_points_assigned = std::max(0, m_merc_aa_currency.aa_points_assigned - refunded);

	SaveMercenaryAA();

	SendMercenaryAAActionLaurion(ability_id, 0, 0, LaurionAAActionClear);
	SendMercenaryAACountersLaurion();
	SendMercenaryStats();
}

// Action 4: FUN_1401a5690(player) refunds the whole mercenary AA tree.
void Client::RefundMercenaryAALaurion()
{
	m_merc_aa_ranks.clear();
	m_merc_aa_currency.aa_points_assigned = 0;

	SaveMercenaryAA();

	SendMercenaryAAActionLaurion(0, 0,
		static_cast<uint64>(static_cast<uint32>(std::max(0, m_merc_aa_currency.aa_points))),
		LaurionAAActionRefundAll);
	SendMercenaryAACountersLaurion();
	SendMercenaryStats();

	Log(Logs::General, Logs::Mercenaries, "Refunded all mercenary AA for %s.", GetName());
}

// ---------------------------------------------------------------------------
// Currency message, transfer offer, level upgrade prompt, record dump
// ---------------------------------------------------------------------------

// OP_MercenaryCurrencyMessage (Laurion 0x466d).
//
// The client appends the currency name itself, resolved from db_str categories 17 (regular) and 18
// (alternate) through the player's own currency table, and prints "%s%s.". So the server supplies
// only the leading half of the sentence and must not name the currency in it. Passing currency_id 0
// makes the client fall back to the MercenaryInfo.altCurrencyId it already has.
void Client::SendMercenaryCurrencyMessageLaurion(bool use_alt_currency, int32 currency_id, const std::string &text)
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	const uint32 payload = 8 + static_cast<uint32>(text.size()) + 1;

	auto outapp = new EQApplicationPacket(OP_MercenaryCurrencyMessage, payload);
	outapp->WriteUInt8(use_alt_currency ? 1 : 0);
	outapp->WriteUInt8(0);
	outapp->WriteUInt8(0);
	outapp->WriteUInt8(0);
	outapp->WriteSInt32(currency_id);
	outapp->WriteString(text.c_str());
	FastQueuePacket(&outapp);
}

// OP_MercenaryTransferOffer (Laurion 0xd966).
//
// FUN_1402ad4e0 stores the offer at mgr+0x2D8..0x2F0 and pops a 10 second confirmation window.
// If an offer is already pending it does not replace it: it echoes the received buffer back on
// 0x452c with Result forced to 4, which is the "already pending" code. The server therefore has to
// track the pending offer itself or the two sides disagree about which offer is live.
//
// The cost wording the client picks from the buffer:
//   gold == 0 and alt == 0            "...wants to transfer his mercenary to you, is this ok?"
//   altCurrencyOnly == 0, id == -1    "...must pay an upgrade cost of %d gold."
//   altCurrencyOnly == 0, id != -1    "...must pay an upgrade cost of %d gold or %d %s."
//   altCurrencyOnly != 0              "...must pay an upgrade cost of %d %s."
void Client::SendMercenaryTransferOfferLaurion(uint32 target_spawn_id, int32 gold_cost,
	int32 alt_currency_cost, int32 alt_currency_id, bool alt_currency_only)
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	if (!GetMerc() || !GetMerc()->GetID()) {
		// The client resolves SourceSpawnID through SpawnManager and dereferences the result to read
		// the name at spawn+0xB4, so an id that does not exist in the client's spawn table is a
		// crash risk rather than a no-op.
		LogError("Refusing to send a mercenary transfer offer from {}: no mercenary spawn.", GetName());
		return;
	}

	auto outapp = new EQApplicationPacket(OP_MercenaryTransferOffer, sizeof(MercenaryTransfer_Struct));
	outapp->WriteSInt32(static_cast<int32>(GetMerc()->GetID()));
	outapp->WriteSInt32(static_cast<int32>(target_spawn_id));
	outapp->WriteSInt32(gold_cost);
	outapp->WriteSInt32(alt_currency_cost);
	outapp->WriteSInt32(alt_currency_id);
	outapp->WriteSInt32(alt_currency_only ? 1 : 0);
	outapp->WriteSInt32(0); // Result: the offer packet leaves this at 0; only 0x452c carries a code
	FastQueuePacket(&outapp);

	Log(Logs::General, Logs::Mercenaries,
		"Offered mercenary transfer from %s to spawn %u (gold %d, alt %d of currency %d, alt-only %d).",
		GetName(), target_spawn_id, gold_cost, alt_currency_cost, alt_currency_id, alt_currency_only ? 1 : 0);
}

// OP_MercenaryLevelUpgrade (Laurion 0x4a03), 120 bytes.
//
// Only the first five dwords and the name at +0x14 are understood by FUN_1402aeda0; the rest of the
// block is copied verbatim into mgr+0x2F4 for the popup's answer path. EQEmu has no unshrouding
// mechanic, so nothing triggers this automatically -- it exists so the opcode is implementable and
// reachable from a quest or a future shroud system rather than being a hole in the map.
void Client::SendMercenaryLevelUpgradeLaurion(int32 plat_cost, int32 alt_currency_cost,
	int32 from_level, int32 to_level, const std::string &mercenary_name)
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	MercenaryLevelUpgrade_Struct s{};
	s.PlatCost        = plat_cost;
	s.AltCurrencyCost = alt_currency_cost;
	s.FromLevel       = from_level;
	s.ToLevel         = to_level;
	s.Unknown10       = 0;

	// The client reads +0x14 as a %s argument, so the name has to be NUL terminated inside the
	// block. Truncating is safe; overflowing it would walk into the opaque tail the popup stores.
	const size_t capacity = sizeof(s.MercenaryName) - 1;
	const std::string name = mercenary_name.substr(0, capacity);
	memcpy(s.MercenaryName, name.c_str(), name.size());

	auto outapp = new EQApplicationPacket(OP_MercenaryLevelUpgrade, sizeof(MercenaryLevelUpgrade_Struct));
	outapp->WriteData(reinterpret_cast<const uint8_t *>(&s), sizeof(s));
	FastQueuePacket(&outapp);
}

// OP_MercenaryRecordDump (Laurion 0x2d48).
//
// Asks the client to walk its own saved mercenary records and report them back on 0x5783. The
// strings are matched on the client side by looking for "RecordId=<n>!", so the only useful content
// a server can put here is a list of such markers. This is a diagnostic facility: EQEmu has no
// mercenary loadout storage yet, so it is exposed for GM/quest use and logged rather than wired to
// any player facing flow.
void Client::SendMercenaryRecordDumpLaurion(uint64 echo, const std::vector<std::string> &records)
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	uint32 payload = 12;
	for (const auto &r : records) {
		// Every entry is a u32 length followed by exactly that many bytes; FUN_1405646e0 advances the
		// cursor by 4 + length, so the total is determined up front and cannot drift.
		payload += 4 + static_cast<uint32>(r.size());
	}

	auto outapp = new EQApplicationPacket(OP_MercenaryRecordDump, payload);
	outapp->WriteUInt64(echo);
	outapp->WriteUInt32(static_cast<uint32>(records.size()));
	for (const auto &r : records) {
		outapp->WriteUInt32(static_cast<uint32>(r.size()));
		outapp->WriteData(reinterpret_cast<const uint8_t *>(r.data()), static_cast<uint32>(r.size()));
	}

	FastQueuePacket(&outapp);
}

// OP_MercenaryLogfileRequest (Laurion 0x5baa) [Client->Server], opcode only.
//
// FUN_140166c40 prints "Generating mercenary logfile on the server." locally and sends the bare
// opcode. It is a request for the server to write out what it knows about this player's mercenaries.
void Client::Handle_OP_MercenaryLogfileRequest(const EQApplicationPacket *app)
{
	if (app->size != 0) {
		LogDebug("Size mismatch in OP_MercenaryLogfileRequest expected [0] got [{}]", app->size);
		return;
	}

	const int max_slots = std::min(RuleI(Mercs, MaxMercSlots), MAXMERCS);

	Log(Logs::General, Logs::Mercenaries,
		"Mercenary logfile for %s (char_id %u): %d slots, active slot %d, %u mercenary AA abilities granted.",
		GetName(), CharacterID(), max_slots, GetMercSlot(), m_merc_aa_ranks.size());

	for (int slot = 0; slot < max_slots; ++slot) {
		const MercInfo &info = GetMercInfo(static_cast<uint8>(slot));
		if (!info.mercid) {
			continue;
		}

		Log(Logs::General, Logs::Mercenaries,
			"  slot %d: merc_id %u, template %u, name \"%s\", stance %u, auto-assist %s, suspended %s, "
			"suspended_time %u, timer_remaining %d, hp %d/%d, mana %d/%d, endurance %d/%d.",
			slot, info.mercid, info.MercTemplateID, info.merc_name, info.Stance,
			info.AutoAssist ? "on" : "off", info.IsSuspended ? "yes" : "no",
			info.SuspendedTime, info.MercTimerRemaining,
			info.hp, 0, info.mana, 0, info.endurance, 0);
	}

	for (const auto &kv : m_merc_aa_ranks) {
		const auto *def = zone->FindMercenaryAAAbility(kv.first);
		Log(Logs::General, Logs::Mercenaries, "  mercenary AA ability %u: rank %u (group %d).",
			kv.first, kv.second, def ? def->group_id : 0);
	}

	Log(Logs::General, Logs::Mercenaries,
		"  mercenary AA currency: exp %.2f%%, points %d, assigned %d.",
		m_merc_aa_currency.aa_exp_pct, m_merc_aa_currency.aa_points,
		m_merc_aa_currency.aa_points_assigned);
}
