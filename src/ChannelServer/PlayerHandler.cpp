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
#include "PlayerHandler.hpp"
#include "Common/Algorithm.hpp"
#include "Common/AttackData.hpp"
#include "Common/GameLogicUtilities.hpp"
#include "Common/InterHeader.hpp"
#include "Common/ItemDataProvider.hpp"
#include "Common/MpEaterData.hpp"
#include "Common/PacketWrapper.hpp"
#include "Common/Randomizer.hpp"
#include "Common/PacketReader.hpp"
#include "Common/ReturnDamageData.hpp"
#include "Common/SkillConstants.hpp"
#include "Common/SkillDataProvider.hpp"
#include "Common/SkillType.hpp"
#include "Common/TimeUtilities.hpp"
#include "Common/Timer.hpp"
#include "Common/WidePoint.hpp"
#include "ChannelServer/ChannelServer.hpp"
#include "ChannelServer/Drop.hpp"
#include "ChannelServer/DropHandler.hpp"
#include "ChannelServer/DropsPacket.hpp"
#include "ChannelServer/InventoryPacket.hpp"
#include "ChannelServer/MapleTvs.hpp"
#include "ChannelServer/Maps.hpp"
#include "ChannelServer/Mist.hpp"
#include "ChannelServer/MobHandler.hpp"
#include "ChannelServer/MonsterBookPacket.hpp"
#include "ChannelServer/MovementHandler.hpp"
#include "ChannelServer/MysticDoor.hpp"
#include "ChannelServer/Player.hpp"
#include "ChannelServer/PlayerDataProvider.hpp"
#include "ChannelServer/PlayerPacket.hpp"
#include "ChannelServer/PlayerSkills.hpp"
#include "ChannelServer/PlayersPacket.hpp"
#include "ChannelServer/Skills.hpp"
#include "ChannelServer/SkillsPacket.hpp"
#include "ChannelServer/StatusInfo.hpp"
#include "ChannelServer/Summon.hpp"
#include "ChannelServer/SummonHandler.hpp"
#include <functional>

