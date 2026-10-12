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
#include "say_link.h"

#include "common/emu_constants.h"
#include "common/misc_functions.h"

#include <cstring>
#include <cstdlib>

// These link body helpers never touch the database, so they are kept in their own translation unit.
// The client patch layers call them, and those layers are linked into every server, each of which has
// its own `database` global (ZoneDatabase in zone, WorldDatabase in world). If these functions lived
// in say_link.cpp with SayLinkEngine, calling them from a patch would pull say_link.obj - and its
// ZoneDatabase reference - into world.exe, which fails to link.

static bool IsLinkHexChar(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
}

// __ConvertItemTags (0x140230AE0) reads the data block length from TagSizes (DAT_1408b2290), a table of
// ten entries indexed by the etag character. It only range checks the etag when it is called in
// validating mode. The text logging path - CEverQuest__OutputTextToLog (0x140105BC0), which is what
// writes eqlog.txt - calls it with the check skipped, so a '\x12' followed by anything other than a
// digit indexes past the table, lands on a pointer, and the resulting "length" copies out of bounds and
// crashes the client. That is why an etag has to be a digit, not just a hex character.
bool EQ::saylink::IsLinkEtag(char etag)
{
	return (etag >= '0' && etag <= '9');
}

// Test the fixed block widths directly. Laurion is checked first: a segment whose display text is itself
// hex would satisfy both widths, and only the longer one can be the data block. A segment that only fills
// the canonical width is a RoF2 link even when the display text continues with hex characters.
size_t EQ::saylink::DetectLinkDataWidth(const std::string &say_link_body)
{
	if (say_link_body.empty() || !IsLinkEtag(say_link_body[0])) {
		return 0;
	}

	const size_t laurion_data = EQ::constants::GetSayLinkBodySize(versions::ClientVersion::Laurion) - 1;
	const size_t canonical_data = EQ::constants::GetSayLinkBodySize(versions::ClientVersion::RoF2) - 1;

	auto fills_width = [&](size_t width) -> bool {
		if (say_link_body.size() < width + 1) {
			return false;
		}
		for (size_t i = 1; i <= width; ++i) {
			if (!IsLinkHexChar(say_link_body[i])) {
				return false;
			}
		}
		return true;
	};

	if (fills_width(laurion_data)) {
		return laurion_data;
	}

	if (fills_width(canonical_data)) {
		return canonical_data;
	}

	return 0;
}

bool EQ::saylink::DegenerateLinkBodyFromMessage(SayLinkBody_Struct &say_link_body_struct, const std::string &message, size_t link_open)
{
	const std::string segment = message.substr(link_open + 1);

	// Only the item tag has a fixed size data block.
	// Only the item tag has a fixed size data block, and its etag has to be a digit for the client.
	if (segment.empty() || segment[0] != '0') {
		return false;
	}

	const size_t data_length = DetectLinkDataWidth(segment);

	if (data_length != EQ::constants::GetSayLinkBodySize(versions::ClientVersion::Laurion) - 1 &&
		data_length != EQ::constants::GetSayLinkBodySize(versions::ClientVersion::RoF2) - 1) {
		return false;
	}

	return DegenerateLinkBody(say_link_body_struct, segment.substr(0, data_length + 1), versions::ClientVersion::RoF2);
}

bool EQ::saylink::DegenerateLinkBody(SayLinkBody_Struct &say_link_body_struct, const std::string &say_link_body, versions::ClientVersion client_version)
{
	memset(&say_link_body_struct, 0, sizeof(say_link_body_struct));

	const size_t client_size = EQ::constants::GetSayLinkBodySize(client_version);
	const size_t canonical_size = EQ::constants::SAY_LINK_BODY_SIZE;
	const size_t laurion_size = EQ::constants::GetSayLinkBodySize(versions::ClientVersion::Laurion);

	if (say_link_body.length() != client_size && say_link_body.length() != canonical_size && say_link_body.length() != laurion_size) {
		return false;
	}

	// The socket luck and item luck fields only exist in the Laurion layout, so the field order is
	// decided by the length the body actually has, not by who asked for it. A body that arrives
	// already normalised to the canonical form parses with the canonical order.
	const bool is_laurion = (say_link_body.length() == laurion_size);

	uint32 *augment_fields[6] = {
		&say_link_body_struct.augment_1,
		&say_link_body_struct.augment_2,
		&say_link_body_struct.augment_3,
		&say_link_body_struct.augment_4,
		&say_link_body_struct.augment_5,
		&say_link_body_struct.augment_6
	};

	size_t pos = 0;
	auto read_hex = [&](size_t width) -> uint32 {
		uint32 value = (uint32) strtol(say_link_body.substr(pos, width).c_str(), nullptr, 16);
		pos += width;
		return value;
	};

	say_link_body_struct.action_id = (uint8) read_hex(1);
	say_link_body_struct.item_id   = read_hex(5);

	for (int i = 0; i < 6; ++i) {
		*augment_fields[i] = read_hex(5);
		if (is_laurion) {
			say_link_body_struct.socket_luck[i] = read_hex(5);
		}
	}

	say_link_body_struct.is_evolving   = (uint8) read_hex(1);
	say_link_body_struct.evolve_group  = read_hex(4);
	say_link_body_struct.evolve_level  = (uint8) read_hex(2);
	say_link_body_struct.ornament_icon = read_hex(5);
	if (is_laurion) {
		say_link_body_struct.luck = read_hex(5);
	}
	say_link_body_struct.hash = read_hex(8);

	return true;
}

