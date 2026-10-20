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
#include "client.h"

#include "common/database.h"
#include "common/guilds.h"
#include "common/patches/laurion_structs.h"
#include "common/strings.h"
#include "zone/guild_mgr.h"
#include "zone/worldserver.h"

extern WorldServer worldserver;

void Client::SendGuildMOTD(bool GetGuildMOTDReply) {
	const bool laurion_layout = ClientVersion() == EQ::versions::ClientVersion::Laurion;

	auto outapp = new EQApplicationPacket(OP_GuildMOTD, laurion_layout
		? sizeof(Laurion::structs::GuildMOTD_Struct)
		: sizeof(GuildMOTD_Struct));

	// When the Client gets an OP_GuildMOTD, it compares the text to the version it has previously stored.
	// If the text in the OP_GuildMOTD packet is the same, it does nothing. If not the same, it displays
	// the new MOTD and then stores the new text.
	//
	// When the Client receives an OP_GetGuildMOTDReply, it displays the text in the packet.
	//
	// So OP_GuildMOTD should be sent on zone entry and when an Officer changes the MOTD, and OP_GetGuildMOTDReply
	// should be sent when the client issues the /getguildmotd command.
	//
	// Laurion collapses both onto one opcode (0x5a8d): ZonePacket__dispatchRecv has a single case for the
	// record and CGuild::SetGuildMotd (FUN_1400c4770) does the compare-and-store itself, so the opcode
	// choice below only matters to the older clients.
	//
	if(GetGuildMOTDReply)
		outapp->SetOpcode(OP_GetGuildMOTDReply);

	char motd_text[512]   = {0};
	char motd_author[64]  = {0};

	if(IsInAGuild()) {
		if(!guild_mgr.GetGuildMOTD(GuildID(), motd_text, motd_author)) {
			motd_author[0] = '\0';
			strcpy(motd_text, "ERROR GETTING MOTD!");
		}
	} else {
		//we have to send them an empty MOTD anywyas.
		motd_text[0] = '\0';	//just to be sure
		motd_author[0] = '\0';	//just to be sure

	}

	if (laurion_layout) {
		// 656 byte record: guid at +0, recipient name at +8, author at +0x48, text at +0x8C.
		// FUN_1400c4770 writes CGuild + 0x79 (GuildMOTD) from +0x8C and CGuild + 0x280 (MOTD_Author)
		// from +0x48, then refreshes MOTDViewer / MOTDAuthorLabel through FUN_1403ccde0.
		auto gu = (Laurion::structs::GuildMOTD_Struct*) outapp->pBuffer;
		gu->GuildGuid = (uint64) GuildIDForWire();
		strn0cpy(gu->Name, m_pp.name, sizeof(gu->Name));
		strn0cpy(gu->SetbyName, motd_author, sizeof(gu->SetbyName));
		gu->Gap088 = 0;
		strn0cpy(gu->Motd, motd_text, sizeof(gu->Motd));
		gu->Gap28C = 0;
	} else {
		GuildMOTD_Struct *motd = (GuildMOTD_Struct *) outapp->pBuffer;
		motd->unknown0 = 0;
		strn0cpy(motd->name, m_pp.name, 64);
		strn0cpy(motd->setby_name, motd_author, sizeof(motd->setby_name));
		motd->unknown132 = 0;
		strn0cpy(motd->motd, motd_text, sizeof(motd->motd));
	}

	LogGuilds("Sending OP_GuildMOTD of length [{}]", outapp->size);

	FastQueuePacket(&outapp);
}

void Client::SendGuildURL()
{
	if (ClientVersion() < EQ::versions::ClientVersion::SoF)
		return;

	if(IsInAGuild())
	{
		auto outapp =
		    new EQApplicationPacket(OP_GuildUpdate, sizeof(GuildUpdateURLAndChannel_Struct));

		GuildUpdateURLAndChannel_Struct *guuacs = (GuildUpdateURLAndChannel_Struct*) outapp->pBuffer;

		if(guild_mgr.GetGuildURL(GuildID(), guuacs->Text))
		{
			if (ClientVersion() == EQ::versions::ClientVersion::Laurion) {
				// Laurion reads the record payload at +88, not +80
				auto laurion_app = new EQApplicationPacket(OP_GuildUpdate, sizeof(Laurion::structs::GuildUpdate_Struct));
				auto gu = (Laurion::structs::GuildUpdate_Struct*) laurion_app->pBuffer;
				gu->Action = Laurion::structs::GuildUpdateActionURL;
				strn0cpy(gu->Payload.Text, guuacs->Text, sizeof(gu->Payload.Text));
				FastQueuePacket(&laurion_app);
			}
			else {
				guuacs->Action = GuildUpdateURL;
				FastQueuePacket(&outapp);
			}
		}
		else
			safe_delete(outapp);
	}
}

void Client::SendGuildChannel()
{
	if (ClientVersion() < EQ::versions::ClientVersion::SoF)
		return;

	if(IsInAGuild())
	{
		auto outapp =
		    new EQApplicationPacket(OP_GuildUpdate, sizeof(GuildUpdateURLAndChannel_Struct));

		GuildUpdateURLAndChannel_Struct *guuacs = (GuildUpdateURLAndChannel_Struct*) outapp->pBuffer;

		if(guild_mgr.GetGuildChannel(GuildID(), guuacs->Text))
		{
			if (ClientVersion() == EQ::versions::ClientVersion::Laurion) {
				// Laurion reads the record payload at +88, not +80
				auto laurion_app = new EQApplicationPacket(OP_GuildUpdate, sizeof(Laurion::structs::GuildUpdate_Struct));
				auto gu = (Laurion::structs::GuildUpdate_Struct*) laurion_app->pBuffer;
				gu->Action = Laurion::structs::GuildUpdateActionChannel;
				strn0cpy(gu->Payload.Text, guuacs->Text, sizeof(gu->Payload.Text));
				FastQueuePacket(&laurion_app);
			}
			else {
				guuacs->Action = GuildUpdateChannel;

				FastQueuePacket(&outapp);
			}
		}
		else
			safe_delete(outapp);
	}
}

