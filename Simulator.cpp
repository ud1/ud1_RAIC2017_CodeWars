// v6

#include "Simulator.hpp"
#include <iterator>
#include <algorithm>


constexpr double MAX_SPEED = 1.2;
constexpr double SCAN_WIDTH = MAX_SPEED * 2.0 + UNIT_RAD_DOUBLED;
constexpr double HEAL_RANGE = 10.0;
constexpr double HEAL_RANGE2 = HEAL_RANGE * HEAL_RANGE;

long g_tick;

const char* UnitTypeNames[] = {
	"ARV", "FIGHTER", "HELICOPTER", "IFV", "TANK"
};

const char *getUnitTypeName(UnitType unitType)
{
	if (unitType == UnitType::NONE)
		return "NONE";
	return UnitTypeNames[(int) unitType];
}

const char* ActionTypeNames[] = {
	"NONE", "CLEAR_AND_SELECT", "ADD_TO_SELECTION", "DESELECT", "ASSIGN",
	"DISMISS", "DISBAND", "MOVE", "ROTATE", "SCALE", "SETUP_VEHICLE_PRODUCTION", "TACTICAL_NUCLEAR_STRIKE", "COUNT"
};

const char *getActionTypeName(MyActionType actionType)
{
	if (actionType == MyActionType::NONE)
		return "NONE";
	
	return ActionTypeNames[(int) actionType];
}

struct UnitPropsArray {
	UnitProps data[5];
	
	UnitPropsArray() {
		UnitProps &arv = data[(int) UnitType::ARV];
		arv.speed = 0.4;
		arv.viewRange = 60;
		arv.groundAttackRange = 0;
		arv.airAttackRange = 0;
		arv.groundDamage = 0;
		arv.airDamage = 0;
		arv.groundDefence = 50;
		arv.airDefence = 20;
		arv.buildTime = 60;

		UnitProps &fighter = data[(int) UnitType::FIGHTER];
		fighter.speed = 1.2;
		fighter.viewRange = 120;
		fighter.groundAttackRange = 0;
		fighter.airAttackRange = 20;
		fighter.groundDamage = 0;
		fighter.airDamage = 100;
		fighter.groundDefence = 70;
		fighter.airDefence = 70;
		fighter.buildTime = 90;
		
		UnitProps &helicopter = data[(int) UnitType::HELICOPTER];
		helicopter.speed = 0.9;
		helicopter.viewRange = 100;
		helicopter.groundAttackRange = 20;
		helicopter.airAttackRange = 18;
		helicopter.groundDamage = 100;
		helicopter.airDamage = 80;
		helicopter.groundDefence = 40;
		helicopter.airDefence = 40;
		helicopter.buildTime = 75;
		
		UnitProps &ifv = data[(int) UnitType::IFV];
		ifv.speed = 0.4;
		ifv.viewRange = 80;
		ifv.groundAttackRange = 18;
		ifv.airAttackRange = 20;
		ifv.groundDamage = 90;
		ifv.airDamage = 80;
		ifv.groundDefence = 60;
		ifv.airDefence = 80;
		ifv.buildTime = 60;
		
		UnitProps &tank = data[(int) UnitType::TANK];
		tank.speed = 0.3;
		tank.viewRange = 80;
		tank.groundAttackRange = 20;
		tank.airAttackRange = 18;
		tank.groundDamage = 100;
		tank.airDamage = 60;
		tank.groundDefence = 80;
		tank.airDefence = 60;
		tank.buildTime = 60;
	}
	
} unitPropsArray;

const UnitProps &getProps(UnitType unitType)
{
	return unitPropsArray.data[(int) unitType];
}

