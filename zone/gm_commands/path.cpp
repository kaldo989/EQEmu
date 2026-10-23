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
#include "zone/client.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

void command_path(Client *c, const Seperator *sep)
{
	if (zone->pathing) {
		zone->pathing->DebugCommand(c, sep);
	}
}

// #pathtest <x> <y> <z> [straight|nav] [forward|reverse]
//
// Sends the Ctrl+F path reply (OP_PlayerPathReply, 0x4d39) on demand, without going through the Find window, so
// the line the client draws can be compared against EQEmu's own route and against both waypoint orders
// (docs/findwnd.md 7.11). With no mode words it sends the two point straight line from the player to the point:
// the simplest reply the client can be given, which is the baseline for telling a bad route apart from a bad
// packet. nav runs the same Zone::BuildMysticalPath the Find window uses and reports what it decided; reverse
// sends the waypoints goal first instead of player first. Pair it with #path show, which walks EQEmu's navmesh
// route as visible PathNode NPCs from the client to their current target.
void command_pathtest(Client *c, const Seperator *sep)
{
	if (sep->argnum < 3) {
		c->Message(Chat::White, "#pathtest <x> <y> <z> [straight|nav] [plain|reverse]: send yourself a mystical path to that point");
		c->Message(Chat::White, "default order is what live sends: anchor point, then you, then the walk, goal last");
		c->Message(Chat::White, "plain drops the anchor so the line starts at you, reverse flips the whole list, both only for comparison");
		return;
	}

	const glm::vec3 start(c->GetX(), c->GetY(), c->GetZ());
	const glm::vec3 goal(
		static_cast<float>(atof(sep->arg[1])),
		static_cast<float>(atof(sep->arg[2])),
		static_cast<float>(atof(sep->arg[3]))
	);

	bool use_navmesh = false;
	int  order       = 0;   // 0 live order, 1 player first, 2 reversed

	for (int i = 4; i <= sep->argnum; ++i) {
		if (!strcasecmp(sep->arg[i], "nav")) {
			use_navmesh = true;
		}
		else if (!strcasecmp(sep->arg[i], "plain")) {
			order = 1;
		}
		else if (!strcasecmp(sep->arg[i], "reverse")) {
			order = 2;
		}
	}

	const auto route = use_navmesh ? zone->BuildMysticalPath(start, goal) : ZoneMysticalPathRoute{};

	// BuildMysticalPath already returns live order: an anchor point, then where the player is, then the walk with
	// the goal last. "plain" removes the anchor, which is the order the server sent before docs/findwnd.md 7.12,
	// and "reverse" flips the whole list, which is the order the Ghidra read of FUN_1402f2f90 suggested and live
	// disproved.
	std::vector<glm::vec3> waypoints = use_navmesh ? route.waypoints : std::vector<glm::vec3>{};

	if (!use_navmesh) {
		const float distance = glm::distance(start, goal);
		uint32 points = static_cast<uint32>(distance / 20.0f) + 2;

		waypoints.push_back(goal);

		for (uint32 i = 0; i < points; ++i) {
			const float factor = static_cast<float>(i) / static_cast<float>(points - 1);
			waypoints.push_back(start + (goal - start) * factor);
		}
	}

	const char *order_name = "live";

	if (order == 1) {
		order_name = "player first";

		if (waypoints.size() > 1) {
			waypoints.erase(waypoints.begin());
		}
	}
	else if (order == 2) {
		order_name = "reversed";
		std::reverse(waypoints.begin(), waypoints.end());
	}

	std::string detail;
	if (use_navmesh) {
		detail = fmt::format(
			" goal gap [{:.1f}] start z snap [{:.1f}] goal z snap [{:.1f}] nav nodes [{}] portals crossed [{}]",
			route.goal_gap,
			route.start_snap_z,
			route.goal_snap_z,
			route.nav_nodes,
			route.portals_bridged
		);
	}

	c->Message(Chat::White, fmt::format(
		"#pathtest: [{} waypoints, {} order, {}] from [{:.1f} {:.1f} {:.1f}] to [{:.1f} {:.1f} {:.1f}]{}",
		waypoints.size(),
		order_name,
		use_navmesh ? route.source : "straight line",
		start.x,
		start.y,
		start.z,
		goal.x,
		goal.y,
		goal.z,
		detail
	).c_str());

	c->SendPlayerPathReply(waypoints);
}