void Client::SendGuildRanks()
{
	if (ClientVersion() < EQ::versions::ClientVersion::RoF)
		return;

	int permissions = GUILD_MAX_FUNCTIONS + 1; //Static number of permissions in all EQ clients as of May 2014
	int ranks = GUILD_RECRUIT + 1; // Static number of RoF+ ranks as of May 2014
	int j = 1;
	int i = 1;
	if(IsInAGuild())
	{
		while(j < ranks)
		{
			while(i < permissions)
			{
					if (ClientVersion() == EQ::versions::ClientVersion::Laurion) {
						// Laurion permission record: rank +88, function_id +92, value +96
						auto laurion_app = new EQApplicationPacket(OP_GuildUpdate,
							sizeof(Laurion::structs::GuildUpdate_Struct));
						auto gu = (Laurion::structs::GuildUpdate_Struct*) laurion_app->pBuffer;
						gu->Action = Laurion::structs::GuildUpdateActionPermission;
						gu->Payload.Permission.Rank = j;
						gu->Payload.Permission.FunctionID = i;
						gu->Payload.Permission.Value = guild_mgr.CheckPermission(GuildID(), j, (GuildAction)i);
						FastQueuePacket(&laurion_app);
						i++;
						continue;
					}

					auto outapp = new EQApplicationPacket(OP_GuildUpdate,
						sizeof(GuildUpdateRanks_Struct));
					GuildUpdateRanks_Struct* guuacs = (GuildUpdateRanks_Struct*)outapp->pBuffer;
					strncpy(guuacs->Unknown0012, GetCleanName(), 64);
					guuacs->Action = GuildUpdatePermissions;
					guuacs->RankID = j;
					guuacs->GuildID = GuildID();
					guuacs->PermissionID = i;
					guuacs->PermissionVal = guild_mgr.CheckPermission(GuildID(), j, (GuildAction)i);
					guuacs->Unknown0089[0] = 0x2c;
					guuacs->Unknown0089[1] = 0x01;
					guuacs->Unknown0089[2] = 0x00;
					FastQueuePacket(&outapp);
					i++;
			}
			j++;
			i = 1;
		}
	}
}

void Client::SendGuildRankNames()
{
	if (IsInAGuild() && (ClientVersion() >= EQ::versions::ClientVersion::RoF)) {
		auto guild = guild_mgr.GetGuildByGuildID(GuildID());
		if (!guild) {
			return;
		}

		for (int i = 1; i <= GUILD_MAX_RANK; i++) {

			auto                 outapp = new EQApplicationPacket(OP_GuildUpdate, sizeof(GuildUpdateUCPStruct));
			GuildUpdateUCPStruct* gucp = (GuildUpdateUCPStruct*)outapp->pBuffer;

			if (ClientVersion() == EQ::versions::ClientVersion::Laurion) {
				// Laurion rank name record: rank +88, name +92, action 3
				if (guild->rank_names[i].empty()) {
					safe_delete(outapp);
					continue;
				}

				auto laurion_app = new EQApplicationPacket(OP_GuildUpdate, sizeof(Laurion::structs::GuildUpdate_Struct));
				auto gu = (Laurion::structs::GuildUpdate_Struct*) laurion_app->pBuffer;
				gu->Action = Laurion::structs::GuildUpdateActionRankName;
				gu->Payload.RankName.Rank = i;
				strn0cpy(gu->Payload.RankName.RankName, guild->rank_names[i].c_str(), sizeof(gu->Payload.RankName.RankName));
				QueuePacket(laurion_app);
				safe_delete(laurion_app);
				safe_delete(outapp);
				continue;
			}

			gucp->payload.rank_name.rank = i;
			if (guild->rank_names[i].empty()) {
				continue;
			}

			strn0cpy(
				gucp->payload.rank_name.rank_name,
				guild->rank_names[i].c_str(),
				sizeof(gucp->payload.rank_name.rank_name)
			);
			gucp->action = GuildUpdateRanks;

			QueuePacket(outapp);
			safe_delete(outapp);
		}
	}
}

void Client::SendGuildSpawnAppearance() {
	if (!IsInAGuild()) {
		// clear guildtag
		SendAppearancePacket(AppearanceType::GuildID, GuildIDForWire());
		SendGuildSpawnGuildID(GetID(), GuildIDForWire());
		LogGuilds("Sending spawn appearance for no guild tag");
	} else {
		uint8  rank    = guild_mgr.GetDisplayedRank(GuildID(), GuildRank(), CharacterID());
		uint32 display = guild_mgr.CheckPermission(GuildID(), GuildRank(), GUILD_ACTION_DISPLAY_GUILD_NAME) ? 1 : 0;
		LogGuilds("Sending spawn appearance for guild [{}] at rank [{}]", GuildID(), rank);
		SendAppearancePacket(AppearanceType::GuildID, GuildID(), true);
		SendAppearancePacket(AppearanceType::GuildRank, rank, true);
		SendAppearancePacket(AppearanceType::GuildShow, display, true);
		SendGuildSpawnGuildID(GetID(), GuildIDForWire());
	}
	UpdateWho();
}

// One OP_GuildsList record in the shape the Laurion client parses: u64 guid at +0, NUL
// terminated name at +8. FUN_1400c1250 case 0x2d0e copies at most 0x3f name bytes and needs a
// payload larger than 7, so the name is capped at 63 here.
// EQEmu's guild update actions (0 URL, 1 channel, 4 ranks, 5 permissions) do not match
// the values this client switches on (0 URL, 1 channel, 3 rank name, 4 permission).
static uint32 LaurionGuildUpdateAction(uint32 eq_action)
{
	switch (eq_action) {
		case GuildUpdateURL:          return Laurion::structs::GuildUpdateActionURL;
		case GuildUpdateChannel:      return Laurion::structs::GuildUpdateActionChannel;
		case GuildUpdateRanks:        return Laurion::structs::GuildUpdateActionRankName;
		case GuildUpdatePermissions:  return Laurion::structs::GuildUpdateActionPermission;
		default:                      return eq_action;
	}
}

EQApplicationPacket* Client::MakeGuildUpdateRecord(uint32 action, uint32 rank, uint32 function_id, uint32 value)
{
	if (ClientVersion() == EQ::versions::ClientVersion::Laurion) {
		auto outapp = new EQApplicationPacket(OP_GuildUpdate, sizeof(Laurion::structs::GuildUpdate_Struct));
		auto gu = (Laurion::structs::GuildUpdate_Struct*) outapp->pBuffer;
		gu->Action = LaurionGuildUpdateAction(action);
		gu->Payload.Permission.Rank = rank;
		gu->Payload.Permission.FunctionID = function_id;
		gu->Payload.Permission.Value = value;
		return outapp;
	}

	auto outapp = new EQApplicationPacket(OP_GuildUpdate, sizeof(GuildPermission_Struct));
	auto gu = (GuildPermission_Struct*) outapp->pBuffer;
	gu->Action = action;
	gu->rank = rank;
	gu->function_id = function_id;
	gu->value = value;
	return outapp;
}

EQApplicationPacket* Client::MakeGuildUpdateString(uint32 action, const char *text)
{
	if (ClientVersion() == EQ::versions::ClientVersion::Laurion) {
		auto outapp = new EQApplicationPacket(OP_GuildUpdate, sizeof(Laurion::structs::GuildUpdate_Struct));
		auto gu = (Laurion::structs::GuildUpdate_Struct*) outapp->pBuffer;
		gu->Action = LaurionGuildUpdateAction(action);
		strn0cpy(gu->Payload.Text, text, sizeof(gu->Payload.Text));
		return outapp;
	}

	auto outapp = new EQApplicationPacket(OP_GuildUpdate, sizeof(GuildUpdateURLAndChannel_Struct));
	auto gu = (GuildUpdateURLAndChannel_Struct*) outapp->pBuffer;
	gu->Action = action;
	strn0cpy(gu->Text, text, sizeof(gu->Text));
	return outapp;
}