MicroShiftMatrix::MicroShiftMatrix() {
	MicroShiftValues v;
	
	std::vector<MicroShiftValues> &tankPos = pos[(int) UnitType::TANK];
	v.unitType = UnitType::IFV;
	v.val = 40;
	v.dist2 = 20*20;
	tankPos.push_back(v);
	
	v.unitType = UnitType::TANK;
	v.val = 20;
	v.dist2 = 20*20;
	tankPos.push_back(v);
	
	v.unitType = UnitType::TANK;
	v.val = 20;
	v.dist2 = 18*18;
	tankPos.push_back(v);
	
	std::vector<MicroShiftValues> &tankNeg = neg[(int) UnitType::TANK];
	v.unitType = UnitType::TANK;
	v.val = 20;
	v.dist2 = 20*20;
	tankNeg.push_back(v);
	
	v.unitType = UnitType::HELICOPTER;
	v.val = 40;
	v.dist2 = 20*20;
	tankNeg.push_back(v);
	
	v.unitType = UnitType::IFV;
	v.val = 10;
	v.dist2 = 18*18;
	tankNeg.push_back(v);
	
	
	
	std::vector<MicroShiftValues> &fighterPos = pos[(int) UnitType::FIGHTER];
	v.unitType = UnitType::HELICOPTER;
	v.val = 60;
	v.dist2 = 20*20;
	fighterPos.push_back(v);
	
	v.unitType = UnitType::FIGHTER;
	v.val = 30;
	v.dist2 = 20*20;
	fighterPos.push_back(v);
	
	std::vector<MicroShiftValues> &fighterNeg = neg[(int) UnitType::FIGHTER];
	v.unitType = UnitType::IFV;
	v.val = 10;
	v.dist2 = 20*20;
	fighterNeg.push_back(v);
	
	v.unitType = UnitType::HELICOPTER;
	v.val = 10;
	v.dist2 = 18*18;
	fighterNeg.push_back(v);
	
	v.unitType = UnitType::FIGHTER;
	v.val = 30;
	v.dist2 = 20*20;
	fighterNeg.push_back(v);
	
	
	
	std::vector<MicroShiftValues> &helicopterPos = pos[(int) UnitType::HELICOPTER];
	v.unitType = UnitType::TANK;
	v.val = 40;
	v.dist2 = 20*20;
	helicopterPos.push_back(v);
	
	v.unitType = UnitType::IFV;
	v.val = 20;
	v.dist2 = 20*20;
	helicopterPos.push_back(v);
	
	v.unitType = UnitType::HELICOPTER;
	v.val = 40;
	v.dist2 = 18*18;
	helicopterPos.push_back(v);
	
	v.unitType = UnitType::FIGHTER;
	v.val = 10;
	v.dist2 = 18*18;
	helicopterPos.push_back(v);
	
	std::vector<MicroShiftValues> &helicopterNeg = neg[(int) UnitType::HELICOPTER];
	v.unitType = UnitType::TANK;
	v.val = 20;
	v.dist2 = 18*18;
	helicopterNeg.push_back(v);
	
	v.unitType = UnitType::IFV;
	v.val = 40;
	v.dist2 = 20*20;
	helicopterNeg.push_back(v);
	
	v.unitType = UnitType::HELICOPTER;
	v.val = 40;
	v.dist2 = 18*18;
	helicopterNeg.push_back(v);
	
	v.unitType = UnitType::FIGHTER;
	v.val = 60;
	v.dist2 = 20*20;
	helicopterNeg.push_back(v);
	
	
	
	std::vector<MicroShiftValues> &ifvPos = pos[(int) UnitType::IFV];
	v.unitType = UnitType::HELICOPTER;
	v.val = 40;
	v.dist2 = 20*20;
	ifvPos.push_back(v);
	
	v.unitType = UnitType::IFV;
	v.val = 30;
	v.dist2 = 18*18;
	ifvPos.push_back(v);
	
	v.unitType = UnitType::TANK;
	v.val = 10;
	v.dist2 = 18*18;
	ifvPos.push_back(v);
	
	v.unitType = UnitType::FIGHTER;
	v.val = 10;
	v.dist2 = 20*20;
	ifvPos.push_back(v);
	
	std::vector<MicroShiftValues> &ifvNeg = neg[(int) UnitType::IFV];
	v.unitType = UnitType::TANK;
	v.val = 40;
	v.dist2 = 20*20;
	ifvNeg.push_back(v);
	
	v.unitType = UnitType::IFV;
	v.val = 30;
	v.dist2 = 18*18;
	ifvNeg.push_back(v);
	
	v.unitType = UnitType::HELICOPTER;
	v.val = 20;
	v.dist2 = 20*20;
	ifvNeg.push_back(v);
}

MicroShiftMatrix microShiftMatrix;

double getDamage(UnitType from, UnitType to)
{
	const UnitProps &p1 = getProps(from);
	const UnitProps &p2 = getProps(to);
	
	double result = 0.0;
	if (isGroundUnit(to))
		result += p1.groundDamage;
	else
		result += p1.airDamage;
	
	if (isGroundUnit(from))
		result -= p2.groundDefence;
	else
		result -= p2.airDefence;
	
	if (result < 0)
		return 0;
	
	return result;
}

double getAttackRange(UnitType from, UnitType to)
{
	const UnitProps &p1 = getProps(from);
	
	if (isGroundUnit(to))
		return p1.groundAttackRange;
	else
		return p1.airAttackRange;
}

Simulator::Simulator()
{
	units.reserve(2000);
}

void Simulator::synchonizeWith(const Simulator &oth, int mySide)
{
	if (tick < 0)
	{
		std::copy (oth.cells, oth.cells + CELLS_X * CELLS_Y, cells);
	}
	
	tick = oth.tick;
	enableFOW = oth.enableFOW;
	
	std::unordered_map<long, short> otherSimUnitMap;
	for (short ind = 0; ind < (short) oth.units.size(); ++ind)
	{
		const MyUnit &u = oth.units[ind];
		if (!enableFOW || u.visible || u.side == mySide /*|| mySide == 0*/)
			otherSimUnitMap[oth.units[ind].id] = ind;
	}
	
	units.erase(unstable_remove_if(units.begin(), units.end(), [&otherSimUnitMap](MyUnit &u){
		return !otherSimUnitMap.count(u.id);
	}), units.end());
		
	std::unordered_map<long, short> mySimUnitMap;
	for (short ind = 0; ind < (short) units.size(); ++ind)
	{
		mySimUnitMap[units[ind].id] = ind;
	}
	
	for (const MyUnit &p : oth.units)
	{
		MyUnit unit = p;
		if (enableFOW && !p.visible && p.side != mySide /*&& mySide != 0*/)
			continue;
			
		if (mySide == 1)
			unit.side = 1 - unit.side;
		
		if (!mySimUnitMap.count(unit.id))
		{
			unit.vel = P(0, 0);
			unit.moveStartPos = unit.pos;
			unit.lastMovedTick = tick;
			unit.activeMove = -1;
			units.push_back(unit);
		}
		else
		{
			MyUnit &myUnit = units[mySimUnitMap[unit.id]];
			unit.vel = unit.pos - myUnit.pos;
			if (unit.vel.len2() > 0.001)
				unit.lastMovedTick = tick;
			unit.activeMove = -1;
			myUnit = unit;
		}
	}
	
	for (int i = 0; i < 2; ++i)
	{
		int side = i;
		if (mySide == 1)
			side = 1 - i;
		
		players[i] = oth.players[side];
	}
	
	for (const Building &f : oth.buildings)
	{
		Building *b = nullptr;

		for (Building &bl : buildings) {
			if (bl.id == f.id) {
				b = &bl;
				break;
			}
		}
		
		if (!b) {
			buildings.push_back(Building());
			b = &*buildings.rbegin();
		}

		b->id = f.id;
		b->type = f.type;
		if (f.side == -1)
			b->side = -1;
		else if (f.side == mySide)
			b->side = 0;
		else
			b->side = 1;
		
		b->pos = f.pos;
		b->capturePoints = mySide == 0 ? f.capturePoints : -f.capturePoints;
		b->unitType = f.unitType;
		b->productionProgress = f.productionProgress;
	}
}

