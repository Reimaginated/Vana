/*
Copyright (C) 2008-2010 Vana Development Team

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

#include "Types.h"
#include <string>
#include <vector>

using std::string;
using std::vector;

class PacketCreator;
class Player;
class WorldServerConnection;

namespace WorldServerConnectPacket {
	void findPlayer(WorldServerConnection *player, int32_t playerid, const string &findee_name);
	void whisperPlayer(WorldServerConnection *player, int32_t playerid, const string &whisperee, const string &message);
	void scrollingHeader(WorldServerConnection *player, const string &message);
	void groupChat(WorldServerConnection *player, int8_t type, int32_t playerid, const vector<int32_t> &receivers, const string &chat);
	void rankingCalculation(WorldServerConnection *player);
	void toChannels(WorldServerConnection *player, PacketCreator &packet);
	void toWorlds(WorldServerConnection *player, PacketCreator &packet);
	void reloadMcdb(WorldServerConnection *player, const string &type);
	void reloadCashServers(WorldServerConnection *player);
};
