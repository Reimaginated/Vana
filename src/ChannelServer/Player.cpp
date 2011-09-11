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
#include "Player.h"
#include "BuddyListHandler.h"
#include "BuddyListPacket.h"
#include "ChannelServer.h"
#include "ChatHandler.h"
#include "CmsgHeader.h"
#include "CommandHandler.h"
#include "Connectable.h"
#include "Database.h"
#include "DropHandler.h"
#include "Fame.h"
#include "GameConstants.h"
#include "GameLogicUtilities.h"
#include "Instance.h"
#include "Inventory.h"
#include "InventoryHandler.h"
#include "InventoryPacket.h"
#include "IpUtilities.h"
#include "ItemDataProvider.h"
#include "KeyMaps.h"
#include "LevelsPacket.h"
#include "MapPacket.h"
#include "Maps.h"
#include "MobHandler.h"
#include "MonsterBookPacket.h"
#include "Npc.h"
#include "NpcHandler.h"
#include "PacketReader.h"
#include "Party.h"
#include "PartyHandler.h"
#include "Pet.h"
#include "PetHandler.h"
#include "PlayerDataProvider.h"
#include "PlayerHandler.h"
#include "PlayerPacket.h"
#include "Quests.h"
#include "Randomizer.h"
#include "ReactorHandler.h"
#include "ServerPacket.h"
#include "Session.h"
#include "SkillMacros.h"
#include "Skills.h"
#include "StringUtilities.h"
#include "Summon.h"
#include "SummonHandler.h"
#include "SyncPacket.h"
#include "TimeUtilities.h"
#include "TradeHandler.h"
#include "WorldServerConnection.h"
#include <boost/array.hpp>
#include <stdexcept>

Player::Player() :
	m_fallCounter(0),
	m_shop(0),
	m_itemEffect(0),
	m_chair(0),
	m_mapChair(0),
	m_tradeId(0),
	m_portalCount(0),
	m_tradeState(false),
	m_saveOnDc(true),
	m_isConnect(false),
	m_npc(nullptr),
	m_party(nullptr),
	m_instance(nullptr),
	m_changingChannel(false)
{
}

Player::~Player() {
	if (m_isConnect) {
		Map *curMap = Maps::getMap(m_map);
		if (getMapChair() != 0) {
			curMap->playerSeated(getMapChair(), nullptr);
		}
		curMap->removePlayer(this);
		m_isConnect = false;

		if (isTrading()) {
			TradeHandler::cancelTrade(this);
		}
		int32_t isLeader = 0;
		if (Party *party = getParty()) {
			party->setMember(getId(), nullptr);
			isLeader = getParty()->isLeader(getId()) ? 1 : 0;
		}
		if (Instance *instance = getInstance()) {
			instance->removePlayer(getId());
			instance->sendMessage(PlayerDisconnect, getId(), isLeader);
		}
		//if (this->getHp() == 0)
		//	this->acceptDeath();
		// "Bug" in global, would be fixed here:
		// When disconnecting and dead, you actually go back to forced return map before the death return map
		// (that means that it's parsed while logging in, not while logging out)
		if (PortalInfo *closest =curMap->getNearestSpawnPoint(getPos())) {
			m_mapPos = closest->id;
		}

		if (m_saveOnDc) {
			saveAll(true);
			setOnline(false);
		}
		if (ChannelServer::Instance()->isConnected()) {
			if (!isChangingChannel()) {
				SyncPacket::BuddyPacket::buddyOnline(getId(), getBuddyList()->getBuddyIds(), false);
			}
			// Do not connect to worldserver if the worldserver has disconnected
			SyncPacket::PlayerPacket::disconnect(getId());
		}
		PlayerDataProvider::Instance()->removePlayer(this);
	}
}

