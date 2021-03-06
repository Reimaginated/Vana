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
#include "SkillsPacket.hpp"
#include "Common/ChargeOrStationarySkillData.hpp"
#include "Common/GameConstants.hpp"
#include "Common/Session.hpp"
#include "ChannelServer/Maps.hpp"
#include "ChannelServer/Player.hpp"
#include "ChannelServer/Skills.hpp"
#include "ChannelServer/SmsgHeader.hpp"

namespace Vana {
namespace ChannelServer {
namespace Packets {
namespace Skills {

PACKET_IMPL(addSkill, skill_id_t skillId, const PlayerSkillInfo &skillInfo) {
	PacketBuilder builder;
	builder
		.add<header_t>(SMSG_SKILL_ADD)
		.add<int8_t>(1)
		.add<int16_t>(1)
		.add<skill_id_t>(skillId)
		.add<int32_t>(skillInfo.level)
		.add<int32_t>(skillInfo.playerMaxSkillLevel)
		.add<int8_t>(1);
	return builder;
}

SPLIT_PACKET_IMPL(showSkill, player_id_t playerId, skill_id_t skillId, skill_level_t level, uint8_t direction, bool party, bool self) {
	SplitPacketBuilder builder;
	PacketBuilder buffer;
	buffer
		.add<int8_t>(party ? 2 : 1)
		.add<skill_id_t>(skillId)
		.add<skill_level_t>(level);

	switch (skillId) {
		case Vana::Skills::Hero::MonsterMagnet:
		case Vana::Skills::Paladin::MonsterMagnet:
		case Vana::Skills::DarkKnight::MonsterMagnet:
			buffer.add<uint8_t>(direction);
			break;
	}

	if (self) {
		if (party) {
			builder.player.add<header_t>(SMSG_THEATRICS);
		}
		else {
			builder.player
				.add<header_t>(SMSG_SKILL_SHOW)
				.add<player_id_t>(playerId);
		}
		builder.player.addBuffer(buffer);
	}
	else {
		builder.map
			.add<header_t>(SMSG_SKILL_SHOW)
			.add<player_id_t>(playerId)
			.addBuffer(buffer);
	}
	return builder;
}

PACKET_IMPL(healHp, health_t hp) {
	PacketBuilder builder;
	builder
		.add<header_t>(SMSG_THEATRICS)
		.add<int8_t>(0x0A)
		.add<health_t>(hp);
	return builder;
}

SPLIT_PACKET_IMPL(showSkillEffect, player_id_t playerId, skill_id_t skillId) {
	SplitPacketBuilder builder;
	PacketBuilder buffer;
	switch (skillId) {
		case Vana::Skills::FpWizard::MpEater:
		case Vana::Skills::IlWizard::MpEater:
		case Vana::Skills::Cleric::MpEater:
			buffer
				.add<int8_t>(1)
				.add<skill_id_t>(skillId)
				.add<int8_t>(1);
			break;
		case Vana::Skills::ChiefBandit::MesoGuard:
		case Vana::Skills::DragonKnight::DragonBlood:
			buffer
				.add<int8_t>(5)
				.add<skill_id_t>(skillId);
			break;
		default:
			return builder;
	}

	builder.player
		.add<header_t>(SMSG_THEATRICS)
		.addBuffer(buffer);

	builder.map
		.add<header_t>(SMSG_SKILL_SHOW)
		.add<player_id_t>(playerId)
		.addBuffer(buffer);
	return builder;
}

SPLIT_PACKET_IMPL(showChargeOrStationarySkill, player_id_t playerId, const ChargeOrStationarySkillData &info) {
	SplitPacketBuilder builder;
	builder.map
		.add<header_t>(SMSG_CHARGE_OR_STATIONARY_SKILL)
		.add<player_id_t>(playerId)
		.add<skill_id_t>(info.skillId)
		.add<skill_level_t>(info.level)
		.add<int8_t>(info.direction)
		.add<int8_t>(info.weaponSpeed);
	return builder;
}

SPLIT_PACKET_IMPL(endChargeOrStationarySkill, player_id_t playerId, const ChargeOrStationarySkillData &info) {
	SplitPacketBuilder builder;
	builder.map
		.add<header_t>(SMSG_CHARGE_OR_STATIONARY_SKILL_END)
		.add<player_id_t>(playerId)
		.add<skill_id_t>(info.skillId);
	return builder;
}

SPLIT_PACKET_IMPL(showMagnetSuccess, map_object_t mapMobId, uint8_t success) {
	SplitPacketBuilder builder;
	builder.map
		.add<header_t>(SMSG_MOB_DRAGGED)
		.add<map_object_t>(mapMobId)
		.add<uint8_t>(success);
	return builder;
}

PACKET_IMPL(sendCooldown, skill_id_t skillId, seconds_t time) {
	PacketBuilder builder;
	builder
		.add<header_t>(SMSG_SKILL_COOLDOWN)
		.add<skill_id_t>(skillId)
		.add<int16_t>(static_cast<int16_t>(time.count()));
	return builder;
}

SPLIT_PACKET_IMPL(showBerserk, player_id_t playerId, skill_level_t level, bool on) {
	SplitPacketBuilder builder;
	PacketBuilder buffer;
	buffer
		.add<int8_t>(1)
		.add<skill_id_t>(Vana::Skills::DarkKnight::Berserk)
		.add<skill_level_t>(level)
		.add<bool>(on);

	builder.player
		.add<header_t>(SMSG_THEATRICS)
		.addBuffer(buffer);

	builder.map
		.add<header_t>(SMSG_SKILL_SHOW)
		.add<player_id_t>(playerId)
		.addBuffer(buffer);
	return builder;
}

}
}
}
}