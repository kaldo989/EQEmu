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
#include "bodytypes.h"

#include <algorithm>

// body type -> extra client property IDs that body type also satisfies.
//
// Every entry here is justified by a gate the client actually applies in
// CharacterZoneClient__CastSpell (eqgame.exe, Ghidra project SoF) plus the body types
// zone/spells.cpp accepts for the matching SpellTargetType. The goal is that the client never
// rejects a target the server would have accepted.
static const std::map<uint8_t, std::vector<uint8_t>> body_type_client_aliases = {
	// ST_Undead (target type 10, and 21 which shares its switch case) tests property 3 "Undead"
	// and prints eqstr 219 "This spell only works on the undead." locally, without ever sending
	// OP_CastSpell. zone/spells.cpp accepts SummonedUndead and Vampire for ST_Undead, so both
	// must advertise Undead: this is what makes Mend Bones / Renew Bones able to heal a
	// necromancer's Bone Walk pet (npc body type 8).
	{ BodyType::SummonedUndead, { BodyType::Undead } },
	{ BodyType::Vampire,        { BodyType::Undead } },

	// ST_Summoned (target type 11, and 22 which shares its switch case) tests property 5
	// "Construct" and prints eqstr 216 "This spell only works on constructs or elementals."
	// zone/spells.cpp accepts Summoned/Summoned2/Summoned3, which in this database is every mage
	// elemental pet (SumAirR2..R16), enchanter familiar, animated weapon, shaman spirit, druid
	// animal spirit and cleric hammer - 34 Banish/Ward/Expulse/Exile/Destroy/Turn Summoned spells
	// are otherwise unusable from the client's point of view.
	{ BodyType::Summoned,       { BodyType::Construct } },
	{ BodyType::Summoned2,      { BodyType::Construct } },
	{ BodyType::Summoned3,      { BodyType::Construct } },

	// ST_Giant (17) tests property 4 and ST_Dragon (18) tests property 26, both additionally
	// requiring the zone to be a Velious/Kunark world. zone/spells.cpp normalizes RaidGiant ->
	// Giant and VeliousDragon -> Dragon before comparing, so the client has to see the same.
	{ BodyType::RaidGiant,      { BodyType::Giant } },
	{ BodyType::VeliousDragon,  { BodyType::Dragon } },

	// Body type 29 ("Dragon2") is deliberately NOT aliased to Dragon even though the database uses
	// it for the Kunark/DoN flightless dragons (Trakanon, Gorenaire, Nagafen, Sapara, Klandicar,
	// Nexus Scion): zone/spells.cpp does not normalize it either, and eqstr 226 restricts these
	// spells to "dragons in Velious". Aliasing it would make the client accept targets the server
	// rejects. Body type 32 ("Dragon3") is not aliased for a simpler reason - the NPCs using it
	// (Innoruuk, Rizlona, bonewalkers, swarm pets, supply chests, town NPCs) are not dragons at all.
};

std::string BodyType::GetName(uint8 body_type_id)
{
	return IsValid(body_type_id) ? body_type_names[body_type_id] : "UNKNOWN BODY TYPE";
}

bool BodyType::IsValid(uint8 body_type_id)
{
	return body_type_names.find(body_type_id) != body_type_names.end();
}

std::vector<uint8_t> BodyType::GetClientProperties(uint8 body_type_id)
{
	std::vector<uint8_t> properties;
	properties.push_back(body_type_id);

	auto aliases = body_type_client_aliases.find(body_type_id);
	if (aliases != body_type_client_aliases.end()) {
		for (const uint8_t &alias : aliases->second) {
			if (std::find(properties.begin(), properties.end(), alias) == properties.end()) {
				properties.push_back(alias);
			}
		}
	}

	return properties;
}