void Player::handleRequest(PacketReader &packet) {
	try {
		header_t header = packet.getHeader();
		if (!m_isConnect) {
			// We don't want to accept any other packet than the one for loading the character
			if (header == CMSG_PLAYER_LOAD) {
				playerConnect(packet);
			}
		}
		else {
			switch (header) {
				case CMSG_ADMIN_COMMAND: CommandHandler::handleAdminCommand(this, packet); break;
				case CMSG_ADMIN_MESSENGER: PlayerHandler::handleAdminMessenger(this, packet); break;
				case CMSG_ATTACK_ENERGY_CHARGE: PlayerHandler::useEnergyChargeAttack(this, packet); break;
				case CMSG_ATTACK_MAGIC: PlayerHandler::useSpellAttack(this, packet); break;
				case CMSG_ATTACK_MELEE: PlayerHandler::useMeleeAttack(this, packet); break;
				case CMSG_ATTACK_RANGED: PlayerHandler::useRangedAttack(this, packet); break;
				case CMSG_BUDDY: BuddyListHandler::handleBuddyList(this, packet); break;
				case CMSG_CASH_ITEM_USE: InventoryHandler::useCashItem(this, packet); break;
				case CMSG_CASH_SHOP: PlayerPacket::sendBlockedMessage(this, PlayerPacket::BlockMessages::NoCashShop); break;
				case CMSG_CHAIR: InventoryHandler::handleChair(this, packet); break;
				case CMSG_CHALKBOARD: InventoryPacket::sendChalkboardUpdate(this); setChalkboard(""); break;
				case CMSG_CHANNEL_CHANGE: changeChannel(packet.get<int8_t>()); break;
				case CMSG_COMMAND: CommandHandler::handleCommand(this, packet); break;
				case CMSG_DROP_MESOS: DropHandler::dropMesos(this, packet); break;
				case CMSG_EMOTE: PlayerHandler::handleFacialExpression(this, packet); break;
				case CMSG_FAME: Fame::handleFame(this, packet); break;
				case CMSG_FRIENDLY_MOB_DAMAGE: MobHandler::friendlyDamaged(this, packet); break;
				case CMSG_HAMMER: InventoryHandler::handleHammerTime(this); break;
				case CMSG_ITEM_CANCEL: InventoryHandler::cancelItem(this, packet); break;
				case CMSG_ITEM_EFFECT: InventoryHandler::useItemEffect(this, packet); break;
				case CMSG_ITEM_LOOT: DropHandler::lootItem(this, packet); break;
				case CMSG_ITEM_MOVE: InventoryHandler::itemMove(this, packet); break;
				case CMSG_ITEM_USE: InventoryHandler::useItem(this, packet); break;
				case CMSG_KEYMAP: changeKey(packet); break;
				case CMSG_MACRO_LIST: changeSkillMacros(packet); break;
				case CMSG_MAP_CHANGE: Maps::usePortal(this, packet); break;
				case CMSG_MAP_CHANGE_SPECIAL: Maps::useScriptedPortal(this, packet); break;
				case CMSG_MESSAGE_GROUP: ChatHandler::handleGroupChat(this, packet); break;
				case CMSG_MOB_CONTROL: MobHandler::monsterControl(this, packet); break;
				case CMSG_MOB_EXPLOSION: MobHandler::handleBomb(this, packet); break;
				case CMSG_MOB_TURNCOAT_DAMAGE: MobHandler::handleTurncoats(this, packet); break;
				case CMSG_MONSTER_BOOK: PlayerHandler::handleMonsterBook(this, packet); break;
				case CMSG_MTS: PlayerPacket::sendBlockedMessage(this, PlayerPacket::BlockMessages::MtsUnavailable); break;
				case CMSG_MULTI_STAT_ADDITION: getStats()->addStatMulti(packet); break;
				case CMSG_MYSTIC_DOOR_ENTRY: PlayerHandler::handleDoorUse(this, packet); break;
				case CMSG_NPC_ANIMATE: NpcHandler::handleNpcAnimation(this, packet); break;
				case CMSG_NPC_TALK: NpcHandler::handleNpc(this, packet); break;
				case CMSG_NPC_TALK_CONT: NpcHandler::handleNpcIn(this, packet); break;
				case CMSG_PARTY: PartyHandler::handleRequest(this, packet); break;
				case CMSG_PET_CHAT: PetHandler::handleChat(this, packet); break;
				case CMSG_PET_COMMAND: PetHandler::handleCommand(this, packet); break;
				case CMSG_PET_CONSUME_POTION: PetHandler::handleConsumePotion(this, packet); break;
				case CMSG_PET_FOOD_USE: PetHandler::handleFeed(this, packet); break;
				case CMSG_PET_LOOT: DropHandler::petLoot(this, packet); break;
				case CMSG_PET_MOVEMENT: PetHandler::handleMovement(this, packet); break;
				case CMSG_PET_SUMMON: PetHandler::handleSummon(this, packet); break;
				case CMSG_PLAYER_CHAT: ChatHandler::handleChat(this, packet); break;
				case CMSG_PLAYER_DAMAGE: PlayerHandler::handleDamage(this, packet); break;
				case CMSG_PLAYER_HEAL: PlayerHandler::handleHeal(this, packet); break;
				case CMSG_PLAYER_INFO: PlayerHandler::handleGetInfo(this, packet); break;
				case CMSG_PLAYER_MOVE: PlayerHandler::handleMoving(this, packet); break;
				case CMSG_PLAYER_ROOM: TradeHandler::tradeHandler(this, packet); break;
				case CMSG_QUEST_OBTAIN: Quests::getQuest(this, packet); break;
				case CMSG_REACTOR_HIT: ReactorHandler::hitReactor(this, packet); break;
				case CMSG_REACTOR_TOUCH: ReactorHandler::touchReactor(this, packet); break;
				case CMSG_REVIVE_EFFECT: InventoryHandler::useItemEffect(this, packet); break;
				case CMSG_SCROLL_USE: InventoryHandler::useScroll(this, packet); break;
				case CMSG_SHOP: NpcHandler::useShop(this, packet); break;
				case CMSG_SKILL_ADD: Skills::addSkill(this, packet); break;
				case CMSG_SKILL_CANCEL: Skills::cancelSkill(this, packet); break;
				case CMSG_SKILL_USE: Skills::useSkill(this, packet); break;
				case CMSG_SKILLBOOK_USE: InventoryHandler::useSkillbook(this, packet); break;
				case CMSG_SPECIAL_SKILL: PlayerHandler::handleSpecialSkills(this, packet); break;
				case CMSG_STAT_ADDITION: getStats()->addStat(packet); break;
				case CMSG_STORAGE: NpcHandler::useStorage(this, packet); break;
				case CMSG_SUMMON_ATTACK: PlayerHandler::useSummonAttack(this, packet); break;
				case CMSG_SUMMON_BAG_USE: InventoryHandler::useSummonBag(this, packet); break;
				case CMSG_SUMMON_DAMAGE: SummonHandler::damageSummon(this, packet); break;
				case CMSG_SUMMON_MOVEMENT: SummonHandler::moveSummon(this, packet); break;
				case CMSG_TELEPORT_ROCK: InventoryHandler::handleRockFunctions(this, packet); break;
				case CMSG_TELEPORT_ROCK_USE: InventoryHandler::handleRockTeleport(this, Items::SpecialTeleportRock, packet); break;
				case CMSG_TOWN_SCROLL_USE: InventoryHandler::useReturnScroll(this, packet); break;
				case CMSG_USE_CHAIR: InventoryHandler::useChair(this, packet); break;
				case CMSG_USE_REWARD_ITEM: InventoryHandler::handleRewardItem(this, packet); break;
				case CMSG_USE_SCRIPT_ITEM: InventoryHandler::handleScriptItem(this, packet); break;
			}
		}
	}
	catch (std::range_error) {
		// Packet data didn't match the packet length somewhere
		// This isn't always evidence of tampering with packets
		// We may not process the structure properly

		packet.reset();
		std::ostringstream x;
		x << "Player ID: " << getId() << "; Packet: " << packet;
		ChannelServer::Instance()->log(LogTypes::MalformedPacket, x.str());
		getSession()->disconnect();
	}
}