EQApplicationPacket* Client::MakeGuildUpdateRankName(uint32 rank, const char *rank_name)
{
	if (ClientVersion() == EQ::versions::ClientVersion::Laurion) {
		auto outapp = new EQApplicationPacket(OP_GuildUpdate, sizeof(Laurion::structs::GuildUpdate_Struct));
		auto gu = (Laurion::structs::GuildUpdate_Struct*) outapp->pBuffer;
		gu->Action = Laurion::structs::GuildUpdateActionRankName;
		gu->Payload.RankName.Rank = rank;
		strn0cpy(gu->Payload.RankName.RankName, rank_name, sizeof(gu->Payload.RankName.RankName));
		return outapp;
	}

	auto outapp = new EQApplicationPacket(OP_GuildUpdate, sizeof(GuildUpdateUCPStruct));
	auto gu = (GuildUpdateUCPStruct*) outapp->pBuffer;
	gu->action = GuildUpdateRanks;
	gu->payload.rank_name.rank = rank;
	strn0cpy(gu->payload.rank_name.rank_name, rank_name, sizeof(gu->payload.rank_name.rank_name));
	return outapp;
}

void Client::SendGuildNameRecord(uint64 guid, const char *name)
{
	if (!name || !*name) {
		return;
	}

	uint32 name_length = static_cast<uint32>(strlen(name));
	if (name_length > 63) {
		name_length = 63;
	}

	uint32 size = static_cast<uint32>(sizeof(guid) + name_length + 1);
	auto outapp = new EQApplicationPacket(OP_GuildsList, size);
	memcpy(outapp->pBuffer, &guid, sizeof(guid));
	memcpy(outapp->pBuffer + sizeof(guid), name, name_length);
	outapp->pBuffer[sizeof(guid) + name_length] = 0;

	FastQueuePacket(&outapp);
}

void Client::SendGuildInviteRecord(const char *invitee_name, const char *inviter_name, uint32 guild_id, uint32 rank)
{
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		auto outapp = new EQApplicationPacket(OP_GuildInvite, sizeof(GuildCommand_Struct));
		GuildCommand_Struct *gc = (GuildCommand_Struct *) outapp->pBuffer;
		strn0cpy(gc->othername, invitee_name, sizeof(gc->othername));
		strn0cpy(gc->myname, inviter_name, sizeof(gc->myname));
		gc->guildeqid = static_cast<uint16>(guild_id);
		gc->officer = rank;
		FastQueuePacket(&outapp);
		return;
	}

	// Laurion's receive handler FUN_1401fa510 (dispatched for 0x2d05 from
	// ZonePacket__dispatchRecv at 0x1401d99f4) reads the record as:
	//   +0x00 invitee name   - compared against the local player name, so it must be us
	//   +0x40 inviter name
	//   +0x80 guild as a full u64 EqGuid
	//   +0x88 rank           - must be 1..8 or the popup is not created
	// and it only shows the invite popup when the guild field matches ours or we are guildless.
	// Sending the narrow 136 byte struct makes the client read the rank from outside the record and
	// the guild as rank << 32 | guild_id, so FUN_1401fa510 skips the popup and sends response 9
	// (decline) straight away.
	auto outapp = new EQApplicationPacket(OP_GuildInvite, sizeof(Laurion::structs::GuildCommand_Struct));
	Laurion::structs::GuildCommand_Struct *gc = (Laurion::structs::GuildCommand_Struct *) outapp->pBuffer;
	strn0cpy(gc->OtherName, invitee_name, sizeof(gc->OtherName));
	strn0cpy(gc->MyName, inviter_name, sizeof(gc->MyName));
	gc->GuildEqID = guild_id;
	gc->Officer = rank;

	LogGuilds("Sending Laurion OP_GuildInvite record of length [{}] to [{}] (guild [{}], rank [{}])",
				outapp->size, invitee_name, guild_id, rank);

	FastQueuePacket(&outapp);
}

void Client::SendGuildList()
{
	auto guilds_list = guild_mgr.MakeGuildList();

	// Laurion reads one guid + name record per packet, not the cereal archive below.
	if (ClientVersion() == EQ::versions::ClientVersion::Laurion) {
		for (const auto &entry : guilds_list.guild_detail) {
			SendGuildNameRecord(entry.guild_id, entry.guild_name.c_str());
		}

		return;
	}

	std::stringstream           ss;
	cereal::BinaryOutputArchive ar(ss);
	{ ar(guilds_list); }

	uint32 packet_size = ss.str().length();

	std::unique_ptr<EQApplicationPacket> out(new EQApplicationPacket(OP_GuildsList, packet_size));
	memcpy(out->pBuffer, ss.str().data(), out->size);

	QueuePacket(out.get());
}


// FUN_1400c1250 case 0x5f77 does not read the legacy Internal_GuildMembers_Struct. It parses
// the payload as a serialized archive (FUN_1405646E0 reads a u32 length then that many bytes):
//   u32 field_length
//   bytes[field_length]        <- the guild name
//   u32 count
//   per member: u64 GUID, then a NUL terminated name
// Sending the legacy struct makes the client take the first 4 bytes of the guild name as the
// field length - for "Slackers" that is 0x63616C53 ("Slac") - and memcpy about 1.6 GB, which is
// the EXCEPTION_ACCESS_VIOLATION seen at login.
void Client::SendGuildMemberListPayload(const char *prefix_name) {
	if (ClientVersion() == EQ::versions::ClientVersion::Laurion) {
		std::vector<CharGuildInfo*> members;
		if (!guild_mgr.GetEntireGuild(GuildID(), members)) {
			return;
		}

		uint32 prefix_length = static_cast<uint32>(strlen(prefix_name));
		uint32 size = 4 + prefix_length + 4;
		for (auto* member : members) {
			size += 8 + static_cast<uint32>(member->char_name.length()) + 1;
		}

		auto outapp = new EQApplicationPacket(OP_GuildMemberDetails, size);
		uint8* p = outapp->pBuffer;

		// length prefixed field: the guild name
		*reinterpret_cast<uint32*>(p) = prefix_length;
		p += 4;
		memcpy(p, prefix_name, prefix_length);
		p += prefix_length;

		*reinterpret_cast<uint32*>(p) = static_cast<uint32>(members.size());
		p += 4;

		for (auto* member : members) {
			// EqGuid is u32 id + u32 padding (see the spawn appearance encode in laurion.cpp)
			*reinterpret_cast<uint32*>(p) = member->char_id;
			p += 4;
			*reinterpret_cast<uint32*>(p) = 0;
			p += 4;

			uint32 name_length = static_cast<uint32>(member->char_name.length());
			memcpy(p, member->char_name.c_str(), name_length);
			p[name_length] = 0;
			p += name_length + 1;
		}

		for (auto* member : members) {
			safe_delete(member);
		}

		LogGuilds("Sending Laurion OP_GuildMemberDetails name entry of length [{}]", outapp->size);
		FastQueuePacket(&outapp);
		return;
	}

	uint32 len;
	uint8 *data = guild_mgr.MakeGuildMembers(GuildID(), prefix_name, len);
	if(data == nullptr)
		return;	//invalid guild, shouldent happen.

	auto outapp = new EQApplicationPacket(OP_GuildMemberList);
	outapp->size = len;
	outapp->pBuffer = data;
	data = nullptr;

	LogGuilds("Sending OP_GuildMemberList of length [{}]", outapp->size);

	FastQueuePacket(&outapp);
}

