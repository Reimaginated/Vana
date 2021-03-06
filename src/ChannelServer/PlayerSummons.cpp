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
#include "PlayerSummons.hpp"
#include "Common/Algorithm.hpp"
#include "Common/GameConstants.hpp"
#include "Common/GameLogicUtilities.hpp"
#include "Common/PacketReader.hpp"
#include "Common/Timer.hpp"
#include "Common/TimeUtilities.hpp"
#include "ChannelServer/Map.hpp"
#include "ChannelServer/Player.hpp"
#include "ChannelServer/Summon.hpp"
#include "ChannelServer/SummonHandler.hpp"
#include "ChannelServer/SummonsPacket.hpp"
#include <functional>

namespace Vana {
namespace ChannelServer {

PlayerSummons::PlayerSummons(Player *player) :
	m_player{player}
{
}

auto PlayerSummons::addSummon(Summon *summon, seconds_t time) -> void {
	summon_id_t summonId = summon->getId();
	Vana::Timer::Id id{TimerType::BuffTimer, summonId, 1};
	Vana::Timer::Timer::create(
		[this, summonId](const time_point_t &now) {
			SummonHandler::removeSummon(
				ref_ptr_t<Player>{m_player},
				summonId,
				false,
				SummonMessages::OutOfTime,
				true);
		},
		id,
		m_player->getTimerContainer(),
		time);

	m_summons.push_back(summon);
}

auto PlayerSummons::removeSummon(summon_id_t summonId, bool fromTimer) -> void {
	Summon *summon = getSummon(summonId);
	if (summon != nullptr) {
		if (!fromTimer) {
			Vana::Timer::Id id{TimerType::BuffTimer, summonId, 1};
			m_player->getTimerContainer()->removeTimer(id);
		}
		ext::remove_element(m_summons, summon);
		delete summon;
		SummonHandler::summonIds.release(summonId);
	}
}

auto PlayerSummons::getSummon(summon_id_t summonId) -> Summon * {
	for (auto &summon : m_summons) {
		if (summon->getId() == summonId) {
			return summon;
		}
	}
	return nullptr;
}

auto PlayerSummons::forEach(function_t<void(Summon *)> func) -> void {
	auto copy = m_summons;
	for (auto &summon : copy) {
		func(summon);
	}
}

auto PlayerSummons::changedMap() -> void {
	auto copy = m_summons;
	for (auto &summon : copy) {
		if (summon->getMovementType() == Summon::Static) {
			SummonHandler::removeSummon(
				ref_ptr_t<Player>{m_player},
				summon->getId(),
				false,
				SummonMessages::None);
		}
	}
}

auto PlayerSummons::getSummonTimeRemaining(summon_id_t summonId) const -> seconds_t {
	Vana::Timer::Id id{TimerType::BuffTimer, summonId, 1};
	return m_player->getTimerContainer()->getRemainingTime<seconds_t>(id);
}

auto PlayerSummons::getTransferPacket() const -> PacketBuilder {
	PacketBuilder builder;
	builder.add<uint8_t>(static_cast<uint8_t>(m_summons.size()));
	if (m_summons.size() > 0) {
		for (const auto &summon : m_summons) {
			if (summon->getMovementType() == Summon::Static) {
				continue;
			}
			builder.add<skill_id_t>(summon->getSkillId());
			builder.add<seconds_t>(getSummonTimeRemaining(summon->getId()));
			builder.add<skill_level_t>(summon->getSkillLevel());
		}
	}
	return builder;
}

auto PlayerSummons::parseTransferPacket(PacketReader &reader) -> void {
	uint8_t size = reader.get<uint8_t>();
	if (size > 0) {
		for (uint8_t i = 0; i < size; ++i) {
			skill_id_t skillId = reader.get<skill_id_t>();
			seconds_t timeLeft = reader.get<seconds_t>();
			skill_level_t level = reader.get<skill_level_t>();

			Summon *summon = new Summon{SummonHandler::summonIds.lease(), skillId, level, m_player->isFacingLeft(), m_player->getPos()};
			summon->setPos(m_player->getPos());
			addSummon(summon, timeLeft);
			m_player->sendMap(Packets::showSummon(m_player->getId(), summon, false));
		}
	}
}

}
}