void Player::playerConnect(PacketReader &packet) {
	int32_t id = packet.get<int32_t>();
	if (!Connectable::Instance()->checkPlayer(id, getIp())) {
		// Hacking
		getSession()->disconnect();
		return;
	}
	m_id = id;

	// Character info
	mysqlpp::Query query = Database::getCharDb().query();
	query << "SELECT c.*, u.gm_level, u.admin FROM characters c "
		<< "INNER JOIN user_accounts u ON c.user_id = u.user_id "
		<< "WHERE c.character_id = " << id;

	mysqlpp::StoreQueryResult res = query.store();
	if (res.empty()) {
		// Hacking
		getSession()->disconnect();
		return;
	}

	res[0]["name"].to_string(m_name);
	m_userId = res[0]["user_id"];
	m_map = res[0]["map"];
	m_gmLevel = res[0]["gm_level"];
	m_admin = StringUtilities::atob(res[0]["admin"]);
	m_eyes = res[0]["eyes"];
	m_hair = res[0]["hair"];
	m_worldId = static_cast<int8_t>(res[0]["world_id"]);
	m_gender = static_cast<int8_t>(res[0]["gender"]);
	m_skin = static_cast<int8_t>(res[0]["skin"]);
	m_mapPos = static_cast<int8_t>(res[0]["pos"]);
	m_buddylistSize = static_cast<uint8_t>(res[0]["buddylist_size"]);

	// Stats
	m_stats.reset(new PlayerStats(this, static_cast<uint8_t>(res[0]["level"]),
		static_cast<int16_t>(res[0]["job"]),
		static_cast<int16_t>(res[0]["fame"]),
		static_cast<int16_t>(res[0]["str"]),
		static_cast<int16_t>(res[0]["dex"]),
		static_cast<int16_t>(res[0]["int"]),
		static_cast<int16_t>(res[0]["luk"]),
		static_cast<int16_t>(res[0]["ap"]),
		static_cast<uint16_t>(res[0]["hpmp_ap"]),
		static_cast<int16_t>(res[0]["sp"]),
		static_cast<int16_t>(res[0]["chp"]),
		static_cast<int16_t>(res[0]["mhp"]),
		static_cast<int16_t>(res[0]["cmp"]),
		static_cast<int16_t>(res[0]["mmp"]),
		res[0]["exp"]));

	// Inventory
	m_mounts.reset(new PlayerMounts(this));
	m_pets.reset(new PlayerPets(this));
	boost::array<uint8_t, Inventories::InventoryCount> maxSlots;
	maxSlots[0] = static_cast<uint8_t>(res[0]["equip_slots"]);
	maxSlots[1] = static_cast<uint8_t>(res[0]["use_slots"]);
	maxSlots[2] = static_cast<uint8_t>(res[0]["setup_slots"]);
	maxSlots[3] = static_cast<uint8_t>(res[0]["etc_slots"]);
	maxSlots[4] = static_cast<uint8_t>(res[0]["cash_slots"]);
	m_inventory.reset(new PlayerInventory(this, maxSlots, res[0]["mesos"]));
	m_storage.reset(new PlayerStorage(this));

	// Skills
	m_skills.reset(new PlayerSkills(this));

	// Buffs/summons
	m_activeBuffs.reset(new PlayerActiveBuffs(this));
	m_summons.reset(new PlayerSummons(this));

	bool checked = false;
	if (PacketReader *pack = Connectable::Instance()->getPacket(id)) {
		// Packet transferring on channel switch
		checked = true;
		setConnectionTime(pack->get<int64_t>());

		PlayerActiveBuffs *buffs = getActiveBuffs();
		buffs->read(*pack);
		if (buffs->hasHyperBody()) {
			int32_t skillId = buffs->getHyperBody();
			uint8_t hbLevel = buffs->getActiveSkillLevel(skillId);
			SkillLevelInfo *hb = SkillDataProvider::Instance()->getSkill(skillId, hbLevel);
			getStats()->setHyperBody(hb->x, hb->y);
		}

		getSummons()->read(*pack);
	}
	else {
		// No packet, that means that they're connecting for the first time
		setConnectionTime(time(nullptr));
	}

	Connectable::Instance()->playerEstablished(id);

	// The rest
	m_variables.reset(new PlayerVariables(this));
	m_buddyList.reset(new PlayerBuddyList(this));
	m_quests.reset(new PlayerQuests(this));
	m_monsterBook.reset(new PlayerMonsterBook(this));

	getMonsterBook()->setCover(res[0]["book_cover"]);

	// Key Maps and Macros
	KeyMaps keyMaps;
	keyMaps.load(id);

	SkillMacros skillMacros;
	skillMacros.load(id);

	getStats()->checkHpMp(); // Adjust down HP or MP if necessary

	if (isGm() || isAdmin()) {
		if (!checked) {
			m_map = Maps::GmMap;
			m_mapPos = -1;
		}
	}
	else if (Maps::getMap(m_map)->getForcedReturn() != Maps::NoMap) {
		m_map = Maps::getMap(m_map)->getForcedReturn();
		m_mapPos = -1;
	}
	else if (getStats()->getHp() == 0) {
		m_map = Maps::getMap(m_map)->getReturnMap();
		m_mapPos = -1;
	}

	m_pos = Maps::getMap(m_map)->getSpawnPoint(m_mapPos)->pos;
	m_stance = 0;
	m_foothold = 0;

	PlayerPacket::connectData(this);

	if (ChannelServer::Instance()->getScrollingHeader().size() > 0) {
		ServerPacket::showScrollingHeader(this, ChannelServer::Instance()->getScrollingHeader());
	}

	for (int8_t i = 0; i < Inventories::MaxPetCount; i++) {
		if (Pet *pet = getPets()->getSummoned(i)) {
			pet->setPos(Maps::getMap(m_map)->getSpawnPoint(m_mapPos)->pos);
		}
	}

	PlayerPacket::showKeys(this, &keyMaps);

	BuddyListPacket::update(this, BuddyListPacket::ActionTypes::Add);
	getBuddyList()->checkForPendingBuddy();

	PlayerPacket::showSkillMacros(this, &skillMacros);

	Maps::addPlayer(this, m_map);

	std::ostringstream x;
	x << getName() << " (" << getId() << ") connected from " << IpUtilities::ipToString(getIp());
	ChannelServer::Instance()->log(LogTypes::Info, x.str());

	setOnline(true);
	m_isConnect = true;
	SyncPacket::PlayerPacket::connect(this);
	SyncPacket::BuddyPacket::buddyOnline(getId(), getBuddyList()->getBuddyIds(), true);
}