// FUN_1400c1250 case 0x43f7 (which runs FUN_1400c2b00) is the record that actually builds the
// guild window: it sets CGuild::GuildGuid (__Guilds + 0x18), CGuild + 0x2D1 and allocates
// `count` GuildMember objects. The fields are consumed in this order, so the payload has to be
// written exactly like this - EQEmu's legacy cereal archive for OP_GuildMemberList does not match:
//   u32 field_length + bytes   <- guild name (parsed into a string, not stored)
//   u64 GuildGuid              <- raw, no length prefix
//   u8  flag                   <- raw
//   u32 count                  <- raw
//   per member: u32 field_length + bytes (name)
void Client::SendGuildRosterRecord() {
	std::vector<CharGuildInfo*> members;
	if (!guild_mgr.GetEntireGuild(GuildID(), members)) {
		return;
	}

	const char *guild_name = guild_mgr.GetGuildName(GuildID());
	uint32 name_length = static_cast<uint32>(strlen(guild_name));

	uint32 size = 4 + name_length + 8 + 1 + 4;
	for (auto* member : members) {
		// 4 byte length prefix + name + 5 u32 + 2 u8 + 2 u32 + 1 u8
		// 4 byte length prefix + name + 5 u32 + 2 u8 + 2 u32 + 1 u8
		// + 4 byte length prefix + public note + u32 + 2 u8
		size += 4 + static_cast<uint32>(member->char_name.length()) + 31;
		size += 4 + static_cast<uint32>(member->public_note.length()) + 6;
	}

	auto outapp = new EQApplicationPacket(OP_GuildMemberList, size);
	uint8* p = outapp->pBuffer;

	*reinterpret_cast<uint32*>(p) = name_length;
	p += 4;
	memcpy(p, guild_name, name_length);
	p += name_length;

	*reinterpret_cast<uint64*>(p) = GuildID();
	p += 8;
	*p = 0;
	p += 1;
	*reinterpret_cast<uint32*>(p) = static_cast<uint32>(members.size());
	p += 4;

	for (auto* member : members) {
		uint32 member_name_length = static_cast<uint32>(member->char_name.length());
		*reinterpret_cast<uint32*>(p) = member_name_length;
		p += 4;
		memcpy(p, member->char_name.c_str(), member_name_length);
		p += member_name_length;

		// Raw fields follow the length prefixed name. The client bounds-checks every read and
		// leaves the destination field at its default when the packet runs out, so the order
		// here is what the guild window actually sees.
		*reinterpret_cast<uint32*>(p) = member->level;        p += 4;  // GuildMember + 0x58
		*reinterpret_cast<uint32*>(p) = (member->banker ? 1 : 0) + (member->alt ? 2 : 0); p += 4; // + 0x5C
		*reinterpret_cast<uint32*>(p) = member->class_;       p += 4;  // + 0x60
		*reinterpret_cast<uint32*>(p) = member->rank;         p += 4;  // + 0x64
		*reinterpret_cast<uint32*>(p) = member->time_last_on; p += 4;  // + 0x188
		*p = 0; p += 1;                                                  // + 0x3A8
		*p = 0; p += 1;                                                  // + 0x3A9
		*reinterpret_cast<uint32*>(p) = member->total_tribute; p += 4;  // + 0x19C
		*reinterpret_cast<uint32*>(p) = member->last_tribute;  p += 4;  // + 0x1A0
		*p = 1; p += 1;                                                  // + 0x14 bMainProfile

		// The public note is a length prefixed field as well. If it is missing the client reads the
		// next member's name as this member's note, and the following member runs off the end of the
		// packet - that is the blank roster row with level 0, no rank and unknown zone.
		uint32 note_length = static_cast<uint32>(member->public_note.length());
		*reinterpret_cast<uint32*>(p) = note_length;
		p += 4;
		memcpy(p, member->public_note.c_str(), note_length);
		p += note_length;

		// GuildMember + 0x0C. A nonzero value also sets + 0x08 bOnline, which decides whether the
		// window draws the row. FUN_1403cf550 renders the zone column by passing this value to
		// FUN_140649710(pinstWorldData, id, out, 1), which indexes the client zone table at
		// WorldData + 0x30 + (id & 0x7fff) * 8 - so this must be the client zone id (zoneidnumber),
		// not a hardcoded instance number. Sending 1 made every online member show as South Qeynos.
		*reinterpret_cast<uint32*>(p) = member->online ? member->zone_id : 0; p += 4;  // + 0x0C
		*p = member->tribute_enable ? 1 : 0; p += 1;                     // + 0x198
		*p = 0; p += 1;                                                  // + 0x15 bOfflineMode
	}

	LogGuilds("Sending Laurion OP_GuildMemberList roster of length [{}]", outapp->size);
	FastQueuePacket(&outapp);

	for (auto* member : members) {
		safe_delete(member);
	}
}

// The client only refreshes PcClient::GuildID (pinstLocalPC + 0x1730) from the profile packet -
// FUN_1402c01a0 copies the u64 guild field into it, and nothing else writes it. The guild window
// and every /guild command check that field, so after a guild join or leave the profile has to be
// resent or the client still thinks the player is guildless and shows the create guild prompt.
void Client::SendPlayerProfile() {
	auto outapp = new EQApplicationPacket(OP_PlayerProfile, sizeof(PlayerProfile_Struct));

	m_pp.entityid = GetID();
	memcpy(outapp->pBuffer, &m_pp, outapp->size);
	outapp->priority = 6;
	FastQueuePacket(&outapp);
}

// 0x408f is the only record that writes PlayerClient::GuildID (pinstLocalPlayer + 0x208) for a
// client that is already in zone. GuildWindow (FUN_1403c9730) returns early when that field is 0,
// so a player who joins mid-session keeps showing the create guild panel even though
// pinstLocalPC + 0x1730 is correct. Passing spawn_id 0 means "the local player".
void Client::SendGuildSpawnGuildID(uint32 spawn_id, uint64 guild_guid) {
	if (ClientVersion() != EQ::versions::ClientVersion::Laurion) {
		return;
	}

	auto outapp = new EQApplicationPacket(OP_GuildSpawnGuildID, sizeof(Laurion::structs::GuildSpawnGuildID_Struct));
	auto out = (Laurion::structs::GuildSpawnGuildID_Struct*) outapp->pBuffer;

	out->SpawnID   = spawn_id;
	out->GuildGuid = guild_guid;

	FastQueuePacket(&outapp);
}

