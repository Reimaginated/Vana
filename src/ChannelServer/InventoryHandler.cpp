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
#include "InventoryHandler.hpp"
#include "Common/CurseDataProvider.hpp"
#include "Common/EquipDataProvider.hpp"
#include "Common/GameLogicUtilities.hpp"
#include "Common/InterHeader.hpp"
#include "Common/ItemConstants.hpp"
#include "Common/ItemDataProvider.hpp"
#include "Common/MobDataProvider.hpp"
#include "Common/NpcDataProvider.hpp"
#include "Common/PacketReader.hpp"
#include "Common/PacketWrapper.hpp"
#include "Common/Randomizer.hpp"
#include "Common/ScriptDataProvider.hpp"
#include "Common/ShopDataProvider.hpp"
#include "Common/ValidCharDataProvider.hpp"
#include "ChannelServer/ChannelServer.hpp"
#include "ChannelServer/Drop.hpp"
#include "ChannelServer/Inventory.hpp"
#include "ChannelServer/InventoryPacket.hpp"
#include "ChannelServer/MapleTvs.hpp"
#include "ChannelServer/Maps.hpp"
#include "ChannelServer/Pet.hpp"
#include "ChannelServer/PetHandler.hpp"
#include "ChannelServer/PetsPacket.hpp"
#include "ChannelServer/Player.hpp"
#include "ChannelServer/PlayerDataProvider.hpp"
#include "ChannelServer/ReactorHandler.hpp"