void Player::setMap(int32_t mapId, PortalInfo *portal, bool instance) {
	if (!Maps::getMap(mapId)) {
		MapPacket::portalBlocked(this);
		return;
	}
	Map *oldMap = Maps::getMap(m_map);
	Map *newMap = Maps::getMap(mapId);

	if (portal == nullptr) {
		portal = newMap->getSpawnPoint();
	}

	if (!instance) {
		// Only trigger the message for natural map changes not caused by moveAllPlayers, etc.
		int32_t isPartyLeader = (getParty() != nullptr && getParty()->isLeader(getId()) ? 1 : 0);
		if (Instance *i = oldMap->getInstance()) {
			i->sendMessage(PlayerChangeMap, getId(), mapId, m_map, isPartyLeader);
		}
		if (Instance *i = newMap->getInstance()) {
			i->sendMessage(PlayerChangeMap, getId(), mapId, m_map, isPartyLeader);
		}
	}

	oldMap->removePlayer(this);
	m_map = mapId;
	m_mapPos = portal->id;
	m_usedPortals.clear();
	setPos(Pos(portal->pos.x, portal->pos.y - 40));
	setStance(0);
	setFh(0);
	setFallCounter(0);

	// Prevent chair Denial of Service
	if (getMapChair() != 0) {
		oldMap->playerSeated(getMapChair(), nullptr);
		setMapChair(0);
	}
	if (getChair() != 0) {
		setChair(0);
	}

	for (int8_t i = 0; i < Inventories::MaxPetCount; i++) {
		if (Pet *pet = getPets()->getSummoned(i)) {
			pet->setPos(portal->pos);
		}
	}

	// Puppets and non-moving summons don't go with you
	if (getSummons()->getPuppet() != nullptr) {
		SummonHandler::removeSummon(this, true, false, SummonMessages::None);
	}
	if (getSummons()->getSummon() != nullptr && getSummons()->getSummon()->getType() == Summon::Static) {
		SummonHandler::removeSummon(this, false, false, SummonMessages::None);
	}

	if (getActiveBuffs()->hasMarkedMonster()) {
		Buffs::endBuff(this, getActiveBuffs()->getHomingBeacon());
	}
	if (!getChalkboard().empty() && !newMap->canChalkboard()) {
		setChalkboard("");
	}
	SyncPacket::PlayerPacket::updateMap(getId(), mapId);
	MapPacket::changeMap(this);
	Maps::addPlayer(this, mapId);
}