void Client::SendGuildMembers() {
	SendGuildMemberListPayload(GetName());

	auto pack =
	    new ServerPacket(ServerOP_RequestOnlineGuildMembers, sizeof(ServerRequestOnlineGuildMembers_Struct));

	ServerRequestOnlineGuildMembers_Struct *srogms = (ServerRequestOnlineGuildMembers_Struct*)pack->pBuffer;

	srogms->FromID = CharacterID();
	srogms->GuildID = GuildID();

	worldserver.SendPacket(pack);

	safe_delete(pack);

	// We need to send the Guild URL and Channel name again, as sending OP_GuildMemberList appears to clear this information out.
	SendGuildURL();
	SendGuildChannel();
}

void Client::RefreshGuildInfo()
{
	uint32 OldGuildID = guild_id;

	guildrank = GUILD_RANK_NONE;
	guild_id = GUILD_NONE;

	bool WasBanker = GuildBanker;

	CharGuildInfo info;
	if(!guild_mgr.GetCharInfo(CharacterID(), info)) {
		LogGuilds("Unable to obtain guild char info for [{}] ([{}])", GetName(), CharacterID());
		return;
	}

	guildrank = info.rank;
	guild_id = info.guild_id;
	GuildBanker = info.banker ||
		guild_mgr.IsGuildLeader(GuildID(), CharacterID()) ||
		guild_mgr.GetBankerFlag(CharacterID()) ||
		ClientVersion() >= EQ::versions::ClientVersion::RoF ? true : false;

	if(zone->GetZoneID() == Zones::GUILDHALL)
	{
		if(WasBanker != GuildBanker)
		{
			auto outapp = new EQApplicationPacket(OP_SetGuildRank, sizeof(GuildSetRank_Struct));

			GuildSetRank_Struct *gsrs = (GuildSetRank_Struct*)outapp->pBuffer;

			gsrs->rank = guildrank;
			strn0cpy(gsrs->member_name, GetName(), sizeof(gsrs->member_name));
			gsrs->banker = GuildBanker;

			FastQueuePacket(&outapp);
		}

		if((guild_id != OldGuildID) && GuildBanks)
		{
			// Unsure about this for RoF+ ... But they don't have that action anymore so fuck it
			if (ClientVersion() < EQ::versions::ClientVersion::RoF)
				ClearGuildBank();

			if (guild_id != GUILD_NONE)
				GuildBanks->SendGuildBank(this);
		}
	}

	SendGuildSpawnAppearance();
}

void EntityList::SendGuildMOTD(uint32 guild_id)
{
	if (guild_id == GUILD_NONE) {
		return;
	}

	for (auto const &c: client_list) {
		if (c.second->GuildID() == guild_id) {
			c.second->SendGuildMOTD();
		}
	}
}

void EntityList::SendGuildChannel(uint32 guild_id)
{
	if (guild_id == GUILD_NONE) {
		return;
	}

	for (auto const &c: client_list) {
		if (c.second->GuildID() == guild_id) {
			c.second->SendGuildChannel();
		}
	}
}

void EntityList::SendGuildURL(uint32 guild_id)
{
	if (guild_id == GUILD_NONE) {
		return;
	}

	for (auto const &c: client_list) {
		if (c.second->GuildID() == guild_id) {
			c.second->SendGuildURL();
		}
	}
}

void EntityList::SendGuildSpawnAppearance(uint32 guild_id)
{
	if (guild_id == GUILD_NONE) {
		return;
	}
	auto it = client_list.begin();
	while (it != client_list.end()) {
		Client *client = it->second;
		if (client->GuildID() == guild_id) {
			client->SendGuildSpawnAppearance();
		}
		++it;
	}
}

void EntityList::RefreshAllGuildInfo(uint32 guild_id)
{
	if (guild_id == GUILD_NONE) {
		return;
	}
	auto it = client_list.begin();
	while (it != client_list.end()) {
		Client *client = it->second;
		if (client->GuildID() == guild_id) {
			client->RefreshGuildInfo();
		}
		++it;
	}
}

void EntityList::SendGuildMembers(uint32 guild_id)
{
	if (guild_id == GUILD_NONE) {
		return;
	}

	//this could be optimized a bit to only build the member's packet once
	//and then keep swapping out the name in the packet on each send.

	auto it = client_list.begin();
	while (it != client_list.end()) {
		Client *client = it->second;
		if (client->GuildID() == guild_id) {
			//client->SendGuildMembers();
		}
		++it;
	}
}

void EntityList::SendGuildList()
{
	for (auto const& c : client_list) {
		c.second->SendGuildList();
	}
}

void Client::SendGuildJoin(GuildJoin_Struct *gj)
{
	auto outapp = new EQApplicationPacket(OP_GuildManageAdd, sizeof(GuildJoin_Struct));
	auto outgj  = (GuildJoin_Struct *) outapp->pBuffer;
	outgj->class_   = gj->class_;
	outgj->guild_id = gj->guild_id;
	outgj->level    = gj->level;
	outgj->rank     = gj->rank;
	outgj->zoneid   = gj->zoneid;
	strcpy(outgj->name, gj->name);

	LogGuilds("Sending OP_GuildManageAdd for join of length [{}]", outapp->size);

	FastQueuePacket(&outapp);
}

void EntityList::GuildSetPreRoFBankerFlag(uint32 guild_id, uint32 guild_rank, bool bank_status)
{
	auto guild_members = [&]() -> std::vector<Client *> {
		std::vector<Client *> members = {};
		for (auto const       &c: entity_list.GetClientList()) {
			if (c.second->GuildID() == guild_id && c.second->GuildRank() == guild_rank) {
				members.push_back(c.second);
			}
		}
		return members;
	};

	for (auto const &c: guild_members()) {
		auto outapp = new ServerPacket(
			ServerOP_GuildRankUpdate,
			sizeof(ServerGuildRankUpdate_Struct)
		);

		guild_mgr.UpdateDbBankerFlag(c->CharacterID(), bank_status);
		c->SetGuildBanker(bank_status);
		CharGuildInfo cgi;
		guild_mgr.GetCharInfo(c->CharacterID(), cgi);

		auto *sgrus = (ServerGuildRankUpdate_Struct *) outapp->pBuffer;
		sgrus->guild_id  = guild_id;
		sgrus->rank      = guild_rank;
		sgrus->banker    = (bank_status ? 1 : 0) + (cgi.alt ? 2 : 0);
		sgrus->no_update = true;
		strn0cpy(sgrus->member_name, c->GetCleanName(), sizeof(sgrus->member_name));

		worldserver.SendPacket(outapp);
		safe_delete(outapp);
	}
}

