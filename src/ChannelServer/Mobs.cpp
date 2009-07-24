/*
Copyright (C) 2008-2009 Vana Development Team

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
#include "Mobs.h"
#include "Drops.h"
#include "DropsPacket.h"
#include "GameConstants.h"
#include "GameLogicUtilities.h"
#include "Instance.h"
#include "Inventory.h"
#include "Levels.h"
#include "Maps.h"
#include "Mist.h"
#include "MobsPacket.h"
#include "MovementHandler.h"
#include "PacketCreator.h"
#include "PacketReader.h"
#include "Party.h"
#include "Randomizer.h"
#include "Skills.h"
#include "SkillsPacket.h"
#include "Timer/Time.h"
#include "Timer/Timer.h"
#include <functional>

using std::tr1::bind;

// Mob status stuff
const int32_t Mobs::mobstatuses[StatusEffects::Mob::Count] = { // Order by value ascending
	StatusEffects::Mob::Watk,
	StatusEffects::Mob::Wdef,
	StatusEffects::Mob::Matk,
	StatusEffects::Mob::Mdef,
	StatusEffects::Mob::Acc,

	StatusEffects::Mob::Avoid,
	StatusEffects::Mob::Speed,
	StatusEffects::Mob::Stun,
	StatusEffects::Mob::Freeze,
	StatusEffects::Mob::Poison,

	StatusEffects::Mob::Seal,
	StatusEffects::Mob::NoClue1,
	StatusEffects::Mob::WeaponAttackUp,
	StatusEffects::Mob::WeaponDefenseUp,
	StatusEffects::Mob::MagicAttackUp,

	StatusEffects::Mob::MagicDefenseUp,
	StatusEffects::Mob::Doom,
	StatusEffects::Mob::ShadowWeb,
	StatusEffects::Mob::WeaponImmunity,
	StatusEffects::Mob::MagicImmunity,

	StatusEffects::Mob::NoClue2,
	StatusEffects::Mob::NoClue3,
	StatusEffects::Mob::NinjaAmbush,
	StatusEffects::Mob::NoClue4,
	StatusEffects::Mob::VenomousWeapon,

	StatusEffects::Mob::NoClue5,
	StatusEffects::Mob::NoClue6,
	StatusEffects::Mob::Empty,
	StatusEffects::Mob::Hypnotize,
	StatusEffects::Mob::WeaponDamageReflect,

	StatusEffects::Mob::MagicDamageReflect,
	StatusEffects::Mob::NoClue7
};

StatusInfo::StatusInfo(int32_t status, int32_t val, int32_t skillid, clock_t time) :
status(status),
val(val),
skillid(skillid),
mobskill(0),
level(0),
reflection(0),
time(time)
{
	if (val == StatusEffects::Mob::Freeze && skillid != Jobs::FPArchMage::Paralyze) {
		this->time += Randomizer::Instance()->randInt(time);
	}
}

StatusInfo::StatusInfo(int32_t status, int32_t val, int16_t mobskill, int16_t level, clock_t time) :
status(status),
val(val),
skillid(-1),
mobskill(mobskill),
level(level),
time(time),
reflection(-1)
{
}

StatusInfo::StatusInfo(int32_t status, int32_t val, int16_t mobskill, int16_t level, int32_t reflect, clock_t time) :
status(status),
val(val),
skillid(-1),
mobskill(mobskill),
level(level),
time(time),
reflection(reflect)
{
}

/* Mob class */
Mob::Mob(int32_t id, int32_t mapid, int32_t mobid, const Pos &pos, int16_t fh, int8_t controlstatus) :
MovableLife(fh, pos, 2),
originfh(fh),
id(id),
mapid(mapid),
spawnid(-1),
mobid(mobid),
timers(new Timer::Container),
info(MobDataProvider::Instance()->getMobInfo(mobid)),
facingdirection(1),
controlstatus(controlstatus)
{
	initMob();
}

Mob::Mob(int32_t id, int32_t mapid, int32_t mobid, const Pos &pos, int32_t spawnid, int8_t direction, int16_t fh) :
MovableLife(fh, pos, 2),
originfh(fh),
id(id),
mapid(mapid),
spawnid(spawnid),
mobid(mobid),
timers(new Timer::Container),
info(MobDataProvider::Instance()->getMobInfo(mobid)),
facingdirection(direction),
controlstatus(1)
{
	initMob();
}

void Mob::initMob() {
	this->hp = info.hp;
	this->mp = info.mp;
	if (info.flying) {
		setFh(0);
		originfh = 0;
	}

	owner = 0; // Pointers
	horntailsponge = 0;
	control = 0;

	webplayerid = 0; // Skill stuff
	weblevel = 0;
	counter = 0;
	venomcount = 0;
	mpeatercount = 0;
	taunteffect = 100;

	Map *map = Maps::getMap(mapid);
	Instance *instance = map->getInstance();
	if (instance != 0) {
		instance->sendMessage(MobSpawn, mobid, id, mapid);
	}

	status = StatusEffects::Mob::Empty;
	StatusInfo empty = StatusInfo(StatusEffects::Mob::Empty, 0, 0, 0);
	statuses[empty.status] = empty;

	if (info.hprecovery > 0) {
		new Timer::Timer(bind(&Mob::naturalHealHp, this, info.hprecovery),
			Timer::Id(Timer::Types::MobHealTimer, 0, 0),
			getTimers(), 0, 10 * 1000);
	}
	if (info.mprecovery > 0) {
		new Timer::Timer(bind(&Mob::naturalHealMp, this, info.mprecovery),
			Timer::Id(Timer::Types::MobHealTimer, 1, 1),
			getTimers(), 0, 10 * 1000);
	}
	if (info.removeafter > 0) {
		new Timer::Timer(bind(&Mob::applyDamage, this, 0, info.hp, false),
			Timer::Id(Timer::Types::MobRemoveTimer, mobid, id),
			map->getTimers(), Timer::Time::fromNow(info.removeafter * 1000));
	}
}