void Simulator::registerMove(const MyMove &move, int side)
{
	//LOG("REG MV " << side << " " << move);
	
	int ind = moves[side].size();
	moves[side].push_back(move);
	
	if (move.action == MyActionType::TACTICAL_NUCLEAR_STRIKE)
	{
		if (players[side].remainingNuclearStrikeCooldownTicks == 0)
		{
			players[side].remainingNuclearStrikeCooldownTicks = 1200;
			players[side].nextNuclearStrikeTick = tick + 30;
			players[side].nextNuclearStrikeVehicleId = move.vehicleId;
			players[side].nuclearStrike = move.p;
		}
		return;
	}
	
	if (move.action == MyActionType::SETUP_VEHICLE_PRODUCTION)
	{
		for (Building &b : buildings)
		{
			if (b.id == move.facilityId && b.side == side && b.type == BuildingType::VEHICLE_FACTORY)
			{
				b.unitType = move.unitType;
				b.productionProgress = 0.0;
			}
		}
		return;
	}
	
	for (MyUnit &myUnit : units)
	{
		if (myUnit.side == side)
		{
			if (move.action == MyActionType::CLEAR_AND_SELECT)
			{
				myUnit.selected = false;
			}
			
			if (move.action == MyActionType::CLEAR_AND_SELECT || move.action == MyActionType::ADD_TO_SELECTION || move.action == MyActionType::DESELECT)
			{
				bool select = move.action != MyActionType::DESELECT;
				if (move.group > 0)
				{
					assert(move.group <= MAX_GROUPS);
					if (myUnit.hasGroup(move.group))
						myUnit.selected = select;
				}
				else if (move.p1.x <= myUnit.pos.x && move.p1.y <= myUnit.pos.y && move.p2.x >= myUnit.pos.x && move.p2.y >= myUnit.pos.y)
				{
					if (move.unitType == UnitType::NONE || myUnit.type == move.unitType)
						myUnit.selected = select;
				}
			}
			
			if (myUnit.selected)
			{
				if (move.action == MyActionType::MOVE || move.action == MyActionType::ROTATE || move.action == MyActionType::SCALE)
				{
					myUnit.activeMove = ind;
					myUnit.moveStartPos = myUnit.pos;
				}
				else if (move.action == MyActionType::ASSIGN)
				{
					assert(move.group > 0 && move.group <= MAX_GROUPS);
					myUnit.addGroup(move.group);
				}
				else if (move.action == MyActionType::DISMISS)
				{
					assert(move.group > 0 && move.group <= MAX_GROUPS);
					myUnit.removeGroup(move.group);
				}
			}
			
			if (move.action == MyActionType::DISBAND)
			{
				assert(move.group > 0 && move.group <= MAX_GROUPS);
				myUnit.removeGroup(move.group);
			}
		}
	}
}

void Simulator::applyMoves()
{
	bool nuclearUnitFound[2] = {false, false};
	for (MyUnit &myUnit : units)
	{
		if (myUnit.activeMove >= 0)
		{
			const MyMove &move = moves[myUnit.side][myUnit.activeMove];
			double maxSpeed = getMaxSpeed(myUnit);
			if (move.maxSpeed > 0.0 && move.maxSpeed < maxSpeed)
				maxSpeed = move.maxSpeed;
			
			if (move.action == MyActionType::MOVE || move.action == MyActionType::SCALE)
			{
				P targetPoint = (move.action == MyActionType::MOVE) ? myUnit.moveStartPos + move.p : move.p + (myUnit.moveStartPos - move.p) * move.factor;
				P speed = targetPoint - myUnit.pos;
				double l2 = speed.len2();
				if (l2 > sqr(maxSpeed))
				{
					speed = speed.norm() * maxSpeed;
				}
				
				myUnit.vel = speed;
				
				if (l2 <= 1e-6)
				{
					myUnit.activeMove = -1;
				}
			}
			else if (move.action == MyActionType::ROTATE)
			{
				P r = myUnit.moveStartPos - move.p;
				if (r.len2() > 0.0)
				{
					double startAngle = r.getAngle();
					
					P curR = myUnit.pos - move.p;
					double angle = curR.getAngle();
					
					double deltaA = normalizeAngle(startAngle + move.angle - angle);
					if (std::abs(deltaA) > 1e-6)
					{
						double maxAngularSpeed = maxSpeed / r.len();
						if (move.maxAngularSpeed > 0.0 && maxAngularSpeed > move.maxAngularSpeed)
						{
							maxAngularSpeed = move.maxAngularSpeed;
						}
						
						if (std::abs(deltaA) < maxAngularSpeed)
						{
							maxAngularSpeed = deltaA;
						}
						else if (deltaA < 0)
						{
							maxAngularSpeed = -maxAngularSpeed;
						}
						
						myUnit.vel = move.p + r.rotate(angle - startAngle + maxAngularSpeed) - myUnit.pos;
					}
					else
					{
						myUnit.vel = P(0, 0);
						myUnit.activeMove = -1;
					}
				}
			}
		}
		
		MyPLayer &p = players[myUnit.side];
		if (p.nextNuclearStrikeTick >= 0 && p.nextNuclearStrikeVehicleId == myUnit.id)
		{
			nuclearUnitFound[myUnit.side] = true;
			double range = getVisionRange(myUnit);
			if (myUnit.pos.dist2(p.nuclearStrike) > sqr(range))
			{
				LOG("NUKE MISSED OUT OF RANGE " << myUnit.side);
				
				p.nextNuclearStrikeVehicleId = -1;
				p.nextNuclearStrikeTick = -1;
			}
		}
	}
	
	for (int i = 0; i < 2; ++i)
	{
		MyPLayer &p = players[i];
		if (!nuclearUnitFound[i] && p.nextNuclearStrikeVehicleId >= 0)
		{
			p.nextNuclearStrikeVehicleId = -1;
			p.nextNuclearStrikeTick = -1;
			
			LOG("NUKE MISSED DEAD " << i);
		}
	}
}

