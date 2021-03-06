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
#include "PlayerStorage.hpp"
#include "Common/Algorithm.hpp"
#include "Common/Database.hpp"
#include "Common/GameConstants.hpp"
#include "Common/GameLogicUtilities.hpp"
#include "Common/MiscUtilities.hpp"
#include "ChannelServer/ChannelServer.hpp"
#include "ChannelServer/Inventory.hpp"
#include "ChannelServer/Player.hpp"
#include "ChannelServer/StoragePacket.hpp"
#include <algorithm>

namespace Vana {
namespace ChannelServer {

PlayerStorage::PlayerStorage(Player *player) :
	m_player{player}
{
	load();
}

PlayerStorage::~PlayerStorage() {
	/* TODO FIXME just convert the damn Item * to ref_ptr_t or owned_ptr_t */
	std::for_each(std::begin(m_items), std::end(m_items), [](Item *item) { delete item; });
}

auto PlayerStorage::takeItem(storage_slot_t slot) -> void {
	auto iter = std::begin(m_items) + slot;
	delete *iter;
	m_items.erase(iter);
}

auto PlayerStorage::setSlots(storage_slot_t slots) -> void {
	m_slots = ext::constrain_range(slots, Inventories::MinSlotsStorage, Inventories::MaxSlotsStorage);
}

auto PlayerStorage::addItem(Item *item) -> void {
	inventory_t inv = GameLogicUtilities::getInventory(item->getId());
	storage_slot_t i;
	for (i = 0; i < m_items.size(); ++i) {
		if (GameLogicUtilities::getInventory(m_items[i]->getId()) > inv) {
			break;
		}
	}
	m_items.insert(std::begin(m_items) + i, item);
}

auto PlayerStorage::getNumItems(inventory_t inv) -> storage_slot_t {
	storage_slot_t itemNum = 0;
	for (storage_slot_t i = 0; i < m_items.size(); ++i) {
		if (GameLogicUtilities::getInventory(m_items[i]->getId()) == inv) {
			itemNum++;
		}
	}
	return itemNum;
}

auto PlayerStorage::changeMesos(mesos_t mesos) -> void {
	m_mesos -= mesos;
	m_player->send(Packets::Storage::changeMesos(getSlots(), m_mesos));
}

auto PlayerStorage::load() -> void {
	auto &db = Database::getCharDb();
	auto &sql = db.getSession();
	soci::row row;
	account_id_t accountId = m_player->getAccountId();
	world_id_t worldId = m_player->getWorldId();

	sql.once
		<< "SELECT s.slots, s.mesos, s.char_slots "
		<< "FROM " << db.makeTable("storage") << " s "
		<< "WHERE s.account_id = :account AND s.world_id = :world "
		<< "LIMIT 1",
		soci::use(accountId, "account"),
		soci::use(worldId, "world"),
		soci::into(row);

	if (sql.got_data()) {
		m_slots = row.get<storage_slot_t>("slots");
		m_mesos = row.get<mesos_t>("mesos");
		m_charSlots = row.get<int32_t>("char_slots");
	}
	else {
		auto &config = ChannelServer::getInstance().getConfig();
		m_slots = config.defaultStorageSlots;
		m_mesos = 0;
		m_charSlots = config.defaultChars;
		sql.once
			<< "INSERT INTO " << db.makeTable("storage") << " (account_id, world_id, slots, mesos, char_slots) "
			<< "VALUES (:account, :world, :slots, :mesos, :chars)",
			soci::use(accountId, "account"),
			soci::use(worldId, "world"),
			soci::use(m_slots, "slots"),
			soci::use(m_mesos, "mesos"),
			soci::use(m_charSlots, "chars");
	}

	m_items.reserve(m_slots);

	string_t location = "storage";

	soci::rowset<> rs = (sql.prepare
		<< "SELECT i.* "
		<< "FROM " << db.makeTable("items") << " i "
		<< "WHERE i.location = :location AND i.account_id = :account AND i.world_id = :world "
		<< "ORDER BY i.slot ASC",
		soci::use(location, "location"),
		soci::use(accountId, "account"),
		soci::use(worldId, "world"));

	for (const auto &row : rs) {
		Item *item = new Item{row};
		addItem(item);
	}
}

auto PlayerStorage::save() -> void {
	using namespace soci;
	world_id_t worldId = m_player->getWorldId();
	account_id_t accountId = m_player->getAccountId();
	player_id_t playerId = m_player->getId();

	auto &db = Database::getCharDb();
	auto &sql = db.getSession();
	sql.once
		<< "UPDATE " << db.makeTable("storage") << " "
		<< "SET slots = :slots, mesos = :mesos, char_slots = :chars "
		<< "WHERE account_id = :account AND world_id = :world",
		use(accountId, "account"),
		use(worldId, "world"),
		use(m_slots, "slots"),
		use(m_mesos, "mesos"),
		use(m_charSlots, "chars");

	sql.once
		<< "DELETE FROM " << db.makeTable("items") << " "
		<< "WHERE location = :location AND account_id = :account AND world_id = :world",
		use(Item::Storage, "location"),
		use(accountId, "account"),
		use(worldId, "world");

	storage_slot_t max = getNumItems();

	if (max > 0) {
		vector_t<ItemDbRecord> v;
		for (storage_slot_t i = 0; i < max; ++i) {
			ItemDbRecord rec{i, playerId, accountId, worldId, Item::Storage, getItem(i)};
			v.push_back(rec);
		}
		Item::databaseInsert(db, v);
	}
}

}
}