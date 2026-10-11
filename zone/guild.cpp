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
	auto outapp = new EQApplicationPacket(OP_GuildMOTD, sizeof(GuildMOTD_Struct));

	// When the Client gets an OP_GuildMOTD, it compares the text to the version it has previously stored.
	// If the text in the OP_GuildMOTD packet is the same, it does nothing. If not the same, it displays
	// the new MOTD and then stores the new text.
	//
	// When the Client receives an OP_GetGuildMOTDReply, it displays the text in the packet.
	//
	// So OP_GuildMOTD should be sent on zone entry and when an Officer changes the MOTD, and OP_GetGuildMOTDReply
	// should be sent when the client issues the /getguildmotd command.
	//
	if(GetGuildMOTDReply)
		outapp->SetOpcode(OP_GetGuildMOTDReply);

	GuildMOTD_Struct *motd = (GuildMOTD_Struct *) outapp->pBuffer;
	motd->unknown0 = 0;
	strn0cpy(motd->name, m_pp.name, 64);

	if(IsInAGuild()) {
		if(!guild_mgr.GetGuildMOTD(GuildID(), motd->motd, motd->setby_name)) {
			motd->setby_name[0] = '\0';
			strcpy(motd->motd, "ERROR GETTING MOTD!");
		}
	} else {
		//we have to send them an empty MOTD anywyas.
		motd->motd[0] = '\0';	//just to be sure
		motd->setby_name[0] = '\0';	//just to be sure

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
		LogGuilds("Sending spawn appearance for no guild tag");
	} else {
		uint8  rank    = guild_mgr.GetDisplayedRank(GuildID(), GuildRank(), CharacterID());
		uint32 display = guild_mgr.CheckPermission(GuildID(), GuildRank(), GUILD_ACTION_DISPLAY_GUILD_NAME) ? 1 : 0;
		LogGuilds("Sending spawn appearance for guild [{}] at rank [{}]", GuildID(), rank);
		SendAppearancePacket(AppearanceType::GuildID, GuildID(), true);
		SendAppearancePacket(AppearanceType::GuildRank, rank, true);
		SendAppearancePacket(AppearanceType::GuildShow, display, true);
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
		size += 4 + static_cast<uint32>(member->char_name.length());
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
	}

	for (auto* member : members) {
		safe_delete(member);
	}

	LogGuilds("Sending Laurion OP_GuildMemberList roster of length [{}]", outapp->size);
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
	for (auto &c: client_list) {
		if (c.second->GuildID() == guild_id) {
			c.second->SendGuildMemberAdd(guild_id, level, class_, rank_, 0, zone_id, player_name);
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

void EntityList::SendGuildMemberRemove(uint32 guild_id, std::string player_name)
{
	for (auto &c: client_list) {
		if (c.second->GuildID() == guild_id) {
			auto outapp = new EQApplicationPacket(OP_GuildMemberDelete, sizeof(GuildMemberDelete_Struct));
			auto out    = (GuildMemberDelete_Struct *) outapp->pBuffer;

			out->guild_id = guild_id;
			strn0cpy(out->player_name, player_name.c_str(), sizeof(out->player_name));

			c.second->QueuePacket(outapp);
			safe_delete(outapp);

			c.second->SetGuildListDirty(true);

			if (player_name.compare(c.second->GetCleanName()) == 0) {
				c.second->SetGuildID(GUILD_NONE);
				c.second->SetGuildRank(GUILD_RANK_NONE);
				c.second->SetGuildTributeOptIn(false);
				c.second->SendGuildActiveTributes(0);
				c.second->SetGuildListDirty(false);
				c.second->SendGuildList();
				c.second->SendAppearancePacket(AppearanceType::GuildID, GUILD_NONE, true);
				c.second->SendAppearancePacket(AppearanceType::GuildRank, GUILD_RANK_NONE, true);
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

void EntityList::SendGuildMemberRankAltBanker(uint32 guild_id, uint32 rank_, std::string player_name, bool alt, bool banker)
{
	for (auto const &c: client_list) {
		if (c.second->GuildID() == guild_id) {
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