void Simulator::step()
{
	++tick;
	resetCells();
	resetAxisSorts();
	applyMoves();
	moveUnits();
	attackAndHealUnits();
	updateBuildings();
	/*resetCells();
	resetAxisSorts();*/
	
	for (int i = 0; i < 2; ++i)
	{
		if (players[i].remainingNuclearStrikeCooldownTicks > 0)
			players[i].remainingNuclearStrikeCooldownTicks--;
	}
}

void Simulator::resetCells()
{
	for (int i = 0; i < MICROCELLS_NUMBER; ++i)
		microcells[i].clear();
	
	for (short ind = 0; ind < (short) units.size(); ++ind)
	{
		MyUnit &p = units[ind];
		int x = p.pos.x / MICROCELL_SIZE;
		int y = p.pos.y / MICROCELL_SIZE;
		getMicrocell(x, y, p.side, p.type).push_back(ind);
	}
}

void Simulator::resetAxisSorts()
{
	unitsSortedByX.clear();
	unitsSortedByY.clear();
	for (short ind = 0; ind < (short) units.size(); ++ind)
	{
		unitsSortedByX.push_back(ind);
		unitsSortedByY.push_back(ind);
	}
	
	std::sort(unitsSortedByX.begin(), unitsSortedByX.end(), [this](short ind1, short ind2){
		double x1 = units[ind1].pos.x;
		double x2 = units[ind2].pos.x;
		return x1 < x2;
	});
	
	std::sort(unitsSortedByY.begin(), unitsSortedByY.end(), [this](short ind1, short ind2){
		double y1 = units[ind1].pos.y;
		double y2 = units[ind2].pos.y;
		return y1 < y2;
	});
}

bool checkUnitPosInArray(const std::vector<P> &points, const P &p)
{
	for (const P &o : points)
	{
		if (o.dist2(p) < sqr(4.0))
			return false;
	}
	
	return true;
}

void Simulator::updateBuildings()
{
	if (!buildings.empty())
	{
		std::vector<P> unitsInBuildings[20];
		
		for (const MyUnit &p : units)
		{
			bool groudUnit = isGroundUnit(p.type);
			if (groudUnit)
			{
				for (Building &b : buildings)
				{
					if (b.checkPoint(p.pos))
					{
						if (p.side == 0)
							b.capturePoints += 0.005;
						else
							b.capturePoints -= 0.005;
					}
				}
			}
			
			for (int i = 0; i < buildings.size(); ++i)
			{
				Building &b = buildings[i];
				if (b.side >= 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE && b.productionProgress >= 59 && b.checkPointWithRad(p.pos))
				{
					bool factoryGroundUnits = isGroundUnit(b.unitType);
					if (groudUnit && factoryGroundUnits || !groudUnit && !factoryGroundUnits && b.side == p.side)
					{
						int buildTime = getProps(b.unitType).buildTime;
						
						if (b.productionProgress >= buildTime - 1)
							unitsInBuildings[i].push_back(p.pos);
					}
				}
			}
		}
		
		for (int i = 0; i < buildings.size(); ++i)
		{
			Building &b = buildings[i];
			b.capturePoints = clamp(b.capturePoints, -100.0, 100.0);
			
			if ((b.side == 0 && b.capturePoints <= 0.0) || (b.side == 1 && b.capturePoints >= 0.0))
			{
				b.side = -1;
				b.unitType = UnitType::NONE;
				b.productionProgress = 0.0;
			}
			
			if (b.capturePoints == 100.0 && b.side == -1)
			{
				b.side = 0;
				players[0].score += 100;
			}
			else if (b.capturePoints == -100.0 && b.side == -1)
			{
				b.side = 1;
				players[1].score += 100;
			}
			
			if (b.side != -1 && b.unitType != UnitType::NONE)
			{
				b.productionProgress++;
				int buildTime = getProps(b.unitType).buildTime;
				if (b.productionProgress >= buildTime)
				{
					for (int ipos = 0; ipos < 11*11; ++ipos)
					{
						int x = ipos % 11;
						int y = ipos / 11;
						
						P pos;
						if (b.side == 0)
							pos = b.pos - P(30, 30) + P(x, y) * 6.0;
						else
							pos = b.pos + P(30, 30) - P(x, y) * 6.0;
						
						if (checkUnitPosInArray(unitsInBuildings[i], pos))
						{
							MyUnit newUnit;
							newUnit.side = b.side;
							newUnit.pos = pos;
							newUnit.id = idSeq++;
							newUnit.type = b.unitType;
							units.push_back(newUnit);
							
							unitStats[newUnit.side].unitStats[(int) newUnit.type].produced++;
							
							b.productionProgress = 0;
							break;
						}
					}
					
					if (b.productionProgress >= buildTime)
					{
						b.productionProgress = buildTime - 1;
					}
				}
			}
		}
	}
}

double Simulator::getMaxSpeed(const MyUnit &unit) const
{
	double result = unitVel(unit.type);
	int cellX = unit.pos.x / CELL_SIZE;
	int cellY = unit.pos.y / CELL_SIZE;
	
	if (cellX < 0 || cellX >= CELLS_X || cellY < 0 || cellY >= CELLS_Y)
		return result;
	
	const Cell &c = cell(cellX, cellY);
	bool isGroud = isGroundUnit(unit.type);
	if (isGroud)
	{
		if (c.groundType == GroundType::FOREST)
			return result * 0.8;
		
		if (c.groundType == GroundType::SWAMP)
			return result * 0.6;
	}
	else
	{
		if (c.weatherType == MyWeatherType::CLOUDY)
			return result * 0.8;
		
		if (c.weatherType == MyWeatherType::RAIN)
			return result * 0.6;
	}
	
	return result;
}