string Player::getMedalName() {
	std::ostringstream ret;
	if (int32_t itemId = getInventory()->getEquippedId(EquipSlots::Medal)) {
		// Check if there's an item at that slot
		ret << "<" << ItemDataProvider::Instance()->getItemName(itemId) << "> ";
	}
	ret << getName();
	return ret.str();
}

void Player::changeChannel(int8_t channel) {
	SyncPacket::PlayerPacket::changeChannel(this, channel);
}

void Player::changeKey(PacketReader &packet) {
	int32_t mode = packet.get<int32_t>();
	int32_t howMany = packet.get<int32_t>();

	enum KeyModes : int32_t {
		ChangeKeys = 0x00,
		AutoHpPotion = 0x01,
		AutoMpPotion = 0x02
	};

	if (mode == ChangeKeys) {
		if (howMany == 0)
			return;

		KeyMaps keyMaps; // We don't need old values here because it is only used to save the new values
		for (int32_t i = 0; i < howMany; i++) {
			int32_t pos = packet.get<int32_t>();
			int8_t type = packet.get<int8_t>();
			int32_t action = packet.get<int32_t>();
			keyMaps.add(pos, new KeyMaps::KeyMap(type, action));
		}

		// Update to MySQL
		keyMaps.save(getId());
	}
	else if (mode == AutoHpPotion) {
		getInventory()->setAutoHpPot(howMany);
	}
	else if (mode == AutoMpPotion) {
		getInventory()->setAutoMpPot(howMany);
	}
}