void Client::SetGuildRank(uint32 rank)
{
	guildrank = rank;
}

void Client::SetGuildTributeOptIn(bool state)
{
	guild_tribute_opt_in = state;
}

void Client::SetGuildID(uint32 g_id)
{
	guild_id = g_id;
}

void EntityList::UpdateGuildTributes(uint32 guild_id)
{
	for (auto const &c: entity_list.GetClientList()) {
		if (c.second->GuildID() == guild_id) {
			c.second->DoGuildTributeUpdate();
		}
	}
}

/*
void EntityList::SendGuildJoin(GuildJoin_Struct* gj){
	LinkedListIterator<Client*> iterator(client_list);

	iterator.Reset();
	while(iterator.MoreElements())
	{
		Client* client = iterator.GetData()->CastToClient();
		if (client->IsInGuild(gj->guild_id))
			client->SendGuildJoin(gj);
		iterator.Advance();
	}
}*/



/*bool Client::SetGuild(uint32 in_guild_id, uint8 in_rank) {
	if (in_guild_id == 0) {
		// update DB
		if (!guild_mgr.SetGuild(character_id, 0, GUILD_MEMBER))
			return false;
		// clear guildtag
		guild_id = GUILD_NONE;
		SendAppearancePacket(AppearanceType::GuildID, GUILD_NONE);
		SendAppearancePacket(AppearanceType::GuildRank, GUILD_RANK_NONE);
		UpdateWho();
		return true;
	} else {
		if (!guild_mgr.SetGuild(character_id, in_guild_id, in_rank))
			return false;
		guildrank = in_rank;
		if (guild_id != in_guild_id) {
			guild_id = in_guild_id;
			SendAppearancePacket(AppearanceType::GuildID, in_guild_id);
		}
		SendAppearancePacket(AppearanceType::GuildRank, in_rank);
		UpdateWho();
		return true;
	}
	UpdateWho();
	return false;
}*/

/*
void Client::GuildChangeRank(uint32 guild_id, uint32 oldrank, uint32 newrank){
	GuildChangeRank(GetName(), guild_id, oldrank, newrank);
}

void Client::GuildChangeRank(const char* name, uint32 guild_id, uint32 oldrank, uint32 newrank) {
	EQApplicationPacket* outapp = new EQApplicationPacket(OP_GuildManageStatus, sizeof(GuildManageStatus_Struct));
	GuildManageStatus_Struct* gms = (GuildManageStatus_Struct*) outapp->pBuffer;
	gms->guild_id = guild_id;
	strcpy(gms->name, name);
	gms->newrank = newrank;
	gms->oldrank = oldrank;
	entity_list.QueueClientsGuild(this, outapp, false, guild_id);
	safe_delete(outapp);
	SendGuildMembers(guild_id, true);
}*/

void Client::SendGuildMembersList()
{
	auto guild_name = guild_mgr.GetGuildName(GuildID());

	// Laurion needs both: case 0x43f7 builds the CGuild object and its member list (what the
	// guild window reads), case 0x5f77 fills the guild name table used for member tags.
	if (ClientVersion() == EQ::versions::ClientVersion::Laurion) {
		SendGuildRosterRecord();
	}

	SendGuildMemberListPayload(guild_name);

	SendGuildMOTD();
	SendGuildChannel();
	SendGuildURL();

	SetGuildListDirty(false);
}

void Client::SendGuildMemberAdd(
	uint32 guild_id,
	uint32 level,
	uint32 class_,
	uint32 rank_,
	uint32 guild_show,
	uint32 zone_id,
	std::string player_name
)
{
	if (GetGuildListDirty()) {
		SendGuildMembersList();
		return;
	}


	if (ClientVersion() == EQ::versions::ClientVersion::Laurion) {
		auto laurion_app = new EQApplicationPacket(OP_GuildMemberAdd, sizeof(Laurion::structs::GuildMemberAdd_Struct));
		auto lou = (Laurion::structs::GuildMemberAdd_Struct*) laurion_app->pBuffer;

		lou->Guid      = guild_id;
		lou->Level     = level;
		lou->Class     = class_;
		lou->Rank      = rank_;
		lou->GuildShow = guild_show;
		lou->ZoneID    = zone_id;
		lou->LastOn    = time(nullptr);
		strn0cpy(lou->Name, player_name.c_str(), sizeof(lou->Name));

		QueuePacket(laurion_app);
		safe_delete(laurion_app);
		return;
	}
	auto outapp = new EQApplicationPacket(OP_GuildMemberAdd, sizeof(GuildMemberAdd_Struct));
	auto out    = (GuildMemberAdd_Struct *) outapp->pBuffer;

	out->guild_id   = guild_id;
	out->last_on    = time(nullptr);
	out->level      = level;
	out->zone_id    = zone_id;
	out->rank_      = rank_;
	out->guild_show = guild_show;
	out->class_     = class_;
	strn0cpy(out->player_name, player_name.c_str(), sizeof(out->player_name));

	QueuePacket(outapp);
	safe_delete(outapp);
}

void Client::SendGuildMemberRename(uint32 guild_id, std::string player_name, std::string new_player_name)
{
	auto outapp = new EQApplicationPacket(OP_GuildMemberRename, sizeof(GuildRenameMember_Struct));
	auto *out = (GuildRenameMember_Struct *) outapp->pBuffer;

	out->guild_id = guild_id;
	strn0cpy(out->player_name, player_name.c_str(), sizeof(out->player_name));
	strn0cpy(out->new_player_name, new_player_name.c_str(), sizeof(out->new_player_name));

	QueuePacket(outapp);
	safe_delete(outapp);

}

void Client::SendGuildMemberDelete(uint32 guild_id, std::string player_name)
{
	auto outapp = new EQApplicationPacket(OP_GuildMemberDelete, sizeof(GuildMemberDelete_Struct));
	auto out    = (GuildMemberDelete_Struct *) outapp->pBuffer;

	out->guild_id = guild_id;
	strn0cpy(out->player_name, player_name.c_str(), sizeof(out->player_name));

	QueuePacket(outapp);
	safe_delete(outapp);

	SetGuildListDirty(true);

}

void Client::SendGuildMemberLevel(uint32 guild_id, uint32 level, std::string player_name)
{
	auto outapp = new EQApplicationPacket(OP_GuildMemberLevel, sizeof(GuildMemberLevel_Struct));
	auto out    = (GuildMemberLevel_Struct *) outapp->pBuffer;

	out->guild_id = guild_id;
	out->level    = level;
	strn0cpy(out->player_name, player_name.c_str(), sizeof(out->player_name));

	QueuePacket(outapp);
	safe_delete(outapp);
}