double Simulator::getVisionRange(const MyUnit &unit) const
{
	const UnitProps &props = getProps(unit.type);
	double result = props.viewRange;
	
	int cellX = unit.pos.x / CELL_SIZE;
	int cellY = unit.pos.y / CELL_SIZE;
	
	if (cellX < 0 || cellX >= CELLS_X || cellY < 0 || cellY >= CELLS_Y)
		return result;
	
	const Cell &c = cell(cellX, cellY);
	bool isGroud = isGroundUnit(unit.type);
	if (isGroud)
	{
		if (c.groundType == GroundType::FOREST)
			return result * 0.8;
		
		if (c.groundType == GroundType::SWAMP)
			return result;
	}
	else
	{
		if (c.weatherType == MyWeatherType::CLOUDY)
			return result * 0.8;
		
		if (c.weatherType == MyWeatherType::RAIN)
			return result * 0.6;
	}
	
	return result;
}

void Simulator::updateVisionRangeAndStealthFactor()
{
	for (MyUnit &unit : units)
	{
		const UnitProps &props = getProps(unit.type);
		double viewRange = props.viewRange;
		
		int cellX = unit.pos.x / CELL_SIZE;
		int cellY = unit.pos.y / CELL_SIZE;
		
		const Cell &c = cell(cellX, cellY);
		bool isGroud = isGroundUnit(unit.type);
		unit.visionRange = viewRange;
		unit.stealthFactor = 1.0;
		if (isGroud)
		{
			if (c.groundType == GroundType::FOREST)
			{
				unit.visionRange = viewRange * 0.8;
				unit.stealthFactor = 0.8;
			}
		}
		else
		{
			if (c.weatherType == MyWeatherType::CLOUDY)
			{
				unit.visionRange = viewRange * 0.8;
				unit.stealthFactor = 0.8;
			}
			else if (c.weatherType == MyWeatherType::RAIN)
			{
				unit.visionRange = viewRange * 0.6;
				unit.stealthFactor = 0.6;
			}
		}
	}
}

void Simulator::updateFOW(int ignoreViewSide)
{
	updateVisionRangeAndStealthFactor();
	
	constexpr int VIS_CELL_SIZE = 32;
	constexpr int W = WIDTH / VIS_CELL_SIZE;
	constexpr int H = HEIGHT / VIS_CELL_SIZE;
	std::vector<MyUnit *> unitsInCells[W*H*2];
	
	for (MyUnit &u : units)
	{
		u.visible = false;
		int x = u.pos.x / VIS_CELL_SIZE;
		int y = u.pos.y / VIS_CELL_SIZE;
		
		assert(x >= 0 && x < W);
		assert(y >= 0 && y < H);
		
		unitsInCells[y * W + x + u.side * (W * H)].push_back(&u);
	}
	
	for (MyUnit &u : units)
	{
		if (u.side == ignoreViewSide)
			continue;
		
		double range = u.visionRange;
		int minX = std::max(0.0, (u.pos.x - range)) / VIS_CELL_SIZE;
		int maxX = std::min(WIDTH - 1.0, (u.pos.x + range)) / VIS_CELL_SIZE;
		int minY = std::max(0.0, (u.pos.y - range)) / VIS_CELL_SIZE;
		int maxY = std::min(HEIGHT - 1.0, (u.pos.y + range)) / VIS_CELL_SIZE;
		double visionRange2 = sqr(u.visionRange);
		
		for (int y = minY; y <= maxY; ++y)
		{
			for (int x = minX; x <= maxX; ++x)
			{
				P center = P(x + 0.5, y + 0.5) * VIS_CELL_SIZE;
				P nearPoint = center;
				if (center.x < u.pos.x)
					nearPoint.x += VIS_CELL_SIZE * 0.5;
				else
					nearPoint.x -= VIS_CELL_SIZE * 0.5;
				
				if (center.y < u.pos.y)
					nearPoint.y += VIS_CELL_SIZE * 0.5;
				else
					nearPoint.y -= VIS_CELL_SIZE * 0.5;
				
				if (nearPoint.dist2(u.pos) < visionRange2)
				{
					std::vector<MyUnit *> &othUnits = unitsInCells[y * W + x + (1 - u.side) * (W * H)];
					for (MyUnit *othUnit : othUnits)
					{
						if (!othUnit->visible)
						{
							double othD2 = u.pos.dist2(othUnit->pos);
							if (othD2 <= visionRange2 * sqr(othUnit->stealthFactor))
							{
								othUnit->visible = true;
							}
						}
					}
				}
			}
		}
	}
}

void Simulator::registerAction()
{
	actionTicks.insert(tick);
	while(true) {
		auto it = actionTicks.begin();
		if (it == actionTicks.end() || *it >= tick - 60)
			break;
		
		actionTicks.erase(it);
	}
}

int Simulator::getAvailableActions(int actionsPerMinute) const
{
	size_t actions = std::distance(actionTicks.lower_bound(tick - 59), actionTicks.end());
	return actionsPerMinute - (int) actions;
}

int Simulator::getAvailableActions(int actionsPerMinute, int timeRange) const
{
	size_t actions = std::distance(actionTicks.lower_bound(tick - timeRange), actionTicks.end());
	return actionsPerMinute - (int) actions;
}

bool Simulator::anyMoved(UnitType unitType, int side) const
{
	for (const MyUnit &p : units)
	{
		if ((p.type == unitType || unitType == UnitType::NONE) && p.side == side && p.lastMovedTick >= (tick - 1))
		{
			return true;
		}
	}
	
	return false;
}

