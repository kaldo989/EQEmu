/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
	Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include "zone/client.h"

// Test hook for opcode 0x1d00 (FUN_1401fb190).
//
// The handler resolves the GUID string through FUN_1402cfce0, which walks the profile container array and
// compares ItemBase + 0xc / + 0x14 / + 0x1c. On a match it writes ItemBase::Luck (0x100) and then calls
// CItemDisplayManager::UpdateItem(item, 2) and CItemFuseWnd::UpdateItem(item). Both only repaint windows
// that are already showing this instance, so the observable behaviour is:
//
//   1. open the item stats popup on the slot (Alt + left click, or long click up on an InvSlot)
//   2. run #itemluck <slot>
//   3. the popup re-renders
//
// If nothing repaints, the instance is not in the client's profile container at all, which is the merchant
// and Advanced Loot problem. The value is gated by ItemDefinition::MinLuck .. MaxLuck, and EQEmu currently
// serializes both as 0, so only 0 passes.
void command_itemluck(Client *c, const Seperator *sep)
{
	if (!sep->argnum || !sep->IsNumber(1)) {
		c->Message(Chat::White, "Usage: #itemluck <inventory slot> [luck value]");
		return;
	}

	int16 slot = (int16)Strings::ToUnsignedInt(sep->arg[1]);
	uint32 luck = (sep->argnum > 2 && sep->IsNumber(2)) ? Strings::ToUnsignedInt(sep->arg[2]) : 0;

	EQ::ItemInstance* inst = c->GetInv().GetItem(slot);
	if (!inst) {
		c->Message(Chat::White, "Nothing in slot %i", slot);
		return;
	}

	c->SendItemLuckPacket(slot, inst, luck);

	c->Message(Chat::White,
		"Sent 0x1d00 for slot %i, GUID %016u, luck %u - the client gate is MinLuck 0 .. MaxLuck 0, so only 0 is accepted",
		slot, inst->GetSerialNumber(), luck);
}
