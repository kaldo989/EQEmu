#ifndef LAURION_ZONE_NAMES_H
#define LAURION_ZONE_NAMES_H

#include <algorithm>
#include <cstdint>

// Zone numbers the Laurion client registers in its hardcoded zone table (eqgame.exe:0x140640a40,
// documented in docs/zoneList.md) but has no display name for, because
// G:\Games\LS\Resources\ZoneNames.txt has no entry for them. The client fills such a ZoneInfo with the
// literal string "Not Found" (eqgame.exe:0x140649100), which is what the Ctrl+F Find window then prints
// for a zone connection row - the row text is built entirely from the client's own zone table
// (eqgame.exe:0x140331e80 -> 0x140649710), never from a server supplied name. See docs/findwnd.md 7.2.
//
// Most of these are the pre-rezone copies of zones that later expansions reissued under a new id with
// new geometry (misty 33 / mistythicket 415, tox 38 / toxxulia 414, innothule 46 / innothuleb 413,
// steamfont 56 / steamfontmts 448, highpass 5 / highpasshold 407, oldhighpass 458, pomischief 761 /
// mischiefplane 126, burnedwoods 762 / burningwood 87). A zone_points row that targets one of them is a
// real destination; it just cannot be labelled by the client, so EQEmu sends those ports as POI rows on
// OP_FindLocationsPOIs, where the server supplies both strings (zone/client.cpp
// Client::SendFindLocationsPOIs), and keeps plain connection rows for destinations the client can name.
//
// Regenerate this list when the client changes: docs/tools/findconn_ids_check.py compares peq with the
// client's ZoneNames.txt, and docs/zoneList.md holds the same 24 ids as the "missing from ZoneNames.txt"
// section. Ids 183/184/185/190/999/767/876/479 are tutorial, loading and test zones; the zone drops rows
// for the loading ids outright, so they only matter here for completeness.
namespace LaurionClientZones {

inline constexpr uint16_t kZonesWithoutClientName[] = {
	28,   // nektropos      Nektropos
	33,   // misty          Misty Thicket          (reworked as 415 mistythicket)
	38,   // tox            Toxxulia Forest        (reworked as 414 toxxulia)
	46,   // innothule      Innothule Swamp        (reworked as 413 innothuleb)
	56,   // steamfont      Steamfont Mountains    (reworked as 448 steamfontmts)
	74,   // kerraridge     Kerra Isle
	130,  // erudsxing2     Marauders Mire
	180,  // arena2         The Arena Two
	183,  // tutorial       EverQuest Tutorial
	184,  // load           Loading Zone
	185,  // load2          New Loading Zone
	190,  // clz            Loading
	299,  // qvicb          Qvic, the Hidden Vault
	346,  // barter         The Barter Hall
	410,  // kithforest     Kithicor Forest
	411,  // befallenb      Befallen
	412,  // highpasskeep   HighKeep
	458,  // oldhighpass    Highpass Hold          (reworked as 407 highpasshold)
	479,  // crafthalls     Crafting Halls
	761,  // pomischief     The Plane of Mischief  (reworked as 126 mischiefplane)
	762,  // burnedwoods    The Burning Wood       (reworked as 87 burningwood)
	767,  // interiorwalltest Interior Wall Test
	876,  // clz-25anniversary 25th Anniversary
	999,  // apprentice     Designer Apprentice
};

// The client's own slot rule for its 1001 entry zone array: ids below 1000 are used directly, anything
// above that is read as (id & 0x7fff), which is how instance flagged zone ids are addressed.
inline uint32_t ClientZoneSlot(uint32_t zone_id)
{
	return zone_id < 1000 ? zone_id : (zone_id & 0x7FFF);
}

// True when the client can print a real name for this zone number.
inline bool ZoneHasClientName(uint32_t zone_id)
{
	const uint16_t slot = static_cast<uint16_t>(ClientZoneSlot(zone_id));
	return !std::binary_search(std::begin(kZonesWithoutClientName), std::end(kZonesWithoutClientName), slot);
}

} // namespace LaurionClientZones

#endif // LAURION_ZONE_NAMES_H