bool Simulator::anyMoved(const Group &g) const
{
	for (const MyUnit &p : units)
	{
		if (p.lastMovedTick >= (tick - 1) && g.check(p))
		{
			return true;
		}
	}
	
	return false;
}

bool unitsCanIntersect(const MyUnit &u1, const MyUnit &u2)
{
	bool gr1 = isGroundUnit(u1.type);
	bool gr2 = isGroundUnit(u2.type);
	if (gr1 != gr2)
		return false;
	
	if (!gr1)
		return u1.side == u2.side;
	
	return true;
}

template<typename ITER, int vecComp, int dir>
int moveUnitsTempl(Simulator &sim, ITER itBegin, ITER itEnd)
{
	int count = 0;
	auto prevIt = itBegin;
	for (auto it = itBegin; it != itEnd; ++it)
	{
		short ind = *it;
		MyUnit &unit = sim.units[ind];
		double velComp = unit.vel.comp<vecComp>();
		
		if (unit.lastMovedTick < sim.tick && ((dir == 0 && velComp < 0.0) || (dir == 1 && velComp > 0.0)))
		{
			P newPos = unit.pos + unit.vel;
			if (checkUnitWorldBounds(newPos))
			{
				bool collided = false;
				for (auto it2 = prevIt; it2 != itEnd; ++it2)
				{
					if (it2 != it)
					{
						MyUnit &othUnit = sim.units[*it2];
						if (unitsCanIntersect(unit, othUnit))
						{
							if (dir == 0 && othUnit.pos.comp<vecComp>() > unit.pos.comp<vecComp>() + SCAN_WIDTH)
								break;
							
							if (dir == 1 && othUnit.pos.comp<vecComp>() < unit.pos.comp<vecComp>() - SCAN_WIDTH)
								break;
							
							double d2 = othUnit.pos.dist2(newPos);
							if (d2 < UNIT_RAD_DOUBLED2)
							{
								collided = true;
								break;
							}
						}
					}
				}
				
				if (!collided)
				{
					unit.pos = newPos;
					unit.lastMovedTick = sim.tick;
					++count;
				}
			}
		}
		
		for (; prevIt != itEnd; ++prevIt)
		{
			MyUnit &othUnit = sim.units[*prevIt];
			if (dir == 0 && othUnit.pos.comp<vecComp>() > unit.pos.comp<vecComp>() - SCAN_WIDTH)
				break;
			
			if (dir == 1 && othUnit.pos.comp<vecComp>() < unit.pos.comp<vecComp>() + SCAN_WIDTH)
				break;
		}
	}
	
	return count;
}

int Simulator::moveUnits()
{
	int total = 0;
	for (int k = 0; k < 100; ++k)
	{
		int count = 0;
		
		count += moveUnitsTempl<std::vector<short>::iterator, 0, 0>(*this, unitsSortedByX.begin(), unitsSortedByX.end());
		count += moveUnitsTempl<std::vector<short>::iterator, 1, 0>(*this, unitsSortedByY.begin(), unitsSortedByY.end());
		count += moveUnitsTempl<std::vector<short>::reverse_iterator, 0, 1>(*this, unitsSortedByX.rbegin(), unitsSortedByX.rend());
		count += moveUnitsTempl<std::vector<short>::reverse_iterator, 1, 1>(*this, unitsSortedByY.rbegin(), unitsSortedByY.rend());
		
		total += count;
		if (!count)
			break;
	}
	
	return total;
}

