#define VCMI_DLL
#include "../lib/NetPacks.h"
#include "../hch/CGeneralTextHandler.h"
#include "../hch/CDefObjInfoHandler.h"
#include "../hch/CArtHandler.h"
#include "../hch/CHeroHandler.h"
#include "../hch/CObjectHandler.h"
#include "../lib/VCMI_Lib.h"
#include "../map.h"
#include "../hch/CSpellHandler.h"
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/thread.hpp>
#include <boost/thread/shared_mutex.hpp>

/*
 * NetPacksLib.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

DLL_EXPORT void SetResource::applyGs( CGameState *gs )
{
	gs->getPlayer(player)->resources[resid] = val;
}

DLL_EXPORT void SetResources::applyGs( CGameState *gs )
{
	for(int i=0;i<res.size();i++)
		gs->getPlayer(player)->resources[i] = res[i];
}

DLL_EXPORT void SetPrimSkill::applyGs( CGameState *gs )
{
	CGHeroInstance *hero = gs->getHero(id);
	if(which <4)
	{
		if(abs)
			hero->primSkills[which] = val;
		else
			hero->primSkills[which] += val;
	}
	else if(which == 4) //XP
	{
		if(abs)
			hero->exp = val;
		else
			hero->exp += val;
	}
}

DLL_EXPORT void SetSecSkill::applyGs( CGameState *gs )
{
	CGHeroInstance *hero = gs->getHero(id);
	if(hero->getSecSkillLevel(which) == 0)
	{
		hero->secSkills.push_back(std::pair<int,int>(which, val));
	}
	else
	{
		for(unsigned i=0;i<hero->secSkills.size();i++)
		{
			if(hero->secSkills[i].first == which)
			{
				if(abs)
					hero->secSkills[i].second = val;
				else
					hero->secSkills[i].second += val;
			}
		}
	}
}

DLL_EXPORT void HeroVisitCastle::applyGs( CGameState *gs )
{
	CGHeroInstance *h = gs->getHero(hid);
	CGTownInstance *t = gs->getTown(tid);
	if(start())
	{
		if(garrison())
		{
			t->garrisonHero = h;
			h->visitedTown = t;
			h->inTownGarrison = true;
		}
		else
		{
			t->visitingHero = h;
			h->visitedTown = t;
			h->inTownGarrison = false;
		}
	}
	else
	{
		if(garrison())
		{
			t->garrisonHero = NULL;
			h->visitedTown = NULL;
			h->inTownGarrison = false;
		}
		else
		{
			t->visitingHero = NULL;
			h->visitedTown = NULL;
			h->inTownGarrison = false;
		}
	}
}

DLL_EXPORT void ChangeSpells::applyGs( CGameState *gs )
{
	CGHeroInstance *hero = gs->getHero(hid);

	if(learn)
		BOOST_FOREACH(ui32 sid, spells)
		hero->spells.insert(sid);
	else
		BOOST_FOREACH(ui32 sid, spells)
		hero->spells.erase(sid);
}

DLL_EXPORT void SetMana::applyGs( CGameState *gs )
{
	CGHeroInstance *hero = gs->getHero(hid);
	hero->mana = val;
}

DLL_EXPORT void SetMovePoints::applyGs( CGameState *gs )
{
	CGHeroInstance *hero = gs->getHero(hid);
	hero->movement = val;
}

DLL_EXPORT void FoWChange::applyGs( CGameState *gs )
{
	BOOST_FOREACH(int3 t, tiles)
		gs->getPlayer(player)->fogOfWarMap[t.x][t.y][t.z] = mode;
}

DLL_EXPORT void SetAvailableHeroes::applyGs( CGameState *gs )
{
	gs->getPlayer(player)->availableHeroes.clear();

	CGHeroInstance *h = (hid1>=0 ?  gs->hpool.heroesPool[hid1] : NULL);
	gs->getPlayer(player)->availableHeroes.push_back(h);
	if(h  &&  flags & 1)
	{
		h->army.slots.clear();
		h->army.slots[0] = std::pair<ui32,si32>(VLC->creh->nameToID[h->type->refTypeStack[0]],1);
	}

	h = (hid2>=0 ?  gs->hpool.heroesPool[hid2] : NULL);
	gs->getPlayer(player)->availableHeroes.push_back(h);
	if(flags & 2)
	{
		h->army.slots.clear();
		h->army.slots[0] = std::pair<ui32,si32>(VLC->creh->nameToID[h->type->refTypeStack[0]],1);
	}
}

DLL_EXPORT void GiveBonus::applyGs( CGameState *gs )
{
	CGHeroInstance *h = gs->getHero(hid);
	h->bonuses.push_back(bonus);


	std::string &descr = h->bonuses.back().description;

	if(!bdescr.texts.size() 
		&& bonus.source == HeroBonus::OBJECT 
		&& (bonus.type == HeroBonus::LUCK || bonus.type == HeroBonus::MORALE || bonus.type == HeroBonus::MORALE_AND_LUCK)
		&& gs->map->objects[bonus.id]->ID == 26) //it's morale/luck bonus from an event without description
	{
		descr = VLC->generaltexth->arraytxt[bonus.val > 0 ? 110 : 109]; //+/-%d Temporary until next battle"
		boost::replace_first(descr,"%d",boost::lexical_cast<std::string>(std::abs(bonus.val)));
	}
	else
	{
		descr = toString(bdescr);
	}
}

DLL_EXPORT void ChangeObjPos::applyGs( CGameState *gs )
{
	CGObjectInstance *obj = gs->map->objects[objid];
	if(!obj)
	{
		tlog1 << "Wrong ChangeObjPos: object " << objid << " doesn't exist!\n";
		return;
	}
	gs->map->removeBlockVisTiles(obj);
	obj->pos = nPos;
	gs->map->addBlockVisTiles(obj);
}

DLL_EXPORT void RemoveObject::applyGs( CGameState *gs )
{
	CGObjectInstance *obj = gs->map->objects[id];
	if(obj->ID==HEROI_TYPE)
	{
		CGHeroInstance *h = static_cast<CGHeroInstance*>(obj);
		std::vector<CGHeroInstance*>::iterator nitr = std::find(gs->map->heroes.begin(), gs->map->heroes.end(),h);
		gs->map->heroes.erase(nitr);
		int player = h->tempOwner;
		nitr = std::find(gs->getPlayer(player)->heroes.begin(), gs->getPlayer(player)->heroes.end(), h);
		gs->getPlayer(player)->heroes.erase(nitr);
		if(h->visitedTown)
		{
			if(h->inTownGarrison)
				h->visitedTown->garrisonHero = NULL;
			else
				h->visitedTown->visitingHero = NULL;
			h->visitedTown = NULL;
		}

		//TODO: add to the pool?
	}
	gs->map->objects[id] = NULL;	

	//unblock tiles
	if(obj->defInfo)
	{
		gs->map->removeBlockVisTiles(obj);
	}
}

void TryMoveHero::applyGs( CGameState *gs )
{
	CGHeroInstance *h = gs->getHero(id);
	h->movement = movePoints;
	if(start!=end && result)
	{
		gs->map->removeBlockVisTiles(h);
		h->pos = end;
		gs->map->addBlockVisTiles(h);
	}
	BOOST_FOREACH(int3 t, fowRevealed)
		gs->getPlayer(h->getOwner())->fogOfWarMap[t.x][t.y][t.z] = 1;
}

DLL_EXPORT void SetGarrisons::applyGs( CGameState *gs )
{
	for(std::map<ui32,CCreatureSet>::iterator i = garrs.begin(); i!=garrs.end(); i++)
	{
		CArmedInstance *ai = static_cast<CArmedInstance*>(gs->map->objects[i->first]);
		ai->army = i->second;
		if(ai->ID==TOWNI_TYPE && (static_cast<CGTownInstance*>(ai))->garrisonHero) //if there is a hero in garrison then we must update also his army
			const_cast<CGHeroInstance*>((static_cast<CGTownInstance*>(ai))->garrisonHero)->army = i->second;
		else if(ai->ID==HEROI_TYPE)
		{
			CGHeroInstance *h =  static_cast<CGHeroInstance*>(ai);
			if(h->visitedTown && h->inTownGarrison)
				h->visitedTown->army = i->second;
		}
	}
}

DLL_EXPORT void NewStructures::applyGs( CGameState *gs )
{
	CGTownInstance*t = gs->getTown(tid);
	BOOST_FOREACH(si32 id,bid)
		t->builtBuildings.insert(id);
	t->builded = builded;
}

DLL_EXPORT void SetAvailableCreatures::applyGs( CGameState *gs )
{
	gs->getTown(tid)->strInfo.creatures = creatures;
}

DLL_EXPORT void SetHeroesInTown::applyGs( CGameState *gs )
{
	CGTownInstance *t = gs->getTown(tid);

	CGHeroInstance *v  = gs->getHero(visiting), 
		*g = gs->getHero(garrison);

	t->visitingHero = v;
	t->garrisonHero = g;
	if(v)
	{
		v->visitedTown = t;
		v->inTownGarrison = false;
		gs->map->addBlockVisTiles(v);
	}
	if(g)
	{
		g->visitedTown = t;
		g->inTownGarrison = true;
		gs->map->removeBlockVisTiles(g);
	}
}

DLL_EXPORT void SetHeroArtifacts::applyGs( CGameState *gs )
{
	CGHeroInstance *h = gs->getHero(hid);
	std::vector<ui32> equiped, unequiped;
	for(std::map<ui16,ui32>::const_iterator i = h->artifWorn.begin(); i != h->artifWorn.end(); i++)
		if(!vstd::contains(artifWorn,i->first)  ||  artifWorn[i->first] != i->second)
			unequiped.push_back(i->second);

	for(std::map<ui16,ui32>::const_iterator i = artifWorn.begin(); i != artifWorn.end(); i++)
		if(!vstd::contains(h->artifWorn,i->first)  ||  h->artifWorn[i->first] != i->second)
			equiped.push_back(i->second);

	h->artifacts = artifacts;
	h->artifWorn = artifWorn;

	BOOST_FOREACH(ui32 id, unequiped)
	{
		while(1)
		{
			std::list<HeroBonus>::iterator hlp = std::find_if(h->bonuses.begin(),h->bonuses.end(),boost::bind(HeroBonus::IsFrom,_1,HeroBonus::ARTIFACT,id));
			if(hlp != h->bonuses.end())
			{
				lost.push_back(&*hlp);
				h->bonuses.erase(hlp);
			}
			else
			{
				break;
			}
		}
	}

	BOOST_FOREACH(ui32 id, equiped)
	{
		CArtifact &art = VLC->arth->artifacts[id];
		for(std::list<HeroBonus>::iterator i = art.bonuses.begin(); i != art.bonuses.end(); i++)
		{
			gained.push_back(&*i);
			h->bonuses.push_back(*i);
		}
	}
}

DLL_EXPORT void SetHeroArtifacts::setArtAtPos(ui16 pos, int art)
{
	if(art<0)
	{
		if(pos<19)
			artifWorn.erase(pos);
		else
			artifacts -= artifacts[pos-19];
	}
	else
	{
		if(pos<19)
			artifWorn[pos] = art;
		else
			if(pos-19 < artifacts.size())
				artifacts[pos-19] = art;
			else
				artifacts.push_back(art);
	}
}


DLL_EXPORT void HeroRecruited::applyGs( CGameState *gs )
{
	CGHeroInstance *h = gs->hpool.heroesPool[hid];
	CGTownInstance *t = gs->getTown(tid);
	h->setOwner(player);
	h->pos = tile;
	h->movement =  h->maxMovePoints(true);

	gs->hpool.heroesPool.erase(hid);
	if(h->id < 0)
	{
		h->id = gs->map->objects.size();
		gs->map->objects.push_back(h);
	}
	else
		gs->map->objects[h->id] = h;

	h->initHeroDefInfo();
	gs->map->heroes.push_back(h);
	gs->getPlayer(h->getOwner())->heroes.push_back(h);
	gs->map->addBlockVisTiles(h);
	t->visitingHero = h;
	h->visitedTown = t;
	h->inTownGarrison = false;
}

DLL_EXPORT void GiveHero::applyGs( CGameState *gs )
{
	CGHeroInstance *h = gs->getHero(id);
	gs->map->removeBlockVisTiles(h,true);
	h->setOwner(player);
	h->movement =  h->maxMovePoints(true);
	h->initHeroDefInfo();
	gs->map->heroes.push_back(h);
	gs->getPlayer(h->getOwner())->heroes.push_back(h);
	gs->map->addBlockVisTiles(h);
	h->inTownGarrison = false;
}

DLL_EXPORT void NewTurn::applyGs( CGameState *gs )
{
	gs->day = day;
	BOOST_FOREACH(NewTurn::Hero h, heroes) //give mana/movement point
	{
		CGHeroInstance *hero = gs->getHero(h.id);
		hero->movement = h.move;
		hero->mana = h.mana;
	}

	BOOST_FOREACH(SetResources h, res) //give resources
		h.applyGs(gs);

	BOOST_FOREACH(SetAvailableCreatures h, cres) //set available creatures in towns
		h.applyGs(gs);

	if(resetBuilded) //reset amount of structures set in this turn in towns
		BOOST_FOREACH(CGTownInstance* t, gs->map->towns)
		t->builded = 0;

	BOOST_FOREACH(CGHeroInstance *h, gs->map->heroes)
		h->bonuses.remove_if(HeroBonus::OneDay);

	if(gs->getDate(1) == 7) //new week
		BOOST_FOREACH(CGHeroInstance *h, gs->map->heroes)
			h->bonuses.remove_if(HeroBonus::OneWeek);
}

DLL_EXPORT void SetObjectProperty::applyGs( CGameState *gs )
{
	CGObjectInstance *obj = gs->map->objects[id];
	if(!obj)
		tlog1 << "Wrong object ID - property cannot be set!\n";
	else
		obj->setProperty(what,val);
}

DLL_EXPORT void SetHoverName::applyGs( CGameState *gs )
{
	gs->map->objects[id]->hoverName = toString(name);
}

DLL_EXPORT void HeroLevelUp::applyGs( CGameState *gs )
{
	gs->getHero(heroid)->level = level;
}

DLL_EXPORT void BattleStart::applyGs( CGameState *gs )
{
	gs->curB = info;
}

DLL_EXPORT void BattleNextRound::applyGs( CGameState *gs )
{
	gs->curB->castSpells[0] = gs->curB->castSpells[1] = 0;
	gs->curB->round = round;

	BOOST_FOREACH(CStack *s, gs->curB->stacks)
	{
		s->state -= DEFENDING;
		s->state -= WAITING;
		s->state -= MOVED;
		s->state -= HAD_MORALE;
		s->counterAttacks = 1;

		//remove effects and restore only those with remaining turns in duration
		std::vector<CStack::StackEffect> tmpEffects = s->effects;
		s->effects.clear();
		for(int i=0; i < tmpEffects.size(); i++)
		{
			tmpEffects[i].turnsRemain--;
			if(tmpEffects[i].turnsRemain > 0)
				s->effects.push_back(tmpEffects[i]);
		}
	}
}

DLL_EXPORT void BattleSetActiveStack::applyGs( CGameState *gs )
{
	gs->curB->activeStack = stack;
	CStack *st = gs->curB->getStack(stack);
	if(vstd::contains(st->state,MOVED)) //if stack is moving second time this turn it must had a high morale bonus
		st->state.insert(HAD_MORALE);
}

void BattleResult::applyGs( CGameState *gs )
{
	for(unsigned i=0;i<gs->curB->stacks.size();i++)
		delete gs->curB->stacks[i];

	//remove any "until next battle" bonuses
	CGHeroInstance *h;
	h = gs->getHero(gs->curB->hero1);
	if(h)
		h->bonuses.remove_if(HeroBonus::OneBattle);
	h = gs->getHero(gs->curB->hero2);
	if(h) 
		h->bonuses.remove_if(HeroBonus::OneBattle);

	delete gs->curB;
	gs->curB = NULL;
}

void BattleStackMoved::applyGs( CGameState *gs )
{
	gs->curB->getStack(stack)->position = tile;
}

DLL_EXPORT void BattleStackAttacked::applyGs( CGameState *gs )
{
	CStack * at = gs->curB->getStack(stackAttacked);
	at->amount = newAmount;
	at->firstHPleft = newHP;
	if(killed())
		at->state -= ALIVE;
}

DLL_EXPORT void BattleAttack::applyGs( CGameState *gs )
{
	CStack *attacker = gs->curB->getStack(stackAttacking);
	if(counter())
		attacker->counterAttacks--;
	if(shot())
		attacker->shots--;
	BOOST_FOREACH(BattleStackAttacked stackAttacked, bsa)
		stackAttacked.applyGs(gs);
}

DLL_EXPORT void StartAction::applyGs( CGameState *gs )
{
	CStack *st = gs->curB->getStack(ba.stackNumber);
	switch(ba.actionType)
	{
	case 3:
		st->state.insert(DEFENDING);
		break;
	case 8:
		st->state.insert(WAITING);
		break;
	case 2: case 6: case 7: case 9: case 10: case 11:
		st->state.insert(MOVED);
		break;
	}
}

DLL_EXPORT void SpellCast::applyGs( CGameState *gs )
{
	CGHeroInstance *h = (side) ? gs->getHero(gs->curB->hero2) : gs->getHero(gs->curB->hero1);
	if(h)
	{
		h->mana -= VLC->spellh->spells[id].costs[skill];
		if(h->mana < 0) h->mana = 0;
	}
	if(side >= 0 && side < 2)
	{
		gs->curB->castSpells[side]++;
	}
	if(gs->curB && id == 35) //dispel
	{
		CStack *s = gs->curB->getStackT(tile);
		if(s)
		{
			s->effects.clear(); //removing all effects
		}
	}
}

DLL_EXPORT void SetStackEffect::applyGs( CGameState *gs )
{
	BOOST_FOREACH(ui32 id, stacks)
	{
		CStack *s = gs->curB->getStack(id);
		if(s)
		{
			s->effects.push_back(effect); //adding effect
		}
		else
			tlog1 << "Cannot find stack " << id << std::endl;
	}
}

DLL_EXPORT void StacksInjured::applyGs( CGameState *gs )
{
	BOOST_FOREACH(BattleStackAttacked stackAttacked, stacks)
		stackAttacked.applyGs(gs);
}

DLL_EXPORT void YourTurn::applyGs( CGameState *gs )
{
	gs->currentPlayer = player;
}


DLL_EXPORT void SetSelection::applyGs( CGameState *gs )
{
	gs->getPlayer(player)->currentSelection = id;
}