void Player::changeSkillMacros(PacketReader &packet) {
	uint8_t num = packet.get<int8_t>();
	if (num == 0) {
		return;
	}
	SkillMacros skillMacros;
	for (uint8_t i = 0; i < num; i++) {
		string &name = packet.getString();
		bool shout = packet.getBool();
		int32_t skill1 = packet.get<int32_t>();
		int32_t skill2 = packet.get<int32_t>();
		int32_t skill3 = packet.get<int32_t>();

		skillMacros.add(i, new SkillMacros::SkillMacro(name, shout, skill1, skill2, skill3));
	}
	skillMacros.save(getId());
}

void Player::setHair(int32_t id) {
	m_hair = id;
	PlayerPacket::updateStat(this, Stats::Hair, id);
}

void Player::setEyes(int32_t id) {
	m_eyes = id;
	PlayerPacket::updateStat(this, Stats::Eyes, id);
}

void Player::setSkin(int8_t id) {
	m_skin = id;
	PlayerPacket::updateStat(this, Stats::Skin, id);
}

void Player::saveStats() {
	mysqlpp::Query query = Database::getCharDb().query();
	query << "UPDATE characters SET "
		<< "level = " << static_cast<int16_t>(getStats()->getLevel()) << "," // Queries have problems with int8_t due to being derived from ostream
		<< "job = " << getStats()->getJob() << ","
		<< "str = " << getStats()->getStr() << ","
		<< "dex = " << getStats()->getDex() << ","
		<< "`int` = " << getStats()->getInt() << ","
		<< "luk = " << getStats()->getLuk() << ","
		<< "chp = " << getStats()->getHp() << ","
		<< "mhp = " << getStats()->getMaxHp(true) << ","
		<< "cmp = " << getStats()->getMp() << ","
		<< "mmp = " << getStats()->getMaxMp(true) << ","
		<< "hpmp_ap = " << getStats()->getHpMpAp() << ","
		<< "ap = " << getStats()->getAp() << ","
		<< "sp = " << getStats()->getSp() << ","
		<< "exp = " << getStats()->getExp() << ","
		<< "fame = " << getStats()->getFame() << ","
		<< "map = " << m_map << ","
		<< "pos = " << static_cast<int16_t>(m_mapPos) << ","
		<< "gender = " << static_cast<int16_t>(m_gender) << ","
		<< "skin = " << static_cast<int16_t>(m_skin) << ","
		<< "eyes = " << m_eyes << ","
		<< "hair = " << m_hair << ","
		<< "mesos = " << getInventory()->getMesos() << ","
		<< "equip_slots = " << static_cast<int16_t>(getInventory()->getMaxSlots(Inventories::EquipInventory)) << ","
		<< "use_slots = " << static_cast<int16_t>(getInventory()->getMaxSlots(Inventories::UseInventory)) << ","
		<< "setup_slots = " << static_cast<int16_t>(getInventory()->getMaxSlots(Inventories::SetupInventory)) << ","
		<< "etc_slots = " << static_cast<int16_t>(getInventory()->getMaxSlots(Inventories::EtcInventory)) << ","
		<< "cash_slots = " << static_cast<int16_t>(getInventory()->getMaxSlots(Inventories::CashInventory)) << ","
		<< "buddylist_size = " << static_cast<int16_t>(m_buddylistSize) << ","
		<< "book_cover = " << getMonsterBook()->getCover()
		<< " WHERE character_id = " << getId();
	query.exec();
}

