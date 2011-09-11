/*
Copyright (C) 2008-2011 Vana Development Team

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
#include "PlayerInventory.h"
#include "Database.h"
#include "EquipDataProvider.h"
#include "GameLogicUtilities.h"
#include "Inventory.h"
#include "InventoryPacket.h"
#include "InventoryPacketHelper.h"
#include "ItemConstants.h"
#include "ItemDataProvider.h"
#include "MiscUtilities.h"
#include "PacketCreator.h"
#include "Pet.h"
#include "PetsPacket.h"
#include "Player.h"
#include "PlayerPacket.h"
#include "PlayerPacketHelper.h"
#include "TimeUtilities.h"

PlayerInventory::PlayerInventory(Player *player, const boost::array<uint8_t, Inventories::InventoryCount> &maxSlots, int32_t mesos) :
	m_maxSlots(maxSlots),
	m_mesos(mesos),
	m_hammer(-1),
	m_player(player)
{
	boost::array<int32_t, 2> g = {0};

	for (size_t i = 0; i < Inventories::EquippedSlots; i++) {
		m_equipped[i] = g;
	}

	load();
}

PlayerInventory::~PlayerInventory() {
	typedef boost::array<ItemInventory, Inventories::InventoryCount> ItemInvArr;
	for (ItemInvArr::iterator iter = m_items.begin(); iter != m_items.end(); iter++) {
		std::for_each(iter->begin(), iter->end(), MiscUtilities::DeleterPairAssoc<ItemInventory::value_type>());
	}
}

void PlayerInventory::load() {
	mysqlpp::Query query = Database::getCharDb().query();
	query << "SELECT i.*, p.* "
			<< "FROM items i "
			<< "LEFT JOIN pets p ON i.pet_id = p.pet_id "
			<< "WHERE i.location = " << mysqlpp::quote << "inventory" << " AND i.character_id = " << m_player->getId();
	mysqlpp::StoreQueryResult res = query.store();

	enum TableFields {
		ItemCharId = 0,
		Inv, Slot, Location, UserId, WorldId,
		ItemId, Amount, Slots, Scrolls, iStr,
		iDex, iInt, iLuk, iHp, iMp,
		iWatk, iMatk, iWdef, iMdef, iAcc,
		iAvo, iHand, iSpeed, iJump, Flags,
		Hammers, PetId, Name, ExpirationTime,
		// End of items, start of pets
		PetsId, Index, PetName, Level, Closeness,
		Fullness
	};

	Item *item;
	string temp;
	for (size_t i = 0; i < res.num_rows(); ++i) {
		mysqlpp::Row &row = res[i];
		item = new Item(row[ItemId]);
		item->setAmount(row[Amount]);
		item->setSlots(static_cast<int8_t>(row[Slots]));
		item->setScrolls(static_cast<int8_t>(row[Scrolls]));
		item->setStr(row[iStr]);
		item->setDex(row[iDex]);
		item->setInt(row[iInt]);
		item->setLuk(row[iLuk]);
		item->setHp(row[iHp]);
		item->setMp(row[iMp]);
		item->setWatk(row[iWatk]);
		item->setMatk(row[iMatk]);
		item->setWdef(row[iWdef]);
		item->setMdef(row[iMdef]);
		item->setAccuracy(row[iAcc]);
		item->setAvoid(row[iAvo]);
		item->setHands(row[iHand]);
		item->setSpeed(row[iSpeed]);
		item->setJump(row[iJump]);
		item->setPetId(row[PetId]);
		item->setFlags(static_cast<int16_t>(row[Flags]));
		item->setHammers(row[Hammers]);
		item->setExpirationTime(row[ExpirationTime]);
		row[Name].to_string(temp);
		item->setName(temp);

		addItem(static_cast<int8_t>(row[Inv]), row[Slot], item, true);
		if (item->getPetId() != 0) {
			Pet *pet = new Pet(
				m_player,
				item,
				static_cast<int8_t>(row[Index]),
				static_cast<string>(row[PetName]),
				static_cast<uint8_t>(row[Level]),
				static_cast<int16_t>(row[Closeness]),
				static_cast<uint8_t>(row[Fullness]),
				static_cast<uint8_t>(row[Slot])
			);
			m_player->getPets()->addPet(pet);
		}
	}

	query << "SELECT t.map_index, t.map_id FROM teleport_rock_locations t WHERE t.character_id = " << m_player->getId();
	res = query.store();

	for (size_t i = 0; i < res.num_rows(); ++i) {
		int8_t index = static_cast<int8_t>(res[i][0]);
		int32_t mapId = res[i][1];

		if (index >= Inventories::TeleportRockMax) {
			m_vipLocations.push_back(mapId);
		}
		else {
			m_rockLocations.push_back(mapId);
		}
	}
}

void PlayerInventory::save() {
	mysqlpp::Query query = Database::getCharDb().query();
	query << "DELETE FROM items WHERE location = " << mysqlpp::quote << "inventory" << " AND character_id = " << m_player->getId();
	query.exec();

	bool firstRun = true;
	for (int8_t i = Inventories::EquipInventory; i <= Inventories::InventoryCount; i++) {
		ItemInventory &itemsinv = m_items[i - 1];
		for (ItemInventory::iterator iter = itemsinv.begin(); iter != itemsinv.end(); iter++) {
			Item *item = iter->second;
			if (firstRun) {
				query << "INSERT INTO items VALUES (";
				firstRun = false;
			}
			else {
				query << ",(";
			}
			query << m_player->getId() << ","
				<< static_cast<int16_t>(i) << ","
				<< iter->first << ","
				<< mysqlpp::quote << "inventory" << ","
				<< m_player->getUserId() << ","
				<< static_cast<int16_t>(m_player->getWorldId()) << ","
				<< item->getId() << ","
				<< item->getAmount() << ","
				<< static_cast<int16_t>(item->getSlots()) << ","
				<< static_cast<int16_t>(item->getScrolls()) << ","
				<< item->getStr() << ","
				<< item->getDex() << ","
				<< item->getInt() << ","
				<< item->getLuk() << ","
				<< item->getHp() << ","
				<< item->getMp() << ","
				<< item->getWatk() << ","
				<< item->getMatk() << ","
				<< item->getWdef() << ","
				<< item->getMdef() << ","
				<< item->getAccuracy() << ","
				<< item->getAvoid() << ","
				<< item->getHands() << ","
				<< item->getSpeed() << ","
				<< item->getJump() << ","
				<< item->getFlags() << ","
				<< item->getHammers() << ","
				<< item->getPetId() << ","
				<< mysqlpp::quote << item->getName() << ","
				<< item->getExpirationTime() << ")";
		}
	}
	if (!firstRun) {
		query.exec();
	}
	query << "DELETE FROM teleport_rock_locations WHERE character_id = " << m_player->getId();
	query.exec();

	for (size_t i = 0; i < m_rockLocations.size(); i++) {
		int32_t mapId = m_rockLocations[i];
		query << "INSERT INTO teleport_rock_locations VALUES ("
				<< m_player->getId() << ","
				<< static_cast<uint16_t>(i) << ","
				<< mapId << ")";
		query.exec();
	}
	for (size_t i = 0; i < m_vipLocations.size(); i++) {
		int32_t mapId = m_vipLocations[i];
		query << "INSERT INTO teleport_rock_locations VALUES ("
				<< m_player->getId() << ","
				<< static_cast<uint16_t>(i + Inventories::TeleportRockMax) << ","
				<< mapId << ")";
		query.exec();
	}
}

void PlayerInventory::addMaxSlots(int8_t inventory, int8_t rows) {
	inventory -= 1;
	m_maxSlots[inventory] += (rows * 4);
	if (m_maxSlots[inventory] > 100) {
		m_maxSlots[inventory] = 100;
	}
	if (m_maxSlots[inventory] < 24) {
		// Retard
		m_maxSlots[inventory] = 24;
	}
	InventoryPacket::updateSlots(m_player, inventory + 1, m_maxSlots[inventory]);
}

void PlayerInventory::setMesos(int32_t mesos, bool sendPacket) {
	if (mesos < 0) {
		mesos = 0;
	}
	m_mesos = mesos;
	PlayerPacket::updateStat(m_player, Stats::Mesos, m_mesos, sendPacket);
}

bool PlayerInventory::modifyMesos(int32_t mod, bool sendPacket) {
	if (mod < 0) {
		if (-mod > m_mesos) {
			return false;
		}
		m_mesos += mod;
	}
	else {
		int32_t mesotest = m_mesos + mod;
		if (mesotest < 0) {
			return false;
		}
		m_mesos = mesotest;
	}
	PlayerPacket::updateStat(m_player, Stats::Mesos, m_mesos, sendPacket);
	return true;
}

void PlayerInventory::addItem(int8_t inv, int16_t slot, Item *item, bool isLoading) {
	m_items[inv - 1][slot] = item;
	int32_t itemId = item->getId();
	if (m_itemAmounts.find(itemId) != m_itemAmounts.end()) {
		m_itemAmounts[itemId] += item->getAmount();
	}
	else {
		m_itemAmounts[itemId] = item->getAmount();
	}
	if (slot < 0) {
		addEquipped(slot, itemId);
		m_player->getStats()->setEquip(slot, item, isLoading);
	}
}

Item * PlayerInventory::getItem(int8_t inv, int16_t slot) {
	if (!GameLogicUtilities::isValidInventory(inv)) {
		return nullptr;
	}
	inv -= 1;
	if (m_items[inv].find(slot) != m_items[inv].end()) {
		return m_items[inv][slot];
	}
	return nullptr;
}

void PlayerInventory::deleteItem(int8_t inv, int16_t slot, bool updateAmount) {
	inv -= 1;
	if (m_items[inv].find(slot) != m_items[inv].end()) {
		if (updateAmount) {
			Item *x = m_items[inv][slot];
			m_itemAmounts[x->getId()] -= x->getAmount();
		}
		if (slot < 0) {
			addEquipped(slot, 0);
			m_player->getStats()->setEquip(slot, nullptr);
		}
		delete m_items[inv][slot];
		m_items[inv].erase(slot);
	}
}

void PlayerInventory::setItem(int8_t inv, int16_t slot, Item *item) {
	inv -= 1;
	if (item == nullptr) {
		m_items[inv].erase(slot);
		if (slot < 0) {
			addEquipped(slot, 0);
			m_player->getStats()->setEquip(slot, nullptr);
		}
	}
	else {
		m_items[inv][slot] = item;
		if (slot < 0) {
			addEquipped(slot, item->getId());
			m_player->getStats()->setEquip(slot, item);
		}
	}
}

int16_t PlayerInventory::getItemAmountBySlot(int8_t inv, int16_t slot) {
	inv -= 1;
	return (m_items[inv].find(slot) != m_items[inv].end() ? m_items[inv][slot]->getAmount() : 0);
}

void PlayerInventory::addEquipped(int16_t slot, int32_t itemId) {
	if (abs(slot) == EquipSlots::Mount) {
		m_player->getMounts()->setCurrentMount(itemId);
	}

	int8_t cash = GameLogicUtilities::isCashSlot(slot) ? 1 : 0;
	m_equipped[GameLogicUtilities::stripCashSlot(slot)][cash] = itemId;
}

int32_t PlayerInventory::getEquippedId(int16_t slot, bool cash) {
	return m_equipped[slot][(cash ? 1 : 0)];
}

void PlayerInventory::addEquippedPacket(PacketCreator &packet) {
	for (int8_t i = 0; i < Inventories::EquippedSlots; i++) {
		// Shown items
		if (m_equipped[i][0] > 0 || m_equipped[i][1] > 0) {
			packet.add<int8_t>(i);
			if (m_equipped[i][1] <= 0 || (i == EquipSlots::Weapon && m_equipped[i][0] > 0)) {
				// Normal weapons always here
				packet.add<int32_t>(m_equipped[i][0]);
			}
			else {
				packet.add<int32_t>(m_equipped[i][1]);
			}
		}
	}
	packet.add<int8_t>(-1);
	for (int8_t i = 0; i < Inventories::EquippedSlots; i++) {
		// Covered items
		if (m_equipped[i][1] > 0 && m_equipped[i][0] > 0 && i != EquipSlots::Weapon) {
			packet.add<int8_t>(i);
			packet.add<int32_t>(m_equipped[i][0]);
		}
	}
	packet.add<int8_t>(-1);
	packet.add<int32_t>(m_equipped[EquipSlots::Weapon][1]); // Cash weapon
}

uint16_t PlayerInventory::getItemAmount(int32_t itemId) {
	return m_itemAmounts.find(itemId) != m_itemAmounts.end() ? m_itemAmounts[itemId] : 0;
}

bool PlayerInventory::isEquippedItem(int32_t itemId) {
	ItemInventory &equips = m_items[Inventories::EquipInventory - 1];
	bool has = false;
	for (ItemInventory::iterator iter = equips.begin(); iter != equips.end(); iter++) {
		if (iter->first == itemId) {
			has = true;
			break;
		}
	}
	return has;
}

bool PlayerInventory::hasOpenSlotsFor(int32_t itemId, int16_t amount, bool canStack) {
	int16_t required = 0;
	int8_t inv = GameLogicUtilities::getInventory(itemId);
	if (!GameLogicUtilities::isStackable(itemId)) {
		required = amount; // These aren't stackable
	}
	else {
		int16_t maxSlot = ItemDataProvider::Instance()->getMaxSlot(itemId);
		uint16_t existing = getItemAmount(itemId) % maxSlot;
		// Bug in global:
		// It doesn't matter if you already have a slot with a partial stack or not, non-shops require at least 1 empty slot
		if (canStack && existing > 0) {
			// If not, calculate how many slots necessary
			existing += amount;
			if (existing > maxSlot) {
				// Only have to bother with required slots if it would put us over the limit of a slot
				required = static_cast<int16_t>(existing / maxSlot);
				if ((existing % maxSlot) > 0) {
					required += 1;
				}
			}
		}
		else {
			// If it is, treat it as though no items exist at all
			required = static_cast<int16_t>(amount / maxSlot);
			if ((amount % maxSlot) > 0) {
				required += 1;
			}
		}
	}
	return getOpenSlotsNum(inv) >= required;
}

int16_t PlayerInventory::getOpenSlotsNum(int8_t inv) {
	int16_t openSlots = 0;
	for (int16_t i = 1; i <= getMaxSlots(inv); i++) {
		if (getItem(inv, i) == nullptr) {
			openSlots++;
		}
	}
	return openSlots;
}

int32_t PlayerInventory::doShadowStars() {
	for (int16_t s = 1; s <= getMaxSlots(Inventories::UseInventory); s++) {
		Item *item = getItem(Inventories::UseInventory, s);
		if (item == nullptr) {
			continue;
		}
		if (GameLogicUtilities::isStar(item->getId()) && item->getAmount() >= Items::ShadowStarsCost) {
			Inventory::takeItemSlot(m_player, Inventories::UseInventory, s, Items::ShadowStarsCost);
			return item->getId();
		}
	}
	return 0;
}
void PlayerInventory::addRockMap(int32_t mapId, int8_t type) {
	const int8_t mode = InventoryPacket::RockModes::Add;
	if (type == InventoryPacket::RockTypes::Regular) {
		if (m_rockLocations.size() < Inventories::TeleportRockMax) {
			m_rockLocations.push_back(mapId);
		}
		InventoryPacket::sendRockUpdate(m_player, mode, type, m_rockLocations);
	}
	else if (type == InventoryPacket::RockTypes::Vip) {
		if (m_vipLocations.size() < Inventories::VipRockMax) {
			m_vipLocations.push_back(mapId);
			// Want packet
		}
		InventoryPacket::sendRockUpdate(m_player, mode, type, m_vipLocations);
	}
}

void PlayerInventory::delRockMap(int32_t mapId, int8_t type) {
	const int8_t mode = InventoryPacket::RockModes::Delete;
	if (type == InventoryPacket::RockTypes::Regular) {
		for (size_t k = 0; k < m_rockLocations.size(); k++) {
			if (m_rockLocations[k] == mapId) {
				m_rockLocations.erase(m_rockLocations.begin() + k);
				InventoryPacket::sendRockUpdate(m_player, mode, type, m_rockLocations);
				break;
			}
		}
	}
	else if (type == InventoryPacket::RockTypes::Vip) {
		for (size_t k = 0; k < m_vipLocations.size(); k++) {
			if (m_vipLocations[k] == mapId) {
				m_vipLocations.erase(m_vipLocations.begin() + k);
				InventoryPacket::sendRockUpdate(m_player, mode, type, m_vipLocations);
				break;
			}
		}
	}
}

bool PlayerInventory::ensureRockDestination(int32_t mapId) {
	for (size_t k = 0; k < m_rockLocations.size(); k++) {
		if (m_rockLocations[k] == mapId) {
			return true;
		}
	}
	for (size_t k = 0; k < m_vipLocations.size(); k++) {
		if (m_vipLocations[k] == mapId) {
			return true;
		}
	}
	return false;
}

void PlayerInventory::addWishListItem(int32_t itemId) {
	m_wishlist.push_back(itemId);
}

void PlayerInventory::connectData(PacketCreator &packet) {
	packet.add<int32_t>(m_mesos);

	for (uint8_t i = Inventories::EquipInventory; i <= Inventories::InventoryCount; i++) {
		packet.add<int8_t>(getMaxSlots(i));
	}

	// Go through equips
	ItemInventory &equips = m_items[Inventories::EquipInventory - 1];
	ItemInventory::iterator iter;
	for (iter = equips.begin(); iter != equips.end(); ++iter) {
		if (iter->first < 0 && iter->first > -100) {
			PlayerPacketHelper::addItemInfo(packet, iter->first, iter->second);
		}
	}
	packet.add<int8_t>(0);
	for (iter = equips.begin(); iter != equips.end(); ++iter) {
		if (iter->first < -100) {
			PlayerPacketHelper::addItemInfo(packet, iter->first, iter->second);
		}
	}
	packet.add<int8_t>(0);
	for (iter = equips.begin(); iter != equips.end(); ++iter) {
		if (iter->first > 0) {
			PlayerPacketHelper::addItemInfo(packet, iter->first, iter->second);
		}
	}
	packet.add<int8_t>(0);

	// Equips done, do rest of user's items starting with Use
	for (int8_t i = Inventories::UseInventory; i <= Inventories::InventoryCount; i++) {
		for (int16_t s = 1; s <= getMaxSlots(i); s++) {
			Item *item = getItem(i, s);
			if (item == nullptr) {
				continue;
			}
			if (item->getPetId() == 0) {
				PlayerPacketHelper::addItemInfo(packet, s, item);
			}
			else {
				Pet *pet = m_player->getPets()->getPet(item->getPetId());
				packet.add<int8_t>(static_cast<int8_t>(s));
				PetsPacket::addInfo(packet, pet);
			}
		}
		packet.add<int8_t>(0);
	}
}

void PlayerInventory::rockPacket(PacketCreator &packet) {
	InventoryPacketHelper::fillRockPacket(packet, m_rockLocations, Inventories::TeleportRockMax);
	InventoryPacketHelper::fillRockPacket(packet, m_vipLocations, Inventories::VipRockMax);
}

void PlayerInventory::wishListPacket(PacketCreator &packet) {
	packet.add<uint8_t>(m_wishlist.size());
	for (size_t i = 0; i < m_wishlist.size(); i++) {
		packet.add<int32_t>(m_wishlist[i]);
	}
}

void PlayerInventory::checkExpiredItems() {
	vector<int32_t> expiredItemIds;
	for (int8_t i = Inventories::EquipInventory; i <= Inventories::InventoryCount; i++) {
		for (int16_t s = 1; s <= getMaxSlots(i); s++) {
			Item *item = getItem(i, s);

			if (item != nullptr && item->getExpirationTime() != Items::NoExpiration && item->getExpirationTime() <= TimeUtilities::getServerTime()) {
				expiredItemIds.push_back(item->getId());
				Inventory::takeItemSlot(m_player, i, s, item->getAmount());
			}
		}
	}
	if (expiredItemIds.size() > 0) {
		InventoryPacket::sendItemExpired(m_player, &expiredItemIds);
	}
}