void Mob::naturalHealHp(int32_t amount) {
	if (getHp() < getMHp()) {
		int32_t hp = getHp() + amount;
		int32_t sponge = amount;
		if (hp < 0 || hp > getMHp()) {
			sponge = getMHp() - getHp(); // This is the amount for the sponge
			hp = getMHp();
		}
		setHp(hp);
		if (getSponge() != 0) {
			getSponge()->setHp(getSponge()->getHp() + sponge);
		}
	}
}

void Mob::naturalHealMp(int32_t amount) {
	if (getMp() < getMMp()) {
		int32_t mp = getMp() + amount;
		if (mp < 0 || mp > getMMp())
			mp = getMMp();
		setMp(mp);
	}
}

void Mob::applyDamage(int32_t playerid, int32_t damage, bool poison) {
	if (damage < 0)
		damage = 0;
	if (damage > hp)
		damage = hp - poison; // Keep HP from hitting 0 for poison and from going below 0

	damages[playerid] += damage;
	hp -= damage;

	if (!poison) { // HP bar packet does nothing for showing damage when poison is damaging for whatever reason
		Player *player = Players::Instance()->getPlayer(playerid);
	
		uint8_t percent = static_cast<uint8_t>(hp * 100 / info.hp);

		if (info.hpcolor > 0) { // Boss HP bars - Horntail's damage sponge isn't a boss in the data
			MobsPacket::showBossHp(mapid, mobid, hp, info);
		}
		else if (info.boss) { // Minibosses
			MobsPacket::showHp(mapid, id, percent);
		}
		else if (player != 0) {
			MobsPacket::showHp(player, id, percent);
		}

		Mob *sponge = getSponge(); // Need to preserve the pointer through mob deletion in die()
		if (hp == 0) { // Time to die
			switch (getMobId()) {
				case Mobs::HorntailSponge:
					for (unordered_map<int32_t, Mob *>::iterator spawniter = spawns.begin(); spawniter != spawns.end(); spawniter++) {
						new Timer::Timer(bind(&Mob::die, spawniter->second, true),
							Timer::Id(Timer::Types::HorntailTimer, id, spawniter->first),
							0, Timer::Time::fromNow(400));
					}
					break;
				case Mobs::ZakumArm1:
				case Mobs::ZakumArm2:
				case Mobs::ZakumArm3:
				case Mobs::ZakumArm4:
				case Mobs::ZakumArm5:
				case Mobs::ZakumArm6:
				case Mobs::ZakumArm7:
				case Mobs::ZakumArm8:
					if (getOwner() != 0 && getOwner()->getSpawnCount() == 1) {
						// Last linked arm died
						int8_t cstatus = Mobs::ControlStatus::ControlNormal;
						getOwner()->setControlStatus(cstatus);
					}
					break;
			}
			die(player);
		}
		if (sponge != 0) {
			sponge->applyDamage(playerid, damage, false); // Apply damage after you can be sure that all the units are linked and ready
		}
	}
	// TODO: Fix this, for some reason, it causes issues within the timer container
//	else if (hp == 1) {
//		removeStatus(StatusEffects::Mob::Poison);
//	}
}

void Mob::applyWebDamage() {
	int32_t webdamage = getMHp() / (50 - weblevel);
	if (webdamage > hp)
		webdamage = hp - 1; // Keep HP from hitting 0

	if (webdamage != 0) {
		damages[webplayerid] += webdamage;
		hp -= webdamage;
		MobsPacket::hurtMob(this, webdamage);
	}
}

void Mob::addStatus(int32_t playerid, vector<StatusInfo> &statusinfo) {
	int32_t addedstatus = 0;
	vector<int32_t> reflection;
	for (size_t i = 0; i < statusinfo.size(); i++) {
		int32_t cstatus = statusinfo[i].status;
		bool alreadyhas = (statuses.find(cstatus) != statuses.end());
		switch (cstatus) {
			case StatusEffects::Mob::Poison: // Status effects that do not renew
			case StatusEffects::Mob::Doom:
				if (alreadyhas)
					continue;
				break;
			case StatusEffects::Mob::ShadowWeb:
				webplayerid = playerid;
				weblevel = static_cast<uint8_t>(statusinfo[i].val);
				Maps::getMap(mapid)->setWebbedCount(Maps::getMap(mapid)->getWebbedCount() + 1);
				break;
			case StatusEffects::Mob::MagicAttackUp:
				if (statusinfo[i].skillid == Jobs::NightLord::Taunt || statusinfo[i].skillid == Jobs::Shadower::Taunt) {
					taunteffect = (100 - statusinfo[i].val) + 100; // Value passed as 100 - x, so 100 - value will = x
				}
				break;
			case StatusEffects::Mob::VenomousWeapon:
				setVenomCount(getVenomCount() + 1);
				if (alreadyhas) {
					statusinfo[i].val += statuses[cstatus].val; // Increase the damage
				}
				break;
			case StatusEffects::Mob::WeaponDamageReflect:
			case StatusEffects::Mob::MagicDamageReflect:
				reflection.push_back(statusinfo[i].reflection);
				break;
		}

		statuses[cstatus] = statusinfo[i];
		addedstatus += cstatus;

		switch (cstatus) {
			case StatusEffects::Mob::Poison:
			case StatusEffects::Mob::VenomousWeapon:
			case StatusEffects::Mob::NinjaAmbush: // Damage timer for poison(s)
				new Timer::Timer(bind(&Mob::applyDamage, this, playerid, statusinfo[i].val, true),
					Timer::Id(Timer::Types::MobStatusTimer, cstatus, 1),
					getTimers(), 0, 1000);
				break;
		}

		new Timer::Timer(bind(&Mob::removeStatus, this, cstatus, true),
			Timer::Id(Timer::Types::MobStatusTimer, cstatus, 0),
			getTimers(), Timer::Time::fromNow(statusinfo[i].time * 1000));
	}
	// Calculate new status mask
	status = 0;
	for (unordered_map<int32_t, StatusInfo>::iterator iter = statuses.begin(); iter != statuses.end(); iter++) {
		status += iter->first;
	}
	MobsPacket::applyStatus(this, addedstatus, statusinfo, 300, reflection);
}