void Player::saveAll(bool saveCooldowns) {
	saveStats();
	getInventory()->save();
	getStorage()->save();
	getMonsterBook()->save();
	getMounts()->save();
	getPets()->save();
	getQuests()->save();
	getSkills()->save(saveCooldowns);
	getVariables()->save();
}

void Player::setOnline(bool online) {
	int32_t onlineId = online ? ChannelServer::Instance()->getOnlineId() : 0;
	mysqlpp::Query query = Database::getCharDb().query();
	query << "UPDATE user_accounts u "
			<< "INNER JOIN characters c ON u.user_id = c.user_id "
			<< "SET "
			<< "	u.online = " << onlineId <<	", "
			<< "	c.online = " << online << " "
			<< "WHERE c.character_id = " << getId();
	query.exec();
}

void Player::setLevelDate() {
	mysqlpp::Query query = Database::getCharDb().query();
	query << "UPDATE characters c SET c.time_level = NOW() WHERE c.character_id = " << getId();
	query.exec();
}

void Player::acceptDeath(bool wheel) {
	int32_t toMap = (Maps::getMap(m_map) ? Maps::getMap(m_map)->getReturnMap() : m_map);
	if (wheel) {
		toMap = getMap();
	}
	getStats()->setHp(50, false);
	getActiveBuffs()->removeBuff();
	setMap(toMap);
}

bool Player::equippedUtility(int16_t slot, int32_t itemId) const {
	return getInventory()->getEquippedId(slot) == itemId;
}

bool Player::hasGmEquip() const {
	if (equippedUtility(EquipSlots::Helm, Items::GmHat)) {
		return true;
	}
	if (equippedUtility(EquipSlots::Top, Items::GmTop)) {
		return true;
	}
	if (equippedUtility(EquipSlots::Bottom, Items::GmBottom)) {
		return true;
	}
	if (equippedUtility(EquipSlots::Weapon, Items::GmWeapon)) {
		return true;
	}
	return false;
}

void Player::setBuddyListSize(uint8_t size) {
	m_buddylistSize = size;
	BuddyListPacket::showSize(this);
}

uint8_t Player::getPortalCount(bool add) {
	if (add) {
		m_portalCount++;
	}
	return m_portalCount;
}

void Player::initializeRng(PacketCreator &packet) {
	uint32_t seed1 = Randomizer::Instance()->randInt();
	uint32_t seed2 = Randomizer::Instance()->randInt();
	uint32_t seed3 = Randomizer::Instance()->randInt();

	m_randStream.reset(new TauswortheGenerator(seed1, seed2, seed3));

	packet.add<uint32_t>(seed1);
	packet.add<uint32_t>(seed2);
	packet.add<uint32_t>(seed3);
}