void Simulator::attackAndHealUnits()
{
	std::vector<MyUnit *> otherUnits[5];
	std::vector<MyUnit *> unitsToHeal;
	double posDamage[5];
	
	for (int i = 0; i < 2; ++i)
	{
		MyPLayer &p = players[i];
		if (p.nextNuclearStrikeTick == tick)
		{
			int x = p.nuclearStrike.x / MICROCELL_SIZE;
			int y = p.nuclearStrike.y / MICROCELL_SIZE;
			
			for (int yy =-1; yy <= 1; ++yy)
			{
				for (int xx =-1; xx <= 1; ++xx)
				{
					int x2 = x + xx;
					int y2 = y + yy;
					
					if (x2 >= 0 && x2 < MICROCELLS_X && y2 >= 0 && y2 < MICROCELLS_Y)
					{
						for (int side = 0; side < 2; ++side)
						{
							for (int unitType = 0; unitType < 5; ++unitType)
							{
								std::vector<short> &otherMicroCell = getMicrocell(x2, y2, side, (UnitType) unitType);
								for (short otherInd : otherMicroCell)
								{
									MyUnit &unit = units[otherInd];
									double l2 = p.nuclearStrike.dist2(unit.pos);
									if (l2 < sqr(50.0))
									{
										double damage = 99.0 * (50.0 - std::sqrt(l2)) / 50.0;
										unit.durability -= damage;
										unitStats[unit.side].unitStats[(int) unit.type].damageByNuke += damage;
									}
								}
							}
						}
					}
				}
			}
			
			p.nextNuclearStrikeTick = -1;
			p.nextNuclearStrikeVehicleId = -1;
		}
	}
	
	for (int y = 0; y < MICROCELLS_Y; ++y)
	{
		for (int x = 0; x < MICROCELLS_X; ++x)
		{
			for (int side = 0; side <= 1; ++side)
			{
				for (int unitType = 0; unitType < 5; ++unitType)
				{
					std::vector<short> &microCell = getMicrocell(x, y, side, (UnitType) unitType);
					for (short ind : microCell)
					{
						MyUnit &myUnit = units[ind];
						if (myUnit.attackCooldown > 0)
						{
							myUnit.attackCooldown--;
							continue;
						}
						
						if (myUnit.durability <= 0.0)
							continue;
						
						for (int i = 0; i < 5; ++i)
						{
							otherUnits[i].clear();
							posDamage[i] = 0;
						}
						
						for (int othUnitType = 0; othUnitType < 5; ++othUnitType)
						{
							double damage = getDamage((UnitType) unitType, (UnitType) othUnitType);
							double range2 = sqr(getAttackRange((UnitType) unitType, (UnitType) othUnitType));
							
							if (damage > 0 && range2 > 0.0)
							{
								for (int yy =-1; yy <= 1; ++yy)
								{
									for (int xx =-1; xx <= 1; ++xx)
									{
										int x2 = x + xx;
										int y2 = y + yy;
										
										if (x2 >= 0 && x2 < MICROCELLS_X && y2 >= 0 && y2 < MICROCELLS_Y)
										{
											std::vector<short> &otherMicroCell = getMicrocell(x2, y2, 1 - side, (UnitType) othUnitType);
											
											for (short otherInd : otherMicroCell)
											{
												MyUnit &otherUnit = units[otherInd];
												if (otherUnit.durability <= 0.0)
													continue;
												
												if (myUnit.pos.dist2(otherUnit.pos) < range2)
												{
													otherUnits[othUnitType].push_back(&otherUnit);
												}
											}

										}
									}
								}
								
								if (!otherUnits[othUnitType].empty())
									posDamage[othUnitType] = damage;
							}
						}
						
						double totalDamage = 0.0;
						for (int i = 0; i < 5; ++i)
							totalDamage += posDamage[i] * otherUnits[i].size();
						
						if (totalDamage > 0.0)
						{
							double dmg = random.getDouble() * totalDamage;
							
							for (int i = 0; i < 5; ++i)
							{
								double totalForUnitType = posDamage[i] * otherUnits[i].size();
								if (dmg < totalForUnitType)
								{
									int ind = dmg / posDamage[i];
									MyUnit *unit = otherUnits[i][ind];
									unit->durability -= posDamage[i];
									unitStats[myUnit.side].unitStats[(int) myUnit.type].damageMade += posDamage[i];
									myUnit.attackCooldown = 59;
									break;
								}
								
								dmg -= totalForUnitType;
							}
						}
					}
				}
				
				////////////// ARV
				std::vector<short> &microCell = getMicrocell(x, y, side, UnitType::ARV);
				for (short ind : microCell)
				{
					MyUnit &myUnit = units[ind];
					if (myUnit.durability <= 0.0)
						continue;
					unitsToHeal.clear();
					
					for (int othUnitType = 0; othUnitType < 5; ++othUnitType)
					{
						for (int yy = -1; yy <= 1; ++yy)
						{
							for (int xx = -1; xx <= 1; ++xx)
							{
								int x2 = x + xx;
								int y2 = y + yy;
								
								if (x2 >= 0 && x2 < MICROCELLS_X && y2 >= 0 && y2 < MICROCELLS_Y)
								{
									std::vector<short> &otherMicroCell = getMicrocell(x2, y2, side, (UnitType) othUnitType);
									
									for (short otherInd : otherMicroCell)
									{
										MyUnit &otherUnit = units[otherInd];
										if (otherUnit.durability <= 0.0)
											continue;
										if (otherUnit.durability < 100.0 && otherUnit.durability > 0.0 && myUnit.pos.dist2(otherUnit.pos) < HEAL_RANGE2)
										{
											unitsToHeal.push_back(&otherUnit);
										}
									}
								}
							}
						}
					}
					
					double totalToHeal = 0.0;
					for (MyUnit *myUnit: unitsToHeal)
					{
						totalToHeal += (100.0 - myUnit->durability);
					}
					
					if (totalToHeal > 0.0)
					{
						double r = random.getDouble() * totalToHeal;
						
						for (MyUnit *myUnit: unitsToHeal)
						{
							r -= (100.0 - myUnit->durability);
							if (r <= 0.0)
							{
								myUnit->durability += 0.1;
								unitStats[myUnit->side].healed += 0.1;
								break;
							}
						}
					}
				}
			}
		}
	}
	
	units.erase(unstable_remove_if(units.begin(), units.end(), [this](MyUnit &u){
		if (u.durability < 1.0)
		{
			players[1 - u.side].score++;
			unitStats[u.side].unitStats[(int) u.type].died++;
			return true;
		}
		return false;
	}), units.end());
}

void Simulator::updateStats()
{
	myCount.clear();
	enemyCount.clear();
	nukeVehicleInd = -1;
	selectCount = 0;
	
	for (int i = 0; i < units.size(); ++i)
	{
		const MyUnit &p = units[i];
		
		if (p.side == 0)
		{
			myCount[p.type]++;
			if (p.selected)
				++selectCount;
		}
		else
		{
			enemyCount[p.type]++;
		}
		
		if (p.id == players[0].nextNuclearStrikeVehicleId)
			nukeVehicleInd = i;
	}
	
	for (Group &group : groups)
	{
		group.center = P(0, 0);
		group.bbox.p1 = P(WIDTH, HEIGHT);
		group.bbox.p2 = P(0, 0);
		group.size = 0;
		for (int i = 0 ; i < 5; ++i) {
			group.sizeByTypes[i] = 0;
			group.healthByTypes[i] = 0.0;
		}
		
		group.health = 0.0;
	}
	
	for (Building &b : buildings)
	{
		b.unitCount = 0;
		for (int i = 0; i < 5; ++i)
			b.unitCountByType[i] = 0;
	}
	
	for (Group &group : groups)
	{
		group.unitInd = -1;
	}
		
	for (int i = 0; i < units.size(); ++i)
	{
		const MyUnit &p = units[i];
		for (Group &group : groups)
		{
			if (group.check(p))
			{
				group.center += p.pos;
				++group.size;
				++group.sizeByTypes[(int) p.type];
				group.health += p.durability;
				group.healthByTypes[(int) p.type] += p.durability;
				group.bbox.p1 = P(std::min(group.bbox.p1.x, p.pos.x), std::min(group.bbox.p1.y, p.pos.y));
				group.bbox.p2 = P(std::max(group.bbox.p2.x, p.pos.x), std::max(group.bbox.p2.y, p.pos.y));
				
				if (group.unitId >= 0)
					group.unitInd = i;
			}
		}
		
		if (!p.groups.any())
		{
			for (Building &b : buildings)
			{
				if (p.side == 0 && b.checkPoint(p.pos))
				{
					b.unitCount++;
					b.unitCountByType[(int) p.type]++;
				}
			}
		}
	}
	
	for (Group &group : groups)
	{
		if (group.size)
		{
			group.center /= group.size;
		}
	}
}