namespace Vana {
namespace ChannelServer {

auto InventoryHandler::moveItem(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	reader.skip<tick_count_t>();
	inventory_t inv = reader.get<inventory_t>();
	inventory_slot_t slot1 = reader.get<inventory_slot_t>();
	inventory_slot_t slot2 = reader.get<inventory_slot_t>();
	bool dropped = (slot2 == 0);
	bool equippedSlot1 = (slot1 < 0);
	bool equippedSlot2 = (slot2 < 0);
	inventory_slot_t strippedSlot1 = GameLogicUtilities::stripCashSlot(slot1);
	inventory_slot_t strippedSlot2 = GameLogicUtilities::stripCashSlot(slot2);
	auto testSlot = [&slot1, &slot2, &strippedSlot1, &strippedSlot2](inventory_slot_t testSlot) -> bool {
		return (slot1 < 0 && strippedSlot1 == testSlot) || (slot2 < 0 && strippedSlot2 == testSlot);
	};

	if (dropped) {
		Item *item1 = player->getInventory()->getItem(inv, slot1);
		if (item1 == nullptr) {
			return;
		}
		InventoryHandler::dropItem(player, reader, item1, slot1, inv);
	}
	else {
		player->getInventory()->swapItems(inv, slot1, slot2);
	}

	if (equippedSlot1 || equippedSlot2) {
		auto testPetSlot = [&player, &testSlot](int16_t equipSlot, int32_t petIndex) {
			if (testSlot(equipSlot)) {
				if (Pet *pet = player->getPets()->getSummoned(petIndex)) {
					player->sendMap(Packets::Pets::changeName(player->getId(), pet));
				}
			}
		};
		// Check if any label ring changed, so we can update the look of the apropos pet
		testPetSlot(EquipSlots::PetLabelRing1, 0);
		testPetSlot(EquipSlots::PetLabelRing2, 1);
		testPetSlot(EquipSlots::PetLabelRing3, 2);

		player->sendMap(Packets::Inventory::updatePlayer(player));
	}
}

auto InventoryHandler::dropItem(ref_ptr_t<Player> player, PacketReader &reader, Item *item, inventory_slot_t slot, inventory_t inv) -> void {
	slot_qty_t amount = reader.get<slot_qty_t>();
	if (!GameLogicUtilities::isStackable(item->getId())) {
		amount = item->getAmount();
	}
	else if (amount <= 0 || amount > item->getAmount()) {
		// Hacking
		return;
	}
	else if (item->hasLock()) {
		// Hacking
		return;
	}
	if (GameLogicUtilities::isGmEquip(item->getId()) || item->hasLock()) {
		// We don't allow these to be dropped or traded
		return;
	}

	Item droppedItem(item);
	droppedItem.setAmount(amount);
	if (item->getAmount() == amount) {
		vector_t<InventoryPacketOperation> ops;
		ops.emplace_back(Packets::Inventory::OperationTypes::ModifySlot, item, slot);
		player->send(Packets::Inventory::inventoryOperation(true, ops));

		player->getInventory()->deleteItem(inv, slot);
	}
	else {
		item->decAmount(amount);
		player->getInventory()->changeItemAmount(item->getId(), -amount);

		vector_t<InventoryPacketOperation> ops;
		ops.emplace_back(Packets::Inventory::OperationTypes::ModifyQuantity, item, slot);
		player->send(Packets::Inventory::inventoryOperation(true, ops));
	}

	auto itemInfo = ChannelServer::getInstance().getItemDataProvider().getItemInfo(droppedItem.getId());
	bool isTradeable = droppedItem.hasKarma() || !(droppedItem.hasTradeBlock() || itemInfo->quest || itemInfo->noTrade);
	Drop *drop = new Drop(player->getMapId(), droppedItem, player->getPos(), player->getId(), true);
	drop->setTime(0);
	drop->setTradeable(isTradeable);
	drop->doDrop(player->getPos());

	if (isTradeable) {
		// Drop is deleted otherwise, avoid like plague
		ReactorHandler::checkDrop(player, drop);
	}
}

auto InventoryHandler::useItem(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	reader.skip<tick_count_t>();
	inventory_slot_t slot = reader.get<inventory_slot_t>();
	item_id_t itemId = reader.get<item_id_t>();
	const slot_qty_t amountUsed = 1;
	if (player->getStats()->isDead() || player->getInventory()->getItemAmountBySlot(Inventories::UseInventory, slot) < amountUsed) {
		// Hacking
		return;
	}

	Item *item = player->getInventory()->getItem(Inventories::UseInventory, slot);
	if (item == nullptr || item->getId() != itemId) {
		// Hacking
		return;
	}

	if (!player->hasGmBenefits()) {
		Inventory::takeItemSlot(player, Inventories::UseInventory, slot, amountUsed);
	}

	Inventory::useItem(player, itemId);
}

auto InventoryHandler::cancelItem(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	item_id_t itemId = reader.get<item_id_t>();
	Buffs::endBuff(player, player->getActiveBuffs()->translateToSource(itemId));
}

auto InventoryHandler::useSkillbook(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	reader.skip<tick_count_t>();
	inventory_slot_t slot = reader.get<inventory_slot_t>();
	item_id_t itemId = reader.get<item_id_t>();

	Item *item = player->getInventory()->getItem(Inventories::UseInventory, slot);
	if (item == nullptr || item->getId() != itemId) {
		// Hacking
		return;
	}

	auto skillbookItems = ChannelServer::getInstance().getItemDataProvider().getItemSkills(itemId);
	if (skillbookItems == nullptr) {
		// Hacking
		return;
	}

	skill_id_t skillId = 0;
	skill_level_t newMaxLevel = 0;
	bool use = false;
	bool succeed = false;

	for (const auto &s : *skillbookItems) {
		skillId = s.skillId;
		newMaxLevel = s.maxLevel;
		if (GameLogicUtilities::itemSkillMatchesJob(skillId, player->getStats()->getJob())) {
			if (player->getSkills()->getSkillLevel(skillId) >= s.reqLevel) {
				if (player->getSkills()->getMaxSkillLevel(skillId) < newMaxLevel) {
					if (Randomizer::percentage<int8_t>() < s.chance) {
						player->getSkills()->setMaxSkillLevel(skillId, newMaxLevel);
						succeed = true;
					}

					Inventory::takeItemSlot(player, Inventories::UseInventory, slot, 1);
					break;
				}
			}
		}
	}

	if (skillId != 0) {
		player->sendMap(Packets::Inventory::useSkillbook(player->getId(), skillId, newMaxLevel, true, succeed));
	}
}

auto InventoryHandler::useChair(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	item_id_t chairId = reader.get<item_id_t>();
	player->setChair(chairId);
	player->sendMap(Packets::Inventory::sitChair(player->getId(), chairId));
}

auto InventoryHandler::handleChair(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	seat_id_t chair = reader.get<seat_id_t>();
	Map *map = player->getMap();
	if (chair == -1) {
		if (player->getChair() != 0) {
			player->setChair(0);
		}
		else {
			map->playerSeated(player->getMapChair(), nullptr);
			player->setMapChair(0);
		}
		map->send(Packets::Inventory::stopChair(player->getId(), false), player);
	}
	else {
		// Map chair
		if (map->seatOccupied(chair)) {
			map->send(Packets::Inventory::stopChair(player->getId(), true), player);
		}
		else {
			map->playerSeated(chair, player);
			player->setMapChair(chair);
			player->send(Packets::Inventory::sitMapChair(chair));
		}
	}
}

auto InventoryHandler::useSummonBag(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	reader.skip<tick_count_t>();
	inventory_slot_t slot = reader.get<inventory_slot_t>();
	item_id_t itemId = reader.get<item_id_t>();

	auto itemInfo = ChannelServer::getInstance().getItemDataProvider().getItemSummons(itemId);
	if (itemInfo == nullptr) {
		// Most likely hacking
		return;
	}

	Item *inventoryItem = player->getInventory()->getItem(Inventories::UseInventory, slot);
	if (inventoryItem == nullptr || inventoryItem->getId() != itemId) {
		// Hacking
		return;
	}

	Inventory::takeItemSlot(player, Inventories::UseInventory, slot, 1);

	for (const auto &bag : *itemInfo) {
		if (Randomizer::percentage<uint32_t>() < bag.chance) {
			if (ChannelServer::getInstance().getMobDataProvider().mobExists(bag.mobId)) {
				player->getMap()->spawnMob(bag.mobId, player->getPos());
			}
		}
	}
}

auto InventoryHandler::useReturnScroll(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	reader.skip<tick_count_t>();
	inventory_slot_t slot = reader.get<inventory_slot_t>();
	item_id_t itemId = reader.get<item_id_t>();

	Item *item = player->getInventory()->getItem(Inventories::UseInventory, slot);
	if (item == nullptr || item->getId() != itemId) {
		// Hacking
		return;
	}
	auto info = ChannelServer::getInstance().getItemDataProvider().getConsumeInfo(itemId);
	if (info == nullptr) {
		// Probably hacking
		return;
	}

	Inventory::takeItemSlot(player, Inventories::UseInventory, slot, 1);

	map_id_t map = info->moveTo;
	player->setMap(map == Vana::Maps::NoMap ? player->getMap()->getReturnMap() : map);
}

auto InventoryHandler::useScroll(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	reader.skip<tick_count_t>();
	inventory_slot_t slot = reader.get<inventory_slot_t>();
	inventory_slot_t equipSlot = reader.get<inventory_slot_t>();
	bool whiteScroll = (reader.get<int16_t>() == 2);
	bool legendarySpirit = reader.get<bool>();

	Item *item = player->getInventory()->getItem(Inventories::UseInventory, slot);
	Item *equip = player->getInventory()->getItem(Inventories::EquipInventory, equipSlot);
	if (item == nullptr || equip == nullptr) {
		// Most likely hacking
		return;
	}
	if (legendarySpirit && !player->getSkills()->hasLegendarySpirit()) {
		// Hacking
		return;
	}
	if (!legendarySpirit && equipSlot > 0) {
		// Hacking
		return;
	}

	item_id_t itemId = item->getId();
	int8_t succeed = -1;
	bool cursed = false;

	if (legendarySpirit && equip->getSlots() == 0) {
		// This is actually allowed by the game for some reason, the server is expected to send an error
	}
	else if (HackingResult::NotHacking != ChannelServer::getInstance().getItemDataProvider().scrollItem(ChannelServer::getInstance().getEquipDataProvider(), itemId, equip, whiteScroll, player->hasGmBenefits(), succeed, cursed)) {
		return;
	}

	if (succeed != -1) {
		if (whiteScroll) {
			Inventory::takeItem(player, Items::WhiteScroll, 1);
		}

		Inventory::takeItemSlot(player, Inventories::UseInventory, slot, 1);
		player->sendMap(Packets::Inventory::useScroll(player->getId(), succeed, cursed, legendarySpirit));

		if (!cursed) {
			player->getStats()->setEquip(equipSlot, equip);

			vector_t<InventoryPacketOperation> ops;
			ops.emplace_back(Packets::Inventory::OperationTypes::AddItem, equip, equipSlot);
			player->send(Packets::Inventory::inventoryOperation(true, ops));
		}
		else {
			vector_t<InventoryPacketOperation> ops;
			ops.emplace_back(Packets::Inventory::OperationTypes::ModifySlot, equip, equipSlot);
			player->send(Packets::Inventory::inventoryOperation(true, ops));

			player->getInventory()->deleteItem(Inventories::EquipInventory, equipSlot);
		}

		player->sendMap(Packets::Inventory::updatePlayer(player));
	}
	else {
		if (legendarySpirit) {
			player->sendMap(Packets::Inventory::useScroll(player->getId(), succeed, cursed, legendarySpirit));
		}
		player->send(Packets::Inventory::blankUpdate());
	}
}

auto InventoryHandler::useBuffItem(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	reader.skip<tick_count_t>();
	inventory_slot_t slot = reader.get<inventory_slot_t>();
	item_id_t itemId = reader.get<item_id_t>();
	Item *item = player->getInventory()->getItem(Inventories::UseInventory, slot);
	if (item == nullptr || item->getId() != itemId) {
		// Hacking
		return;
	}

	string_t targetName = reader.get<string_t>();
	if (auto target = ChannelServer::getInstance().getPlayerDataProvider().getPlayer(targetName)) {
		if (target == player) {
			// TODO FIXME packet
			// Need failure packet here (if it is a failure)
		}
		if (player->getMapId() != target->getMapId()) {
			// TODO FIXME packet
			// Need failure packet here
		}
		else {
			Inventory::useItem(target, itemId);
			Inventory::takeItem(player, itemId, 1);
		}
	}
	else {
		// TODO FIXME packet
		// Need failure packet here
	}
}

auto InventoryHandler::useCashItem(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	inventory_slot_t itemSlot = reader.get<inventory_slot_t>();
	item_id_t itemId = reader.get<item_id_t>();

	Item *test = player->getInventory()->getItem(Inventories::CashInventory, itemSlot);
	if (test == nullptr || test->getId() != itemId) {
		// Hacking
		return;
	}

	auto itemInfo = ChannelServer::getInstance().getItemDataProvider().getItemInfo(itemId);
	bool used = false;
	if (GameLogicUtilities::getItemType(itemId) == Items::Types::WeatherCash) {
		string_t message = reader.get<string_t>();
		reader.skip<tick_count_t>();
		if (message.length() <= 35) {
			Map *map = player->getMap();
			message = player->getName() + " 's message : " + message;
			used = map->createWeather(player, false, Items::WeatherTime, itemId, message);
		}
	}
	else if (GameLogicUtilities::getItemType(itemId) == Items::Types::CashPetFood) {
		Pet *pet = player->getPets()->getSummoned(0);
		if (pet != nullptr) {
			if (pet->getFullness() < Stats::MaxFullness) {
				player->send(Packets::Pets::showAnimation(player->getId(), pet, 1));
				pet->modifyFullness(Stats::MaxFullness, false);
				pet->addCloseness(100); // All cash pet food gives 100 closeness
				used = true;
			}
		}
	}
	else {
		switch (itemId) {
			case Items::TeleportRock:
			case Items::TeleportCoke:
			case Items::VipRock: // Only occurs when you actually try to move somewhere
				used = handleRockTeleport(player, itemId, reader);
				break;
			case Items::FirstJobSpReset:
			case Items::SecondJobSpReset:
			case Items::ThirdJobSpReset:
			case Items::FourthJobSpReset: {
				skill_id_t toSkill = reader.get<int32_t>();
				skill_id_t fromSkill = reader.get<int32_t>();
				if (!player->getSkills()->addSkillLevel(fromSkill, -1, true)) {
					// Hacking
					return;
				}
				if (!player->getSkills()->addSkillLevel(toSkill, 1, true)) {
					// Hacking
					return;
				}
				used = true;
				break;
			}
			case Items::ApReset: {
				int32_t toStat = reader.get<int32_t>();
				int32_t fromStat = reader.get<int32_t>();
				player->getStats()->addStat(toStat, 1, true);
				player->getStats()->addStat(fromStat, -1, true);
				used = true;
				break;
			}
			case Items::Megaphone: {
				string_t msg = player->getMedalName() + " : " + reader.get<string_t>();
				// In global, this sends to everyone on the current channel, not the map
				ChannelServer::getInstance().getPlayerDataProvider().send(Packets::Inventory::showMegaphone(msg));
				used = true;
				break;
			}
			case Items::SuperMegaphone: {
				string_t msg = player->getMedalName() + " : " + reader.get<string_t>();
				bool whisper = reader.get<bool>();
				auto &builder = Packets::Inventory::showSuperMegaphone(msg, whisper);
				ChannelServer::getInstance().sendWorld(
					Vana::Packets::prepend(builder, [](PacketBuilder &header) {
						header.add<header_t>(IMSG_TO_ALL_PLAYERS);
					}));
				used = true;
				break;
			}
			case Items::DiabloMessenger:
			case Items::Cloud9Messenger:
			case Items::LoveholicMessenger: {
				string_t msg1 = reader.get<string_t>();
				string_t msg2 = reader.get<string_t>();
				string_t msg3 = reader.get<string_t>();
				string_t msg4 = reader.get<string_t>();

				auto &builder = Packets::Inventory::showMessenger(player->getName(), msg1, msg2, msg3, msg4, reader.getBuffer(), reader.getBufferLength(), itemId);
				ChannelServer::getInstance().sendWorld(
					Vana::Packets::prepend(builder, [](PacketBuilder &header) {
						header.add<header_t>(IMSG_TO_ALL_PLAYERS);
					}));
				used = true;
				break;
			}
			case Items::ItemMegaphone: {
				string_t msg = player->getMedalName() + " : " + reader.get<string_t>();
				bool whisper = reader.get<bool>();
				Item *item = nullptr;
				if (reader.get<bool>()) {
					inventory_t inv = static_cast<inventory_t>(reader.get<int32_t>());
					inventory_slot_t slot = static_cast<inventory_slot_t>(reader.get<int32_t>());
					item = player->getInventory()->getItem(inv, slot);
					if (item == nullptr) {
						// Hacking
						return;
					}
				}

				auto &builder = Packets::Inventory::showItemMegaphone(msg, whisper, item);
				ChannelServer::getInstance().sendWorld(
					Vana::Packets::prepend(builder, [](PacketBuilder &header) {
						header.add<header_t>(IMSG_TO_ALL_PLAYERS);
					}));
				used = true;
				break;
			}
			case Items::ArtMegaphone: {
				int8_t lines = reader.get<int8_t>();
				if (lines < 1 || lines > 3) {
					// Hacking
					return;
				}
				string_t text[3];
				for (int8_t i = 0; i < lines; i++) {
					text[i] = player->getMedalName() + " : " + reader.get<string_t>();
				}

				bool whisper = reader.get<bool>();
				auto &builder = Packets::Inventory::showTripleMegaphone(lines, text[0], text[1], text[2], whisper);
				ChannelServer::getInstance().sendWorld(
					Vana::Packets::prepend(builder, [](PacketBuilder &header) {
						header.add<header_t>(IMSG_TO_ALL_PLAYERS);
					}));

				used = true;
				break;
			}
			case Items::PetNameTag: {
				string_t name = reader.get<string_t>();
				if (ChannelServer::getInstance().getValidCharDataProvider().isForbiddenName(name) ||
					ChannelServer::getInstance().getCurseDataProvider().isCurseWord(name)) {
					// Don't think it's hacking, but it should be forbidden
					return;
				}
				else {
					PetHandler::changeName(player, name);
					used = true;
				}
				break;
			}
			case Items::ItemNameTag: {
				inventory_slot_t slot = reader.get<inventory_slot_t>();
				if (slot != 0) {
					Item *item = player->getInventory()->getItem(Inventories::EquipInventory, slot);
					if (item == nullptr) {
						// Hacking or failure, dunno
						return;
					}
					item->setName(player->getName());

					vector_t<InventoryPacketOperation> ops;
					ops.emplace_back(Packets::Inventory::OperationTypes::AddItem, item, slot);
					player->send(Packets::Inventory::inventoryOperation(true, ops));
					used = true;
				}
				break;
			}
			case Items::ScissorsOfKarma:
			case Items::ItemLock: {
				inventory_t inv = static_cast<inventory_t>(reader.get<int32_t>());
				inventory_slot_t slot = static_cast<inventory_slot_t>(reader.get<int32_t>());
				if (slot != 0) {
					Item *item = player->getInventory()->getItem(inv, slot);
					if (item == nullptr) {
						// Hacking or failure, dunno
						return;
					}
					switch (itemId) {
						case Items::ItemLock:
							if (item->hasLock()) {
								// Hacking
								return;
							}
							item->setLock(true);
							break;
						case Items::ScissorsOfKarma: {
							auto equipInfo = ChannelServer::getInstance().getItemDataProvider().getItemInfo(item->getId());

							if (!equipInfo->karmaScissors) {
								// Hacking
								return;
							}
							if (item->hasTradeBlock() || item->hasKarma()) {
								// Hacking
								return;
							}
							item->setKarma(true);
							break;
						}
					}

					vector_t<InventoryPacketOperation> ops;
					ops.emplace_back(Packets::Inventory::OperationTypes::AddItem, item, slot);
					player->send(Packets::Inventory::inventoryOperation(true, ops));
					used = true;
				}
				break;
			}
			case Items::MapleTvMessenger:
			case Items::Megassenger: {
				bool hasReceiver = (reader.get<int8_t>() == 3);
				bool showWhisper = (itemId == Items::Megassenger ? reader.get<bool>() : false);
				auto receiver = ChannelServer::getInstance().getPlayerDataProvider().getPlayer(reader.get<string_t>());
				int32_t time = 15;

				if ((hasReceiver && receiver != nullptr) || (!hasReceiver && receiver == nullptr)) {
					string_t msg1 = reader.get<string_t>();
					string_t msg2 = reader.get<string_t>();
					string_t msg3 = reader.get<string_t>();
					string_t msg4 = reader.get<string_t>();
					string_t msg5 = reader.get<string_t>();
					reader.skip<tick_count_t>();

					ChannelServer::getInstance().getMapleTvs().addMessage(player, receiver, msg1, msg2, msg3, msg4, msg5, itemId - (itemId == Items::Megassenger ? 3 : 0), time);

					if (itemId == Items::Megassenger) {
						auto &builder = Packets::Inventory::showSuperMegaphone(player->getMedalName() + " : " + msg1 + msg2 + msg3 + msg4 + msg5, showWhisper);
						ChannelServer::getInstance().sendWorld(
							Vana::Packets::prepend(builder, [](PacketBuilder &header) {
								header.add<header_t>(IMSG_TO_ALL_PLAYERS);
							}));
					}
					used = true;
				}
				break;
			}
			case Items::MapleTvStarMessenger:
			case Items::StarMegassenger: {
				int32_t time = 30;
				bool showWhisper = (itemId == Items::StarMegassenger ? reader.get<bool>() : false);
				string_t msg1 = reader.get<string_t>();
				string_t msg2 = reader.get<string_t>();
				string_t msg3 = reader.get<string_t>();
				string_t msg4 = reader.get<string_t>();
				string_t msg5 = reader.get<string_t>();
				reader.skip<tick_count_t>();

				ChannelServer::getInstance().getMapleTvs().addMessage(player, nullptr, msg1, msg2, msg3, msg4, msg5, itemId - (itemId == Items::StarMegassenger ? 3 : 0), time);

				if (itemId == Items::StarMegassenger) {
					auto &builder = Packets::Inventory::showSuperMegaphone(player->getMedalName() + " : " + msg1 + msg2 + msg3 + msg4 + msg5, showWhisper);
					ChannelServer::getInstance().sendWorld(
						Vana::Packets::prepend(builder, [](PacketBuilder &header) {
							header.add<header_t>(IMSG_TO_ALL_PLAYERS);
						}));
				}
				used = true;
				break;
			}
			case Items::MapleTvHeartMessenger:
			case Items::HeartMegassenger: {
				bool showWhisper = (itemId == Items::HeartMegassenger ? reader.get<bool>() : false);
				string_t name = reader.get<string_t>();
				auto receiver = ChannelServer::getInstance().getPlayerDataProvider().getPlayer(name);
				int32_t time = 60;

				if (receiver != nullptr) {
					string_t msg1 = reader.get<string_t>();
					string_t msg2 = reader.get<string_t>();
					string_t msg3 = reader.get<string_t>();
					string_t msg4 = reader.get<string_t>();
					string_t msg5 = reader.get<string_t>();
					reader.skip<tick_count_t>();

					ChannelServer::getInstance().getMapleTvs().addMessage(player, receiver, msg1, msg2, msg3, msg4, msg5, itemId - (itemId == Items::HeartMegassenger ? 3 : 0), time);

					if (itemId == Items::HeartMegassenger) {
						auto &builder = Packets::Inventory::showSuperMegaphone(player->getMedalName() + " : " + msg1 + msg2 + msg3 + msg4 + msg5, showWhisper);
						ChannelServer::getInstance().sendWorld(
							Vana::Packets::prepend(builder, [](PacketBuilder &header) {
								header.add<header_t>(IMSG_TO_ALL_PLAYERS);
							}));
					}
					used = true;
				}
				break;
			}
			case Items::BronzeSackOfMesos:
			case Items::SilverSackOfMesos:
			case Items::GoldSackOfMesos: {
				mesos_t mesos = itemInfo->mesos;
				if (!player->getInventory()->modifyMesos(mesos)) {
					player->send(Packets::Inventory::sendMesobagFailed());
				}
				else {
					player->send(Packets::Inventory::sendMesobagSucceed(mesos));
					used = true;
				}
				break;
			}
			case Items::Chalkboard:
			case Items::Chalkboard2: {
				string_t msg = reader.get<string_t>();
				player->setChalkboard(msg);
				player->sendMap(Packets::Inventory::sendChalkboardUpdate(player->getId(), msg));
				break;
			}
			case Items::FungusScroll:
			case Items::OinkerDelight:
			case Items::ZetaNightmare:
				Inventory::useItem(player, itemId);
				used = true;
				break;
			case Items::ViciousHammer: {
				inventory_t inv = static_cast<inventory_t>(reader.get<int32_t>());
				inventory_slot_t slot = static_cast<inventory_slot_t>(reader.get<int32_t>());
				Item *item = player->getInventory()->getItem(inv, slot);
				if (item == nullptr || item->getHammers() == Items::MaxHammers || ChannelServer::getInstance().getEquipDataProvider().getSlots(item->getId()) == 0) {
					// Hacking, probably
					return;
				}
				item->incHammers();
				item->incSlots();
				player->send(Packets::Inventory::sendHammerSlots(item->getHammers()));
				player->getInventory()->setHammerSlot(slot);
				used = true;
				break;
			}
			case Items::CongratulatorySong:
				player->sendMap(Packets::Inventory::playCashSong(itemId, player->getName()));
				used = true;
				break;
		}
	}
	if (used) {
		Inventory::takeItem(player, itemId, 1);
	}
	else {
		player->send(Packets::Inventory::blankUpdate());
	}
}

auto InventoryHandler::useItemEffect(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	item_id_t itemId = reader.get<item_id_t>();
	if (player->getInventory()->getItemAmount(itemId) == 0) {
		// Hacking
		return;
	}
	player->setItemEffect(itemId);
	player->sendMap(Packets::Inventory::useItemEffect(player->getId(), itemId));
}

auto InventoryHandler::handleRockFunctions(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	int8_t mode = reader.get<int8_t>();
	int8_t type = reader.get<int8_t>();

	enum Modes : int8_t {
		Remove = 0x00,
		Add = 0x01
	};

	if (mode == Remove) {
		map_id_t map = reader.get<map_id_t>();
		player->getInventory()->delRockMap(map, type);
	}
	else if (mode == Add) {
		map_id_t map = player->getMapId();
		Map *m = Maps::getMap(map);
		if (m->canVip() && m->getContinent() != 0) {
			player->getInventory()->addRockMap(map, type);
		}
		else {
			// Hacking, the client doesn't allow this to occur
			player->send(Packets::Inventory::sendRockError(Packets::Inventory::RockErrors::CannotSaveMap, type));
		}
	}
}

auto InventoryHandler::handleRockTeleport(ref_ptr_t<Player> player, item_id_t itemId, PacketReader &reader) -> bool {
	if (itemId == Items::SpecialTeleportRock) {
		inventory_slot_t slot = reader.get<inventory_slot_t>();
		item_id_t postedItemId = reader.get<item_id_t>();

		Item *item = player->getInventory()->getItem(Inventories::UseInventory, slot);
		if (item == nullptr || item->getId() != itemId || itemId != postedItemId) {
			// Hacking
			return false;
		}
	}

	int8_t type = itemId != Items::VipRock ?
		Packets::Inventory::RockTypes::Regular :
		Packets::Inventory::RockTypes::Vip;

	bool used = false;
	int8_t mode = reader.get<int8_t>();
	map_id_t targetMapId = -1;

	enum Modes : int8_t {
		PresetMap = 0x00,
		Ign = 0x01
	};
	if (mode == PresetMap) {
		targetMapId = reader.get<map_id_t>();
		if (!player->getInventory()->ensureRockDestination(targetMapId)) {
			// Hacking
			return false;
		}
	}
	else if (mode == Ign) {
		string_t targetName = reader.get<string_t>();
		auto target = ChannelServer::getInstance().getPlayerDataProvider().getPlayer(targetName);
		if (target != nullptr && target != player) {
			targetMapId = target->getMapId();
		}
		else if (target == nullptr) {
			player->send(Packets::Inventory::sendRockError(Packets::Inventory::RockErrors::DifficultToLocate, type));
		}
		else if (target == player) {
			// Hacking
			return false;
		}
	}
	if (targetMapId != -1) {
		Map *destination = Maps::getMap(targetMapId);
		Map *origin = player->getMap();
		if (!destination->canVip()) {
			player->send(Packets::Inventory::sendRockError(Packets::Inventory::RockErrors::CannotGo, type));
		}
		else if (!origin->canVip()) {
			player->send(Packets::Inventory::sendRockError(Packets::Inventory::RockErrors::CannotGo, type));
		}
		else if (player->getMapId() == targetMapId) {
			player->send(Packets::Inventory::sendRockError(Packets::Inventory::RockErrors::AlreadyThere, type));
		}
		else if (type == 0 && destination->getContinent() != origin->getContinent()) {
			player->send(Packets::Inventory::sendRockError(Packets::Inventory::RockErrors::CannotGo, type));
		}
		else if (player->getStats()->getLevel() < 7 && origin->getContinent() == 0 && destination->getContinent() != 0) {
			player->send(Packets::Inventory::sendRockError(Packets::Inventory::RockErrors::NoobsCannotLeaveMapleIsland, type));
		}
		else {
			player->setMap(targetMapId);
			used = true;
		}
	}

	reader.skip<tick_count_t>();

	if (itemId == Items::SpecialTeleportRock) {
		if (used) {
			Inventory::takeItem(player, itemId, 1);
		}
		else {
			player->send(Packets::Inventory::blankUpdate());
		}
	}
	return used;
}

auto InventoryHandler::handleHammerTime(ref_ptr_t<Player> player) -> void {
	if (!player->getInventory()->isHammering()) {
		// Hacking
		return;
	}
	inventory_slot_t hammerSlot = player->getInventory()->getHammerSlot();
	Item *item = player->getInventory()->getItem(Inventories::EquipInventory, hammerSlot);
	player->send(Packets::Inventory::sendHammerUpdate());
	player->send(Packets::Inventory::sendHulkSmash(hammerSlot, item));
	player->getInventory()->setHammerSlot(-1);
}

auto InventoryHandler::handleRewardItem(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	inventory_slot_t slot = reader.get<inventory_slot_t>();
	item_id_t itemId = reader.get<item_id_t>();
	Item *item = player->getInventory()->getItem(Inventories::UseInventory, slot);
	if (item == nullptr || item->getId() != itemId) {
		// Hacking or hacking failure
		player->send(Packets::Inventory::blankUpdate());
		return;
	}

	auto rewards = ChannelServer::getInstance().getItemDataProvider().getItemRewards(itemId);
	if (rewards == nullptr) {
		// Hacking or no information in the database
		player->send(Packets::Inventory::blankUpdate());
		return;
	}

	auto reward = Randomizer::select(rewards);
	Inventory::takeItem(player, itemId, 1);
	Item *rewardItem = new Item(reward->rewardId, reward->quantity);
	Inventory::addItem(player, rewardItem, true);
	player->sendMap(Packets::Inventory::sendRewardItemAnimation(player->getId(), itemId, reward->effect));
}

auto InventoryHandler::handleScriptItem(ref_ptr_t<Player> player, PacketReader &reader) -> void {
	if (player->getNpc() != nullptr || player->getShop() != 0 || player->getTradeId() != 0) {
		// Hacking
		player->send(Packets::Inventory::blankUpdate());
		return;
	}

	reader.skip<tick_count_t>();
	inventory_slot_t slot = reader.get<inventory_slot_t>();
	item_id_t itemId = reader.get<item_id_t>();

	Item *item = player->getInventory()->getItem(Inventories::UseInventory, slot);
	if (item == nullptr || item->getId() != itemId) {
		// Hacking or hacking failure
		player->send(Packets::Inventory::blankUpdate());
		return;
	}

	auto &channel = ChannelServer::getInstance();
	string_t scriptName = channel.getScriptDataProvider().getScript(&channel, itemId, ScriptTypes::Item);
	if (scriptName.empty()) {
		// Hacking or no script for item found
		player->send(Packets::Inventory::blankUpdate());
		return;
	}

	npc_id_t npcId = channel.getItemDataProvider().getItemInfo(itemId)->npc;

	// Let's run the NPC
	Npc *npc = new Npc{npcId, player, scriptName};
	if (!npc->checkEnd()) {
		// NPC is running/script found
		// Delete the item used
		Inventory::takeItem(player, itemId, 1);
		npc->run();
	}
	else {
		// NPC didn't run/no script found
		player->send(Packets::Inventory::blankUpdate());
	}
}

}
}