bool EQ::saylink::GenerateLinkBody(std::string &say_link_body, const SayLinkBody_Struct &say_link_body_struct, versions::ClientVersion client_version)
{
	// action_id is the etag. It is written as a single hex digit, so a value above 9 would render as
	// A-F, which is outside the client's TagSizes table. Clamp it to the digit range instead of emitting
	// a link the client cannot parse.
	const uint8 etag = IsLinkEtag(static_cast<char>('0' + (say_link_body_struct.action_id & 0x0F)))
		? static_cast<uint8>(say_link_body_struct.action_id & 0x0F)
		: 0;

	if (client_version == EQ::versions::ClientVersion::Laurion) {
		// Laurion client layout (client ItemBase__CreateItemTagString): every augment socket is
		// written as a pair of %05X fields (socket item id + socket luck), plus a trailing item luck.
		say_link_body = StringFormat(
			"%1X" "%05X"
			"%05X%05X" "%05X%05X" "%05X%05X" "%05X%05X" "%05X%05X" "%05X%05X"
			"%1X" "%04X" "%02X" "%05X" "%05X" "%08X",
			etag,
			(0x000FFFFF & say_link_body_struct.item_id),
			(0x000FFFFF & say_link_body_struct.augment_1), (0x000FFFFF & say_link_body_struct.socket_luck[0]),
			(0x000FFFFF & say_link_body_struct.augment_2), (0x000FFFFF & say_link_body_struct.socket_luck[1]),
			(0x000FFFFF & say_link_body_struct.augment_3), (0x000FFFFF & say_link_body_struct.socket_luck[2]),
			(0x000FFFFF & say_link_body_struct.augment_4), (0x000FFFFF & say_link_body_struct.socket_luck[3]),
			(0x000FFFFF & say_link_body_struct.augment_5), (0x000FFFFF & say_link_body_struct.socket_luck[4]),
			(0x000FFFFF & say_link_body_struct.augment_6), (0x000FFFFF & say_link_body_struct.socket_luck[5]),
			(0x0F & say_link_body_struct.is_evolving),
			(0x0000FFFF & say_link_body_struct.evolve_group),
			(0xFF & say_link_body_struct.evolve_level),
			(0x000FFFFF & say_link_body_struct.ornament_icon),
			(0x000FFFFF & say_link_body_struct.luck),
			(0xFFFFFFFF & say_link_body_struct.hash)
		);
	} else {
		say_link_body = StringFormat(
			"%1X" "%05X" "%05X" "%05X" "%05X" "%05X" "%05X" "%05X" "%1X" "%04X" "%02X" "%05X" "%08X",
			etag,
			(0x000FFFFF & say_link_body_struct.item_id),
			(0x000FFFFF & say_link_body_struct.augment_1),
			(0x000FFFFF & say_link_body_struct.augment_2),
			(0x000FFFFF & say_link_body_struct.augment_3),
			(0x000FFFFF & say_link_body_struct.augment_4),
			(0x000FFFFF & say_link_body_struct.augment_5),
			(0x000FFFFF & say_link_body_struct.augment_6),
			(0x0F & say_link_body_struct.is_evolving),
			(0x0000FFFF & say_link_body_struct.evolve_group),
			(0xFF & say_link_body_struct.evolve_level),
			(0x000FFFFF & say_link_body_struct.ornament_icon),
			(0xFFFFFFFF & say_link_body_struct.hash)
		);
	}

	if (say_link_body.length() != EQ::constants::GetSayLinkBodySize(client_version)) {
		return false;
	}

	return true;
}