void Mob::statusPacket(PacketCreator &packet) {
	packet.add<int32_t>(status);
	for (uint8_t i = 0; i < StatusEffects::Mob::Count; i++) { // Val/skillid pairs must be ordered in the packet by status value ascending
		int32_t status = Mobs::mobstatuses[i];
		if (hasStatus(status)) {
			if (status != StatusEffects::Mob::Empty) {
				packet.add<int16_t>(static_cast<int16_t>(statuses[status].val));
				if (statuses[status].skillid >= 0) {
					packet.add<int32_t>(statuses[status].skillid);
				}
				else {
					packet.add<int16_t>(statuses[status].mobskill);
					packet.add<int16_t>(statuses[status].level);
				}
				packet.add<int16_t>(1);
			}
			else {
				packet.add<int32_t>(0);
			}
		}
	}
}

void Mob::removeStatus(int32_t status, bool fromTimer) {
	if (hasStatus(status) && getHp() > 0) {
		StatusInfo stat = statuses[status];
		switch (status) {
			case StatusEffects::Mob::ShadowWeb:
				weblevel = 0;
				webplayerid = 0;
				Maps::getMap(mapid)->setWebbedCount(Maps::getMap(mapid)->getWebbedCount() - 1);
				break;
			case StatusEffects::Mob::MagicAttackUp:
				if (stat.skillid == Jobs::NightLord::Taunt || stat.skillid == Jobs::Shadower::Taunt) {
					taunteffect = 100;
				}
				break;
			case StatusEffects::Mob::VenomousWeapon:
				setVenomCount(0);
			case StatusEffects::Mob::Poison: // Stop poison damage timer
				getTimers()->removeTimer(Timer::Id(Timer::Types::MobStatusTimer, status, 1));
				break;
		}
		if (!fromTimer) {
			getTimers()->removeTimer(Timer::Id(Timer::Types::MobStatusTimer, status, 0));
		}
		this->status -= status;
		statuses.erase(status);
		MobsPacket::removeStatus(this, status);
	}
}

bool Mob::hasStatus(int32_t status) const {
	return ((this->status & status) != 0);
}

bool Mob::hasImmunity() const {
	int32_t mask = StatusEffects::Mob::WeaponImmunity | StatusEffects::Mob::MagicImmunity;
	return ((status & mask) != 0 || hasReflect());
}

bool Mob::hasReflect() const {
	int32_t mask = StatusEffects::Mob::WeaponDamageReflect | StatusEffects::Mob::MagicDamageReflect;
	return ((status & mask) != 0);
}

bool Mob::hasWeaponReflect() const {
	return hasStatus(StatusEffects::Mob::WeaponDamageReflect);
}

bool Mob::hasMagicReflect() const {
	return hasStatus(StatusEffects::Mob::MagicDamageReflect);
}

int32_t Mob::getStatusValue(int32_t status) {
	return (hasStatus(status) ? statuses[status].val : 0);
}

int32_t Mob::getMagicReflection() {
	return getStatusValue(StatusEffects::Mob::MagicDamageReflect);
}

int32_t Mob::getWeaponReflection() {
	return getStatusValue(StatusEffects::Mob::WeaponDamageReflect);
}

void Mob::setControl(Player *control, bool spawn, Player *display) {
	/*if (this->control != 0)
		MobsPacket::endControlMob(this->control, this);*/
	this->control = control;
	if (control != 0)
		MobsPacket::requestControl(control, this, false);
	else if (spawn) {
		MobsPacket::requestControl(control, this, spawn, display);
	}
}

void Mob::endControl() {
	if (control != 0 && control->getMap() == getMapId())
		MobsPacket::endControlMob(control, this);
}

