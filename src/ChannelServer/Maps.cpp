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
#include "Maps.hpp"
#include "Common/FileUtilities.hpp"
#include "Common/PacketReader.hpp"
#include "ChannelServer/ChannelServer.hpp"
#include "ChannelServer/Instance.hpp"
#include "ChannelServer/Inventory.hpp"
#include "ChannelServer/LuaPortal.hpp"
#include "ChannelServer/MapDataProvider.hpp"
#include "ChannelServer/MapPacket.hpp"
#include "ChannelServer/PetHandler.hpp"
#include "ChannelServer/Player.hpp"
#include "ChannelServer/PlayerPacket.hpp"
#include "ChannelServer/PlayerDataProvider.hpp"
#include "ChannelServer/SummonHandler.hpp"
#include <string>

namespace Vana {
namespace ChannelServer {

auto Maps::getMap(map_id_t mapId) -> Map * {
	return ChannelServer::getInstance().getMap(mapId);
}

auto Maps::unloadMap(map_id_t mapId) -> void {
	ChannelServer::getInstance().unloadMap(mapId);
}

auto Maps::usePortal(ref_ptr_t<Player> player, const PortalInfo * const portal) -> void {
	if (portal->disabled) {
		player->send(Packets::Map::portalBlocked());
		player->send(Packets::Player::showMessage("The portal is closed for now.", Packets::Player::NoticeTypes::Red));
		return;
	}

	if (portal->script.size() != 0) {
		// Check for "onlyOnce" portal
		if (portal->onlyOnce && player->usedPortal(portal->id)) {
			player->send(Packets::Map::portalBlocked());
			return;
		}

		string_t filename = ChannelServer::getInstance().getScriptDataProvider().buildScriptPath(ScriptTypes::Portal, portal->script);

		if (FileUtilities::fileExists(filename)) {
			LuaPortal luaEnv = {filename, player->getId(), player->getMapId(), portal};

			if (!luaEnv.playerMapChanged()) {
				player->send(Packets::Map::portalBlocked());
			}
			if (portal->onlyOnce && !luaEnv.portalFailed()) {
				player->addUsedPortal(portal->id);
			}
		}
		else {
			string_t message = player->isGm() ?
				"Portal '" + portal->script + "' is currently unavailable." :
				"This portal is currently unavailable.";

			player->send(Packets::Player::showMessage(message, Packets::Player::NoticeTypes::Red));
			player->send(Packets::Map::portalBlocked());
		}
	}
	else {
		// Normal portal
		Map *toMap = getMap(portal->toMap);
		if (toMap == nullptr) {
			player->send(Packets::Player::showMessage("Bzzt. The map you're attempting to travel to doesn't exist.", Packets::Player::NoticeTypes::Red));
			player->send(Packets::Map::portalBlocked());
			return;
		}
		const PortalInfo * const nextPortal = toMap->getPortal(portal->toName);
		player->setMap(portal->toMap, nextPortal);
	}
}

auto Maps::usePortal(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	reader.skip<portal_count_t>();

	int32_t opcode = reader.get<int32_t>();
	switch (opcode) {
		case 0: // Dead
			if (player->getStats()->isDead()) {
				reader.unk<string_t>(); // Seemingly useless
				reader.unk<int8_t>(); // Seemingly useless
				bool wheel = reader.get<bool>();
				if (wheel) {
					if (player->getInventory()->getItemAmount(Items::WheelOfDestiny) <= 0) {
						player->acceptDeath(false);
						return;
					}
					Inventory::takeItem(player, Items::WheelOfDestiny, 1);
				}
				player->acceptDeath(wheel);
			}
			break;
		case -1: {
			string_t portalName = reader.get<string_t>();

			Map *toMap = player->getMap();
			if (toMap == nullptr) {
				return;
			}
			const PortalInfo * const portal = toMap->getPortal(portalName);
			if (portal == nullptr) {
				return;
			}
			usePortal(player, portal);
			break;
		}
		default: {
			// GM Map change (command "/m")
			if (player->isGm()) {
				if (getMap(opcode) != nullptr) {
					player->setMap(opcode);
				}
				else {
					// TODO FIXME message
				}
			}
			else {
				// Hacking
				return;
			}
		}
	}
}

auto Maps::useScriptedPortal(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	reader.skip<portal_count_t>();
	string_t portalName = reader.get<string_t>();

	const PortalInfo * const portal = player->getMap()->getPortal(portalName);
	if (portal == nullptr) {
		return;
	}
	usePortal(player, portal);
}

auto Maps::addPlayer(ref_ptr_t<Player> player, map_id_t mapId) -> void {
	getMap(mapId)->addPlayer(player);
	getMap(mapId)->showObjects(player);
	PetHandler::showPets(player);
	SummonHandler::showSummon(player);
	// Bug in global - would be fixed here:
	// Berserk doesn't display properly when switching maps with it activated - client displays, but no message is sent to any client
	// player->getActiveBuffs()->checkBerserk(true) would override the default of only displaying changes
}

}
}