namespace Vana {
namespace ChannelServer {

auto PlayerHandler::handleDoorUse(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	player_id_t doorPlayerId = reader.get<player_id_t>();
	bool toTown = !reader.get<bool>();
	auto doorHolder = ChannelServer::getInstance().getPlayerDataProvider().getPlayer(doorPlayerId);
	if (doorHolder == nullptr || (doorHolder->getParty() != player->getParty() && doorHolder != player)) {
		// Hacking or lag
		return;
	}
	ref_ptr_t<MysticDoor> door = doorHolder->getSkills()->getMysticDoor();
	if (door == nullptr) {
		// Hacking or lag
		return;
	}

	if (player->getMapId() != door->getMapId() && player->getMapId() != door->getTownId()) {
		// Hacking
		return;
	}

	if ((player->getMapId() == door->getTownId()) == toTown) {
		// Hacking
		return;
	}

	if (toTown) {
		player->setMap(door->getTownId(), door->getPortalId(), door->getTownPos());
	}
	else {
		player->setMap(door->getMapId(), MysticDoor::PortalId, door->getMapPos());
	}
}

auto PlayerHandler::handleDamage(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	const int8_t BumpDamage = -1;
	const int8_t MapDamage = -2;

	reader.skip<tick_count_t>();
	int8_t type = reader.get<int8_t>();
	reader.skip<uint8_t>(); // Element - 0x00 = elementless, 0x01 = ice, 0x02 = fire, 0x03 = lightning
	damage_t damage = reader.get<damage_t>();
	bool damageApplied = false;
	bool deadlyAttack = false;
	uint8_t hit = 0;
	uint8_t stance = 0;
	mob_skill_id_t disease = 0;
	mob_skill_level_t level = 0;
	health_t mpBurn = 0;
	map_object_t mapMobId = 0;
	mob_id_t mobId = 0;
	skill_id_t noDamageId = 0;
	ReturnDamageData pgmr;

	if (type != MapDamage) {
		mobId = reader.get<mob_id_t>();
		mapMobId = reader.get<map_object_t>();
		auto mob = player->getMap()->getMob(mapMobId);
		if (mob != nullptr && mob->getMobId() != mobId) {
			// Hacking
			return;
		}
		if (type != BumpDamage) {
			if (mob == nullptr) {
				// TODO FIXME: Restructure so the attack works fine even if the mob dies?
				return;
			}

			auto attack = ChannelServer::getInstance().getMobDataProvider().getMobAttack(mob->getMobIdOrLink(), type);
			if (attack == nullptr) {
				// Hacking
				return;
			}
			disease = attack->disease;
			level = attack->level;
			mpBurn = attack->mpBurn;
			deadlyAttack = attack->deadlyAttack;
		}

		hit = reader.get<uint8_t>(); // Knock direction
		pgmr.reduction = reader.get<uint8_t>();
		reader.unk<uint8_t>(); // I think reduction is a short, but it's a byte in the S -> C packet, so..
		if (pgmr.reduction != 0) {
			pgmr.isPhysical = reader.get<bool>();
			pgmr.mapMobId = reader.get<map_object_t>();
			if (pgmr.mapMobId != mapMobId) {
				// Hacking
				return;
			}
			reader.skip<int8_t>(); // 0x06 for Power Guard, 0x00 for Mana Reflection?
			reader.skip<Point>(); // Mob position garbage
			pgmr.pos = reader.get<Point>();
			pgmr.damage = damage;
			if (pgmr.isPhysical) {
				// Only Power Guard decreases damage
				damage = (damage - (damage * pgmr.reduction / 100));
			}
			mob->applyDamage(player->getId(), (pgmr.damage * pgmr.reduction / 100));
		}
	}

	if (type == MapDamage) {
		level = reader.get<mob_skill_level_t>();
		disease = reader.get<mob_skill_id_t>();
	}
	else {
		stance = reader.get<int8_t>(); // Power Stance
		auto powerStance = player->getActiveBuffs()->getPowerStanceSource();
		if (stance > 0 && !powerStance.is_initialized()) {
			// Hacking
			return;
		}
	}

	if (damage == -1) {
		if (!player->getSkills()->hasNoDamageSkill()) {
			// Hacking
			return;
		}
		noDamageId = player->getSkills()->getNoDamageSkill();
	}

	if (disease > 0 && damage != 0) {
		// Fake/Guardian don't prevent disease
		Buffs::addBuff(player, disease, level, milliseconds_t{0});
	}

	health_t mp = player->getStats()->getMp();
	health_t hp = player->getStats()->getHp();

	auto deadlyAttackFunc = [&player, &mp](bool setHp) {
		if (mp > 0) {
			player->getStats()->setMp(1);
		}
		if (setHp) {
			player->getStats()->setHp(1);
		}
	};

	if (damage > 0 && !player->hasGmBenefits()) {
		auto mesoGuard = player->getActiveBuffs()->getMesoGuardSource();
		if (mesoGuard.is_initialized() && player->getInventory()->getMesos() > 0) {
			auto &source = mesoGuard.get();
			int16_t mesoRate = player->getActiveBuffs()->getBuffSkillInfo(source)->x; // Meso Guard meso %
			int16_t mesoLoss = static_cast<int16_t>(mesoRate * damage / 2 / 100);
			mesos_t mesos = player->getInventory()->getMesos();
			mesos_t newMesos = mesos - mesoLoss;

			if (newMesos < 0) {
				// Special damage calculation for not having enough mesos
				double reduction = 2.0 - static_cast<double>(mesos / mesoLoss) / 2.0;
				damage = static_cast<uint16_t>(damage / reduction);
				// This puts us pretty close to the damage observed clientside, needs improvement
				// TODO FIXME formula
			}
			else {
				damage /= 2;
				// Usually displays 1 below the actual damage but is sometimes accurate - no clue why
			}

			player->getInventory()->setMesos(newMesos);
			player->getStats()->damageHp(static_cast<uint16_t>(damage));

			if (deadlyAttack) {
				deadlyAttackFunc(false);
			}
			else if (mpBurn > 0) {
				player->getStats()->damageMp(mpBurn);
			}
			damageApplied = true;

			player->sendMap(Packets::Skills::showSkillEffect(player->getId(), source.getSkillId()));
		}

		auto magicGuard = player->getActiveBuffs()->getMagicGuardSource();
		if (magicGuard.is_initialized()) {
			if (deadlyAttack) {
				deadlyAttackFunc(true);
			}
			else if (mpBurn > 0) {
				player->getStats()->damageMp(mpBurn);
				player->getStats()->damageHp(static_cast<uint16_t>(damage));
			}
			else {
				int16_t reduc = player->getActiveBuffs()->getBuffSkillInfo(magicGuard.get())->x;
				int32_t mpDamage = (damage * reduc) / 100;
				int32_t hpDamage = damage - mpDamage;

				if (mpDamage < mp || player->getActiveBuffs()->hasInfinity()) {
					player->getStats()->damageMp(mpDamage);
					player->getStats()->damageHp(hpDamage);
				}
				else if (mpDamage >= mp) {
					player->getStats()->setMp(0);
					player->getStats()->damageHp(hpDamage + (mpDamage - mp));
				}
			}
			damageApplied = true;
		}

		if (player->getSkills()->hasAchilles()) {
			skill_id_t skillId = player->getSkills()->getAchilles();
			double red = (2.0 - player->getSkills()->getSkillInfo(skillId)->x / 1000.0);

			player->getStats()->damageHp(static_cast<uint16_t>(damage / red));

			if (deadlyAttack) {
				deadlyAttackFunc(false);
			}
			else if (mpBurn > 0) {
				player->getStats()->damageMp(mpBurn);
			}

			damageApplied = true;
		}

		if (!damageApplied) {
			if (deadlyAttack) {
				deadlyAttackFunc(true);
			}
			else {
				player->getStats()->damageHp(static_cast<uint16_t>(damage));
			}

			if (mpBurn > 0) {
				player->getStats()->damageMp(mpBurn);
			}
		}

		player->getActiveBuffs()->takeDamage(damage);
	}
	player->sendMap(Packets::Players::damagePlayer(player->getId(), damage, mobId, hit, type, stance, noDamageId, pgmr));
}

auto PlayerHandler::handleFacialExpression(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	int32_t face = reader.get<int32_t>();
	player->sendMap(Packets::Players::faceExpression(player->getId(), face));
}

auto PlayerHandler::handleGetInfo(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	reader.skip<tick_count_t>();
	player_id_t playerId = reader.get<player_id_t>();
	if (auto info = ChannelServer::getInstance().getPlayerDataProvider().getPlayer(playerId)) {
		player->send(Packets::Players::showInfo(info, reader.get<bool>()));
	}
}

auto PlayerHandler::handleHeal(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	reader.skip<tick_count_t>();
	health_t hp = reader.get<health_t>();
	health_t mp = reader.get<health_t>();
	if (player->getStats()->isDead() || hp > 400 || mp > 1000 || (hp > 0 && mp > 0)) {
		// Hacking
		return;
	}
	player->getStats()->modifyHp(hp);
	player->getStats()->modifyMp(mp);
}

auto PlayerHandler::handleMoving(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	if (reader.get<uint8_t>() != player->getPortalCount()) {
		// Portal count doesn't match, usually an indication of hacking
		return;
	}
	reader.reset(11);
	MovementHandler::parseMovement(player.get(), reader);
	reader.reset(11);
	player->sendMap(Packets::Players::showMoving(player->getId(), reader.getBuffer(), reader.getBufferLength()));

	if (player->getFoothold() == 0 && !player->isUsingGmHide()) {
		// Player is floating in the air
		// GMs might be legitimately in this state (due to GM fly)
		// We shouldn't mess with them because they have the tools to get out of falling off the map anyway
		map_id_t mapId = player->getMapId();
		Point playerPos = player->getPos();
		Map *map = Maps::getMap(mapId);

		Point floor;
		if (map->findFloor(playerPos, floor) == SearchResult::NotFound) {
			// There are no footholds below the player
			int8_t count = player->getFallCounter();
			if (count > 3) {
				player->setMap(mapId);
			}
			else {
				player->setFallCounter(++count);
			}
		}
	}
	else if (player->getFallCounter() > 0) {
		player->setFallCounter(0);
	}
}

auto PlayerHandler::handleSpecialSkills(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	skill_id_t skillId = reader.get<skill_id_t>();
	switch (skillId) {
		case Vana::Skills::Hero::MonsterMagnet:
		case Vana::Skills::Paladin::MonsterMagnet:
		case Vana::Skills::DarkKnight::MonsterMagnet:
		case Vana::Skills::Marksman::PiercingArrow:
		case Vana::Skills::FpArchMage::BigBang:
		case Vana::Skills::IlArchMage::BigBang:
		case Vana::Skills::Bishop::BigBang: {
			ChargeOrStationarySkillData info;
			info.skillId = skillId;
			info.level = reader.get<skill_level_t>();
			info.direction = reader.get<uint8_t>();
			info.weaponSpeed = reader.get<uint8_t>();
			player->setChargeOrStationarySkill(info);
			player->sendMap(Packets::Skills::endChargeOrStationarySkill(player->getId(), info));
			break;
		}
		case Vana::Skills::ChiefBandit::Chakra: {
			stat_t dex = player->getStats()->getDex(true);
			stat_t luk = player->getStats()->getLuk(true);
			int16_t recovery = player->getSkills()->getSkillInfo(skillId)->y;
			int16_t maximum = (luk * 66 / 10 + dex) * 2 / 10 * (recovery / 100 + 1);
			int16_t minimum = (luk * 33 / 10 + dex) * 2 / 10 * (recovery / 100 + 1);
			// Maximum = (luk * 6.6 + dex) * 0.2 * (recovery% / 100 + 1)
			// Minimum = (luk * 3.3 + dex) * 0.2 * (recovery% / 100 + 1)
			// I used 66 / 10 and 2 / 10 respectively to get 6.6 and 0.2 without using floating points
			player->getStats()->modifyHp(Randomizer::rand<int16_t>(maximum, minimum));
			break;
		}
	}
}

auto PlayerHandler::handleMonsterBook(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	item_id_t cardId = reader.get<item_id_t>();
	if (cardId != 0 && player->getMonsterBook()->getCard(cardId) == 0) {
		// Hacking
		return;
	}
	mob_id_t newCover = 0;
	if (cardId != 0) {
		optional_t<mob_id_t> mobCoverId =
			ChannelServer::getInstance().getItemDataProvider().getMobId(cardId);

		if (mobCoverId.is_initialized()) {
			newCover = mobCoverId.get();
		}
		else {
			// Hacking?
			newCover = 0;
		}
	}
	player->getMonsterBook()->setCover(newCover);
	player->send(Packets::MonsterBook::changeCover(cardId));
}

auto PlayerHandler::handleAdminMessenger(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	if (!player->isAdmin()) {
		// Hacking
		return;
	}
	ref_ptr_t<Player> receiver = nullptr;
	bool hasTarget = reader.get<int8_t>() == 2;
	int8_t sort = reader.get<int8_t>();
	bool useWhisper = reader.get<bool>();
	int8_t type = reader.get<int8_t>();
	player_id_t characterId = reader.get<player_id_t>();

	if (player->getId() != characterId) {
		return;
	}

	string_t line1 = reader.get<string_t>();
	string_t line2 = reader.get<string_t>();
	string_t line3 = reader.get<string_t>();
	string_t line4 = reader.get<string_t>();
	string_t line5 = reader.get<string_t>();
	if (hasTarget) {
		receiver = ChannelServer::getInstance().getPlayerDataProvider().getPlayer(reader.get<string_t>());
	}

	int32_t time = 15;
	switch (type) {
		case 1: time = 30; break;
		case 2: time = 60; break;
	}

	ChannelServer::getInstance().getMapleTvs().addMessage(player, receiver, line1, line2, line3, line4, line5, 5075000 + type, time);
	if (sort == 1) {
		out_stream_t output;
		output << player->getMedalName() << " : " << line1 << line2 << line3 << line4 << line5;

		auto &builder = Packets::Inventory::showSuperMegaphone(output.str(), useWhisper);
		ChannelServer::getInstance().sendWorld(
			Vana::Packets::prepend(builder, [](PacketBuilder &header) {
				header.add<header_t>(IMSG_TO_ALL_PLAYERS);
			}));
	}
}

auto PlayerHandler::useBombSkill(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	// TODO FIXME packet
	// Ignore this position in favor of player->getPos()?
	WidePoint playerPos = reader.get<WidePoint>();
	charge_time_t charge = reader.get<charge_time_t>();
	skill_id_t skillId = reader.get<skill_id_t>();

	if (player->getSkills()->getSkillLevel(skillId) == 0) {
		// Hacking
		return;
	}
	if (!ext::in_range_inclusive(charge, 0, 1000)) {
		// Hacking
		return;
	}

	player->sendMap(Packets::Players::useBombAttack(player->getId(), charge, skillId, playerPos));
}

auto PlayerHandler::useMeleeAttack(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	AttackData attack = compileAttack(player, reader, SkillType::Melee);
	if (attack.portals != player->getPortalCount()) {
		// Usually evidence of hacking
		return;
	}
	skill_id_t masteryId = player->getSkills()->getMastery();
	int8_t damagedTargets = 0;
	skill_id_t skillId = attack.skillId;
	skill_level_t level = attack.skillLevel;

	if (skillId != Vana::Skills::All::RegularAttack) {
		if (Skills::useAttackSkill(player, skillId) == Result::Failure) {
			// Most likely hacking, could feasibly be lag
			return;
		}
	}

	player->sendMap(Packets::Players::useMeleeAttack(player->getId(), masteryId, player->getSkills()->getSkillLevel(masteryId), attack));

	map_id_t map = player->getMapId();
	auto pickpocket = player->getActiveBuffs()->getPickpocketSource();
	bool ppok = !attack.isMesoExplosion && pickpocket.is_initialized();
	Point origin;
	vector_t<damage_t> ppDamages;
	auto picking = !pickpocket.is_initialized() ?
		nullptr :
		player->getActiveBuffs()->getBuffSkillInfo(pickpocket.get());
	bool reflectApplied = player->hasGmBenefits();

	for (const auto &target : attack.damages) {
		damage_t targetTotal = 0;
		int8_t connectedHits = 0;
		auto mob = Maps::getMap(map)->getMob(target.first);
		if (mob == nullptr) {
			continue;
		}
		if (mob->hasWeaponReflection() && !reflectApplied) {
			auto &reflect = mob->getWeaponReflection().get();
			int32_t amount = Randomizer::range<int32_t>(reflect.val, reflect.reflection);
			player->getStats()->modifyHp(-amount);
			reflectApplied = true;
		}

		origin = mob->getPos(); // Info for pickpocket before mob is set to nullptr (in the case that mob dies)
		for (const auto &hit : target.second) {
			damage_t damage = hit;
			if (damage != 0) {
				connectedHits++;
				targetTotal += damage;
			}
			if (ppok && Randomizer::percentage<uint16_t>() < picking->prop) {
				 // Make sure this is a melee attack and not meso explosion, plus pickpocket being active
				ppDamages.push_back(damage);
			}
			if (mob == nullptr) {
				if (ppok) {
					// Roll along after the mob is dead to finish getting damage values for pickpocket
					continue;
				}
				break;
			}
			if (skillId == Vana::Skills::Paladin::HeavensHammer) {
				damage = (mob->isBoss() ? Stats::MaxDamage : (mob->getHp() - 1)); // If a Paladin wants to prove that it does something else, feel free
			}
			else if (skillId == Vana::Skills::Bandit::Steal && !mob->isBoss()) {
				DropHandler::doDrops(player->getId(), map, mob->getLevel(), mob->getMobId(), mob->getPos(), false, false, mob->getTauntEffect(), true);
			}
			int32_t tempHp = mob->getHp();
			mob->applyDamage(player->getId(), damage);
			if (tempHp <= damage) {
				// Mob was killed, so set the Mob pointer to 0
				mob = nullptr;
			}
		}
		if (targetTotal > 0) {
			if (mob != nullptr && mob->getHp() > 0) {
				MobHandler::handleMobStatus(player->getId(), mob, skillId, level, player->getInventory()->getEquippedId(EquipSlots::Weapon), connectedHits); // Mob status handler (freeze, stun, etc)
				if (mob->getHp() < mob->getSelfDestructHp()) {
					mob->explode();
				}
			}
			damagedTargets++;
		}
		uint8_t ppSize = static_cast<uint8_t>(ppDamages.size());
		for (uint8_t pickpocket = 0; pickpocket < ppSize; ++pickpocket) {
			// Drop stuff for Pickpocket
			Point ppPos = origin;
			ppPos.x += (ppSize % 2 == 0 ? 5 : 0) + (ppSize / 2) - 20 * ((ppSize / 2) - pickpocket);

			int32_t ppMesos = (ppDamages[pickpocket] * picking->x) / 10000; // TODO FIXME formula
			Drop *ppDrop = new Drop{player->getMapId(), ppMesos, ppPos, player->getId(), true};
			ppDrop->setTime(100);
			Vana::Timer::Timer::create(
				[ppDrop, origin](const time_point_t &now) {
					ppDrop->doDrop(origin);
				},
				Vana::Timer::Id{
					TimerType::PickpocketTimer,
					player->getId(),
					player->getActiveBuffs()->getPickpocketCounter()
				},
				nullptr,
				milliseconds_t{175 * pickpocket});
		}
		ppDamages.clear();
	}

	if (player->getSkills()->hasEnergyCharge()) {
		player->getActiveBuffs()->increaseEnergyChargeLevel(damagedTargets);
	}

	if (player->getSkills()->hasDarkSightInterruptionSkill()) {
		auto source = player->getActiveBuffs()->getDarkSightSource();
		if (source.is_initialized()) {
			Skills::stopSkill(player, source.get());
		}
	}

	switch (skillId) {
		case Vana::Skills::ChiefBandit::MesoExplosion: {
			uint8_t items = reader.get<int8_t>();
			Map *map = player->getMap();
			for (uint8_t i = 0; i < items; i++) {
				map_object_t objId = reader.get<map_object_t>();
				reader.unk<uint8_t>();
				if (Drop *drop = map->getDrop(objId)) {
					if (!drop->isMesos()) {
						// Hacking
						return;
					}
					map->send(Packets::Drops::explodeDrop(drop->getId()));
					map->removeDrop(drop->getId());
					delete drop;
				}
			}
			break;
		}
		case Vana::Skills::Marauder::EnergyDrain:
		case Vana::Skills::ThunderBreaker::EnergyDrain: {
			int32_t hpRecover = static_cast<int32_t>(attack.totalDamage * player->getSkills()->getSkillInfo(skillId)->x / 100);
			if (hpRecover > player->getStats()->getMaxHp()) {
				player->getStats()->setHp(player->getStats()->getMaxHp());
			}
			else {
				player->getStats()->modifyHp(static_cast<int16_t>(hpRecover));
			}
			break;
		}
		case Vana::Skills::Crusader::SwordPanic: // Crusader finishers
		case Vana::Skills::Crusader::SwordComa:
		case Vana::Skills::Crusader::AxePanic:
		case Vana::Skills::Crusader::AxeComa:
		case Vana::Skills::DawnWarrior::Panic:
		case Vana::Skills::DawnWarrior::Coma:
			player->getActiveBuffs()->resetCombo();
			break;
		case Vana::Skills::NightWalker::PoisonBomb: {
			auto skill = ChannelServer::getInstance().getSkillDataProvider().getSkill(skillId, level);
			Mist *mist = new Mist{player->getMapId(), player, skill->buffTime, skill->dimensions.move(attack.projectilePos), skillId, level, true};
			break;
		}
		case Vana::Skills::Crusader::Shout:
		case Vana::Skills::Gm::SuperDragonRoar:
		case Vana::Skills::SuperGm::SuperDragonRoar:
			break;
		case Vana::Skills::DragonKnight::DragonRoar: {
			int16_t xProperty = ChannelServer::getInstance().getSkillDataProvider().getSkill(skillId, level)->x;
			uint16_t reduction = (player->getStats()->getMaxHp() / 100) * xProperty;
			if (reduction < player->getStats()->getHp()) {
				player->getStats()->damageHp(reduction);
			}
			else {
				// Hacking
				return;
			}
			Buffs::addBuff(player, Vana::Skills::DragonKnight::DragonRoar, level, 0);
			break;
		}
		case Vana::Skills::DragonKnight::Sacrifice: {
			if (attack.totalDamage > 0) {
				int16_t xProperty = player->getSkills()->getSkillInfo(skillId)->x;
				int32_t hpDamage = static_cast<int32_t>(attack.totalDamage * xProperty / 100);
				if (hpDamage >= player->getStats()->getHp()) {
					hpDamage = player->getStats()->getHp() - 1;
				}
				if (hpDamage > 0) {
					player->getStats()->damageHp(hpDamage);
				}
			}
			break;
		}
		case Vana::Skills::WhiteKnight::ChargeBlow: {
			skill_level_t skillLevel = player->getSkills()->getSkillLevel(Vana::Skills::Paladin::AdvancedCharge);
			int16_t xProperty = 0;
			if (skillLevel > 0) {
				xProperty = ChannelServer::getInstance().getSkillDataProvider().getSkill(Vana::Skills::Paladin::AdvancedCharge, skillLevel)->x;
			}
			if ((xProperty != 100) && (xProperty == 0 || Randomizer::percentage<int16_t>() > (xProperty - 1))) {
				player->getActiveBuffs()->stopCharge();
			}
			break;
		}
		default:
			if (attack.totalDamage > 0) {
				player->getActiveBuffs()->addCombo();
			}
	}
}

auto PlayerHandler::useRangedAttack(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	AttackData attack = compileAttack(player, reader, SkillType::Ranged);
	if (attack.portals != player->getPortalCount()) {
		// Usually evidence of hacking
		return;
	}
	skill_id_t masteryId = player->getSkills()->getMastery();
	skill_id_t skillId = attack.skillId;
	skill_level_t level = attack.skillLevel;
	int8_t damagedTargets = 0;

	if (Skills::useAttackSkillRanged(player, skillId, attack.starPos, attack.cashStarPos, attack.starId) == Result::Failure) {
		// Most likely hacking, could feasibly be lag
		return;
	}

	player->sendMap(Packets::Players::useRangedAttack(player->getId(), masteryId, player->getSkills()->getSkillLevel(masteryId), attack));

	switch (skillId) {
		case Vana::Skills::Bowmaster::Hurricane:
		case Vana::Skills::WindArcher::Hurricane:
		case Vana::Skills::Corsair::RapidFire:
			if (!player->hasChargeOrStationarySkill()) {
				ChargeOrStationarySkillData info;
				info.skillId = skillId;
				info.direction = attack.animation;
				info.weaponSpeed = attack.weaponSpeed;
				info.level = level;
				player->setChargeOrStationarySkill(info);
				player->sendMap(Packets::Skills::endChargeOrStationarySkill(player->getId(), info));
			}
			break;
	}

	int32_t maxHp = 0;
	damage_t firstHit = 0;
	bool reflectApplied = player->hasGmBenefits();
	for (const auto &target : attack.damages) {
		map_object_t mapMobId = target.first;
		auto mob = player->getMap()->getMob(mapMobId);
		if (mob == nullptr) {
			continue;
		}
		if (mob->hasWeaponReflection() && !reflectApplied) {
			auto &reflect = mob->getWeaponReflection().get();
			int32_t amount = Randomizer::range<int32_t>(reflect.val, reflect.reflection);
			player->getStats()->modifyHp(-amount);
			reflectApplied = true;
		}
		damage_t targetTotal = 0;
		int8_t connectedHits = 0;

		for (const auto &hit : target.second) {
			damage_t damage = hit;

			if (damage != 0) {
				connectedHits++;
				targetTotal += damage;
			}
			if (firstHit == 0) {
				firstHit = damage;
			}
			if (mob == nullptr) {
				continue;
			}
			maxHp = mob->getMaxHp();
			if (skillId == Vana::Skills::Ranger::MortalBlow || skillId == Vana::Skills::Sniper::MortalBlow) {
				auto sk = player->getSkills()->getSkillInfo(skillId);
				int32_t hpPercentage = maxHp * sk->x / 100; // Percentage of HP required for Mortal Blow activation
				if (mob->getHp() < hpPercentage && Randomizer::percentage<int16_t>() < sk->y) {
					damage = mob->getHp();
				}
			}

			int32_t tempHp = mob->getHp();
			mob->applyDamage(player->getId(), damage);
			if (tempHp <= damage) {
				mob = nullptr;
			}
			else if (skillId == Vana::Skills::Outlaw::HomingBeacon || skillId == Vana::Skills::Corsair::Bullseye) {
				Buffs::addBuff(player, skillId, level, 0, mapMobId);
			}
		}
		if (targetTotal > 0) {
			if (mob != nullptr && mob->getHp() > 0) {
				MobHandler::handleMobStatus(player->getId(), mob, skillId, level, player->getInventory()->getEquippedId(EquipSlots::Weapon), connectedHits, firstHit); // Mob status handler (freeze, stun, etc)
				if (mob->getHp() < mob->getSelfDestructHp()) {
					mob->explode();
				}
			}
			damagedTargets++;
		}
	}

	if (player->getSkills()->hasEnergyCharge()) {
		player->getActiveBuffs()->increaseEnergyChargeLevel(damagedTargets);
	}

	if (player->getSkills()->hasDarkSightInterruptionSkill()) {
		auto source = player->getActiveBuffs()->getDarkSightSource();
		if (source.is_initialized()) {
			Skills::stopSkill(player, source.get());
		}
	}

	switch (skillId) {
		case Vana::Skills::NightWalker::Vampire:
		case Vana::Skills::Assassin::Drain: {
			int16_t xProperty = player->getSkills()->getSkillInfo(skillId)->x;
			int32_t hpRecover = static_cast<int32_t>(attack.totalDamage * xProperty / 100);
			health_t playerMaxHp = player->getStats()->getMaxHp();
			if (hpRecover > maxHp) {
				hpRecover = maxHp;
			}
			if (hpRecover > (playerMaxHp / 2)) {
				hpRecover = playerMaxHp / 2;
			}
			if (hpRecover > playerMaxHp) {
				player->getStats()->setHp(playerMaxHp);
			}
			else {
				player->getStats()->modifyHp(hpRecover);
			}
			break;
		}
		case Vana::Skills::DawnWarrior::SoulBlade:
			if (attack.totalDamage > 0) {
				player->getActiveBuffs()->addCombo();
			}
			break;
	}
}

auto PlayerHandler::useSpellAttack(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	const AttackData &attack = compileAttack(player, reader, SkillType::Magic);
	if (attack.portals != player->getPortalCount()) {
		// Usually evidence of hacking
		return;
	}

	skill_id_t skillId = attack.skillId;
	skill_level_t level = attack.skillLevel;

	if (!attack.isHeal) {
		// Heal is sent as both an attack and as a used skill - it's only sometimes an attack
		if (Skills::useAttackSkill(player, skillId) == Result::Failure) {
			// Most likely hacking, could feasilby be lag
			return;
		}
	}

	player->sendMap(Packets::Players::useSpellAttack(player->getId(), attack));

	MpEaterData eater;
	eater.skillId = player->getSkills()->getMpEater();
	eater.level = player->getSkills()->getSkillLevel(eater.skillId);
	if (eater.level > 0) {
		auto skillInfo = ChannelServer::getInstance().getSkillDataProvider().getSkill(eater.skillId, eater.level);
		eater.prop = skillInfo->prop;
		eater.x = skillInfo->x;
	}

	bool reflectApplied = player->hasGmBenefits();
	for (const auto &target : attack.damages) {
		damage_t targetTotal = 0;
		map_object_t mapMobId = target.first;
		int8_t connectedHits = 0;
		auto mob = player->getMap()->getMob(mapMobId);
		if (mob == nullptr) {
			continue;
		}
		if (attack.isHeal && !mob->isUndead()) {
			// Hacking
			return;
		}
		if (mob->hasMagicReflection() && !reflectApplied) {
			auto &reflect = mob->getMagicReflection().get();
			int32_t amount = Randomizer::range<int32_t>(reflect.val, reflect.reflection);
			player->getStats()->modifyHp(-amount);
			reflectApplied = true;
		}

		for (const auto &hit : target.second) {
			damage_t damage = hit;
			if (damage != 0) {
				connectedHits++;
				targetTotal += damage;
			}
			if (damage != 0 && eater.level != 0 && !eater.used) {
				// MP Eater
				mob->mpEat(player, &eater);
			}
			int32_t tempHp = mob->getHp();
			mob->applyDamage(player->getId(), damage);
			if (tempHp <= damage) {
				// Mob was killed, so set the Mob pointer to 0
				mob = nullptr;
				break;
			}
		}
		if (mob != nullptr && targetTotal > 0 && mob->getHp() > 0) {
			MobHandler::handleMobStatus(player->getId(), mob, skillId, level, player->getInventory()->getEquippedId(EquipSlots::Weapon), connectedHits); // Mob status handler (freeze, stun, etc)
			if (mob->getHp() < mob->getSelfDestructHp()) {
				mob->explode();
			}
		}
	}

	switch (skillId) {
		case Vana::Skills::FpMage::PoisonMist:
		case Vana::Skills::BlazeWizard::FlameGear: {
			auto skill = ChannelServer::getInstance().getSkillDataProvider().getSkill(skillId, level);
			Mist *mist = new Mist{player->getMapId(), player, skill->buffTime, skill->dimensions.move(player->getPos()), skillId, level, true};
			break;
		}
	}
}

auto PlayerHandler::useEnergyChargeAttack(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	AttackData attack = compileAttack(player, reader, SkillType::EnergyCharge);
	skill_id_t masteryId = player->getSkills()->getMastery();
	player->sendMap(Packets::Players::useEnergyChargeAttack(player->getId(), masteryId, player->getSkills()->getSkillLevel(masteryId), attack));

	skill_id_t skillId = attack.skillId;
	skill_level_t level = attack.skillLevel;
	bool reflectApplied = player->hasGmBenefits();

	for (const auto &target : attack.damages) {
		damage_t targetTotal = 0;
		map_object_t mapMobId = target.first;
		int8_t connectedHits = 0;
		auto mob = player->getMap()->getMob(mapMobId);
		if (mob == nullptr) {
			continue;
		}
		if (mob->hasWeaponReflection() && !reflectApplied) {
			auto &reflect = mob->getWeaponReflection().get();
			int32_t amount = Randomizer::range<int32_t>(reflect.val, reflect.reflection);
			player->getStats()->modifyHp(-amount);
			reflectApplied = true;
		}

		for (const auto &hit : target.second) {
			damage_t damage = hit;
			if (damage != 0) {
				connectedHits++;
				targetTotal += damage;
			}
			int32_t tempHp = mob->getHp();
			mob->applyDamage(player->getId(), damage);
			if (tempHp <= damage) {
				// Mob was killed, so set the Mob pointer to 0
				mob = nullptr;
				break;
			}
		}
		if (mob != nullptr && targetTotal > 0 && mob->getHp() > 0) {
			MobHandler::handleMobStatus(player->getId(), mob, skillId, level, player->getInventory()->getEquippedId(EquipSlots::Weapon), connectedHits); // Mob status handler (freeze, stun, etc)
			if (mob->getHp() < mob->getSelfDestructHp()) {
				mob->explode();
			}
		}
	}
}

auto PlayerHandler::useSummonAttack(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	AttackData attack = compileAttack(player, reader, SkillType::Summon);
	Summon *summon = player->getSummons()->getSummon(attack.summonId);
	if (summon == nullptr) {
		// Hacking or some other form of tomfoolery
		return;
	}
	player->sendMap(Packets::Players::useSummonAttack(player->getId(), attack));
	for (const auto &target : attack.damages) {
		damage_t targetTotal = 0;
		map_object_t mapMobId = target.first;
		int8_t connectedHits = 0;
		auto mob = player->getMap()->getMob(mapMobId);
		if (mob == nullptr) {
			continue;
		}
		for (const auto &hit : target.second) {
			damage_t damage = hit;
			if (damage != 0) {
				connectedHits++;
				targetTotal += damage;
			}
			int32_t tempHp = mob->getHp();
			mob->applyDamage(player->getId(), damage);
			if (tempHp <= damage) {
				// Mob was killed, so set the Mob pointer to 0
				mob = nullptr;
				break;
			}
		}
		if (mob != nullptr && targetTotal > 0 && mob->getHp() > 0) {
			MobHandler::handleMobStatus(player->getId(), mob, summon->getSkillId(), summon->getSkillLevel(), player->getInventory()->getEquippedId(EquipSlots::Weapon), connectedHits);
			if (mob->getHp() < mob->getSelfDestructHp()) {
				mob->explode();
			}
		}
	}

	if (summon->getSkillId() == Vana::Skills::Outlaw::Gaviota) {
		SummonHandler::removeSummon(player, attack.summonId, false, SummonMessages::None, false);
	}
}

auto PlayerHandler::compileAttack(ref_ptr_t<Player> player, PacketReader &reader, SkillType skillType) -> AttackData {
	AttackData attack;
	int8_t targets = 0;
	int8_t hits = 0;
	skill_id_t skillId = 0;
	bool mesoExplosion = false;
	bool shadowMeso = false;

	if (skillType != SkillType::Summon) {
		attack.portals = reader.get<uint8_t>();
		uint8_t tByte = reader.get<uint8_t>();
		skillId = reader.get<skill_id_t>();
		targets = tByte / 0x10;
		hits = tByte % 0x10;

		if (skillId != Vana::Skills::All::RegularAttack) {
			attack.skillLevel = player->getSkills()->getSkillLevel(skillId);
		}

		reader.skip<checksum_t>(); // Unk, strange constant that doesn't seem to change
		// Things atttemped: Map changes, character changes, job changes, skill changes, position changes, hitting enemies
		// It appears as 0xF9B16E60 which is 4189154912 unsigned, -105812384 signed, doesn't seem to be a size, probably a CRC
		reader.skip<checksum_t>();

		switch (skillId) {
			case Vana::Skills::Hermit::ShadowMeso:
				attack.isShadowMeso = true;
				shadowMeso = true;
				break;
			case Vana::Skills::ChiefBandit::MesoExplosion:
				attack.isMesoExplosion = true;
				mesoExplosion = true;
				break;
			case Vana::Skills::Cleric::Heal:
				attack.isHeal = true;
				break;
			case Vana::Skills::Gunslinger::Grenade:
			case Vana::Skills::Brawler::CorkscrewBlow:
			case Vana::Skills::ThunderBreaker::CorkscrewBlow:
			case Vana::Skills::Bowmaster::Hurricane:
			case Vana::Skills::WindArcher::Hurricane:
			case Vana::Skills::Marksman::PiercingArrow:
			case Vana::Skills::NightWalker::PoisonBomb:
			case Vana::Skills::Corsair::RapidFire:
			case Vana::Skills::FpArchMage::BigBang:
			case Vana::Skills::IlArchMage::BigBang:
			case Vana::Skills::Bishop::BigBang:
				attack.isChargeSkill = true;
				attack.charge = reader.get<charge_time_t>();
				break;
		}

		attack.display = reader.get<uint8_t>();
		attack.animation = reader.get<uint8_t>();
		attack.weaponClass = reader.get<uint8_t>();
		attack.weaponSpeed = reader.get<uint8_t>();
		attack.ticks = reader.get<tick_count_t>();
	}
	else {
		attack.summonId = reader.get<summon_id_t>(); // Summon ID, not to be confused with summon skill ID
		attack.ticks = reader.get<tick_count_t>();
		attack.animation = reader.get<uint8_t>();
		targets = reader.get<int8_t>();
		hits = 1;
	}

	if (skillType == SkillType::Ranged) {
		inventory_slot_t starSlot = reader.get<inventory_slot_t>();
		inventory_slot_t csStar = reader.get<inventory_slot_t>();
		attack.starPos = starSlot;
		attack.cashStarPos = csStar;
		reader.unk<uint8_t>(); // 0x00 = AoE?
		if (!shadowMeso) {
			if (player->getActiveBuffs()->hasShadowStars() && skillId != Vana::Skills::NightLord::Taunt) {
				attack.starId = reader.get<int32_t>();
			}
			else if (csStar > 0) {
				if (Item *item = player->getInventory()->getItem(Inventories::CashInventory, csStar)) {
					attack.starId = item->getId();
				}
			}
			else if (starSlot > 0) {
				if (Item *item = player->getInventory()->getItem(Inventories::UseInventory, starSlot)) {
					attack.starId = item->getId();
				}
			}
		}
	}

	attack.targets = targets;
	attack.hits = hits;
	attack.skillId = skillId;

	for (int8_t i = 0; i < targets; ++i) {
		map_object_t mapMobId = reader.get<map_object_t>();
		reader.unk<uint32_t>(); // Always 0x06, <two bytes of some kind>, 0x01
		reader.skip<Point>(); // Mob pos
		reader.skip<Point>(); // Damage pos
		if (!mesoExplosion) {
			reader.skip<uint16_t>(); // Distance, I think
		}
		else {
			hits = reader.get<int8_t>(); // Hits for Meso Explosion
		}
		for (int8_t k = 0; k < hits; ++k) {
			damage_t damage = reader.get<damage_t>();
			attack.damages[mapMobId].push_back(damage);
			attack.totalDamage += damage;
		}
		if (skillType != SkillType::Summon) {
			reader.skip<checksum_t>();
		}
	}

	if (skillType == SkillType::Ranged) {
		attack.projectilePos = reader.get<Point>();
	}
	attack.playerPos = reader.get<Point>();

	if (skillId == Vana::Skills::NightWalker::PoisonBomb) {
		attack.projectilePos = reader.get<Point>();
	}
	return attack;
}

}
}