void Mob::die(Player *player, bool fromexplosion) {
	Map *map = Maps::getMap(mapid);

	endControl();

	Timer::Id tid(Timer::Types::MobRemoveTimer, mobid, id);
	if (map->getTimers()->checkTimer(tid) > 0) {
		map->getTimers()->removeTimer(tid);
	}

	// Calculate EXP distribution
	int32_t highestdamager = 0;
	uint32_t highestdamage = 0;
	for (unordered_map<int32_t, uint32_t>::iterator iter = damages.begin(); iter != damages.end(); iter++) {
		if (iter->second > highestdamage) { // Find the highest damager to give drop ownership
			highestdamager = iter->first;
			highestdamage = iter->second;
		}
		Player *damager = Players::Instance()->getPlayer(iter->first);
		if (damager == 0 || damager->getMap() != this->mapid || damager->getStats()->getHp() == 0) // Only give EXP if the damager is in the same channel, on the same map and is alive
			continue;

		uint8_t multiplier = damager == player ? 10 : 8; // Multiplier for player to give the finishing blow is 1 and .8 for others. We therefore set this to 10 or 8 and divide the result in the formula found later on by 10.
		// Account for Holy Symbol
		int16_t hsrate = 0;
		if (damager->getActiveBuffs()->hasHolySymbol()) {
			int32_t hsid = damager->getActiveBuffs()->getHolySymbol();
			hsrate = Skills::skills[hsid][damager->getActiveBuffs()->getActiveSkillLevel(hsid)].x;
		}
		uint32_t exp = (info.exp * (multiplier * iter->second / info.hp)) / 10;
		exp = exp * getTauntEffect() / 100;
		exp += ((exp * hsrate) / 100);
		exp *= ChannelServer::Instance()->getExprate();
		Levels::giveExp(damager, exp, false, (damager == player));
	}

	// Spawn mob(s) the mob is supposed to spawn when it dies
	if (getMobId() == Mobs::SummonHorntail) { // Special Horntail logic to keep Horntail units linked
		int32_t spongeid = 0;
		vector<int32_t> parts;
		for (size_t i = 0; i < info.summon.size(); i++) {
			int32_t spawnid = info.summon[i];
			if (spawnid == Mobs::HorntailSponge)
				spongeid = map->spawnMob(spawnid, m_pos, getFh(), this);
			else {
				int32_t identifier = map->spawnMob(spawnid, m_pos, getFh(), this);
				parts.push_back(identifier);
			}
		}
		Mob *htsponge = Maps::getMap(mapid)->getMob(spongeid);
		for (size_t m = 0; m < parts.size(); m++) {
			Mob *f = map->getMob(parts[m]);
			f->setSponge(htsponge);
			htsponge->addSpawn(parts[m], f);
		}
	}
	else if (getSponge() != 0) { // More special Horntail logic to keep units linked
		getSponge()->removeSpawn(getId());
		for (size_t i = 0; i < info.summon.size(); i++) {
			int32_t ident = map->spawnMob(info.summon[i], m_pos, getFh(), this);
			Mob *mob = map->getMob(ident);
			getSponge()->addSpawn(ident, mob);
		}
	}
	else {
		for (size_t i = 0; i < info.summon.size(); i++) {
			map->spawnMob(info.summon[i], m_pos, getFh(), this);
		}
	}

	// Spawn stuff
	if (spawns.size() > 0) {
		for (unordered_map<int32_t, Mob *>::iterator spawniter = spawns.begin(); spawniter != spawns.end(); spawniter++) {
			spawniter->second->setOwner(0);
		}
	}
	if (getOwner() != 0) {
		owner->removeSpawn(getId());
	}

	if (hasStatus(StatusEffects::Mob::ShadowWeb)) {
		map->setWebbedCount(map->getWebbedCount() - 1);
	}

	// Ending of death stuff
	MobsPacket::dieMob(this, fromexplosion ? 4 : 1);
	Drops::doDrops(highestdamager, mapid, info.level, mobid, getPos(), hasExplosiveDrop(), hasFfaDrop(), getTauntEffect());

	if (player != 0) {
		Party *party = player->getParty();
		if (party != 0) {
			vector<Player *> members = party->getPartyMembers(mapid);
			for (size_t memsize = 0; memsize < members.size(); memsize++) {
				members[memsize]->getQuests()->updateQuestMob(mobid);
			}
		}
		else {
			player->getQuests()->updateQuestMob(mobid);
		}
	}

	if (info.buff != 0) {
		map->buffPlayers(info.buff);
	}

	Instance *instance = map->getInstance();
	if (instance != 0) {
		instance->sendMessage(MobDeath, mobid, id, mapid);
	}
	map->removeMob(id, spawnid);

	delete this;
}

void Mob::explode() {
	die(0, true);
}

void Mob::die(bool showpacket) {
	if (showpacket) {
		endControl();
		MobsPacket::dieMob(this);
		Instance *instance = Maps::getMap(mapid)->getInstance();
		if (instance != 0) {
			instance->sendMessage(MobDeath, mobid, id);
		}
	}
	Maps::getMap(mapid)->removeMob(id, spawnid);
	delete this;
}

void Mob::skillHeal(int32_t basehealhp, int32_t healrange) {
	if (getMobId() == Mobs::HorntailSponge)
		return;

	int32_t amount = Randomizer::Instance()->randInt(healrange) + (basehealhp - (healrange / 2));
	int32_t original = amount;

	if (hp + amount > getMHp()) {
		amount = getMHp() - hp;
		hp = getMHp();
	}
	else {
		hp += amount;
	}

	if (getSponge() != 0) {
		basehealhp = getSponge()->getHp() + amount;
		if (basehealhp < 0 || basehealhp > getSponge()->getMHp()) {
			basehealhp = getSponge()->getMHp();
		}
		getSponge()->setHp(basehealhp);
	}

	MobsPacket::healMob(this, original);
}

void Mob::dispelBuffs() {
	removeStatus(StatusEffects::Mob::Watk);
	removeStatus(StatusEffects::Mob::Wdef);
	removeStatus(StatusEffects::Mob::Matk);
	removeStatus(StatusEffects::Mob::Mdef);
	removeStatus(StatusEffects::Mob::Acc);
	removeStatus(StatusEffects::Mob::Avoid);
	removeStatus(StatusEffects::Mob::Speed);
}

void Mob::doCrashSkill(int32_t skillid) {
	switch (skillid) {
		case Jobs::Crusader::ArmorCrash:
			removeStatus(StatusEffects::Mob::Wdef);
			break;
		case Jobs::WhiteKnight::MagicCrash:
			removeStatus(StatusEffects::Mob::Matk);
			break;
		case Jobs::DragonKnight::PowerCrash:
			removeStatus(StatusEffects::Mob::Watk);
			break;
	}
}

void Mob::mpEat(Player *player, MpEaterInfo *mp) {
	if ((mpeatercount < 3) && (getMp() > 0) && (Randomizer::Instance()->randInt(99) < mp->prop)) {
		mp->onlyonce = true;
		int32_t emp = getMMp() * mp->x / 100;

		if (emp > getMp())
			emp = getMp();
		setMp(getMp() - emp);

		if (emp > 30000)
			emp = 30000;
		player->getStats()->modifyMp(static_cast<int16_t>(emp));

		SkillsPacket::showSkillEffect(player, mp->id);
		mpeatercount++;
	}
}

void Mob::setControlStatus(int8_t newstat) {
	MobsPacket::endControlMob(0, this);
	MobsPacket::spawnMob(0, this, 0, 0);
	controlstatus = newstat;
	Maps::getMap(getMapId())->updateMobControl(this);
}

/* Mobs namespace */
void Mobs::handleBomb(Player *player, PacketReader &packet) {
	int32_t mobid = packet.get<int32_t>();
	Mob *mob = Maps::getMap(player->getMap())->getMob(mobid);
	if (player->getStats()->getHp() == 0 || mob == 0) {
		return;
	}
	if (mob->getSelfDestructHp() == 0) {
		// Hacking, I think
		return;
	}
	mob->explode();
}

