/*
Copyright (C) 2008-2016 Vana Development Team

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2
of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#pragma once

#include "Common/PacketBuilder.hpp"
#include "Common/Types.hpp"

namespace Vana {
	class Item;

	namespace ChannelServer {
		class Player;

		namespace Packets {
			namespace Helpers {
				PACKET(addItemInfo, inventory_slot_t slot, Item *item, bool shortSlot = false);
				PACKET(addPlayerDisplay, ref_ptr_t<Vana::ChannelServer::Player> player);
			}
		}
	}
}