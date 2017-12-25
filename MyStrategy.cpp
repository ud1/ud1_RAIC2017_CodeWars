#include "MyStrategy.h"

#include <cmath>
#include <cstdlib>

using namespace model;
using namespace std;

#include "Strat.hpp"

Strat g_sim;
extern long g_tick;

void initializeSim(Simulator &sim, const Player& me, const World& world, const Game& game)
{
	if (sim.tick < 0)
	{
		for (int y = 0; y < CELLS_Y; ++y)
		{
			for (int x = 0; x < CELLS_Y; ++x)
			{
				Cell &cell = sim.cell(x, y);
				TerrainType terrain = world.getTerrainByCellXY()[x][y];
				
				switch (terrain)
				{
					case TerrainType::PLAIN:
						cell.groundType = GroundType::PLAIN;
						break;
					case TerrainType::SWAMP:
						cell.groundType = GroundType::SWAMP;
						break;
					case TerrainType::FOREST:
						cell.groundType = GroundType::FOREST;
						break;
					default: break;
				}
				
				WeatherType weather = world.getWeatherByCellXY()[x][y];
				switch (weather)
				{
					case WeatherType::CLEAR:
						cell.weatherType = MyWeatherType::FINE;
						break;
					case WeatherType::CLOUD:
						cell.weatherType = MyWeatherType::CLOUDY;
						break;
					case WeatherType::RAIN:
						cell.weatherType = MyWeatherType::RAIN;
						break;
					default: break;
				}
			}
		}
		
		sim.enableFOW = game.isFogOfWarEnabled();
	}
	
	for (const Facility &f : world.getFacilities())
	{
		Building *b = nullptr;

		for (Building &bl : sim.buildings) {
			if (bl.id == f.getId()) {
				b = &bl;
				break;
			}
		}
		
		if (!b) {
			sim.buildings.push_back(Building());
			b = &*sim.buildings.rbegin();
		}

		b->id = f.getId();
		b->type = (BuildingType) f.getType();
		if (f.getOwnerPlayerId() == -1)
			b->side = -1;
		else if (f.getOwnerPlayerId() == me.getId())
			b->side = 0;
		else
			b->side = 1;
		
		b->pos = P(f.getLeft() + 32.0, f.getTop() + 32.0);
		b->capturePoints = f.getCapturePoints();
		b->unitType = (UnitType) f.getVehicleType();
		b->productionProgress = f.getProductionProgress();
	}
	
	sim.tick = world.getTickIndex();
	g_tick = sim.tick;
	
	std::unordered_map<long, short> mySimUnitMap;
	for (short ind = 0; ind < (short) sim.units.size(); ++ind)
	{
		mySimUnitMap[sim.units[ind].id] = ind;
	}
	
	for (const Vehicle &vehicle : world.getNewVehicles())
	{
		MyUnit unit;
		unit.type = (UnitType) vehicle.getType();
		unit.pos = P(vehicle.getX(), vehicle.getY());
		unit.vel = P(0, 0);
		unit.side = (vehicle.getPlayerId() == me.getId()) ? 0 : 1;
		unit.lastMovedTick = sim.tick;
		unit.selected = false;
		unit.durability = vehicle.getDurability();
		unit.id = vehicle.getId();
		unit.attackCooldown = vehicle.getRemainingAttackCooldownTicks();
		for (int g : vehicle.getGroups())
			unit.addGroup(g);
		
		if (!mySimUnitMap.count(unit.id))
		{
			mySimUnitMap[vehicle.getId()] = sim.units.size();
			sim.units.push_back(unit);
		}
		else
		{
			sim.units[mySimUnitMap[vehicle.getId()]] = unit;
		}
	}
	
	for (const VehicleUpdate &vehicleUpdate : world.getVehicleUpdates())
	{
		assert(mySimUnitMap.count(vehicleUpdate.getId()));
		short ind = mySimUnitMap[vehicleUpdate.getId()];
			
		if (vehicleUpdate.getDurability() == 0) {
			sim.units[ind] = sim.units[sim.units.size() - 1];
			mySimUnitMap.erase(vehicleUpdate.getId());
			sim.units.pop_back();
			if (ind < (short) sim.units.size())
				mySimUnitMap[sim.units[ind].id] = ind;
		} else {
			MyUnit &unit = sim.units[ind];
			P newPos = P(vehicleUpdate.getX(), vehicleUpdate.getY());;
			
			if (unit.pos.dist2(newPos) > 0.001) {
				unit.lastMovedTick = sim.tick;
			}
			
			unit.vel = (unit.vel + newPos - unit.pos) * 0.5;
			unit.pos = newPos;
			unit.selected = vehicleUpdate.isSelected();
			unit.durability = vehicleUpdate.getDurability();
			unit.attackCooldown = vehicleUpdate.getRemainingAttackCooldownTicks();
			
			unit.groups.reset();
			for (int g : vehicleUpdate.getGroups())
				unit.addGroup(g);
		}
	}
	
	//LOG("NC " << world.getNewVehicles().size() << " U " << world.getVehicleUpdates().size());
	
	for (const Player &p : world.getPlayers())
	{
		int ind = p.isMe() ? 0 : 1;
		MyPLayer &myP = sim.players[ind];
		
		myP.remainingNuclearStrikeCooldownTicks = p.getRemainingNuclearStrikeCooldownTicks();
		myP.nextNuclearStrikeTick = p.getNextNuclearStrikeTickIndex();
		myP.nextNuclearStrikeVehicleId = p.getNextNuclearStrikeVehicleId();
		myP.nuclearStrike = P(p.getNextNuclearStrikeX(), p.getNextNuclearStrikeY());
		myP.score = p.getScore();
	}
	//std::cout << "NUKE TICKS " << sim.remainingNuclearStrikeCooldownTicks0 << std::endl;
}