void Mobs::friendlyDamaged(Player *player, PacketReader &packet) {
	int32_t mobfrom = packet.get<int32_t>();
	int32_t playerid = packet.get<int32_t>();
	int32_t mobto = packet.get<int32_t>();

	Mob *dealer = Maps::getMap(player->getMap())->getMob(mobfrom);
	Mob *taker = Maps::getMap(player->getMap())->getMob(mobto);
	if (dealer != 0 && taker != 0 && taker->isFriendly()) {
		int32_t damage = dealer->getInfo()->level * Randomizer::Instance()->randInt(100) / 10; // Temp for now until I figure out something more effective
		taker->applyDamage(playerid, damage);
	}
}

void Mobs::handleTurncoats(Player *player, PacketReader &packet) {
	int32_t mobfrom = packet.get<int32_t>();
	int32_t playerid = packet.get<int32_t>();
	int32_t mobto = packet.get<int32_t>();
	packet.skipBytes(1); // Same as player damage, -1 = bump, integer = skill ID
	int32_t damage = packet.get<int32_t>();
	packet.skipBytes(1); // Facing direction
	packet.skipBytes(4); // Some type of pos, damage display, I think

	Mob *damager = Maps::getMap(player->getMap())->getMob(mobfrom);
	Mob *taker = Maps::getMap(player->getMap())->getMob(mobto);
	if (damager != 0 && taker != 0) {
		taker->applyDamage(playerid, damage);
	}
}

void Mobs::monsterControl(Player *player, PacketReader &packet) {
	int32_t mobid = packet.get<int32_t>();

	Mob *mob = Maps::getMap(player->getMap())->getMob(mobid);

	if (mob == 0 || mob->getControlStatus() == Mobs::ControlStatus::ControlNone) {
		return;
	}

	int16_t moveid = packet.get<int16_t>();
	bool useskill = (packet.get<int8_t>() != 0);
	int8_t skill = packet.get<int8_t>();
	uint8_t realskill = 0;
	uint8_t level = 0;
	Pos projectiletarget = packet.getPos();
	packet.skipBytes(5); // 1 byte of always 0?, 4 bytes of always 1 or always 0?
	Pos spot = packet.getPos();

	MovementHandler::parseMovement(mob, packet);

	if (useskill && (skill == -1 || skill == 0)) {
		if (!mob->hasStatus(StatusEffects::Mob::Freeze) && !mob->hasStatus(StatusEffects::Mob::Stun)) {
			int32_t size = mob->getSkillCount();
			bool used = false;
			if (size) {
				bool stop = false;
				uint8_t rand = (uint8_t)Randomizer::Instance()->randInt(size - 1);
				MobSkillInfo *info = MobDataProvider::Instance()->getMobSkill(mob->getMobId(), rand);
				realskill = info->skillid;
 				level = info->level;
				MobSkillLevelInfo mobskill = Skills::mobskills[realskill][level];
				switch (realskill) {
					case MobSkills::WeaponAttackUp:
					case MobSkills::WeaponAttackUpAoe:
						stop = mob->hasStatus(StatusEffects::Mob::Watk);
						break;
					case MobSkills::MagicAttackUp:
					case MobSkills::MagicAttackUpAoe:
						stop = mob->hasStatus(StatusEffects::Mob::Matk);
						break;
					case MobSkills::WeaponDefenseUp:
					case MobSkills::WeaponDefenseUpAoe:
						stop = mob->hasStatus(StatusEffects::Mob::Wdef);
						break;
					case MobSkills::MagicDefenseUp:
					case MobSkills::MagicDefenseUpAoe:
						stop = mob->hasStatus(StatusEffects::Mob::Mdef);
						break;					
					case MobSkills::WeaponImmunity:
					case MobSkills::MagicImmunity:
					case MobSkills::WeaponDamageReflect:
					case MobSkills::MagicDamageReflect:
						stop = mob->hasImmunity();
						break;
					case MobSkills::McSpeedUp:
						stop = mob->hasStatus(StatusEffects::Mob::Speed);
						break;
					case MobSkills::Summon: {
						int16_t spawns = (int16_t)(mob->getSpawns().size());
						int16_t limit = mobskill.limit;
						if (limit == 5000) // Custom limit based on number of players on map
							limit = 30 + Maps::getMap(mob->getMapId())->getNumPlayers() * 2;
						if (spawns >= limit)
							stop = true;
						break;
					}
				}
				if (!stop) {
					time_t now = time(0);
					time_t ls = mob->getLastSkillUse(realskill);
					if (ls == 0 || ((int16_t)(now - ls) > mobskill.interval)) {
						mob->setLastSkillUse(realskill, now);
						int64_t reqhp = mob->getHp() * 100;
						reqhp /= mob->getMHp();
						if ((uint8_t)(reqhp) <= mobskill.hp) {
							if (info->effectAfter == 0) {
								handleMobSkill(mob, realskill, level, mobskill);
							}
							else {
								new Timer::Timer(bind(&Mobs::handleMobSkill, mob, realskill, level, mobskill),
									Timer::Id(Timer::Types::MobSkillTimer, mob->getMobId(), mob->getCounter()),
									mob->getTimers(), Timer::Time::fromNow(info->effectAfter));
							}
							used = true;
						}
					}
				}
			}
			if (!used) { 
				realskill = 0;
				level = 0;
			}
		}
	}
	MobsPacket::moveMobResponse(player, mobid, moveid, useskill, mob->getMp(), realskill, level);
	packet.reset(19);
	MobsPacket::moveMob(player, mobid, useskill, skill, projectiletarget, packet.getBuffer(), packet.getBufferLength());
}

