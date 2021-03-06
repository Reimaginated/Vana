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

#include "Common/Buff.hpp"
#include "Common/BuffInfo.hpp"
#include "Common/BuffSource.hpp"
#include "Common/PacketBuilder.hpp"
#include "Common/SkillConstants.hpp"
#include "Common/Types.hpp"
#include <array>
#include <vector>

namespace Vana {
	namespace ChannelServer {
		class Player;

		enum class BuffPacketValueType {
			Value,
			Packet,
			SpecialPacket,
		};

		struct BuffPacketValue {
			static BuffPacketValue fromValue(uint8_t size, int64_t value) {
				BuffPacketValue ret;
				ret.valueSize = size;
				ret.value = value;
				ret.type = BuffPacketValueType::Value;
				return ret;
			}

			static BuffPacketValue fromPacket(PacketBuilder builder) {
				BuffPacketValue ret;
				ret.builder = builder;
				ret.type = BuffPacketValueType::Packet;
				return ret;
			}

			static BuffPacketValue fromSpecialPacket(PacketBuilder builder) {
				BuffPacketValue ret;
				ret.builder = builder;
				ret.type = BuffPacketValueType::SpecialPacket;
				return ret;
			}

			uint8_t valueSize = 0;
			int64_t value = 0;
			int32_t time = 0;
			BuffPacketValueType type;
			PacketBuilder builder;
		private:
			BuffPacketValue() = default;
		};

		struct BuffPacketStructure {
			BuffPacketStructure()
			{
				std::fill(std::begin(types), std::end(types), 0);
			}

			bool anyMovementBuffs = false;
			buff_array_t types;
			vector_t<BuffPacketValue> values;
		};

		struct BuffPacketValues {
			BuffPacketStructure player;
			optional_t<BuffPacketStructure> map;
			milliseconds_t delay = milliseconds_t{0};
		};

		namespace Buffs {
			auto addBuff(ref_ptr_t<Player> player, item_id_t itemId, const seconds_t &time) -> Result;
			auto addBuff(ref_ptr_t<Player> player, skill_id_t skillId, skill_level_t level, int16_t addedInfo, map_object_t mapMobId = 0) -> Result;
			auto addBuff(ref_ptr_t<Player> player, mob_skill_id_t skillId, mob_skill_level_t level, milliseconds_t delay) -> Result;
			auto endBuff(ref_ptr_t<Player> player, const BuffSource &source, bool fromTimer = false) -> void;
			auto preprocessBuff(ref_ptr_t<Player> player, skill_id_t skillId, skill_level_t level, const seconds_t &time) -> Buff;
			auto preprocessBuff(ref_ptr_t<Player> player, item_id_t itemId, const seconds_t &time) -> Buff;
			auto preprocessBuff(ref_ptr_t<Player> player, mob_skill_id_t skillId, mob_skill_level_t level, const seconds_t &time) -> Buff;
			auto preprocessBuff(ref_ptr_t<Player> player, const BuffSource &source, const seconds_t &time) -> Buff;
			auto preprocessBuff(ref_ptr_t<Player> player, const BuffSource &source, const seconds_t &time, const Buff &buff) -> Buff;
			auto preprocessBuff(const Buff &buff, const vector_t<uint8_t> &bitPositionsToInclude) -> Buff;
			auto convertToPacketTypes(const Buff &buff) -> BuffPacketValues;
			auto convertToPacket(ref_ptr_t<Player> player, const BuffSource &source, const seconds_t &time, const Buff &buff) -> BuffPacketValues;
			auto buffMayApply(ref_ptr_t<Player> player, const BuffSource &source, const seconds_t &time, const BuffInfo &buff) -> bool;
			auto getValue(ref_ptr_t<Player> player, const BuffSource &source, const seconds_t &time, const BuffInfo &buff) -> BuffPacketValue;
			auto getValue(ref_ptr_t<Player> player, const BuffSource &source, const seconds_t &time, uint8_t bitPosition, const BuffMapInfo &buff) -> BuffPacketValue;
			auto getValue(ref_ptr_t<Player> player, const BuffSource &source, const seconds_t &time, uint8_t bitPosition, BuffSkillValue value, uint8_t buffValueSize) -> BuffPacketValue;
		}
	}
}