void applyMove(const MyMove &myMove, Move& move)
{
	if (myMove.action != MyActionType::NONE)
	{
		move.setAction((ActionType) myMove.action);
		move.setLeft(myMove.p1.x);
		move.setRight(myMove.p2.x);
		move.setTop(myMove.p1.y);
		move.setBottom(myMove.p2.y);
		move.setX(myMove.p.x);
		move.setY(myMove.p.y);
		move.setVehicleType((VehicleType) myMove.unitType);
		move.setAngle(myMove.angle);
		move.setFactor(myMove.factor);
		move.setVehicleId(myMove.vehicleId);
		move.setMaxSpeed(myMove.maxSpeed);
		
		LOG("M " << g_sim.tick << " " << getUnitTypeName(myMove.unitType) << " " << (int) myMove.action << " ");
		if (myMove.action == MyActionType::MOVE)
			LOG(myMove.p);
	}
}

void MyStrategy::move(const Player& me, const World& world, const Game& game, Move& move) {
	initializeSim(g_sim, me, world, game);
	
	
	//////////////// LEFT/TOP FORMATION
	/*if (g_sim.tick == 0)
	{
		move.setAction(ActionType::CLEAR_AND_SELECT);
		move.setRight(WIDTH);
		move.setBottom(HEIGHT);
	}
	
	static bool toLeft = true;
	if (g_sim.tick % 60 == 1)
	{
		move.setAction(ActionType::MOVE);
		if (toLeft)
		{
			move.setX(-200.0);
		}
		else
		{
			move.setY(-200.0);
		}
		
		toLeft = !toLeft;
	}
	return;*/
	
	/*static std::map<UnitType, int> moveX;
	if (g_sim.tick == 0)
	{
		std::map<UnitType, P> centers;
		for (int i = 0; i < 5; ++i)
		{
			centers[(UnitType) i] = (g_sim.computeCenter((UnitType) i, 0) - P(45, 45)) / 74.0;
		}
		
		for (int i = 0; i < 2; ++i)
		{
			std::set<int> freeX;
			freeX.insert(0);
			freeX.insert(1);
			freeX.insert(2);
			
			for (int t = 0; t < 5; ++t)
			{
				if (i == 0 && isGroundUnit((UnitType)t) || i == 1 && !isGroundUnit((UnitType)t))
				{
					if (centers[(UnitType)t].y == 1)
						freeX.erase(centers[(UnitType)t].x);
				}
			}
			
			for (int t = 0; t < 5; ++t)
			{
				if (i == 0 && isGroundUnit((UnitType)t) || i == 1 && !isGroundUnit((UnitType)t))
				{
					if (centers[(UnitType)t].y != 1)
					{
						int closestI = *freeX.begin();
						for (int i : freeX)
						{
							if (std::abs(centers[(UnitType)t].x - i) < std::abs(centers[(UnitType)t].x - closestI))
								closestI = i;
						}
						
						moveX[(UnitType)t] = closestI - centers[(UnitType)t].x;
						freeX.erase(closestI);
					}
				}
			}
		}
		
		for (auto it = moveX.begin(); it != moveX.end();)
		{
			if (it->second == 0)
			{
				it = moveX.erase(it);
				continue;
			}
			++it;
		}
	}
	
	if (!g_sim.getAvailableActions(12))
		return;
	
	static bool moveXFinished = false;
	if (!moveX.empty())
	{
		UnitType u = moveX.begin()->first;
		int dx = moveX.begin()->second;
		
		Group g;
		g.unitType = u;
		if (!g_sim.isSelected(g))
		{
			applyMove(g_sim.select(g), move);
			g_sim.registerAction();
			return;
		}
		else
		{
			move.setAction(ActionType::MOVE);
			move.setX(dx * 74.0);
			moveX.erase(moveX.begin());
			g_sim.registerAction();
			return;
		}
	}
	else
	{
		if (!moveXFinished && g_sim.anyMoved(UnitType::NONE, 0))
			return;
		
		moveXFinished = true;
		
		static bool moveYFinished = false;
		
		static int movedUp = 0;
		static int movedDown = 0;
		
		if (movedUp == 0)
		{
			move.setAction(ActionType::CLEAR_AND_SELECT);
			move.setBottom(82);
			move.setRight(1024);
			movedUp = 1;
			g_sim.registerAction();
			return;
		}
		
		if (movedUp == 1)
		{
			move.setAction(ActionType::MOVE);
			move.setY(74);
			movedUp = 2;
			g_sim.registerAction();
			return;
		}
		
		if (movedDown == 0)
		{
			move.setAction(ActionType::CLEAR_AND_SELECT);
			move.setTop(156);
			move.setBottom(1024);
			move.setRight(1024);
			movedDown = 1;
			g_sim.registerAction();
			return;
		}
		
		if (movedDown == 1)
		{
			move.setAction(ActionType::MOVE);
			move.setY(-74);
			movedDown = 2;
			g_sim.registerAction();
			return;
		}
		
		if (!moveYFinished && g_sim.anyMoved(UnitType::NONE, 0))
			return;
		
		moveYFinished = false;
		
		static int scaleStep = 0;
		
		if (scaleStep == 0)
		{
			move.setAction(ActionType::CLEAR_AND_SELECT);
			move.setBottom(1024);
			move.setRight(1024);
			scaleStep = 1;
			g_sim.registerAction();
			return;
		}
		
		if (scaleStep == 1)
		{
			move.setAction(ActionType::SCALE);
			move.setFactor(1.6);
			move.setX(20.0);
			move.setY(119);
			scaleStep = 2;
			g_sim.registerAction();
			return;
		}
	}
	
	return;*/
	
	////////////////
	
	static int lastAction = 0;
	MyMove myMove = g_sim.nextMove();
	if (myMove.action != MyActionType::NONE)
	{
		move.setAction((ActionType) myMove.action);
		move.setLeft(myMove.p1.x);
		move.setRight(myMove.p2.x);
		move.setTop(myMove.p1.y);
		move.setBottom(myMove.p2.y);
		move.setX(myMove.p.x);
		move.setY(myMove.p.y);
		move.setVehicleType((VehicleType) myMove.unitType);
		move.setAngle(myMove.angle);
		move.setFactor(myMove.factor);
		move.setVehicleId(myMove.vehicleId);
		move.setFacilityId(myMove.facilityId);
		move.setMaxSpeed(myMove.maxSpeed);
		move.setGroup(myMove.group);
		
		lastAction = g_sim.tick;
		
		LOG(myMove);
	}
	
	if (g_sim.tick - lastAction > 500)
	{
		int myScore = 0;
		int enemyScore = 0;
		
		for (const Player &p : world.getPlayers())
		{
			if (p.isMe())
			{
				myScore = p.getScore();
			}
			else
			{
				enemyScore = p.getScore();
			}
		}
		
		if (myScore < enemyScore)
		{
			g_sim.angryModeTill = g_sim.tick + 200;
		}
	}
}

MyStrategy::MyStrategy() { }