void Mobs::handleMobSkill(Mob *mob, uint8_t skillid, uint8_t level, const MobSkillLevelInfo &skillinfo) {
	Pos mobpos = mob->getPos();
	Map *map = Maps::getMap(mob->getMapId());
	vector<StatusInfo> statuses;
	bool aoe = false;
	switch (skillid) {
		case MobSkills::WeaponAttackUpAoe:
			aoe = true;
		case MobSkills::WeaponAttackUp:
			statuses.push_back(StatusInfo(StatusEffects::Mob::Watk, skillinfo.x, skillid, level, skillinfo.time));
			break;
		case MobSkills::MagicAttackUpAoe:
			aoe = true;
		case MobSkills::MagicAttackUp:
			statuses.push_back(StatusInfo(StatusEffects::Mob::Matk, skillinfo.x, skillid, level, skillinfo.time));
			break;
		case MobSkills::WeaponDefenseUpAoe:
			aoe = true;
		case MobSkills::WeaponDefenseUp:
			statuses.push_back(StatusInfo(StatusEffects::Mob::Wdef, skillinfo.x, skillid, level, skillinfo.time));
			break;
		case MobSkills::MagicDefenseUpAoe:
			aoe = true;
		case MobSkills::MagicDefenseUp:
			statuses.push_back(StatusInfo(StatusEffects::Mob::Mdef, skillinfo.x, skillid, level, skillinfo.time));
			break;
		case MobSkills::Heal:
			map->healMobs(skillinfo.x, skillinfo.y, mobpos, skillinfo.lt, skillinfo.rb);
			break;
		case MobSkills::Seal:
		case MobSkills::Darkness:
		case MobSkills::Weakness:
		case MobSkills::Stun:
		case MobSkills::Curse:
		case MobSkills::Poison:
		case MobSkills::Slow:
		case MobSkills::Seduce:
		case MobSkills::CrazySkull:
		case MobSkills::Zombify:
			map->statusPlayers(skillid, level, skillinfo.count, skillinfo.prop, mobpos, skillinfo.lt, skillinfo.rb);
			break;
		case MobSkills::Dispel:
			map->dispelPlayers(skillinfo.prop, mobpos, skillinfo.lt, skillinfo.rb);
			break;
		case MobSkills::SendToTown:
			map->sendPlayersToTown(mob->getMobId(), skillinfo.prop, skillinfo.count, mobpos, skillinfo.lt, skillinfo.rb);
			break;
		case MobSkills::PoisonMist:
			new Mist(mob->getMapId(), mob, mobpos, skillinfo, skillid, level);
			break;
		case MobSkills::WeaponImmunity:
			statuses.push_back(StatusInfo(StatusEffects::Mob::WeaponImmunity, skillinfo.x, skillid, level, skillinfo.time));
			break;
		case MobSkills::MagicImmunity:
			statuses.push_back(StatusInfo(StatusEffects::Mob::MagicImmunity, skillinfo.x, skillid, level, skillinfo.time));
			break;
		case MobSkills::WeaponDamageReflect:
			statuses.push_back(StatusInfo(StatusEffects::Mob::WeaponImmunity, skillinfo.x, skillid, level, skillinfo.time));
			statuses.push_back(StatusInfo(StatusEffects::Mob::WeaponDamageReflect, skillinfo.x, skillid, level, skillinfo.y, skillinfo.time));
			break;
		case MobSkills::MagicDamageReflect:
			statuses.push_back(StatusInfo(StatusEffects::Mob::MagicImmunity, skillinfo.x, skillid, level, skillinfo.time));
			statuses.push_back(StatusInfo(StatusEffects::Mob::MagicDamageReflect, skillinfo.x, skillid, level, skillinfo.y, skillinfo.time));
			break;
		case MobSkills::AnyDamageReflect:
			statuses.push_back(StatusInfo(StatusEffects::Mob::WeaponImmunity, skillinfo.x, skillid, level, skillinfo.time));
			statuses.push_back(StatusInfo(StatusEffects::Mob::MagicImmunity, skillinfo.x, skillid, level, skillinfo.time));
			statuses.push_back(StatusInfo(StatusEffects::Mob::WeaponDamageReflect, skillinfo.x, skillid, level, skillinfo.y, skillinfo.time));
			statuses.push_back(StatusInfo(StatusEffects::Mob::MagicDamageReflect, skillinfo.x, skillid, level, skillinfo.y, skillinfo.time));
			break;
		case MobSkills::McSpeedUp:
			statuses.push_back(StatusInfo(StatusEffects::Mob::Speed, skillinfo.x, skillid, level, skillinfo.time));
			break;
		case MobSkills::Summon: {
			int16_t minx, maxx;
			int16_t miny = mobpos.y + skillinfo.lt.y;
			int16_t maxy = mobpos.y + skillinfo.rb.y;
			int16_t d = 0;
			if (mob->isFacingRight()) {
				minx = mobpos.x + skillinfo.rb.x * -1;
				maxx = mobpos.x + skillinfo.lt.x * -1;
			}
			else {
				minx = mobpos.x + skillinfo.lt.x;
				maxx = mobpos.x + skillinfo.rb.x;
			}
			int16_t rangex = maxx - minx;
			int16_t rangey = maxy - miny;
			if (rangex < 0)
				rangex *= -1;
			if (rangey < 0)
				rangey *= -1;
			for (size_t summonsize = 0; summonsize < skillinfo.summons.size(); summonsize++) {
				int32_t spawnid = skillinfo.summons[summonsize];
				int16_t mobx = Randomizer::Instance()->randShort(rangex) + minx;
				int16_t moby = Randomizer::Instance()->randShort(rangey) + miny;
				Pos floor;
				if (mob->getMapId() == Maps::OriginOfClockTower) { // Papulatus' map
					if (spawnid == Mobs::HighDarkstar) { // Keep High Darkstars high
						while ((floor.y > -538 || floor.y == moby) || !GameLogicUtilities::isInBox(mob->getPos(), skillinfo.lt, skillinfo.rb, floor)) {
							// Mobs spawn on the ground, we need them up top
							mobx = Randomizer::Instance()->randShort(rangex) + minx;
							moby = -590;
							floor = map->findFloor(Pos(mobx, moby));
						}
					}
					else if (spawnid == Mobs::LowDarkstar) { // Keep Low Darkstars low
						floor = map->findFloor(Pos(mobx, mobpos.y));
					}
				}
				else {
					mobx = mobpos.x + ((d % 2) ? (35 * (d + 1) / 2) : -(40 * (d / 2)));
					floor = map->findFloor(Pos(mobx, moby));
					if (floor.y == moby) {
						floor.y = mobpos.y;
					}
				}
				map->spawnMob(spawnid, floor, 0, mob, skillinfo.summoneffect);
				d++;
			}
			break;
		}
	}
	if (statuses.size() > 0) {
		if (aoe) {
			map->statusMobs(statuses, mob->getPos(), skillinfo.lt, skillinfo.rb);
		}
		else {
			mob->addStatus(0, statuses);
		}
	}
}