void Client::SendGuildMemberRankAltBanker(uint32 guild_id, uint32 rank, std::string player_name, bool alt, bool banker)
{
	auto outapp = new EQApplicationPacket(OP_GuildMemberRankAltBanker, sizeof(GuildMemberRank_Struct));
	auto out    = (GuildMemberRank_Struct *) outapp->pBuffer;

	out->guild_id = guild_id;
	out->rank_    = rank;
	out->alt_banker = (alt ? 2 : 0) | (banker ? 1 : 0);
	strn0cpy(out->player_name, player_name.c_str(), sizeof(out->player_name));

	QueuePacket(outapp);
	safe_delete(outapp);
}

void Client::SendGuildMemberPublicNote(uint32 guild_id, std::string player_name, std::string public_note)
{
	auto outapp = new EQApplicationPacket(OP_GuildMemberPublicNote, sizeof(GuildMemberPublicNote_Struct));
	auto out    = (GuildMemberPublicNote_Struct *) outapp->pBuffer;

	out->guild_id = guild_id;
	strn0cpy(out->player_name, player_name.c_str(), sizeof(out->player_name));
	strn0cpy(out->public_note, public_note.c_str(), sizeof(out->public_note));

	QueuePacket(outapp);
	safe_delete(outapp);
}

void Client::SendGuildMemberDetails(uint32 guild_id, uint32 zone_id, uint32 offline_mode, std::string player_name)
{
	auto outapp = new EQApplicationPacket(OP_GuildMemberDetails, sizeof(GuildMemberDetails_Struct));
	auto out    = (GuildMemberDetails_Struct *) outapp->pBuffer;

	out->guild_id     = guild_id;
	out->zone_id      = zone_id;
	out->last_on      = time(nullptr);
	out->offline_mode = offline_mode;
	strn0cpy(out->player_name, player_name.c_str(), sizeof(out->player_name));

	QueuePacket(outapp);
	safe_delete(outapp);
}

void Client::SendGuildRenameGuild(uint32 guild_id, std::string new_guild_name)
{
	auto outapp = new EQApplicationPacket(OP_GuildRenameGuild, sizeof(GuildRenameGuild_Struct));
	auto out    = (GuildRenameGuild_Struct *) outapp->pBuffer;

	out->guild_id = guild_id;
	strn0cpy(out->new_guild_name, new_guild_name.c_str(), sizeof(out->new_guild_name));

	QueuePacket(outapp);
	safe_delete(outapp);
}

void EntityList::SendGuildMembersList(uint32 guild_id)
{
	for (auto const& c : client_list) {
		if (c.second->GuildID() == guild_id) {
			c.second->SendGuildMembersList();
			c.second->SetGuildListDirty(false);
		}
	}
}

void EntityList::SendGuildMemberAdd(
	uint32 guild_id,
	uint32 level,
	uint32 class_,
	uint32 rank_,
	uint32 spirit,
	uint32 zone_id,
	std::string player_name
)
{
	uint32 joiner_spawn_id = 0;
	if (auto joiner = GetClientByName(player_name.c_str())) {
		joiner_spawn_id = joiner->GetID();
	}

	for (auto &c: client_list) {
		if (c.second->GuildID() == guild_id) {
			c.second->SendGuildMemberAdd(guild_id, level, class_, rank_, 0, zone_id, player_name);
			c.second->SendGuildSpawnGuildID(joiner_spawn_id, guild_id);
		}

		if (player_name.compare(c.second->GetCleanName()) == 0) {
			c.second->SetGuildID(guild_id);
			c.second->SetGuildRank(rank_);
			c.second->SendGuildList();
			c.second->SetGuildTributeOptIn(false);
			c.second->SendGuildMembersList();
			if (c.second->ClientVersion() >= EQ::versions::ClientVersion::RoF) {
				c.second->SendGuildRanks();
				c.second->SendGuildRankNames();
			}

			if (zone->GetZoneID() == Zones::GUILDHALL && GuildBanks) {
				GuildBanks->SendGuildBank(c.second);
			}

			c.second->SendGuildActiveTributes(guild_id);
			c.second->SendGuildSpawnGuildID(c.second->GetID(), guild_id);
			c.second->SendAppearancePacket(AppearanceType::GuildID, guild_id, true, false, c.second);
			c.second->SendAppearancePacket(AppearanceType::GuildRank, rank_, true, false, c.second);
			c.second->SendAppearancePacket(AppearanceType::GuildShow, guild_mgr.CheckPermission(guild_id, rank_, GUILD_ACTION_DISPLAY_GUILD_NAME) ? 1 : 0);
			c.second->DoGuildTributeUpdate();
		}
	}
}

void EntityList::SendGuildMemberRename(uint32 guild_id, std::string player_name, std::string new_player_name)
{
	for (auto const &c: client_list) {
		if (c.second->GuildID() == guild_id) {
			auto outapp = new EQApplicationPacket(OP_GuildMemberRename, sizeof(GuildRenameMember_Struct));
			auto out    = (GuildRenameMember_Struct *) outapp->pBuffer;

			out->guild_id = guild_id;
			strn0cpy(out->player_name, player_name.c_str(), sizeof(out->player_name));
			strn0cpy(out->new_player_name, new_player_name.c_str(), sizeof(out->new_player_name));

			c.second->QueuePacket(outapp);
			safe_delete(outapp);
		}
	}
}

void Client::SendGuildMemberDeleteRecord(uint32 guild_id, const char *name) {
	if (ClientVersion() == EQ::versions::ClientVersion::Laurion) {
		auto laurion_app = new EQApplicationPacket(OP_GuildMemberDelete, sizeof(Laurion::structs::GuildMemberDelete_Struct));
		auto out = (Laurion::structs::GuildMemberDelete_Struct*) laurion_app->pBuffer;

		out->GuildGuid = guild_id;
		strn0cpy(out->MemberName, name, sizeof(out->MemberName));

		FastQueuePacket(&laurion_app);
		return;
	}

	auto outapp = new EQApplicationPacket(OP_GuildMemberDelete, sizeof(GuildMemberDelete_Struct));
	auto out = (GuildMemberDelete_Struct*) outapp->pBuffer;

	out->guild_id = guild_id;
	strn0cpy(out->player_name, name, sizeof(out->player_name));

	FastQueuePacket(&outapp);
}