double unitVel(UnitType unitType)
{
	switch (unitType)
	{
		case UnitType::ARV:
			return 0.4;
		case UnitType::FIGHTER:
			return 1.2;
		case UnitType::HELICOPTER:
			return 0.9;
		case UnitType::IFV:
			return 0.4;
		case UnitType::TANK:
			return 0.3;
		case UnitType::NONE:
			return 0.3;
		default:
			assert(false);
			return 0.0;
	}
	
	assert(false);
	return 1.0;
}

bool Group::check(const MyUnit &u) const
{
	if (u.side != 0)
		return false;
	
	if (group > 0)
	{
		return u.hasGroup(group);
	}
	
	if (unitId >= 0)
	{
		return u.id == unitId;
	}
	
	assert(unitType != UnitType::NONE);
	return u.type == unitType;
}

bool Group::canIntersectWith(const MyUnit &u) const
{
	bool gr1 = isGroundUnit(unitType);
	bool gr2 = isGroundUnit(u.type);
	if (gr1 != gr2)
		return false;
	
	if (unitType == UnitType::ARV && gr2)
		return true;
	
	if (u.side > 0)
	{
		return false;
	}
	
	/*if (!gr1)
		return u.side == 0;*/
	
	return true;
}

// Is any selected
bool Simulator::isSelected(const Group &group) const
{
	for (const MyUnit &p : units)
	{
		if (p.selected && group.check(p))
			return true;
	}
	
	return false;
}

MyMove Simulator::select(const Group &group)
{
	MyMove result;
	
	if (group.unitId >= 0)
	{
		const MyUnit &u = units[group.unitInd];
		result.p1 = u.pos - P(2, 2);
		result.p2 = u.pos + P(2, 2);
	}
	else
	{
		result.group = group.group;
	}
	result.unitType = group.unitType;
	result.action = MyActionType::CLEAR_AND_SELECT;
	
	return result;
}

bool Simulator::canMoveNuke(const P &shift, const Group &group) const
{
	if (nukeVehicleInd >= 0 && shift.len2() > 0.0)
	{
		MyUnit u = units[nukeVehicleInd];
		if (group.check(u))
		{
			P vel = shift.norm() * unitVel(u.type);
			for (int i = tick; i < players[0].nextNuclearStrikeTick; ++i)
			{
				u.pos += vel;
				
				double range = getVisionRange(u) - 1.0;
				if (u.pos.dist2(players[0].nuclearStrike) >= sqr(range))
					return false;
			}
		}
	}
	
	return true;
}

bool Simulator::canMove(const P &shift, const Group &group) const
{
	if (!canMoveNuke(shift, group))
		return false;
	
	BBox bbox = group.bbox;
	const int cnt = 4;
	const double eps = 10.0;
	bbox.p1 -= P(eps, eps);
	bbox.p2 += P(eps, eps);
	const P ds = shift / (float) cnt;
	const double shiftDt = ds.len() / unitVel(group.unitType);
	
	for (const MyUnit &p : units)
	{
		if (!group.check(p) && group.canIntersectWith(p))
		{
			P pos = p.pos;
			const P dp = p.vel * shiftDt;
			
			for (int i = 1; i <= cnt; ++i)
			{
				P p1 = bbox.p1 + ds*i;
				P p2 = bbox.p2 + ds*i;
				pos += dp;
				if (pos.x > p1.x && pos.y > p1.y && pos.x < p2.x && pos.y < p2.y)
					return false;
			}
		}
	}
	
	return true;
}

bool Simulator::canMoveDetailed(const P &shift, const Group &group, const std::vector<const MyUnit *> &groupUnits, const std::vector<const MyUnit *> &otherUnits) const
{
	if (!canMoveNuke(shift, group))
		return false;
	
	BBox bbox = group.bbox;
	bbox.p1 -= P(4.5, 4.5);
	bbox.p2 += P(4.5, 4.5);
	const int cnt = 4;
	const P ds = shift / (float) cnt;
	const double shiftDt = ds.len() / unitVel(group.unitType);
	const double totalShiftDt = shiftDt * cnt;
	
	for (const MyUnit *p : otherUnits)
	{
		P pos = p->pos;
		const P dp = p->vel * shiftDt;
		
		bool intersectsBBox = false;
		for (int i = 1; i <= cnt; ++i)
		{
			P p1 = bbox.p1 + ds*i;
			P p2 = bbox.p2 + ds*i;
			pos += dp;
			if (pos.x > p1.x && pos.y > p1.y && pos.x < p2.x && pos.y < p2.y)
			{
				intersectsBBox = true;
				break;
			}
		}
		
		if (intersectsBBox)
		{
			for (const MyUnit *myp : groupUnits)
			{
				P p2 = myp->pos + shift - p->vel * totalShiftDt;
				P cp = closestPointToSegment(myp->pos, p2, p->pos);
				if (cp.dist2(p->pos) < sqr(4))
					return false;
			}
		}
	}
	
	return true;
}

Group *Simulator::getGroup(UnitType unitType)
{
	int size = 0;
	Group *res = nullptr;
	for (Group &g : groups)
	{
		if (g.unitType == unitType && size < g.size)
		{
			size = g.size;
			res = &g;
		}
	}
	return res;
}