int32_t Mobs::handleMobStatus(int32_t playerid, Mob *mob, int32_t skillid, uint8_t level, uint8_t weapon_type, int8_t hits, int32_t damage) {
	Player *player = Players::Instance()->getPlayer(playerid);
	vector<StatusInfo> statuses;
	int16_t y = 0;
	bool success = (Randomizer::Instance()->randInt(99) < Skills::skills[skillid][level].prop);
	if (mob->canFreeze()) { // Freezing stuff
		switch (skillid) {
			case Jobs::ILWizard::ColdBeam:
			case Jobs::ILMage::IceStrike:
			case Jobs::ILMage::ElementComposition:
			case Jobs::Sniper::Blizzard:
			case Jobs::ILArchMage::Blizzard:
				statuses.push_back(StatusInfo(StatusEffects::Mob::Freeze, StatusEffects::Mob::Freeze, skillid, Skills::skills[skillid][level].time));
				break;
			case Jobs::Outlaw::IceSplitter:
				if (player->getSkills()->getSkillLevel(Jobs::Corsair::ElementalBoost) > 0)
					y = Skills::skills[Jobs::Corsair::ElementalBoost][player->getSkills()->getSkillLevel(Jobs::Corsair::ElementalBoost)].y;
				statuses.push_back(StatusInfo(StatusEffects::Mob::Freeze, StatusEffects::Mob::Freeze, skillid, Skills::skills[skillid][level].time + y));
				break;
			case Jobs::FPArchMage::Elquines:
			case Jobs::Marksman::Frostprey:
				statuses.push_back(StatusInfo(StatusEffects::Mob::Freeze, StatusEffects::Mob::Freeze, skillid, Skills::skills[skillid][level].x));
				break;
		}
		if ((weapon_type == Weapon1hSword || weapon_type == Weapon2hSword || weapon_type == Weapon1hMace || weapon_type == Weapon2hMace) && player->getActiveBuffs()->hasIceCharge()) { // Ice Charges
			int32_t charge = player->getActiveBuffs()->getCharge();
			statuses.push_back(StatusInfo(StatusEffects::Mob::Freeze, StatusEffects::Mob::Freeze, charge, Skills::skills[charge][player->getActiveBuffs()->getActiveSkillLevel(charge)].y));
		}
	}
	if (mob->canPoison() && mob->getHp() > 1) { // Poisoning stuff
		switch (skillid) {
			case Jobs::All::RegularAttack: // Venomous Star/Stab
			case Jobs::Rogue::LuckySeven:
			case Jobs::Hermit::Avenger:
			case Jobs::NightLord::TripleThrow:
			case Jobs::Rogue::DoubleStab:
			case Jobs::Rogue::Disorder:
			case Jobs::Bandit::SavageBlow:
			case Jobs::ChiefBandit::Assaulter:
			case Jobs::Shadower::Assassinate:
			case Jobs::Shadower::BoomerangStep:
				if (player->getSkills()->hasVenomousWeapon() && mob->getVenomCount() < StatusEffects::Mob::MaxVenomCount) {
					// MAX = (18.5 * [STR + LUK] + DEX * 2) / 100 * Venom matk
					// MIN = (8.0 * [STR + LUK] + DEX * 2) / 100 * Venom matk
					int32_t vskill = player->getSkills()->getVenomousWeapon();
					uint8_t vlevel = player->getSkills()->getSkillLevel(vskill);
					int32_t part1 = player->getStats()->getBaseStat(Stats::Str) + player->getStats()->getBaseStat(Stats::Luk);
					int32_t part2 = player->getStats()->getBaseStat(Stats::Dex) * 2;
					int16_t vatk = Skills::skills[vskill][vlevel].matk;
					int32_t mindamage = ((80 * part1 / 10 + part2) / 100) * vatk;
					int32_t maxdamage = ((185 * part1 / 10 + part2) / 100) * vatk;
					
					damage = Randomizer::Instance()->randInt(maxdamage - mindamage) + mindamage;

					for (int8_t counter = 0; ((counter < hits) && (mob->getVenomCount() < StatusEffects::Mob::MaxVenomCount)); counter++) {
						success = (Randomizer::Instance()->randInt(99) < Skills::skills[vskill][vlevel].prop);
						if (success) {
							statuses.push_back(StatusInfo(StatusEffects::Mob::VenomousWeapon, damage, vskill, Skills::skills[vskill][vlevel].time));
							mob->addStatus(player->getId(), statuses);
							statuses.clear();
						}
					}
				}
				break;
			case Jobs::FPMage::PoisonMist:
				if (damage != 0) // The attack itself doesn't poison them
					break;
			case Jobs::FPWizard::PoisonBreath:
			case Jobs::FPMage::ElementComposition:
				if (skillid == Jobs::FPMage::PoisonMist && damage != 0) // The attack itself doesn't poison them
					break;
				if (success) {
					statuses.push_back(StatusInfo(StatusEffects::Mob::Poison, mob->getMHp() / (70 - level), skillid, Skills::skills[skillid][level].time));
				}
				break;
		}
	}
	if (!mob->isBoss()) { // Seal, Stun, etc
		switch (skillid) {
			case Jobs::Corsair::Hypnotize:
				statuses.push_back(StatusInfo(StatusEffects::Mob::Hypnotize, 1, skillid, Skills::skills[skillid][level].time));
				break;
			case Jobs::Brawler::BackspinBlow:
			case Jobs::Brawler::DoubleUppercut:
			case Jobs::Buccaneer::Demolition:
			case Jobs::Buccaneer::Snatch:
				statuses.push_back(StatusInfo(StatusEffects::Mob::Stun, StatusEffects::Mob::Stun, skillid, Skills::skills[skillid][level].time));
				break;
			case Jobs::Hunter::ArrowBomb:
			case Jobs::Crusader::SwordComa:
			case Jobs::Crusader::AxeComa:
			case Jobs::Crusader::Shout:
			case Jobs::WhiteKnight::ChargeBlow:
			case Jobs::ChiefBandit::Assaulter:
			case Jobs::Shadower::BoomerangStep:
			case Jobs::Gunslinger::BlankShot:
			case Jobs::NightLord::NinjaStorm:
				if (success) {
					statuses.push_back(StatusInfo(StatusEffects::Mob::Stun, StatusEffects::Mob::Stun, skillid, Skills::skills[skillid][level].time));
				}
				break;
			case Jobs::Ranger::SilverHawk:
			case Jobs::Sniper::GoldenEagle:
				if (success) {
					statuses.push_back(StatusInfo(StatusEffects::Mob::Stun, StatusEffects::Mob::Stun, skillid, Skills::skills[skillid][level].x));
				}
				break;
			case Jobs::FPMage::Seal:
			case Jobs::ILMage::Seal:
				if (success) {
					statuses.push_back(StatusInfo(StatusEffects::Mob::Stun, StatusEffects::Mob::Stun, skillid, Skills::skills[skillid][level].time));
				}
				break;
			case Jobs::Priest::Doom:
				if (success) {
					statuses.push_back(StatusInfo(StatusEffects::Mob::Doom, 0x100, skillid, Skills::skills[skillid][level].time));
				}
				break;
			case Jobs::Hermit::ShadowWeb:
				if (success) {
					statuses.push_back(StatusInfo(StatusEffects::Mob::ShadowWeb, level, skillid, Skills::skills[skillid][level].time));
				}
				break;
			case Jobs::FPArchMage::Paralyze:
				if (mob->canPoison()) {
					statuses.push_back(StatusInfo(StatusEffects::Mob::Freeze, StatusEffects::Mob::Freeze, skillid, Skills::skills[skillid][level].time));
				}
				break;
			case Jobs::ILArchMage::IceDemon:
			case Jobs::FPArchMage::FireDemon:
				statuses.push_back(StatusInfo(StatusEffects::Mob::Poison, mob->getMHp() / (70 - level), skillid, Skills::skills[skillid][level].time));
				statuses.push_back(StatusInfo(StatusEffects::Mob::Freeze, StatusEffects::Mob::Freeze, skillid, Skills::skills[skillid][level].x));
				break;
			case Jobs::Shadower::Taunt:
			case Jobs::NightLord::Taunt:
				// I know, these status effect types make no sense, that's just how it works
				statuses.push_back(StatusInfo(StatusEffects::Mob::MagicAttackUp, 100 - Skills::skills[skillid][level].x, skillid, Skills::skills[skillid][level].time));
				statuses.push_back(StatusInfo(StatusEffects::Mob::MagicDefenseUp, 100 - Skills::skills[skillid][level].x, skillid, Skills::skills[skillid][level].time));
				break;
			case Jobs::Outlaw::Flamethrower:
				if (player->getSkills()->getSkillLevel(Jobs::Corsair::ElementalBoost) > 0)
					y = Skills::skills[Jobs::Corsair::ElementalBoost][player->getSkills()->getSkillLevel(Jobs::Corsair::ElementalBoost)].x;
				statuses.push_back(StatusInfo(StatusEffects::Mob::Poison, (damage * (5 + y) / 100), skillid, Skills::skills[skillid][level].time));
				break;
		}
	}
	switch (skillid) {
		case Jobs::Shadower::NinjaAmbush:
		case Jobs::NightLord::NinjaAmbush:
			damage = 2 * (player->getStats()->getBaseStat(Stats::Str) + player->getStats()->getBaseStat(Stats::Luk)) * Skills::skills[skillid][level].damage / 100;
			statuses.push_back(StatusInfo(StatusEffects::Mob::NinjaAmbush, damage, skillid, Skills::skills[skillid][level].time));
			break;
		case Jobs::Rogue::Disorder:
		case Jobs::Page::Threaten:
			statuses.push_back(StatusInfo(StatusEffects::Mob::Watk, Skills::skills[skillid][level].x, skillid, Skills::skills[skillid][level].time));
			statuses.push_back(StatusInfo(StatusEffects::Mob::Wdef, Skills::skills[skillid][level].y, skillid, Skills::skills[skillid][level].time));
			break;
		case Jobs::FPWizard::Slow:
		case Jobs::ILWizard::Slow:
			statuses.push_back(StatusInfo(StatusEffects::Mob::Speed, Skills::skills[skillid][level].x, skillid, Skills::skills[skillid][level].time));
			break;
	}
	if (weapon_type == WeaponBow && player->getActiveBuffs()->getActiveSkillLevel(Jobs::Bowmaster::Hamstring) > 0 && skillid != Jobs::Bowmaster::Phoenix && skillid != Jobs::Ranger::SilverHawk) {
		uint8_t hamlevel = player->getActiveBuffs()->getActiveSkillLevel(Jobs::Bowmaster::Hamstring);
		statuses.push_back(StatusInfo(StatusEffects::Mob::Speed, Skills::skills[Jobs::Bowmaster::Hamstring][hamlevel].x, Jobs::Bowmaster::Hamstring, Skills::skills[Jobs::Bowmaster::Hamstring][hamlevel].y));
	}
	else if (weapon_type == WeaponCrossbow && player->getActiveBuffs()->getActiveSkillLevel(Jobs::Marksman::Blind) > 0 && skillid != Jobs::Marksman::Frostprey && skillid != Jobs::Sniper::GoldenEagle) {
		uint8_t blindlevel = player->getActiveBuffs()->getActiveSkillLevel(Jobs::Marksman::Blind);
		statuses.push_back(StatusInfo(StatusEffects::Mob::Acc, -Skills::skills[Jobs::Marksman::Blind][blindlevel].x, Jobs::Marksman::Blind, Skills::skills[Jobs::Marksman::Blind][blindlevel].y));
	}

	if (statuses.size() > 0) {
		mob->addStatus(playerid, statuses);
	}
	return statuses.size();
}