void EntityList::SendGuildMemberRemove(uint32 guild_id, std::string player_name)
{
	uint32 removed_spawn_id = 0;
	if (auto removed = GetClientByName(player_name.c_str())) {
		removed_spawn_id = removed->GetID();
	}

	for (auto &c: client_list) {
		if (c.second->GuildID() == guild_id) {
			c.second->SendGuildMemberDeleteRecord(guild_id, player_name.c_str());

			c.second->SetGuildListDirty(true);

			if (player_name.compare(c.second->GetCleanName()) == 0) {
				c.second->SetGuildID(GUILD_NONE);
				c.second->SetGuildRank(GUILD_RANK_NONE);
				c.second->SetGuildTributeOptIn(false);
				c.second->SendGuildActiveTributes(0);
				c.second->SetGuildListDirty(false);
				c.second->SendGuildList();
				// pinstLocalPC + 0x1730 is only written by the profile copy, so the removed player
				// still shows as guilded until the profile is resent. SendPlayerProfile() copies m_pp,
				// so clear the profile fields too, not just the Client members.
				c.second->GetPP().guild_id = GUILD_NONE;
				c.second->GetPP().guildrank = GUILD_RANK_NONE;
				c.second->SendPlayerProfile();
				c.second->SendGuildSpawnGuildID(c.second->GetID(), 0);
				c.second->SendAppearancePacket(AppearanceType::GuildID, GUILD_NONE, true);
				c.second->SendAppearancePacket(AppearanceType::GuildRank, GUILD_RANK_NONE, true);
			} else if (removed_spawn_id != 0) {
				// Other members resolve the removed player through pinstSpawnManager, so the record
				// must carry that player's spawn id. Sending 0 here would clear the receiver's own
				// guild instead.
				c.second->SendGuildSpawnGuildID(removed_spawn_id, 0);
			}
		}
	}
}

void EntityList::SendGuildMemberLevel(uint32 guild_id, uint32 level, std::string player_name)
{
	for (auto const &c: client_list) {
		if (c.second->GuildID() == guild_id) {
			auto outapp = new EQApplicationPacket(OP_GuildMemberLevel, sizeof(GuildMemberLevel_Struct));
			GuildMemberLevel_Struct *out = (GuildMemberLevel_Struct *) outapp->pBuffer;

			out->guild_id = guild_id;
			out->level    = level;
			strn0cpy(out->player_name, player_name.c_str(), sizeof(out->player_name));

			c.second->QueuePacket(outapp);
			safe_delete(outapp);
		}
	}
}

void Client::SendGuildManageStatusRecord(uint32 guild_id, uint32 rank, const char *name, uint32 flags) {
	if (ClientVersion() == EQ::versions::ClientVersion::Laurion) {
		auto laurion_app = new EQApplicationPacket(OP_GuildManageStatus, sizeof(Laurion::structs::GuildManageStatus_Struct));
		auto out = (Laurion::structs::GuildManageStatus_Struct*) laurion_app->pBuffer;

		out->GuildGuid     = guild_id;
		out->Rank          = rank;
		strn0cpy(out->Name, name, sizeof(out->Name));
		out->Flags         = flags;
		out->TributeStatus = 0;
		out->Field51       = 0;

		FastQueuePacket(&laurion_app);
		return;
	}

	auto outapp = new EQApplicationPacket(OP_GuildManageStatus, sizeof(GuildManageStatus_Struct));
	auto out = (GuildManageStatus_Struct*) outapp->pBuffer;

	out->guild_id = guild_id;
	out->oldrank  = 0;
	out->newrank  = rank;
	strn0cpy(out->name, name, sizeof(out->name));

	FastQueuePacket(&outapp);
}

void EntityList::SendGuildMemberRankAltBanker(uint32 guild_id, uint32 rank_, std::string player_name, bool alt, bool banker)
{
	for (auto const &c: client_list) {
		if (c.second->GuildID() == guild_id) {
			// Case 0x1212 is the record the client actually parses for a rank change: it updates
			// member Rank, Flags and the window. OP_GuildMemberRankAltBanker (0x0afa) has no case in
			// FUN_1400c1250, so it is kept only for the older clients.
			c.second->SendGuildManageStatusRecord(guild_id, rank_, player_name.c_str(), (alt ? 2 : 0) | (banker ? 1 : 0));

			auto outapp = new EQApplicationPacket(OP_GuildMemberRankAltBanker, sizeof(GuildMemberRank_Struct));
			auto out = (GuildMemberRank_Struct *) outapp->pBuffer;

			out->guild_id   = guild_id;
			out->rank_      = rank_;
			out->alt_banker = (alt ? 2 : 0) | (banker ? 1 : 0);
			strn0cpy(out->player_name, player_name.c_str(), sizeof(out->player_name));

			c.second->QueuePacket(outapp);
			safe_delete(outapp);
		}

		if (player_name.compare(c.second->GetName()) == 0) {
			c.second->SetGuildRank(rank_);
			c.second->SendAppearancePacket(AppearanceType::GuildRank, rank_, false);
			c.second->SendAppearancePacket(AppearanceType::GuildShow,
				guild_mgr.CheckPermission(c.second->GuildID(), c.second->GuildRank(), GUILD_ACTION_DISPLAY_GUILD_NAME) ? 1 : 0,
				true);
		}
	}
}

void EntityList::SendGuildMemberPublicNote(uint32 guild_id, std::string player_name, std::string public_note)
{
	for (auto const &c: client_list) {
		if (c.second->GuildID() == guild_id) {
			auto outapp = new EQApplicationPacket(OP_GuildMemberPublicNote, sizeof(GuildMemberPublicNote_Struct));
			auto out = (GuildMemberPublicNote_Struct *) outapp->pBuffer;

			out->guild_id = guild_id;
			strn0cpy(out->player_name, player_name.c_str(), sizeof(out->player_name));
			strn0cpy(out->public_note, public_note.c_str(), sizeof(out->public_note));

			c.second->QueuePacket(outapp);
			safe_delete(outapp);
		}
	}
}

void EntityList::SendGuildMemberDetails(uint32 guild_id, uint32 zone_id, uint32 offline_mode, std::string player_name)
{
	for (auto const &c: client_list) {
		if (c.second->GuildID() == guild_id) {
			auto outapp = new EQApplicationPacket(OP_GuildMemberDetails, sizeof(GuildMemberDetails_Struct));
			auto out    = (GuildMemberDetails_Struct *) outapp->pBuffer;

			out->guild_id     = guild_id;
			out->zone_id      = zone_id;
			out->last_on      = time(nullptr);
			out->offline_mode = offline_mode;
			strn0cpy(out->player_name, player_name.c_str(), sizeof(out->player_name));

			c.second->QueuePacket(outapp);
			safe_delete(outapp);
		}
	}
}

void EntityList::SendGuildRenameGuild(uint32 guild_id, std::string new_guild_name)
{
	for (auto const &c: client_list) {
		if (c.second->GuildID() == guild_id) {
			auto outapp = new EQApplicationPacket(OP_GuildRenameGuild, sizeof(GuildRenameGuild_Struct));
			auto out    = (GuildRenameGuild_Struct *) outapp->pBuffer;

			out->guild_id = guild_id;
			strn0cpy(out->new_guild_name, new_guild_name.c_str(), sizeof(out->new_guild_name));

			c.second->QueuePacket(outapp);
			safe_delete(outapp);
		}
	}
}

void Client::SendGuildDeletePacket(uint32 guild_id)
{
	auto outapp = new EQApplicationPacket(OP_GuildDeleteGuild, sizeof(GuildDelete_Struct));
	auto data = (GuildDelete_Struct*)outapp->pBuffer;

	data->guild_id = guild_id;
	FastQueuePacket(&outapp);
}
