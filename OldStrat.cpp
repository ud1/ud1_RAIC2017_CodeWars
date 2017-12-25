#include "OldStrat.hpp"

#include <algorithm>
#include <iostream>

namespace StratV8 {
	
double groupPriority(const Group &g) {
	double res = 0.0;
	
	if (g.actionStarted)
		res += 1000.0;
	
	res -= g.lastUpdateTick;
	
	return res;
}

Strat::Strat::Strat()
{
	Group g;
	g.actionStarted = false;
	g.lastUpdateTick = 0;
	g.lastShrinkTick = 0;
	
	g.unitType = UnitType::HELICOPTER;
	groups.push_back(g);
	
	g.unitType = UnitType::IFV;
	groups.push_back(g);
	
	g.unitType = UnitType::FIGHTER;
	groups.push_back(g);
	
	g.unitType = UnitType::TANK;
	groups.push_back(g);
	
	g.unitType = UnitType::ARV;
	groups.push_back(g);
}

void Strat::calcMicroShift(Group &group, P &shift)
{
	if (group.unitType != UnitType::TANK && group.unitType != UnitType::FIGHTER)
		return;
	
	BBox bbox = group.bbox;
	bbox.p1 += shift - P(35, 35);
	bbox.p2 += shift + P(35, 35);
	
	int finalScore = -100000;
	int currentScore = -100000;
	double ticks = 20.0;
	
	//if (group.unitType == UnitType::TANK)
	{
		std::vector<P> enemies[(int) UnitType::COUNT];
		std::vector<P> myUnits;
		
		for (const MyUnit &u : units)
		{
			if (u.side > 0 && (u.type == UnitType::TANK || u.type == UnitType::HELICOPTER || u.type == UnitType::IFV) && bbox.inside(u.pos))
			{
				enemies[(int) u.type].push_back(u.pos + u.vel * ticks);
			}
			else if (group.check(u))
			{
				myUnits.push_back(u.pos);
			}
		}
		
		double L = 8.0;
		P newShift = shift;
		for (int k = 0; k < 4; ++k)
		{
			P foundShift = newShift;
			int foundScore = -100000;
			for (int i = 0; i < 9; ++i)
			{
				P testShift = newShift + P(i / 3 - 1, i % 3 - 1) * L;
				int score = 0;
				bool isCurShift = shift.dist2(testShift) < 0.1;
				
				if (!isCurShift || currentScore == -100000)
				{
					const std::vector<MicroShiftValues> &pos = microShiftMatrix.pos[(int) group.unitType];
					if (!pos.empty())
					{
						for (const P &myP : myUnits)
						{
							P myPos = myP + testShift;
							bool found = false;
							for (const MicroShiftValues &vals : pos)
							{
								if (!found)
								{
									for (const P &enP : enemies[(int) vals.unitType])
									{
										if (myPos.dist2(enP) < vals.dist2)
										{
											score += vals.val;
											found = true;
											break;
										}
									}
								}
							}
						}
					}
					
					// !! decrease SCORE
					int posScore = score;
					score /= 2;
					
					const std::vector<MicroShiftValues> &neg = microShiftMatrix.neg[(int) group.unitType];
					if (!neg.empty())
					{
						for (const MicroShiftValues &vals : neg)
						{
							for (const P &enP : enemies[(int) vals.unitType])
							{
								for (const P &myP : myUnits)
								{
									P myPos = myP + testShift;
									if (myPos.dist2(enP) < vals.dist2)
									{
										score -= vals.val;
										break;
									}
								}
							}
						}
					}
					
					int negScore = posScore / 2 - score;
					
					if (foundScore < score)
					{
						foundShift = testShift;
						foundScore = score;
					}
					
					if (isCurShift)
						currentScore = score;
				}
			}
			
			L /= 2.0;
			newShift = foundShift;
			
			finalScore = foundScore;
		}
		
		if (currentScore >= finalScore)
			return;
		
		shift = newShift;
	}
}

MyMove Strat::calcNextMove()
{
	MyMove result;
	result.action = MyActionType::NONE;
	
	int moves = getAvailableActions(12);
	if (moves < 1)
		return result;
	
	bool angryMode = angryModeTill > tick;

	updateStats();
	
	if (players[0].remainingNuclearStrikeCooldownTicks == 0)
	{
		resetCells();
		
		auto getEnemySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0;
			
			return (int) getMicrocell(x, y, 1, UnitType::FIGHTER).size() / 2
				+ (int) getMicrocell(x, y, 1, UnitType::HELICOPTER).size() / 2
				+ (int) getMicrocell(x, y, 1, UnitType::IFV).size()
				+ (int) getMicrocell(x, y, 1, UnitType::TANK).size()
				- (int) getMicrocell(x, y, 1, UnitType::ARV).size() / 4;
		};
		
		auto getMySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0;
			
			return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
				+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
				+ (int) getMicrocell(x, y, 0, UnitType::ARV).size() / 2;
		};
		
		P bestTarget;
		int bestScore = 0;
		
		for (int y = 0; y < MICROCELLS_Y; ++y) {
			for (int x = 0; x < MICROCELLS_X; ++x) {
				int enemyN = getEnemySize(x, y);
				if (enemyN > 10)
				{
					int totalMyCount = 0;
					int myCenterCount = 0;
					for (int xx = -1; xx <= 1; ++xx)
					{
						for (int yy = -1; yy <= 1; ++yy)
						{
							totalMyCount += getMySize(x + xx, y + yy);
							if (xx == 0 && yy == 0)
								myCenterCount = totalMyCount;
						}
					}
					
					if (totalMyCount < 20 && myCenterCount == 0)
					{
						int totalMyExtCount = totalMyCount;
						if (totalMyExtCount == 0)
						{
							for (int xx = -2; xx <= 2; ++xx)
							{
								for (int yy = -2; yy <= 2; ++yy)
								{
									if (xx < -1 || xx > 1 || yy < -1 || yy > 1)
										totalMyExtCount += getMySize(x + xx, y + yy);
								}
							}
						}
						
						if (totalMyExtCount > 0)
						{
							int totalEnemyCount =
								enemyN +
								getEnemySize(x - 1, y)/2 +
								getEnemySize(x + 1, y)/2 + 
								getEnemySize(x, y - 1)/2 + 
								getEnemySize(x, y + 1)/2;
							
							if (bestScore < totalEnemyCount)
							{
								bestScore = totalEnemyCount;
								bestTarget = P(x + 0.5, y + 0.5) * MICROCELL_SIZE;
							}
						}
					}
				}
			}
		}
		
		if (bestScore > 0)
		{
			// Correct point
			P correctedBestTarget = bestTarget;
			
			std::vector<P> myVehicles;
			std::vector<P> enemyVehicles;
			int myInnerN = 0;
			int enemyInnerN = 0;
			for (const MyUnit &p : units)
			{
				double d2 = p.pos.dist2(bestTarget);
				if (d2 < (75.0*75.0))
				{
					if (d2 < (38.0*38.0))
					{
						if (p.side == 0)
							myInnerN++;
						else
							enemyInnerN++;
					}
					else
					{
						if (p.side == 0)
							myVehicles.push_back(p.pos);
						else
							enemyVehicles.push_back(p.pos);
					}
				}
			}
			
			int foundScore = -100000;
			for (int xx = -2; xx <= 2; ++xx)
			{
				for (int yy = -2; yy <= 2; ++yy)
				{
					P newTarget = bestTarget + P(xx, yy) * 6.0;
					
					int myNum = myInnerN*3;
					int myExtNum = 0;
					
					for (const P &p : myVehicles)
					{
						double d2 = p.dist2(newTarget);
						if (d2 < (50.0*50.0))
						{
							myNum++;
						}
						else if (d2 > (60.0*60.0) && d2 < (70.0*70.0))
						{
							myExtNum++;
						}
					}
					
					if (myExtNum > 0)
					{
						int enNum = enemyInnerN*3;
						for (const P &p : enemyVehicles)
						{
							double d2 = p.dist2(newTarget);
							if (d2 < (50.0*50.0))
							{
								enNum++;
							}
						}
						
						int score = enNum - myNum * 4;
						if (score > foundScore)
						{
							foundScore = score;
							correctedBestTarget = newTarget;
						}
					}
				}
			}
			
			if (foundScore > -100000)
			{
				auto getMySize = [this](int x, int y) {
					if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
						return 0;
					
					return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
						+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
						+ (int) getMicrocell(x, y, 0, UnitType::ARV).size();
				};
				
				int pts = 0;
				long bestId = -1;
				P vehPos;
				
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.durability > 70)
					{
						double dist2 = p.pos.dist2(correctedBestTarget);
						if (dist2 > 60*60 && dist2 < 70*70)
						{
							int xx = p.pos.x / MICROCELL_SIZE;
							int yy = p.pos.y / MICROCELL_SIZE;
							
							int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							if (pts < newPts)
							{
								pts = newPts;
								bestId = p.id;
								vehPos = p.pos;
							}
						}
					}
				}
				
				if (bestId >= 0)
				{
					result.action = MyActionType::TACTICAL_NUCLEAR_STRIKE;
					result.vehicleId = bestId;
					result.p = correctedBestTarget;
					return result;
				}
			}
		}
	}
	
	//return result;
	
	groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group &g){return g.size == 0;}), groups.end());
	
	if (result.action == MyActionType::NONE && !groups.empty())
	{
		std::sort(groups.begin(), groups.end(), [](const Group &g1, const Group &g2){
			double p1 = groupPriority(g1);
			double p2 = groupPriority(g2);
			return p2 < p1;
		});
		
		for (Group &group : groups)
		{
			if (tick - group.lastUpdateTick < 10)
				continue;
			
			P center = group.center;
			const BBox &bbox = group.bbox;
			int groupSize = group.size;
			P diag = bbox.p2 - bbox.p1;
			double area = diag.x * diag.y;
			bool shrinkRequired = area > groupSize * 40.0 && ((tick - group.lastUpdateTick) > 60 || (tick - group.lastShrinkTick) > 300);
			//bool shrinkRequired = false;
			
			const double border = 20.0;
			P c = center;
			double R = 30.0;
			double ticks = R / unitVel(group.unitType);
			double curVal = attractionPoint(center, group.unitType, ticks, angryMode);
			P tp = center;
			bool found = false;
			bool canMoveFlag = false;
			
			for (int k = 0; k < 3.0; ++k)
			{
				ticks = R / unitVel(group.unitType);
				int di = -1;
				
				for (int i = 0; i < 20; ++i)
				{
					P p = c + P(PI * 2.0 / 20.0 * i) * R;
					if (p.x > border && p.y > border && p.x < (WIDTH - border) && p.y < (HEIGHT - border))
					{
						if (canMove(p - center, group))
						{
							canMoveFlag = true;
							double val = attractionPoint(p, group.unitType, ticks, angryMode);
							if (val > curVal)
							{
								curVal = val;
								di = i;
								tp = p;
								found = true;
							}
						}
					}
				}
				
				if (di >= 0)
				{
					c = tp;
				}
				
				R /= 1.5;
			}
			
			bool moveAway = false;
			P dirAway;
			P newShift;
			if (!canMoveFlag)
			{
				dirAway = P(0, 0);
				for (Group &othGroup : groups)
				{
					if (&group != &othGroup && isGroundUnit(group.unitType) == isGroundUnit(othGroup.unitType))
					{
						P dp = group.center - othGroup.center;
						double clen = dp.len();
						
						if (clen < 150.0)
							dirAway += dp / clen;
					}
				}
				
				dirAway *= 10;
				dirAway += P(tick % 7 - 3, tick % 9 - 5);
				moveAway = true;
			}
			else
			{
				newShift = P(0, 0);
				if (found)
					newShift = tp - center;
				
				if (newShift.len() < 15.0)
				{
					calcMicroShift(group, newShift);
				}
			}
			
			found = newShift.len2() > 0.1;
			
			if (found || shrinkRequired || moveAway)
			{
				if (!isSelected(group))
				{
					result = select(group);
					group.actionStarted = true;
				}
				else
				{
					if (shrinkRequired)
					{
						result.action = MyActionType::SCALE;
						result.factor = 0.2;
						result.p = group.center;
						group.lastShrinkTick = tick;
					}
					else if (found)
					{
						result.action = MyActionType::MOVE;
						/*if (group.unitType == UnitType::HELICOPTER)
							result.maxSpeed = unitVel(group.unitType) * 0.6;*/
						
						result.p = newShift;
					}
					else
					{
						result.action = MyActionType::MOVE;
						result.p = dirAway;
						//std::cout << "AWAY " << dirAway.x << " " << dirAway.y << std::endl;
					}
					
					group.lastUpdateTick = tick;
					group.actionStarted = false;
				}
				
				break;
			}
		}
	}
	
	return result;
}

double Strat::attractionPoint(const P from, UnitType unitType, double ticks, bool angryMode)
{
	bool veryAngry = angryMode && tick > 15000;
	
	int myN = myCount[unitType];
	double f2f = 0.2;
	if (unitType == UnitType::FIGHTER && enemyCount[UnitType::HELICOPTER] <= 5 && myN*1.1 > enemyCount[UnitType::FIGHTER] || veryAngry)
		f2f = 0.0;
	
	double i2i = 1.0;
	if (unitType == UnitType::IFV && enemyCount[UnitType::HELICOPTER] <= 5 && myN * 0.7 > enemyCount[UnitType::IFV] || veryAngry)
		i2i = 0.0;
	
	double t2t = 1.0;
	if (unitType == UnitType::TANK && enemyCount[UnitType::ARV] <= 5 && enemyCount[UnitType::IFV] <= 5 && myN * 0.8 > enemyCount[UnitType::TANK]
		|| angryMode && myN > enemyCount[UnitType::TANK] || veryAngry)
		t2t = 0.0;
	
	double ptn = 0.0;
	for (const MyUnit &p : units)
	{
		if (p.side > 0)
		{
			double dist2 = from.dist2(p.pos + p.vel * ticks);
			double pp = 1.0/(1.0 + dist2);
			
			double dangerRad = 50.0;
			if (players[0].remainingNuclearStrikeCooldownTicks < 50)
				dangerRad = 70.0;
			if (!angryMode && unitType == UnitType::HELICOPTER && p.type == UnitType::FIGHTER)
				dangerRad = 120.0;
			
			if (!angryMode && unitType == UnitType::FIGHTER && p.type == UnitType::FIGHTER && myCount[UnitType::FIGHTER] < enemyCount[UnitType::FIGHTER])
				dangerRad = 90.0;
			
			if (unitType == UnitType::ARV)
				dangerRad = 120.0;
			
			double pn = (1.0 - std::min(1.0, dist2/(dangerRad*dangerRad)))*0.5;
			//int enemyN = enemyCount[p.type];
			
			if (unitType == UnitType::HELICOPTER)
			{
				if (p.type == UnitType::TANK)
				{
					ptn += pp;
					//ptn -= pn;
				}
				
				if (p.type == UnitType::ARV)
				{
					ptn += pp*0.1;
				}
				
				if (p.type == UnitType::FIGHTER)
				{
					ptn -= pn;
				}
				
				if (!veryAngry && p.type == UnitType::HELICOPTER)
				{
					ptn -= pn;
				}
				
				if (p.type == UnitType::IFV)
				{
					ptn -= pn;
				}
			}
			else if (unitType == UnitType::IFV)
			{
				if (p.type == UnitType::TANK)
				{
					ptn -= pn;
				}
				
				if (p.type == UnitType::ARV)
				{
					ptn += pp*0.1;
				}
				
				if (p.type == UnitType::FIGHTER)
				{
					ptn += pp*0.2;
				}
				
				if (p.type == UnitType::HELICOPTER)
				{
					ptn += pp;
					ptn -= pn*0.5;
				}
				
				if (p.type == UnitType::IFV)
				{
					ptn += pp*0.2;
					ptn -= pn*i2i * (20.0 + p.durability) / 120.0;
				}
			}
			else if (unitType == UnitType::FIGHTER)
			{
				if (p.type == UnitType::FIGHTER)
				{
					ptn += pp*0.2;
					ptn -= pn*f2f * (20.0 + p.durability) / 120.0;
				}
				
				if (p.type == UnitType::HELICOPTER)
				{
					ptn += pp;
				}
				
				if (!veryAngry && p.type == UnitType::IFV)
				{
					ptn -= pn;
				}
			}
			else if (unitType == UnitType::TANK)
			{
				if (p.type == UnitType::TANK)
				{
					ptn -= pn * t2t * (20.0 + p.durability) / 120.0;
					ptn += pp*0.3;
				}
				
				if (p.type == UnitType::ARV)
				{
					ptn += pp*0.1;
				}
				
				if (p.type == UnitType::HELICOPTER)
				{
					ptn -= pn;
				}
				
				if (p.type == UnitType::IFV)
				{
					ptn += pp;
				}
			}
			else if (unitType == UnitType::ARV)
			{
				if (p.type == UnitType::TANK)
				{
					ptn -= pn;
				}
				
				if (p.type == UnitType::HELICOPTER)
				{
					ptn -= pn;
				}
				
				if (p.type == UnitType::IFV)
				{
					ptn -= pn;
				}
			}
		}
	}
	
	if (unitType == UnitType::HELICOPTER)
	{
		double L = 1.5 * WIDTH;
		if (ptn < 0.0 && myCount[UnitType::IFV] > 10)
		{
			Group *fivG = getGroup(UnitType::IFV);
			if (fivG)
			{
				L = fivG->center.dist(from);
			}
		}
		
		ptn -= L / WIDTH * myCount[UnitType::IFV];
	}
	
	const double keepBorderDist = 40.0;
	double borderPen = std::max(
		std::max(
			std::max(keepBorderDist - from.x, 0.0),
			std::max(keepBorderDist - from.y, 0.0)
		),
		std::max(
			std::max(keepBorderDist - (WIDTH - from.x), 0.0),
			std::max(keepBorderDist - (HEIGHT - from.y), 0.0)
		)
	);
	
	ptn -= borderPen;
	
	return ptn;
}

}

// ============================================================================= V9

namespace StratV9 {
	
double groupPriority(const Group &g) {
	double res = 0.0;
	
	if (g.shrinkAfterNuke)
		res += 1100.0;
	else if (g.actionStarted)
		res += 1000.0;
	
	res -= g.lastUpdateTick;
	
	return res;
}

void DistributionMatrix::clear()
{
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		Cell &cell = cells[i];
		for (int j = 0; j < 5; ++j)
			cell.count[j] = 0;
		for (int j = 0; j < 5; ++j)
			cell.health[j] = 0;
	}
}

void DistributionMatrix::initialize(const Simulator &sim)
{
	clear();
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : sim.units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / DISTR_MAT_CELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / DISTR_MAT_CELL_SIZE;
				
				Cell &cell = getCell(x, y);
				int type = (int) u.type;
				cell.count[type] += 1.0 / K;
				cell.health[type] += u.durability / K;
			}
		}
	}
}

void DistributionMatrix::blur(DistributionMatrix &oth) const
{
	oth.clear();
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			Cell &resCell = oth.getCell(x, y);
			int cnt = 0;
			for (int yy = -1; yy <= 1; ++yy)
			{
				for (int xx = -1; xx <= 1; ++xx)
				{
					int x2 = x + xx;
					int y2 = y + yy;
					if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
					{
						++cnt;
						const Cell &cell = getCell(x2, y2);
						for (int i = 0; i < 5; ++i)
							resCell.count[i] += cell.count[i];
						for (int i = 0; i < 5; ++i)
							resCell.health[i] += cell.health[i];
					}
				}
			}
			
			const Cell &cell = getCell(x, y);
			for (int i = 0; i < 5; ++i)
			{
				if (!cell.count[i])
				{
					resCell.count[i] = 0;
					resCell.health[i] = 0;
				}
			}
			
			/*for (int i = 0; i < 5; ++i)
				resCell.count[i] += (resCell.count[i] + cnt - 1) / cnt;
			for (int i = 0; i < 5; ++i)
				resCell.health[i] /= cnt;*/
		}
	}
}

Strat::Strat::Strat()
{
	Group g;
	g.actionStarted = false;
	g.lastUpdateTick = 0;
	g.lastShrinkTick = 0;
	
	for (int i = 0; i < 1; ++i)
	{
		g.unitType = UnitType::HELICOPTER;
		//g.miniGroupInd = i;
		groups.push_back(g);
	}
	g.miniGroupInd = 0;
	
	g.unitType = UnitType::IFV;
	groups.push_back(g);
	
	g.unitType = UnitType::FIGHTER;
	groups.push_back(g);
	
	g.unitType = UnitType::TANK;
	groups.push_back(g);
	
	g.unitType = UnitType::ARV;
	groups.push_back(g);
}


void Strat::calcMicroShift(Group &group, P &shift)
{
	if (group.unitType != UnitType::TANK && group.unitType != UnitType::FIGHTER)
		return;
	
	BBox bbox = group.bbox;
	bbox.p1 += shift - P(35, 35);
	bbox.p2 += shift + P(35, 35);
	
	int finalScore = -100000;
	int currentScore = -100000;
	double ticks = 20.0;
	
	//if (group.unitType == UnitType::TANK)
	{
		std::vector<P> enemies[(int) UnitType::COUNT];
		std::vector<P> myUnits;
		
		for (const MyUnit &u : units)
		{
			if (u.side > 0 && (u.type == UnitType::TANK || u.type == UnitType::HELICOPTER || u.type == UnitType::IFV) && bbox.inside(u.pos))
			{
				enemies[(int) u.type].push_back(u.pos + u.vel * ticks);
			}
			else if (group.check(u))
			{
				myUnits.push_back(u.pos);
			}
		}
		
		double L = 8.0;
		P newShift = shift;
		for (int k = 0; k < 4; ++k)
		{
			P foundShift = newShift;
			int foundScore = -100000;
			for (int i = 0; i < 9; ++i)
			{
				P testShift = newShift + P(i / 3 - 1, i % 3 - 1) * L;
				int score = 0;
				bool isCurShift = shift.dist2(testShift) < 0.1;
				
				if (!isCurShift || currentScore == -100000)
				{
					const std::vector<MicroShiftValues> &pos = microShiftMatrix.pos[(int) group.unitType];
					if (!pos.empty())
					{
						for (const P &myP : myUnits)
						{
							P myPos = myP + testShift;
							bool found = false;
							for (const MicroShiftValues &vals : pos)
							{
								if (!found)
								{
									for (const P &enP : enemies[(int) vals.unitType])
									{
										if (myPos.dist2(enP) < vals.dist2)
										{
											score += vals.val;
											found = true;
											break;
										}
									}
								}
							}
						}
					}
					
					// !! decrease SCORE
					int posScore = score;
					score /= 2;
					
					const std::vector<MicroShiftValues> &neg = microShiftMatrix.neg[(int) group.unitType];
					if (!neg.empty())
					{
						for (const MicroShiftValues &vals : neg)
						{
							for (const P &enP : enemies[(int) vals.unitType])
							{
								for (const P &myP : myUnits)
								{
									P myPos = myP + testShift;
									if (myPos.dist2(enP) < vals.dist2)
									{
										score -= vals.val;
										break;
									}
								}
							}
						}
					}
					
					int negScore = posScore / 2 - score;
					
					if (foundScore < score)
					{
						foundShift = testShift;
						foundScore = score;
					}
					
					if (isCurShift)
						currentScore = score;
				}
			}
			
			L /= 2.0;
			newShift = foundShift;
			
			finalScore = foundScore;
		}
		
		if (currentScore >= finalScore)
			return;
		
		shift = newShift;
	}
}

void Strat::calcNuclearEfficiency()
{
	for (int i = 0; i < MICROCELLS_X * MICROCELLS_Y; i++)
		nuclearEfficiency[i] = 0;
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / MICROCELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / MICROCELL_SIZE;
				
				double efficiency = 100.0 / (10.0 + u.durability) / K;
				
				nuclearEfficiency[y * MICROCELLS_X + x] += efficiency;
			}
		}
	}
}

MyMove Strat::calcNextMove()
{
	MyMove result;
	result.action = MyActionType::NONE;
	
	int moves = getAvailableActions(12);
	if (moves < 1)
		return result;
	
	if (players[1].remainingNuclearStrikeCooldownTicks < 30)
	{
		if (moves < 3)
			return result;
	}
	
	updateStats();
	
	if (players[1].nextNuclearStrikeTick >= 0)
	{
		bool select = false;
		bool expand = false;
		BBox bbox;
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep == 0)
			{
				BBox gbox = g.bbox;
				gbox.expand(40.0);
				if (gbox.inside(players[1].nuclearStrike))
				{
					bbox.add(g.bbox);
					g.nukeEvadeStep = 1;
					select = true;
				}
			}
			else if (g.nukeEvadeStep == 1)
			{
				expand = true;
			}
		}
		
		if (select)
		{
			result.action = MyActionType::CLEAR_AND_SELECT;
			result.p1 = bbox.p1;
			result.p2 = bbox.p2;
			
			return result;
		}
		else if (expand)
		{
			result.action = MyActionType::SCALE;
			result.p = players[1].nuclearStrike;
			result.factor = 9.0;
			return result;
		}
	}
	else
	{
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep > 0)
				g.shrinkAfterNuke = true;
			if (tick - g.lastShrinkTick > 30)
				g.shrinkAfterNuke = false;
			g.nukeEvadeStep = 0;
		}
	}
	
	
	if (!initialGroupsGerationDone)
	{
		for (Group &g : groups)
		{
			if (g.unitType == UnitType::HELICOPTER && !g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					P allCenter = P(0, 0);
					int count = 0;
					for (const MyUnit &u : units)
					{
						if (u.side == 0 && u.type == g.unitType)
						{
							allCenter += u.pos;
							++count;
						}
					}
					
					allCenter /= count;
					
					if (g.miniGroupInd == 0)
					{
						result.p1 = P(0, 0);
						result.p2 = allCenter;
					}
					else if (g.miniGroupInd == 1)
					{
						result.p1 = P(allCenter.x, 0);
						result.p2 = P(WIDTH, allCenter.y);
					}
					else if (g.miniGroupInd == 2)
					{
						result.p1 = P(0, allCenter.y);
						result.p2 = P(allCenter.x, HEIGHT);
					}
					else if (g.miniGroupInd == 3)
					{
						result.p1 = allCenter;
						result.p2 = P(WIDTH, HEIGHT);
					}
					
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}
		}
		
		initialGroupsGerationDone = true;
	}
	
	bool angryMode = angryModeTill > tick;

	
	DistributionMatrix matr;
	matr.initialize(*this);
	matr.blur(distributionMatrix);
	
	/*if (tick > 532)
	{
		std::cout.width(2);
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &resCell = distributionMatrix.getCell(x, y);
				std::cout << " " << resCell.count[1];
			}
			std::cout << std::endl;
		}
		std::cout << std::endl;
	}*/
	
	if (players[0].remainingNuclearStrikeCooldownTicks == 0)
	{
		resetCells();
		calcNuclearEfficiency();
		
		auto getEnemySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0.0;
			
			return nuclearEfficiency[y * MICROCELLS_X + x];
		};
		
		auto getMySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0;
			
			return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
				+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
				+ (int) getMicrocell(x, y, 0, UnitType::ARV).size() / 2;
		};
		
		P bestTarget;
		int bestScore = 0;
		
		for (int y = 0; y < MICROCELLS_Y; ++y) {
			for (int x = 0; x < MICROCELLS_X; ++x) {
				double enemyN = getEnemySize(x, y);
				if (enemyN > 10)
				{
					int totalMyCount = 0;
					int myCenterCount = 0;
					for (int xx = -1; xx <= 1; ++xx)
					{
						for (int yy = -1; yy <= 1; ++yy)
						{
							totalMyCount += getMySize(x + xx, y + yy);
							if (xx == 0 && yy == 0)
								myCenterCount = totalMyCount;
						}
					}
					
					if (totalMyCount < 20 && myCenterCount == 0)
					{
						int totalMyExtCount = totalMyCount;
						if (totalMyExtCount == 0)
						{
							for (int xx = -2; xx <= 2; ++xx)
							{
								for (int yy = -2; yy <= 2; ++yy)
								{
									if (xx < -1 || xx > 1 || yy < -1 || yy > 1)
										totalMyExtCount += getMySize(x + xx, y + yy);
								}
							}
						}
						
						if (totalMyExtCount > 0)
						{
							double totalEnemyCount =
								enemyN +
								getEnemySize(x - 1, y)/2 +
								getEnemySize(x + 1, y)/2 + 
								getEnemySize(x, y - 1)/2 + 
								getEnemySize(x, y + 1)/2;
							
							if (bestScore < totalEnemyCount)
							{
								bestScore = totalEnemyCount;
								bestTarget = P(x + 0.5, y + 0.5) * MICROCELL_SIZE;
							}
						}
					}
				}
			}
		}
		
		if (bestScore > 0)
		{
			// Correct point
			P correctedBestTarget = bestTarget;
			
			std::vector<P> myVehicles;
			std::vector<P> enemyVehicles;
			int myInnerN = 0;
			int enemyInnerN = 0;
			for (const MyUnit &p : units)
			{
				double d2 = p.pos.dist2(bestTarget);
				if (d2 < (75.0*75.0))
				{
					if (d2 < (38.0*38.0))
					{
						if (p.side == 0)
							myInnerN++;
						else
							enemyInnerN++;
					}
					else
					{
						if (p.side == 0)
							myVehicles.push_back(p.pos);
						else
							enemyVehicles.push_back(p.pos);
					}
				}
			}
			
			int foundScore = -100000;
			for (int xx = -2; xx <= 2; ++xx)
			{
				for (int yy = -2; yy <= 2; ++yy)
				{
					P newTarget = bestTarget + P(xx, yy) * 6.0;
					
					int myNum = myInnerN*3;
					int myExtNum = 0;
					
					for (const P &p : myVehicles)
					{
						double d2 = p.dist2(newTarget);
						if (d2 < (50.0*50.0))
						{
							myNum++;
						}
						else if (d2 > (55.0*55.0) && d2 < (70.0*70.0))
						{
							myExtNum++;
						}
					}
					
					if (myExtNum > 0)
					{
						int enNum = enemyInnerN*3;
						for (const P &p : enemyVehicles)
						{
							double d2 = p.dist2(newTarget);
							if (d2 < (50.0*50.0))
							{
								enNum++;
							}
						}
						
						int score = enNum - myNum * 4;
						if (score > foundScore)
						{
							foundScore = score;
							correctedBestTarget = newTarget;
						}
					}
				}
			}
			
			if (foundScore > -100000)
			{
				auto getMySize = [this](int x, int y) {
					if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
						return 0;
					
					return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
						+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
						+ (int) getMicrocell(x, y, 0, UnitType::ARV).size();
				};
				
				int pts = 0;
				long bestId = -1;
				P vehPos;
				
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.durability > 70)
					{
						double dist2 = p.pos.dist2(correctedBestTarget);
						if (dist2 > 60*60 && dist2 < 70*70)
						{
							int xx = p.pos.x / MICROCELL_SIZE;
							int yy = p.pos.y / MICROCELL_SIZE;
							
							//int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 60.0);
							if (pts < newPts)
							{
								pts = newPts;
								bestId = p.id;
								vehPos = p.pos;
							}
						}
					}
				}
				
				if (bestId >= 0)
				{
					result.action = MyActionType::TACTICAL_NUCLEAR_STRIKE;
					result.vehicleId = bestId;
					result.p = correctedBestTarget;
					return result;
				}
			}
		}
	}
	
	groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group &g){return g.size == 0;}), groups.end());
	
	if (result.action == MyActionType::NONE && !groups.empty())
	{
		debugAttractionPoints.clear();
		
		std::sort(groups.begin(), groups.end(), [](const Group &g1, const Group &g2){
			double p1 = groupPriority(g1);
			double p2 = groupPriority(g2);
			return p2 < p1;
		});
		
		for (Group &group : groups)
		{
			if (tick - group.lastUpdateTick < 10 || tick - group.lastShrinkTick < 30 || group.nukeEvadeStep > 0)
				continue;
			
			calcDangerDistCells(group);
			
			P center = group.center;
			const BBox &bbox = group.bbox;
			int groupSize = group.size;
			P diag = bbox.p2 - bbox.p1;
			double area = diag.x * diag.y;
			bool shrinkRequired = (area > groupSize * 40.0 || group.shrinkAfterNuke) && ((tick - group.lastUpdateTick) > 60 || (tick - group.lastShrinkTick) > 300);
			//bool shrinkRequired = false;
			
			const double border = 20.0;
			P c = center;
			double R = 20 + unitVel(group.unitType) * 40;
			/*if (group.unitType == UnitType::ARV)
				R = 200.0;*/
			double ticks = R / unitVel(group.unitType);
			double curVal = attractionPoint(center, group, ticks, angryMode);
			P tp = center;
			bool found = false;
			bool canMoveFlag = false;
			
			for (int k = 0; k < 3.0; ++k)
			{
				ticks = R / unitVel(group.unitType);
				int di = -1;
				
				for (int i = 0; i < 20; ++i)
				{
					P p = c + P(PI * 2.0 / 20.0 * i) * R;
					if (p.x > border && p.y > border && p.x < (WIDTH - border) && p.y < (HEIGHT - border))
					{
						if (canMove(p - center, group))
						{
							canMoveFlag = true;
							double val = attractionPoint(p, group, ticks, angryMode);
							
							DebugAttractionPointsInfo debugInfo;
							debugInfo.point = c;
							debugInfo.dir = p - c;
							debugInfo.val = val;
							debugAttractionPoints.push_back(debugInfo);
							
							if (val > curVal)
							{
								curVal = val;
								di = i;
								tp = p;
								found = true;
							}
						}
					}
				}
				
				if (di >= 0)
				{
					c = tp;
				}
				
				R /= 1.5;
			}
			
			bool moveAway = false;
			P dirAway;
			P newShift;
			if (!canMoveFlag)
			{
				dirAway = P(0, 0);
				for (Group &othGroup : groups)
				{
					if (&group != &othGroup && isGroundUnit(group.unitType) == isGroundUnit(othGroup.unitType))
					{
						P dp = group.center - othGroup.center;
						double clen = dp.len();
						
						if (clen < 150.0)
							dirAway += dp / clen;
					}
				}
				
				dirAway *= 10;
				dirAway += P(tick % 7 - 3, tick % 9 - 5);
				moveAway = true;
			}
			else
			{
				newShift = P(0, 0);
				if (found)
					newShift = tp - center;
				
				if (newShift.len() < 15.0)
				{
					calcMicroShift(group, newShift);
				}
			}
			
			found = newShift.len2() > 0.1;
			
			if (found || shrinkRequired || moveAway)
			{
				if (!isSelected(group))
				{
					result = select(group);
					group.actionStarted = true;
				}
				else
				{
					if (shrinkRequired)
					{
						result.action = MyActionType::SCALE;
						result.factor = 0.2;
						result.p = group.center;
						group.lastShrinkTick = tick;
					}
					else if (found)
					{
						result.action = MyActionType::MOVE;
						result.p = newShift;
						
						/*if (group.unitType == UnitType::FIGHTER)
							result.maxSpeed = 1.0;*/
					}
					else
					{
						result.action = MyActionType::MOVE;
						result.p = dirAway;
						//std::cout << "AWAY " << dirAway.x << " " << dirAway.y << std::endl;
					}
					
					group.lastUpdateTick = tick;
					group.actionStarted = false;
				}
				
				break;
			}
		}
	}
	
	return result;
}

void Strat::calcDangerDistCells(const Group &group)
{
	dngGr = &group;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
			double totalMyDamage = 0;
			
			for (int myType = 0; myType < 5; ++myType)
			{
				double typeDamage = 0;
				if (group.sizeByTypes[myType])
				{
					for (int enemyType = 0; enemyType < 5; ++enemyType)
					{
						if (cell.count[enemyType])
						{
							double dmg = getDamage((UnitType) myType, (UnitType) enemyType);
							typeDamage = std::max(typeDamage, std::min(dmg * group.sizeByTypes[myType], 120.0 * cell.count[enemyType]));
						}
					}
				}
				totalMyDamage += typeDamage;
			}
			
			double enemyDamage = 0;
			double enemyHealth = 0.0;
			double f2hDmg = 0.0;
			for (int enemyType = 0; enemyType < 5; ++enemyType)
			{
				double typeDamage = 0;
				if (cell.count[enemyType])
				{
					enemyHealth += cell.health[enemyType];
					for (int myType = 0; myType < 5; ++myType)
					{
						if (group.sizeByTypes[myType])
						{
							double dmg = getDamage((UnitType) enemyType, (UnitType) myType);
							typeDamage = std::max(typeDamage, std::min(dmg * cell.count[enemyType], 120.0 * group.sizeByTypes[myType]));
							
							if (enemyType == (int) UnitType::FIGHTER && myType == (int) UnitType::HELICOPTER)
								f2hDmg = typeDamage;
						}
					}
				}
				enemyDamage += typeDamage;
			}
			
			enemyDamage *= 1.4;
			
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			dCell.enemyDamage = enemyDamage;
			dCell.enemyHealth = enemyHealth;
			dCell.totalMyDamage = totalMyDamage;
			dCell.f2hDmg = f2hDmg;
		}
	}
}

double Strat::attractionPoint(const P from, const Group &group, double ticks, bool angryMode)
{
	double res = 0.0;
	double f2hDmgRes = 0.0;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			if (dCell.totalMyDamage > 0.0 || dCell.enemyDamage > 0.0)
			{
				//double pts = group.health / dCell.enemyDamage - dCell.enemyHealth / dCell.totalMyDamage;
				//double pts = (dCell.totalMyDamage - dCell.enemyDamage) / (group.size)/10;
				double alpha = 0.3;
				double alphaM1 = 0.7;
				double pts = (group.health * alphaM1 + dCell.enemyHealth * alpha) / (dCell.enemyHealth*0.01 + dCell.enemyDamage) 
				- (dCell.enemyHealth * alphaM1 + group.health * alpha) / (group.health * 0.01 + dCell.totalMyDamage);
				
				P p = P((x + 0.5) * DISTR_MAT_CELL_SIZE, (y + 0.5) * DISTR_MAT_CELL_SIZE);
				
				double dist2 = p.dist2(from);
				if (pts > 0.0)
				{
					double pp = 1.0/(1.0 + dist2);
					res += pts * pp;
				}
				else
				{
					double dangerRad = 150.0;
					double pn = (1.0 - std::min(1.0, dist2/(dangerRad*dangerRad)));
					res += pts * pn;
				}
				
				//res += pts / p.dist2(from);
			}
			
			f2hDmgRes += dCell.f2hDmg;
		}
	}
	
	if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		double L = 1.5 * WIDTH;
		if (f2hDmgRes > 0.0 && res < 0.0)
		{
			if (fivG && fivG->size > 10)
			{
				L = fivG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::IFV];
		
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 300.0)
			{
				res -= (l - 300.0);
			}
		}
	}
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::HELICOPTER);
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 400.0)
			{
				res -= (l - 400.0);
			}
		}
	}*/
	
	const double keepBorderDist = 40.0;
	double borderPen = std::max(
		std::max(
			std::max(keepBorderDist - from.x, 0.0),
			std::max(keepBorderDist - from.y, 0.0)
		),
		std::max(
			std::max(keepBorderDist - (WIDTH - from.x), 0.0),
			std::max(keepBorderDist - (HEIGHT - from.y), 0.0)
		)
	);
	
	res -= borderPen*10;
	
	return res;
}
}



/////////////////////////////////////////////////////////////// V10

namespace StratV10 {
	
double groupPriority(const Group &g) {
	double res = 0.0;
	
	if (g.shrinkAfterNuke)
		res += 1100.0;
	else if (g.actionStarted)
		res += 1000.0;
	
	res -= g.lastUpdateTick;
	
	return res;
}

void DistributionMatrix::clear()
{
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		Cell &cell = cells[i];
		for (int j = 0; j < 5; ++j)
			cell.count[j] = 0;
		for (int j = 0; j < 5; ++j)
			cell.health[j] = 0;
	}
}

void DistributionMatrix::initialize(const Simulator &sim)
{
	clear();
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : sim.units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / DISTR_MAT_CELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / DISTR_MAT_CELL_SIZE;
				
				Cell &cell = getCell(x, y);
				int type = (int) u.type;
				cell.count[type] += 1.0 / K;
				cell.health[type] += u.durability / K;
			}
		}
	}
}

void DistributionMatrix::blur(DistributionMatrix &oth) const
{
	oth.clear();
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			Cell &resCell = oth.getCell(x, y);
			int cnt = 0;
			for (int yy = -1; yy <= 1; ++yy)
			{
				for (int xx = -1; xx <= 1; ++xx)
				{
					int x2 = x + xx;
					int y2 = y + yy;
					if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
					{
						++cnt;
						const Cell &cell = getCell(x2, y2);
						for (int i = 0; i < 5; ++i)
							resCell.count[i] += cell.count[i];
						for (int i = 0; i < 5; ++i)
							resCell.health[i] += cell.health[i];
					}
				}
			}
			
			const Cell &cell = getCell(x, y);
			for (int i = 0; i < 5; ++i)
			{
				if (!cell.count[i])
				{
					resCell.count[i] = 0;
					resCell.health[i] = 0;
				}
			}
			
			/*for (int i = 0; i < 5; ++i)
				resCell.count[i] += (resCell.count[i] + cnt - 1) / cnt;
			for (int i = 0; i < 5; ++i)
				resCell.health[i] /= cnt;*/
		}
	}
}

Strat::Strat::Strat()
{
	Group g;
	g.actionStarted = false;
	g.lastUpdateTick = 0;
	g.lastShrinkTick = 0;
	
	for (int i = 0; i < 1; ++i)
	{
		g.unitType = UnitType::HELICOPTER;
		//g.miniGroupInd = i;
		groups.push_back(g);
	}
	g.miniGroupInd = 0;
	
	g.unitType = UnitType::IFV;
	groups.push_back(g);
	
	g.unitType = UnitType::FIGHTER;
	groups.push_back(g);
	
	g.unitType = UnitType::TANK;
	groups.push_back(g);
	
	g.unitType = UnitType::ARV;
	groups.push_back(g);
}


void Strat::calcMicroShift(Group &group, P &shift)
{
	if (group.unitType != UnitType::TANK && group.unitType != UnitType::FIGHTER)
		return;
	
	BBox bbox = group.bbox;
	bbox.p1 += shift - P(35, 35);
	bbox.p2 += shift + P(35, 35);
	
	int finalScore = -100000;
	int currentScore = -100000;
	double ticks = 20.0;
	
	//if (group.unitType == UnitType::TANK)
	{
		std::vector<P> enemies[(int) UnitType::COUNT];
		std::vector<P> myUnits;
		
		for (const MyUnit &u : units)
		{
			if (u.side > 0 && (u.type == UnitType::TANK || u.type == UnitType::HELICOPTER || u.type == UnitType::IFV) && bbox.inside(u.pos))
			{
				enemies[(int) u.type].push_back(u.pos + u.vel * ticks);
			}
			else if (group.check(u))
			{
				myUnits.push_back(u.pos);
			}
		}
		
		double L = 8.0;
		P newShift = shift;
		for (int k = 0; k < 4; ++k)
		{
			P foundShift = newShift;
			int foundScore = -100000;
			for (int i = 0; i < 9; ++i)
			{
				P testShift = newShift + P(i / 3 - 1, i % 3 - 1) * L;
				int score = 0;
				bool isCurShift = shift.dist2(testShift) < 0.1;
				
				if (!isCurShift || currentScore == -100000)
				{
					const std::vector<MicroShiftValues> &pos = microShiftMatrix.pos[(int) group.unitType];
					if (!pos.empty())
					{
						for (const P &myP : myUnits)
						{
							P myPos = myP + testShift;
							bool found = false;
							for (const MicroShiftValues &vals : pos)
							{
								if (!found)
								{
									for (const P &enP : enemies[(int) vals.unitType])
									{
										if (myPos.dist2(enP) < vals.dist2)
										{
											score += vals.val;
											found = true;
											break;
										}
									}
								}
							}
						}
					}
					
					// !! decrease SCORE
					int posScore = score;
					score /= 2;
					
					const std::vector<MicroShiftValues> &neg = microShiftMatrix.neg[(int) group.unitType];
					if (!neg.empty())
					{
						for (const MicroShiftValues &vals : neg)
						{
							for (const P &enP : enemies[(int) vals.unitType])
							{
								for (const P &myP : myUnits)
								{
									P myPos = myP + testShift;
									if (myPos.dist2(enP) < vals.dist2)
									{
										score -= vals.val;
										break;
									}
								}
							}
						}
					}
					
					int negScore = posScore / 2 - score;
					
					if (foundScore < score)
					{
						foundShift = testShift;
						foundScore = score;
					}
					
					if (isCurShift)
						currentScore = score;
				}
			}
			
			L /= 2.0;
			newShift = foundShift;
			
			finalScore = foundScore;
		}
		
		if (currentScore >= finalScore)
			return;
		
		shift = newShift;
	}
}

void Strat::calcNuclearEfficiency()
{
	for (int i = 0; i < MICROCELLS_X * MICROCELLS_Y; i++)
		nuclearEfficiency[i] = 0;
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / MICROCELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / MICROCELL_SIZE;
				
				double efficiency = 100.0 / (10.0 + u.durability) / K;
				
				nuclearEfficiency[y * MICROCELLS_X + x] += efficiency;
			}
		}
	}
}

MyMove Strat::calcNextMove()
{
	MyMove result;
	result.action = MyActionType::NONE;
	
	int moves = getAvailableActions(12);
	if (moves < 1)
		return result;
	
	if (players[1].remainingNuclearStrikeCooldownTicks < 30)
	{
		if (moves < 3)
			return result;
	}
	
	updateStats();
	
	if (players[1].nextNuclearStrikeTick >= 0)
	{
		bool select = false;
		bool expand = false;
		BBox bbox;
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep == 0)
			{
				BBox gbox = g.bbox;
				gbox.expand(40.0);
				if (gbox.inside(players[1].nuclearStrike))
				{
					bbox.add(g.bbox);
					g.nukeEvadeStep = 1;
					select = true;
				}
			}
			else if (g.nukeEvadeStep == 1)
			{
				expand = true;
			}
		}
		
		if (select)
		{
			result.action = MyActionType::CLEAR_AND_SELECT;
			result.p1 = bbox.p1;
			result.p2 = bbox.p2;
			
			return result;
		}
		else if (expand)
		{
			result.action = MyActionType::SCALE;
			result.p = players[1].nuclearStrike;
			result.factor = 9.0;
			return result;
		}
	}
	else
	{
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep > 0)
				g.shrinkAfterNuke = true;
			if (tick - g.lastShrinkTick > 30)
				g.shrinkAfterNuke = false;
			g.nukeEvadeStep = 0;
		}
	}
	
	
	if (!initialGroupsGerationDone)
	{
		for (Group &g : groups)
		{
			if (g.unitType == UnitType::HELICOPTER && !g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					P allCenter = P(0, 0);
					int count = 0;
					for (const MyUnit &u : units)
					{
						if (u.side == 0 && u.type == g.unitType)
						{
							allCenter += u.pos;
							++count;
						}
					}
					
					allCenter /= count;
					
					if (g.miniGroupInd == 0)
					{
						result.p1 = P(0, 0);
						result.p2 = allCenter;
					}
					else if (g.miniGroupInd == 1)
					{
						result.p1 = P(allCenter.x, 0);
						result.p2 = P(WIDTH, allCenter.y);
					}
					else if (g.miniGroupInd == 2)
					{
						result.p1 = P(0, allCenter.y);
						result.p2 = P(allCenter.x, HEIGHT);
					}
					else if (g.miniGroupInd == 3)
					{
						result.p1 = allCenter;
						result.p2 = P(WIDTH, HEIGHT);
					}
					
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}
		}
		
		initialGroupsGerationDone = true;
	}
	
	bool angryMode = angryModeTill > tick;

	
	DistributionMatrix matr;
	matr.initialize(*this);
	matr.blur(distributionMatrix);
	
	/*if (tick > 532)
	{
		std::cout.width(2);
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &resCell = distributionMatrix.getCell(x, y);
				std::cout << " " << resCell.count[1];
			}
			std::cout << std::endl;
		}
		std::cout << std::endl;
	}*/
	
	if (players[0].remainingNuclearStrikeCooldownTicks == 0)
	{
		resetCells();
		calcNuclearEfficiency();
		
		auto getEnemySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0.0;
			
			return nuclearEfficiency[y * MICROCELLS_X + x];
		};
		
		auto getMySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0;
			
			return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
				+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
				+ (int) getMicrocell(x, y, 0, UnitType::ARV).size() / 2;
		};
		
		P bestTarget;
		int bestScore = 0;
		
		for (int y = 0; y < MICROCELLS_Y; ++y) {
			for (int x = 0; x < MICROCELLS_X; ++x) {
				double enemyN = getEnemySize(x, y);
				if (enemyN > 10)
				{
					int totalMyCount = 0;
					int myCenterCount = 0;
					for (int xx = -1; xx <= 1; ++xx)
					{
						for (int yy = -1; yy <= 1; ++yy)
						{
							totalMyCount += getMySize(x + xx, y + yy);
							if (xx == 0 && yy == 0)
								myCenterCount = totalMyCount;
						}
					}
					
					if (totalMyCount < 20 && myCenterCount == 0)
					{
						int totalMyExtCount = totalMyCount;
						if (totalMyExtCount == 0)
						{
							for (int xx = -2; xx <= 2; ++xx)
							{
								for (int yy = -2; yy <= 2; ++yy)
								{
									if (xx < -1 || xx > 1 || yy < -1 || yy > 1)
										totalMyExtCount += getMySize(x + xx, y + yy);
								}
							}
						}
						
						if (totalMyExtCount > 0)
						{
							double totalEnemyCount =
								enemyN +
								getEnemySize(x - 1, y)/2 +
								getEnemySize(x + 1, y)/2 + 
								getEnemySize(x, y - 1)/2 + 
								getEnemySize(x, y + 1)/2;
							
							if (bestScore < totalEnemyCount)
							{
								bestScore = totalEnemyCount;
								bestTarget = P(x + 0.5, y + 0.5) * MICROCELL_SIZE;
							}
						}
					}
				}
			}
		}
		
		if (bestScore > 0)
		{
			// Correct point
			P correctedBestTarget = bestTarget;
			
			std::vector<P> myVehicles;
			std::vector<P> enemyVehicles;
			int myInnerN = 0;
			int enemyInnerN = 0;
			for (const MyUnit &p : units)
			{
				double d2 = p.pos.dist2(bestTarget);
				if (d2 < (75.0*75.0))
				{
					if (d2 < (38.0*38.0))
					{
						if (p.side == 0)
							myInnerN++;
						else
							enemyInnerN++;
					}
					else
					{
						if (p.side == 0)
							myVehicles.push_back(p.pos);
						else
							enemyVehicles.push_back(p.pos);
					}
				}
			}
			
			int foundScore = -100000;
			for (int xx = -2; xx <= 2; ++xx)
			{
				for (int yy = -2; yy <= 2; ++yy)
				{
					P newTarget = bestTarget + P(xx, yy) * 6.0;
					
					int myNum = myInnerN*3;
					int myExtNum = 0;
					
					for (const P &p : myVehicles)
					{
						double d2 = p.dist2(newTarget);
						if (d2 < (50.0*50.0))
						{
							myNum++;
						}
						else if (d2 > (55.0*55.0) && d2 < (70.0*70.0))
						{
							myExtNum++;
						}
					}
					
					if (myExtNum > 0)
					{
						int enNum = enemyInnerN*3;
						for (const P &p : enemyVehicles)
						{
							double d2 = p.dist2(newTarget);
							if (d2 < (50.0*50.0))
							{
								enNum++;
							}
						}
						
						int score = enNum - myNum * 4;
						if (score > foundScore)
						{
							foundScore = score;
							correctedBestTarget = newTarget;
						}
					}
				}
			}
			
			if (foundScore > -100000)
			{
				auto getMySize = [this](int x, int y) {
					if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
						return 0;
					
					return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
						+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
						+ (int) getMicrocell(x, y, 0, UnitType::ARV).size();
				};
				
				int pts = 0;
				long bestId = -1;
				P vehPos;
				
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.durability > 70)
					{
						double dist2 = p.pos.dist2(correctedBestTarget);
						if (dist2 > 60*60 && dist2 < 70*70)
						{
							int xx = p.pos.x / MICROCELL_SIZE;
							int yy = p.pos.y / MICROCELL_SIZE;
							
							//int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 60.0);
							if (pts < newPts)
							{
								pts = newPts;
								bestId = p.id;
								vehPos = p.pos;
							}
						}
					}
				}
				
				if (bestId >= 0)
				{
					result.action = MyActionType::TACTICAL_NUCLEAR_STRIKE;
					result.vehicleId = bestId;
					result.p = correctedBestTarget;
					return result;
				}
			}
		}
	}
	
	groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group &g){return g.size == 0;}), groups.end());
	
	if (result.action == MyActionType::NONE && !groups.empty())
	{
		debugAttractionPoints.clear();
		
		std::sort(groups.begin(), groups.end(), [](const Group &g1, const Group &g2){
			double p1 = groupPriority(g1);
			double p2 = groupPriority(g2);
			return p2 < p1;
		});
		
		for (Group &group : groups)
		{
			if (tick - group.lastUpdateTick < 10 || group.nukeEvadeStep > 0)
				continue;
			
			if (group.shrinkActive)
			{
				if (tick - group.lastShrinkTick > 40 || !anyMoved(group))
				{
					group.shrinkActive = false;
				}
				else
				{
					continue;
				}
			}
			
			calcDangerDistCells(group);
			
			P center = group.center;
			const BBox &bbox = group.bbox;
			int groupSize = group.size;
			double area = bbox.area();
			bool shrinkRequired = (area > groupSize * 40.0 || group.shrinkAfterNuke) && ((tick - group.lastUpdateTick) > 60 && (tick - group.lastShrinkTick) > 300);
			P shrinkPoint;
			if (shrinkRequired)
			{
				ShrinkResult shRes = findShrink(group);
				if (shRes.ticks > 7)
				{
					shrinkPoint = shRes.shrinkPoint;
				}
				else
				{
					shrinkRequired = false;
				}
				//LOG("SHRINK RES " << shRes.ticks << " " << shRes.endBBox << " P " << shRes.shrinkPoint);
			}
			//bool shrinkRequired = false;
			
			const double border = 20.0;
			P c = center;
			double R = 20 + unitVel(group.unitType) * 40;
			/*if (group.unitType == UnitType::ARV)
				R = 200.0;*/
			double ticks = R / unitVel(group.unitType);
			double curVal = attractionPoint(center, group, ticks, angryMode);
			P tp = center;
			bool found = false;
			bool canMoveFlag = false;
			
			for (int k = 0; k < 3.0; ++k)
			{
				ticks = R / unitVel(group.unitType);
				int di = -1;
				
				for (int i = 0; i < 20; ++i)
				{
					P p = c + P(PI * 2.0 / 20.0 * i) * R;
					if (p.x > border && p.y > border && p.x < (WIDTH - border) && p.y < (HEIGHT - border))
					{
						if (canMove(p - center, group))
						{
							canMoveFlag = true;
							double val = attractionPoint(p, group, ticks, angryMode);
							
							DebugAttractionPointsInfo debugInfo;
							debugInfo.point = c;
							debugInfo.dir = p - c;
							debugInfo.val = val;
							debugAttractionPoints.push_back(debugInfo);
							
							if (val > curVal)
							{
								curVal = val;
								di = i;
								tp = p;
								found = true;
							}
						}
					}
				}
				
				if (di >= 0)
				{
					c = tp;
				}
				
				R /= 1.5;
			}
			
			bool moveAway = false;
			P dirAway;
			P newShift;
			if (!canMoveFlag)
			{
				dirAway = P(0, 0);
				for (Group &othGroup : groups)
				{
					if (&group != &othGroup && isGroundUnit(group.unitType) == isGroundUnit(othGroup.unitType))
					{
						P dp = group.center - othGroup.center;
						double clen = dp.len();
						
						if (clen < 150.0)
							dirAway += dp / clen;
					}
				}
				
				dirAway *= 10;
				dirAway += P(tick % 7 - 3, tick % 9 - 5);
				moveAway = true;
			}
			else
			{
				newShift = P(0, 0);
				if (found)
					newShift = tp - center;
				
				if (newShift.len() < 15.0)
				{
					calcMicroShift(group, newShift);
				}
			}
			
			found = newShift.len2() > 0.1;
			
			if (found || shrinkRequired || moveAway)
			{
				if (!isSelected(group))
				{
					result = select(group);
					group.actionStarted = true;
				}
				else
				{
					if (shrinkRequired)
					{
						result.action = MyActionType::SCALE;
						result.factor = 0.2;
						result.p = shrinkPoint;
						group.lastShrinkTick = tick;
						group.shrinkActive = true;
					}
					else if (found)
					{
						result.action = MyActionType::MOVE;
						result.p = newShift;
						
						/*if (group.unitType == UnitType::FIGHTER)
							result.maxSpeed = 1.0;*/
					}
					else
					{
						result.action = MyActionType::MOVE;
						result.p = dirAway;
						//std::cout << "AWAY " << dirAway.x << " " << dirAway.y << std::endl;
					}
					
					group.lastUpdateTick = tick;
					group.actionStarted = false;
				}
				
				break;
			}
		}
	}
	
	return result;
}

void Strat::calcDangerDistCells(const Group &group)
{
	dngGr = &group;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
			double totalMyDamage = 0;
			
			for (int myType = 0; myType < 5; ++myType)
			{
				double typeDamage = 0;
				if (group.sizeByTypes[myType])
				{
					for (int enemyType = 0; enemyType < 5; ++enemyType)
					{
						if (cell.count[enemyType])
						{
							double dmg = getDamage((UnitType) myType, (UnitType) enemyType);
							typeDamage = std::max(typeDamage, std::min(dmg * group.sizeByTypes[myType], 120.0 * cell.count[enemyType]));
						}
					}
				}
				totalMyDamage += typeDamage;
			}
			
			double enemyDamage = 0;
			double enemyHealth = 0.0;
			double f2hDmg = 0.0;
			for (int enemyType = 0; enemyType < 5; ++enemyType)
			{
				double typeDamage = 0;
				if (cell.count[enemyType])
				{
					enemyHealth += cell.health[enemyType];
					for (int myType = 0; myType < 5; ++myType)
					{
						if (group.sizeByTypes[myType])
						{
							double dmg = getDamage((UnitType) enemyType, (UnitType) myType);
							typeDamage = std::max(typeDamage, std::min(dmg * cell.count[enemyType], 120.0 * group.sizeByTypes[myType]));
							
							if (enemyType == (int) UnitType::FIGHTER && myType == (int) UnitType::HELICOPTER)
								f2hDmg = typeDamage;
						}
					}
				}
				enemyDamage += typeDamage;
			}
			
			enemyDamage *= 1.4;
			
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			dCell.enemyDamage = enemyDamage;
			dCell.enemyHealth = enemyHealth;
			dCell.totalMyDamage = totalMyDamage;
			dCell.f2hDmg = f2hDmg;
		}
	}
}

double Strat::attractionPoint(const P from, const Group &group, double ticks, bool angryMode)
{
	double res = 0.0;
	double f2hDmgRes = 0.0;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			if (dCell.totalMyDamage > 0.0 || dCell.enemyDamage > 0.0)
			{
				//double pts = group.health / dCell.enemyDamage - dCell.enemyHealth / dCell.totalMyDamage;
				//double pts = (dCell.totalMyDamage - dCell.enemyDamage) / (group.size)/10;
				double alpha = 0.3;
				double alphaM1 = 0.7;
				double pts = (group.health * alphaM1 + dCell.enemyHealth * alpha) / (dCell.enemyHealth*0.01 + dCell.enemyDamage) 
				- (dCell.enemyHealth * alphaM1 + group.health * alpha) / (group.health * 0.01 + dCell.totalMyDamage);
				
				P p = P((x + 0.5) * DISTR_MAT_CELL_SIZE, (y + 0.5) * DISTR_MAT_CELL_SIZE);
				
				double dist2 = p.dist2(from);
				if (pts > 0.0)
				{
					double pp = 1.0/(1.0 + dist2);
					res += pts * pp;
				}
				else
				{
					double dangerRad = 150.0;
					double pn = (1.0 - std::min(1.0, dist2/(dangerRad*dangerRad)));
					res += pts * pn;
				}
				
				//res += pts / p.dist2(from);
			}
			
			f2hDmgRes += dCell.f2hDmg;
		}
	}
	
	if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		double L = 1.5 * WIDTH;
		if (f2hDmgRes > 0.0 && res < 0.0)
		{
			if (fivG && fivG->size > 10)
			{
				L = fivG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::IFV];
		
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 300.0)
			{
				res -= (l - 300.0);
			}
		}
	}
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::HELICOPTER);
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 400.0)
			{
				res -= (l - 400.0);
			}
		}
	}*/
	
	const double keepBorderDist = 40.0;
	double borderPen = std::max(
		std::max(
			std::max(keepBorderDist - from.x, 0.0),
			std::max(keepBorderDist - from.y, 0.0)
		),
		std::max(
			std::max(keepBorderDist - (WIDTH - from.x), 0.0),
			std::max(keepBorderDist - (HEIGHT - from.y), 0.0)
		)
	);
	
	res -= borderPen*10;
	
	return res;
}


ShrinkResult Strat::findShrink(Group &group)
{
	ShrinkResult result;
	
	Simulator sim;
	sim.tick = tick;
	std::copy(cells, cells + CELLS_X * CELLS_Y, sim.cells);
	
	BBox gbox = group.bbox;
	gbox.expand(2.0);
	for (const MyUnit &u : units)
	{
		if (gbox.inside(u.pos))
		{
			sim.units.push_back(u);
			MyUnit &tu = *sim.units.rbegin();
			tu.selected = group.check(tu);
			tu.vel = P(0, 0);
		}
	}
	sim.groups.push_back(group);
	
	double bestArea = WIDTH * HEIGHT;
	for (double x = -1; x <= 1; ++x)
	{
		for (double y = -1; y <= 1; ++y)
		{
			Simulator sim2 = sim;
			MyMove myMove;
			myMove.action = MyActionType::SCALE;
			myMove.p = group.center + P(x, y) * 10.0;
			myMove.factor = 0.2;
			sim2.registerMove(myMove, 0);
			
			int i = 0;
			for (; i < 20; ++i)
			{
				sim2.tick++;
				sim2.resetAxisSorts();
				sim2.applyMoves();
				int cnt = sim2.moveUnits();
				if (!cnt)
					break;
			}
			sim2.updateStats();
			
			double area = sim2.groups.rbegin()->bbox.area();
			if (area < bestArea)
			{
				bestArea = area;
				result.shrinkPoint = myMove.p;
				result.ticks = i;
				result.endBBox = sim2.groups.rbegin()->bbox;
			}
		}
	}
	
	return result;
}
}


////////////////////////////////////////////////////////////// V11
namespace StratV11 {
	double groupPriority(const Group &g) {
	double res = 0.0;
	
	if (g.shrinkAfterNuke)
		res += 1100.0;
	else if (g.actionStarted)
		res += 1000.0;
	
	res -= g.lastUpdateTick;
	
	return res;
}

void DistributionMatrix::clear()
{
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		Cell &cell = cells[i];
		for (int j = 0; j < 5; ++j)
			cell.count[j] = 0;
		for (int j = 0; j < 5; ++j)
			cell.health[j] = 0;
	}
}

void DistributionMatrix::initialize(const Simulator &sim)
{
	clear();
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : sim.units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / DISTR_MAT_CELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / DISTR_MAT_CELL_SIZE;
				
				Cell &cell = getCell(x, y);
				int type = (int) u.type;
				cell.count[type] += 1.0 / K;
				cell.health[type] += u.durability / K;
			}
		}
	}
}

void DistributionMatrix::blur(DistributionMatrix &oth) const
{
	oth.clear();
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			Cell &resCell = oth.getCell(x, y);
			int cnt = 0;
			for (int yy = -1; yy <= 1; ++yy)
			{
				for (int xx = -1; xx <= 1; ++xx)
				{
					int x2 = x + xx;
					int y2 = y + yy;
					if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
					{
						++cnt;
						const Cell &cell = getCell(x2, y2);
						for (int i = 0; i < 5; ++i)
							resCell.count[i] += cell.count[i];
						for (int i = 0; i < 5; ++i)
							resCell.health[i] += cell.health[i];
					}
				}
			}
			
			const Cell &cell = getCell(x, y);
			for (int i = 0; i < 5; ++i)
			{
				if (!cell.count[i])
				{
					resCell.count[i] = 0;
					resCell.health[i] = 0;
				}
			}
			
			/*for (int i = 0; i < 5; ++i)
				resCell.count[i] += (resCell.count[i] + cnt - 1) / cnt;
			for (int i = 0; i < 5; ++i)
				resCell.health[i] /= cnt;*/
		}
	}
}

Strat::Strat::Strat()
{
	Group g;
	g.actionStarted = false;
	g.lastUpdateTick = 0;
	g.lastShrinkTick = 0;
	
	for (int i = 0; i < 1; ++i)
	{
		g.unitType = UnitType::HELICOPTER;
		//g.miniGroupInd = i;
		groups.push_back(g);
	}
	g.miniGroupInd = 0;
	
	g.unitType = UnitType::IFV;
	groups.push_back(g);
	
	g.unitType = UnitType::FIGHTER;
	groups.push_back(g);
	
	g.unitType = UnitType::TANK;
	groups.push_back(g);
	
	g.unitType = UnitType::ARV;
	groups.push_back(g);
}


void Strat::calcMicroShift(Group &group, P &shift)
{
	if (group.unitType != UnitType::TANK && group.unitType != UnitType::FIGHTER)
		return;
	
	BBox bbox = group.bbox;
	bbox.p1 += shift - P(35, 35);
	bbox.p2 += shift + P(35, 35);
	
	int finalScore = -100000;
	int currentScore = -100000;
	double ticks = 20.0;
	
	//if (group.unitType == UnitType::TANK)
	{
		std::vector<P> enemies[(int) UnitType::COUNT];
		std::vector<P> myUnits;
		
		for (const MyUnit &u : units)
		{
			if (u.side > 0 && (u.type == UnitType::TANK || u.type == UnitType::HELICOPTER || u.type == UnitType::IFV) && bbox.inside(u.pos))
			{
				enemies[(int) u.type].push_back(u.pos + u.vel * ticks);
			}
			else if (group.check(u))
			{
				myUnits.push_back(u.pos);
			}
		}
		
		double L = 8.0;
		P newShift = shift;
		for (int k = 0; k < 4; ++k)
		{
			P foundShift = newShift;
			int foundScore = -100000;
			for (int i = 0; i < 9; ++i)
			{
				P testShift = newShift + P(i / 3 - 1, i % 3 - 1) * L;
				int score = 0;
				bool isCurShift = shift.dist2(testShift) < 0.1;
				
				if (!isCurShift || currentScore == -100000)
				{
					const std::vector<MicroShiftValues> &pos = microShiftMatrix.pos[(int) group.unitType];
					if (!pos.empty())
					{
						for (const P &myP : myUnits)
						{
							P myPos = myP + testShift;
							bool found = false;
							for (const MicroShiftValues &vals : pos)
							{
								if (!found)
								{
									for (const P &enP : enemies[(int) vals.unitType])
									{
										if (myPos.dist2(enP) < vals.dist2)
										{
											score += vals.val;
											found = true;
											break;
										}
									}
								}
							}
						}
					}
					
					// !! decrease SCORE
					int posScore = score;
					score /= 2;
					
					const std::vector<MicroShiftValues> &neg = microShiftMatrix.neg[(int) group.unitType];
					if (!neg.empty())
					{
						for (const MicroShiftValues &vals : neg)
						{
							for (const P &enP : enemies[(int) vals.unitType])
							{
								for (const P &myP : myUnits)
								{
									P myPos = myP + testShift;
									if (myPos.dist2(enP) < vals.dist2)
									{
										score -= vals.val;
										break;
									}
								}
							}
						}
					}
					
					int negScore = posScore / 2 - score;
					
					if (foundScore < score)
					{
						foundShift = testShift;
						foundScore = score;
					}
					
					if (isCurShift)
						currentScore = score;
				}
			}
			
			L /= 2.0;
			newShift = foundShift;
			
			finalScore = foundScore;
		}
		
		if (currentScore >= finalScore)
			return;
		
		shift = newShift;
	}
}

void Strat::calcNuclearEfficiency()
{
	for (int i = 0; i < MICROCELLS_X * MICROCELLS_Y; i++)
		nuclearEfficiency[i] = 0;
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / MICROCELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / MICROCELL_SIZE;
				
				double efficiency = 100.0 / (10.0 + u.durability) / K;
				
				nuclearEfficiency[y * MICROCELLS_X + x] += efficiency;
			}
		}
	}
}

MyMove Strat::calcNextMove()
{
	MyMove result;
	result.action = MyActionType::NONE;
	
	int moves = getAvailableActions(12);
	if (moves < 1)
		return result;
	
	if (players[1].remainingNuclearStrikeCooldownTicks < 30)
	{
		if (moves < 3)
			return result;
	}
	
	updateStats();
	
	if (players[1].nextNuclearStrikeTick >= 0)
	{
		bool select = false;
		bool expand = false;
		BBox bbox;
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep == 0)
			{
				BBox gbox = g.bbox;
				gbox.expand(40.0);
				if (gbox.inside(players[1].nuclearStrike))
				{
					bbox.add(g.bbox);
					g.nukeEvadeStep = 1;
					select = true;
				}
			}
			else if (g.nukeEvadeStep == 1)
			{
				expand = true;
			}
		}
		
		if (select)
		{
			result.action = MyActionType::CLEAR_AND_SELECT;
			result.p1 = bbox.p1;
			result.p2 = bbox.p2;
			
			return result;
		}
		else if (expand)
		{
			result.action = MyActionType::SCALE;
			result.p = players[1].nuclearStrike;
			result.factor = 9.0;
			return result;
		}
	}
	else
	{
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep > 0)
				g.shrinkAfterNuke = true;
			if (tick - g.lastShrinkTick > 30)
				g.shrinkAfterNuke = false;
			g.nukeEvadeStep = 0;
		}
	}
	
	
	if (!initialGroupsGerationDone)
	{
		for (Group &g : groups)
		{
			if (g.unitType == UnitType::HELICOPTER && !g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					P allCenter = P(0, 0);
					int count = 0;
					for (const MyUnit &u : units)
					{
						if (u.side == 0 && u.type == g.unitType)
						{
							allCenter += u.pos;
							++count;
						}
					}
					
					allCenter /= count;
					
					if (g.miniGroupInd == 0)
					{
						result.p1 = P(0, 0);
						result.p2 = allCenter;
					}
					else if (g.miniGroupInd == 1)
					{
						result.p1 = P(allCenter.x, 0);
						result.p2 = P(WIDTH, allCenter.y);
					}
					else if (g.miniGroupInd == 2)
					{
						result.p1 = P(0, allCenter.y);
						result.p2 = P(allCenter.x, HEIGHT);
					}
					else if (g.miniGroupInd == 3)
					{
						result.p1 = allCenter;
						result.p2 = P(WIDTH, HEIGHT);
					}
					
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}
		}
		
		initialGroupsGerationDone = true;
	}
	
	bool angryMode = angryModeTill > tick;

	
	DistributionMatrix matr;
	matr.initialize(*this);
	matr.blur(distributionMatrix);
	
	/*if (tick > 532)
	{
		std::cout.width(2);
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &resCell = distributionMatrix.getCell(x, y);
				std::cout << " " << resCell.count[1];
			}
			std::cout << std::endl;
		}
		std::cout << std::endl;
	}*/
	
	if (players[0].remainingNuclearStrikeCooldownTicks == 0)
	{
		resetCells();
		calcNuclearEfficiency();
		
		auto getEnemySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0.0;
			
			return nuclearEfficiency[y * MICROCELLS_X + x];
		};
		
		auto getMySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0;
			
			return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
				+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
				+ (int) getMicrocell(x, y, 0, UnitType::ARV).size() / 2;
		};
		
		P bestTarget;
		int bestScore = 0;
		
		for (int y = 0; y < MICROCELLS_Y; ++y) {
			for (int x = 0; x < MICROCELLS_X; ++x) {
				double enemyN = getEnemySize(x, y);
				if (enemyN > 10)
				{
					int totalMyCount = 0;
					int myCenterCount = 0;
					for (int xx = -1; xx <= 1; ++xx)
					{
						for (int yy = -1; yy <= 1; ++yy)
						{
							totalMyCount += getMySize(x + xx, y + yy);
							if (xx == 0 && yy == 0)
								myCenterCount = totalMyCount;
						}
					}
					
					if (totalMyCount < 20 && myCenterCount == 0)
					{
						int totalMyExtCount = totalMyCount;
						if (totalMyExtCount == 0)
						{
							for (int xx = -2; xx <= 2; ++xx)
							{
								for (int yy = -2; yy <= 2; ++yy)
								{
									if (xx < -1 || xx > 1 || yy < -1 || yy > 1)
										totalMyExtCount += getMySize(x + xx, y + yy);
								}
							}
						}
						
						if (totalMyExtCount > 0)
						{
							double totalEnemyCount =
								enemyN +
								getEnemySize(x - 1, y)/2 +
								getEnemySize(x + 1, y)/2 + 
								getEnemySize(x, y - 1)/2 + 
								getEnemySize(x, y + 1)/2;
							
							if (bestScore < totalEnemyCount)
							{
								bestScore = totalEnemyCount;
								bestTarget = P(x + 0.5, y + 0.5) * MICROCELL_SIZE;
							}
						}
					}
				}
			}
		}
		
		if (bestScore > 0)
		{
			// Correct point
			P correctedBestTarget = bestTarget;
			
			std::vector<P> myVehicles;
			std::vector<P> enemyVehicles;
			int myInnerN = 0;
			int enemyInnerN = 0;
			for (const MyUnit &p : units)
			{
				double d2 = p.pos.dist2(bestTarget);
				if (d2 < (75.0*75.0))
				{
					if (d2 < (38.0*38.0))
					{
						if (p.side == 0)
							myInnerN++;
						else
							enemyInnerN++;
					}
					else
					{
						if (p.side == 0)
							myVehicles.push_back(p.pos);
						else
							enemyVehicles.push_back(p.pos);
					}
				}
			}
			
			int foundScore = -100000;
			for (int xx = -2; xx <= 2; ++xx)
			{
				for (int yy = -2; yy <= 2; ++yy)
				{
					P newTarget = bestTarget + P(xx, yy) * 6.0;
					
					int myNum = myInnerN*3;
					int myExtNum = 0;
					
					for (const P &p : myVehicles)
					{
						double d2 = p.dist2(newTarget);
						if (d2 < (50.0*50.0))
						{
							myNum++;
						}
						else if (d2 > (55.0*55.0) && d2 < (70.0*70.0))
						{
							myExtNum++;
						}
					}
					
					if (myExtNum > 0)
					{
						int enNum = enemyInnerN*3;
						for (const P &p : enemyVehicles)
						{
							double d2 = p.dist2(newTarget);
							if (d2 < (50.0*50.0))
							{
								enNum++;
							}
						}
						
						int score = enNum - myNum * 4;
						if (score > foundScore)
						{
							foundScore = score;
							correctedBestTarget = newTarget;
						}
					}
				}
			}
			
			if (foundScore > -100000)
			{
				auto getMySize = [this](int x, int y) {
					if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
						return 0;
					
					return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
						+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
						+ (int) getMicrocell(x, y, 0, UnitType::ARV).size();
				};
				
				int pts = 0;
				long bestId = -1;
				P vehPos;
				
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.durability > 70)
					{
						double dist2 = p.pos.dist2(correctedBestTarget);
						if (dist2 > 55*55 && dist2 < 70*70)
						{
							bool outOfRange = false;
							
							if (p.vel.len2() > 0.01)
							{
								MyUnit u = p;
								for (int i = 0; i < 30; ++i)
								{
									double visRange = getVisionRange(u) - unitVel(p.type) * 10;
									if (u.pos.dist2(correctedBestTarget) > sqr(visRange))
									{
										outOfRange = true;
										break;
									}
									u.pos += u.vel;
								}
							}
							
							if (outOfRange)
								continue;
							
							int xx = p.pos.x / MICROCELL_SIZE;
							int yy = p.pos.y / MICROCELL_SIZE;
							
							//int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 60.0);
							if (pts < newPts)
							{
								pts = newPts;
								bestId = p.id;
								vehPos = p.pos;
							}
						}
					}
				}
				
				if (bestId >= 0)
				{
					result.action = MyActionType::TACTICAL_NUCLEAR_STRIKE;
					result.vehicleId = bestId;
					result.p = correctedBestTarget;
					return result;
				}
			}
		}
	}
	
	groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group &g){return g.size == 0;}), groups.end());
	
	if (result.action == MyActionType::NONE && !groups.empty())
	{
		debugAttractionPoints.clear();
		
		std::sort(groups.begin(), groups.end(), [](const Group &g1, const Group &g2){
			double p1 = groupPriority(g1);
			double p2 = groupPriority(g2);
			return p2 < p1;
		});
		
		for (Group &group : groups)
		{
			if (tick - group.lastUpdateTick < 10 || group.nukeEvadeStep > 0)
				continue;
			
			if (group.shrinkActive)
			{
				if (tick - group.lastShrinkTick > 40 || !anyMoved(group))
				{
					group.shrinkActive = false;
				}
				else
				{
					continue;
				}
			}
			
			bool limitSpeed = false;
			if (nukeVehicleInd >= 0)
			{
				const MyUnit &u = units[nukeVehicleInd];
				if (group.check(u))
					limitSpeed = true;
			}
			
			calcDangerDistCells(group);
			
			P center = group.center;
			const BBox &bbox = group.bbox;
			int groupSize = group.size;
			double area = bbox.area();
			bool shrinkRequired = (area > groupSize * 40.0 || group.shrinkAfterNuke) && (((tick - group.lastUpdateTick) > 60 || group.shrinkAfterNuke) && (tick - group.lastShrinkTick) > 300);
			P shrinkPoint;
			if (shrinkRequired)
			{
				ShrinkResult shRes = findShrink(group);
				if (shRes.ticks > 7)
				{
					shrinkPoint = shRes.shrinkPoint;
				}
				else
				{
					shrinkRequired = false;
				}
				//LOG("SHRINK RES " << shRes.ticks << " " << shRes.endBBox << " P " << shRes.shrinkPoint);
			}
			//bool shrinkRequired = false;
			
			const double border = 20.0;
			P c = center;
			double R = 20 + unitVel(group.unitType) * 40;
			/*if (group.unitType == UnitType::ARV)
				R = 200.0;*/
			double ticks = R / unitVel(group.unitType);
			double curVal = attractionPoint(center, group, ticks, angryMode);
			P tp = center;
			bool found = false;
			bool canMoveFlag = false;
			
			for (int k = 0; k < 3.0; ++k)
			{
				ticks = R / unitVel(group.unitType);
				int di = -1;
				
				for (int i = 0; i < 20; ++i)
				{
					P p = c + P(PI * 2.0 / 20.0 * i) * R;
					if (p.x > border && p.y > border && p.x < (WIDTH - border) && p.y < (HEIGHT - border))
					{
						if (canMove(p - center, group))
						{
							canMoveFlag = true;
							double val = attractionPoint(p, group, ticks, angryMode);
							
							DebugAttractionPointsInfo debugInfo;
							debugInfo.point = c;
							debugInfo.dir = p - c;
							debugInfo.val = val;
							debugAttractionPoints.push_back(debugInfo);
							
							if (val > curVal)
							{
								curVal = val;
								di = i;
								tp = p;
								found = true;
							}
						}
					}
				}
				
				if (di >= 0)
				{
					c = tp;
				}
				
				R /= 1.5;
			}
			
			bool moveAway = false;
			P dirAway;
			P newShift;
			if (!canMoveFlag)
			{
				dirAway = P(0, 0);
				for (Group &othGroup : groups)
				{
					if (&group != &othGroup && isGroundUnit(group.unitType) == isGroundUnit(othGroup.unitType))
					{
						P dp = group.center - othGroup.center;
						double clen = dp.len();
						
						if (clen < 150.0)
							dirAway += dp / clen;
					}
				}
				
				dirAway *= 10;
				dirAway += P(tick % 7 - 3, tick % 9 - 5);
				moveAway = true;
			}
			else
			{
				newShift = P(0, 0);
				if (found)
					newShift = tp - center;
				
				if (newShift.len() < 15.0)
				{
					calcMicroShift(group, newShift);
				}
			}
			
			found = newShift.len2() > 0.1;
			
			if (found || shrinkRequired || moveAway)
			{
				if (!isSelected(group))
				{
					result = select(group);
					group.actionStarted = true;
				}
				else
				{
					if (shrinkRequired)
					{
						result.action = MyActionType::SCALE;
						result.factor = 0.2;
						result.p = shrinkPoint;
						group.lastShrinkTick = tick;
						group.shrinkActive = true;
					}
					else if (found)
					{
						result.action = MyActionType::MOVE;
						result.p = newShift;
						
						if (limitSpeed)
							result.maxSpeed = unitVel(group.unitType) * 0.6;
						
						/*if (!limitSpeed)
						{
							limitSpeed = !anyEnemiesNearbyByDangerDistr(group);
							if (limitSpeed)
								result.maxSpeed = unitVel(group.unitType) * 0.8;
						}*/
						
						/*if (group.unitType == UnitType::FIGHTER)
							result.maxSpeed = 1.0;*/
					}
					else
					{
						result.action = MyActionType::MOVE;
						result.p = dirAway;
						//std::cout << "AWAY " << dirAway.x << " " << dirAway.y << std::endl;
					}
					
					group.lastUpdateTick = tick;
					group.actionStarted = false;
				}
				
				break;
			}
		}
	}
	
	return result;
}

bool Strat::anyEnemiesNearbyByDangerDistr(const Group &group)
{
	int x = group.center.x / DISTR_MAT_CELL_SIZE;
	int y = group.center.y / DISTR_MAT_CELL_SIZE;
	for (int yy = -7; yy <= 7; ++yy)
	{
		for (int xx = -7; xx <= 7; ++xx)
		{
			if (xx * xx + yy * yy <= 50)
			{
				int x2 = x + xx;
				int y2 = y + yy;
				if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
				{
					DangerDistCell &dCell = dangerDistCells[y2 * DISTR_MAT_CELLS_X + x2];
					if (dCell.enemyDamage > 0.0 || dCell.totalMyDamage > 0.0)
					{
						return true;
					}
				}
			}
		}
	}
	
	return false;
}

void Strat::calcDangerDistCells(const Group &group)
{
	dngGr = &group;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
			double totalMyDamage = 0;
			
			for (int myType = 0; myType < 5; ++myType)
			{
				double typeDamage = 0;
				if (group.sizeByTypes[myType])
				{
					for (int enemyType = 0; enemyType < 5; ++enemyType)
					{
						if (cell.count[enemyType])
						{
							double dmg = getDamage((UnitType) myType, (UnitType) enemyType);
							typeDamage = std::max(typeDamage, std::min(dmg * group.sizeByTypes[myType], 120.0 * cell.count[enemyType]));
						}
					}
				}
				totalMyDamage += typeDamage;
			}
			
			double enemyDamage = 0;
			double enemyHealth = 0.0;
			double f2hDmg = 0.0;
			for (int enemyType = 0; enemyType < 5; ++enemyType)
			{
				double typeDamage = 0;
				if (cell.count[enemyType])
				{
					enemyHealth += cell.health[enemyType];
					for (int myType = 0; myType < 5; ++myType)
					{
						if (group.sizeByTypes[myType])
						{
							double dmg = getDamage((UnitType) enemyType, (UnitType) myType);
							typeDamage = std::max(typeDamage, std::min(dmg * cell.count[enemyType], 120.0 * group.sizeByTypes[myType]));
							
							if (enemyType == (int) UnitType::FIGHTER && myType == (int) UnitType::HELICOPTER)
								f2hDmg = typeDamage;
						}
					}
				}
				enemyDamage += typeDamage;
			}
			
			enemyDamage *= 1.4;
			
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			dCell.enemyDamage = enemyDamage;
			dCell.enemyHealth = enemyHealth;
			dCell.totalMyDamage = totalMyDamage;
			dCell.f2hDmg = f2hDmg;
		}
	}
}

double Strat::attractionPoint(const P from, const Group &group, double ticks, bool angryMode)
{
	double res = 0.0;
	double f2hDmgRes = 0.0;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			if (dCell.totalMyDamage > 0.0 || dCell.enemyDamage > 0.0)
			{
				//double pts = group.health / dCell.enemyDamage - dCell.enemyHealth / dCell.totalMyDamage;
				//double pts = (dCell.totalMyDamage - dCell.enemyDamage) / (group.size)/10;
				double alpha = 0.3;
				double alphaM1 = 0.7;
				double pts = (group.health * alphaM1 + dCell.enemyHealth * alpha) / (dCell.enemyHealth*0.01 + dCell.enemyDamage) 
				- (dCell.enemyHealth * alphaM1 + group.health * alpha) / (group.health * 0.01 + dCell.totalMyDamage);
				
				P p = P((x + 0.5) * DISTR_MAT_CELL_SIZE, (y + 0.5) * DISTR_MAT_CELL_SIZE);
				
				double dist2 = p.dist2(from);
				if (pts > 0.0)
				{
					double pp = 1.0/(1.0 + dist2);
					res += pts * pp;
				}
				else
				{
					double dangerRad = 150.0;
					double pn = (1.0 - std::min(1.0, dist2/(dangerRad*dangerRad)));
					res += pts * pn;
				}
				
				//res += pts / p.dist2(from);
			}
			
			f2hDmgRes += dCell.f2hDmg;
		}
	}
	
	if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		double L = 1.5 * WIDTH;
		if (f2hDmgRes > 0.0 && res < 0.0)
		{
			if (fivG && fivG->size > 10)
			{
				L = fivG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::IFV];
		
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 300.0)
			{
				res -= (l - 300.0);
			}
		}
	}
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::HELICOPTER);
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 400.0)
			{
				res -= (l - 400.0);
			}
		}
	}*/
	
	const double keepBorderDist = 40.0;
	double borderPen = std::max(
		std::max(
			std::max(keepBorderDist - from.x, 0.0),
			std::max(keepBorderDist - from.y, 0.0)
		),
		std::max(
			std::max(keepBorderDist - (WIDTH - from.x), 0.0),
			std::max(keepBorderDist - (HEIGHT - from.y), 0.0)
		)
	);
	
	res -= borderPen*10;
	
	return res;
}


ShrinkResult Strat::findShrink(Group &group)
{
	ShrinkResult result;
	
	Simulator sim;
	sim.tick = tick;
	std::copy(cells, cells + CELLS_X * CELLS_Y, sim.cells);
	
	BBox gbox = group.bbox;
	gbox.expand(2.0);
	for (const MyUnit &u : units)
	{
		if (gbox.inside(u.pos))
		{
			sim.units.push_back(u);
			MyUnit &tu = *sim.units.rbegin();
			tu.selected = group.check(tu);
			tu.vel = P(0, 0);
		}
	}
	sim.groups.push_back(group);
	
	double bestArea = WIDTH * HEIGHT;
	for (double x = -1; x <= 1; ++x)
	{
		for (double y = -1; y <= 1; ++y)
		{
			Simulator sim2 = sim;
			MyMove myMove;
			myMove.action = MyActionType::SCALE;
			myMove.p = group.center + P(x, y) * 10.0;
			myMove.factor = 0.2;
			sim2.registerMove(myMove, 0);
			
			int i = 0;
			for (; i < 20; ++i)
			{
				sim2.tick++;
				sim2.resetAxisSorts();
				sim2.applyMoves();
				int cnt = sim2.moveUnits();
				if (!cnt)
					break;
			}
			sim2.updateStats();
			
			double area = sim2.groups.rbegin()->bbox.area();
			if (area < bestArea)
			{
				bestArea = area;
				result.shrinkPoint = myMove.p;
				result.ticks = i;
				result.endBBox = sim2.groups.rbegin()->bbox;
			}
		}
	}
	
	return result;
}
}

///////////////////////////////////////////////// V13
namespace StratV13 {
	double groupPriority(const Group &g) {
	double res = 0.0;
	
	if (g.shrinkAfterNuke)
		res += 1100.0;
	else if (g.actionStarted)
		res += 1000.0;
	
	res -= g.lastUpdateTick;
	
	return res;
}

void DistributionMatrix::clear()
{
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		Cell &cell = cells[i];
		for (int j = 0; j < 5; ++j)
			cell.count[j] = 0;
		for (int j = 0; j < 5; ++j)
			cell.health[j] = 0;
	}
}

void DistributionMatrix::initialize(const Simulator &sim)
{
	clear();
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : sim.units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / DISTR_MAT_CELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / DISTR_MAT_CELL_SIZE;
				
				Cell &cell = getCell(x, y);
				int type = (int) u.type;
				cell.count[type] += 1.0 / K;
				cell.health[type] += u.durability / K;
			}
		}
	}
}

void DistributionMatrix::blur(DistributionMatrix &oth) const
{
	oth.clear();
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			Cell &resCell = oth.getCell(x, y);
			int cnt = 0;
			for (int yy = -1; yy <= 1; ++yy)
			{
				for (int xx = -1; xx <= 1; ++xx)
				{
					int x2 = x + xx;
					int y2 = y + yy;
					if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
					{
						++cnt;
						const Cell &cell = getCell(x2, y2);
						for (int i = 0; i < 5; ++i)
							resCell.count[i] += cell.count[i];
						for (int i = 0; i < 5; ++i)
							resCell.health[i] += cell.health[i];
					}
				}
			}
			
			const Cell &cell = getCell(x, y);
			for (int i = 0; i < 5; ++i)
			{
				if (!cell.count[i])
				{
					resCell.count[i] = 0;
					resCell.health[i] = 0;
				}
			}
			
			/*for (int i = 0; i < 5; ++i)
				resCell.count[i] += (resCell.count[i] + cnt - 1) / cnt;
			for (int i = 0; i < 5; ++i)
				resCell.health[i] /= cnt;*/
		}
	}
}

Strat::Strat::Strat()
{
	Group g;
	g.actionStarted = false;
	g.lastUpdateTick = 0;
	g.lastShrinkTick = 0;
	
	for (int i = 0; i < 1; ++i)
	{
		g.unitType = UnitType::HELICOPTER;
		//g.miniGroupInd = i;
		groups.push_back(g);
	}
	g.miniGroupInd = 0;
	
	g.unitType = UnitType::IFV;
	groups.push_back(g);
	
	g.unitType = UnitType::FIGHTER;
	groups.push_back(g);
	
	g.unitType = UnitType::TANK;
	groups.push_back(g);
	
	g.unitType = UnitType::ARV;
	groups.push_back(g);
}


void Strat::calcMicroShift(Group &group, P &shift)
{
	if (group.unitType != UnitType::TANK && group.unitType != UnitType::FIGHTER)
		return;
	
	BBox bbox = group.bbox;
	bbox.p1 += shift - P(35, 35);
	bbox.p2 += shift + P(35, 35);
	
	int finalScore = -100000;
	int currentScore = -100000;
	double ticks = 20.0;
	
	//if (group.unitType == UnitType::TANK)
	{
		std::vector<P> enemies[(int) UnitType::COUNT];
		std::vector<P> myUnits;
		
		for (const MyUnit &u : units)
		{
			if (u.side > 0 && (u.type == UnitType::TANK || u.type == UnitType::HELICOPTER || u.type == UnitType::IFV) && bbox.inside(u.pos))
			{
				enemies[(int) u.type].push_back(u.pos + u.vel * ticks);
			}
			else if (group.check(u))
			{
				myUnits.push_back(u.pos);
			}
		}
		
		double L = 8.0;
		P newShift = shift;
		for (int k = 0; k < 4; ++k)
		{
			P foundShift = newShift;
			int foundScore = -100000;
			for (int i = 0; i < 9; ++i)
			{
				P testShift = newShift + P(i / 3 - 1, i % 3 - 1) * L;
				int score = 0;
				bool isCurShift = shift.dist2(testShift) < 0.1;
				
				if (!isCurShift || currentScore == -100000)
				{
					const std::vector<MicroShiftValues> &pos = microShiftMatrix.pos[(int) group.unitType];
					if (!pos.empty())
					{
						for (const P &myP : myUnits)
						{
							P myPos = myP + testShift;
							bool found = false;
							for (const MicroShiftValues &vals : pos)
							{
								if (!found)
								{
									for (const P &enP : enemies[(int) vals.unitType])
									{
										if (myPos.dist2(enP) < vals.dist2)
										{
											score += vals.val;
											found = true;
											break;
										}
									}
								}
							}
						}
					}
					
					// !! decrease SCORE
					int posScore = score;
					score /= 2;
					
					const std::vector<MicroShiftValues> &neg = microShiftMatrix.neg[(int) group.unitType];
					if (!neg.empty())
					{
						for (const MicroShiftValues &vals : neg)
						{
							for (const P &enP : enemies[(int) vals.unitType])
							{
								for (const P &myP : myUnits)
								{
									P myPos = myP + testShift;
									if (myPos.dist2(enP) < vals.dist2)
									{
										score -= vals.val;
										break;
									}
								}
							}
						}
					}
					
					int negScore = posScore / 2 - score;
					
					if (foundScore < score)
					{
						foundShift = testShift;
						foundScore = score;
					}
					
					if (isCurShift)
						currentScore = score;
				}
			}
			
			L /= 2.0;
			newShift = foundShift;
			
			finalScore = foundScore;
		}
		
		if (currentScore >= finalScore)
			return;
		
		shift = newShift;
	}
}

void Strat::calcNuclearEfficiency()
{
	for (int i = 0; i < MICROCELLS_X * MICROCELLS_Y; i++)
		nuclearEfficiency[i] = 0;
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / MICROCELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / MICROCELL_SIZE;
				
				double efficiency = 100.0 / (10.0 + u.durability) / K;
				
				if (u.type == UnitType::ARV)
					efficiency /= 10.0;
				
				if (u.type == UnitType::FIGHTER || u.type == UnitType::HELICOPTER)
					efficiency /= 1.5;
				
				nuclearEfficiency[y * MICROCELLS_X + x] += efficiency;
			}
		}
	}
}

MyMove Strat::calcNextMove()
{
	MyMove result;
	result.action = MyActionType::NONE;
	
	int moves = getAvailableActions(12);
	if (moves < 1)
		return result;
	
	if (players[1].remainingNuclearStrikeCooldownTicks < 30)
	{
		if (moves < 3)
			return result;
	}
	
	updateStats();
	
	if (players[1].nextNuclearStrikeTick >= 0)
	{
		bool select = false;
		bool expand = false;
		BBox bbox;
		int evaidingGroups = 0;
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep == 0)
			{
				BBox gbox = g.bbox;
				gbox.expand(40.0);
				if (gbox.inside(players[1].nuclearStrike))
				{
					bbox.add(g.bbox);
					g.nukeEvadeStep = 1;
					select = true;
					++evaidingGroups;
				}
			}
			else if (g.nukeEvadeStep == 1)
			{
				expand = true;
				g.nukeEvadeStep = 2;
				++evaidingGroups;
			}
			else
			{
				++evaidingGroups;
			}
		}
		
		if (select)
		{
			result.action = MyActionType::CLEAR_AND_SELECT;
			result.p1 = bbox.p1;
			result.p2 = bbox.p2;
			
			return result;
		}
		else if (expand)
		{
			result.action = MyActionType::SCALE;
			result.p = players[1].nuclearStrike;
			result.factor = 9.0;
			return result;
		}
		
		if (evaidingGroups && moves < 3)
			return result;
	}
	else
	{
		for (Group &g : groups)
		{
			if (tick - g.lastShrinkTick > 30)
				g.shrinkAfterNuke = false;
			if (g.nukeEvadeStep > 0)
				g.shrinkAfterNuke = true;
			g.nukeEvadeStep = 0;
		}
	}
	
	
	if (!initialGroupsGerationDone)
	{
		for (Group &g : groups)
		{
			if (g.unitType == UnitType::HELICOPTER && !g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					P allCenter = P(0, 0);
					int count = 0;
					for (const MyUnit &u : units)
					{
						if (u.side == 0 && u.type == g.unitType)
						{
							allCenter += u.pos;
							++count;
						}
					}
					
					allCenter /= count;
					
					if (g.miniGroupInd == 0)
					{
						result.p1 = P(0, 0);
						result.p2 = allCenter;
					}
					else if (g.miniGroupInd == 1)
					{
						result.p1 = P(allCenter.x, 0);
						result.p2 = P(WIDTH, allCenter.y);
					}
					else if (g.miniGroupInd == 2)
					{
						result.p1 = P(0, allCenter.y);
						result.p2 = P(allCenter.x, HEIGHT);
					}
					else if (g.miniGroupInd == 3)
					{
						result.p1 = allCenter;
						result.p2 = P(WIDTH, HEIGHT);
					}
					
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}
		}
		
		initialGroupsGerationDone = true;
	}
	
	bool angryMode = angryModeTill > tick;

	
	DistributionMatrix matr;
	matr.initialize(*this);
	matr.blur(distributionMatrix);
	
	/*if (tick > 532)
	{
		std::cout.width(2);
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &resCell = distributionMatrix.getCell(x, y);
				std::cout << " " << resCell.count[1];
			}
			std::cout << std::endl;
		}
		std::cout << std::endl;
	}*/
	
	if (players[0].remainingNuclearStrikeCooldownTicks == 0)
	{
		resetCells();
		calcNuclearEfficiency();
		
		auto getEnemySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0.0;
			
			return nuclearEfficiency[y * MICROCELLS_X + x];
		};
		
		auto getMySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0;
			
			return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
				+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
				+ (int) getMicrocell(x, y, 0, UnitType::ARV).size() / 2;
		};
		
		P bestTarget;
		int bestScore = 0;
		
		for (int y = 0; y < MICROCELLS_Y; ++y) {
			for (int x = 0; x < MICROCELLS_X; ++x) {
				double enemyN = getEnemySize(x, y);
				if (enemyN > 10)
				{
					int totalMyCount = 0;
					int myCenterCount = 0;
					for (int xx = -1; xx <= 1; ++xx)
					{
						for (int yy = -1; yy <= 1; ++yy)
						{
							totalMyCount += getMySize(x + xx, y + yy);
							if (xx == 0 && yy == 0)
								myCenterCount = totalMyCount;
						}
					}
					
					if (totalMyCount < 20 && myCenterCount == 0)
					{
						int totalMyExtCount = totalMyCount;
						if (totalMyExtCount == 0)
						{
							for (int xx = -2; xx <= 2; ++xx)
							{
								for (int yy = -2; yy <= 2; ++yy)
								{
									if (xx < -1 || xx > 1 || yy < -1 || yy > 1)
										totalMyExtCount += getMySize(x + xx, y + yy);
								}
							}
						}
						
						if (totalMyExtCount > 0)
						{
							double totalEnemyCount =
								enemyN +
								getEnemySize(x - 1, y)/2 +
								getEnemySize(x + 1, y)/2 + 
								getEnemySize(x, y - 1)/2 + 
								getEnemySize(x, y + 1)/2;
							
							if (bestScore < totalEnemyCount)
							{
								bestScore = totalEnemyCount;
								bestTarget = P(x + 0.5, y + 0.5) * MICROCELL_SIZE;
							}
						}
					}
				}
			}
		}
		
		if (bestScore > 0)
		{
			// Correct point
			P correctedBestTarget = bestTarget;
			
			std::vector<P> myVehicles;
			std::vector<P> enemyVehicles;
			int myInnerN = 0;
			int enemyInnerN = 0;
			for (const MyUnit &p : units)
			{
				double d2 = p.pos.dist2(bestTarget);
				if (d2 < (75.0*75.0))
				{
					if (d2 < (38.0*38.0))
					{
						if (p.side == 0)
							myInnerN++;
						else
							enemyInnerN++;
					}
					else
					{
						if (p.side == 0)
							myVehicles.push_back(p.pos);
						else
							enemyVehicles.push_back(p.pos);
					}
				}
			}
			
			int foundScore = -100000;
			for (int xx = -2; xx <= 2; ++xx)
			{
				for (int yy = -2; yy <= 2; ++yy)
				{
					P newTarget = bestTarget + P(xx, yy) * 6.0;
					
					int myNum = myInnerN*3;
					int myExtNum = 0;
					
					for (const P &p : myVehicles)
					{
						double d2 = p.dist2(newTarget);
						if (d2 < (50.0*50.0))
						{
							myNum++;
						}
						else if (d2 > (55.0*55.0) && d2 < (70.0*70.0))
						{
							myExtNum++;
						}
					}
					
					if (myExtNum > 0)
					{
						int enNum = enemyInnerN*3;
						for (const P &p : enemyVehicles)
						{
							double d2 = p.dist2(newTarget);
							if (d2 < (50.0*50.0))
							{
								enNum++;
							}
						}
						
						int score = enNum - myNum * 4;
						if (score > foundScore)
						{
							foundScore = score;
							correctedBestTarget = newTarget;
						}
					}
				}
			}
			
			if (foundScore > -100000)
			{
				auto getMySize = [this](int x, int y) {
					if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
						return 0;
					
					return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
						+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
						+ (int) getMicrocell(x, y, 0, UnitType::ARV).size();
				};
				
				int pts = 0;
				long bestId = -1;
				P vehPos;
				
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.durability > 70)
					{
						double dist2 = p.pos.dist2(correctedBestTarget);
						if (dist2 > 55*55 && dist2 < 70*70)
						{
							bool outOfRange = false;
							
							if (p.vel.len2() > 0.01)
							{
								MyUnit u = p;
								for (int i = 0; i < 30; ++i)
								{
									double visRange = getVisionRange(u) - unitVel(p.type) * 10;
									if (u.pos.dist2(correctedBestTarget) > sqr(visRange))
									{
										outOfRange = true;
										break;
									}
									u.pos += u.vel;
								}
							}
							
							if (outOfRange)
								continue;
							
							int xx = p.pos.x / MICROCELL_SIZE;
							int yy = p.pos.y / MICROCELL_SIZE;
							
							//int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 60.0);
							if (pts < newPts)
							{
								pts = newPts;
								bestId = p.id;
								vehPos = p.pos;
							}
						}
					}
				}
				
				if (bestId >= 0)
				{
					result.action = MyActionType::TACTICAL_NUCLEAR_STRIKE;
					result.vehicleId = bestId;
					result.p = correctedBestTarget;
					return result;
				}
			}
		}
	}
	
	groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group &g){return g.size == 0;}), groups.end());
	
	if (result.action == MyActionType::NONE && !groups.empty())
	{
		debugAttractionPoints.clear();
		
		std::sort(groups.begin(), groups.end(), [](const Group &g1, const Group &g2){
			double p1 = groupPriority(g1);
			double p2 = groupPriority(g2);
			return p2 < p1;
		});
		
		for (Group &group : groups)
		{
			if (tick - group.lastUpdateTick < 10 || group.nukeEvadeStep > 0)
				continue;
			
			if (group.shrinkActive)
			{
				if (tick - group.lastShrinkTick > 40 || !anyMoved(group))
				{
					group.shrinkActive = false;
				}
				else
				{
					continue;
				}
			}
			
			bool limitSpeed = false;
			if (nukeVehicleInd >= 0)
			{
				const MyUnit &u = units[nukeVehicleInd];
				if (group.check(u))
					limitSpeed = true;
			}
			
			calcDangerDistCells(group);
			
			P center = group.center;
			const BBox &bbox = group.bbox;
			int groupSize = group.size;
			double area = bbox.area();
			bool shrinkRequired = (area > groupSize * 40.0 || group.shrinkAfterNuke) && (((tick - group.lastUpdateTick) > 60 || group.shrinkAfterNuke) && (tick - group.lastShrinkTick) > 300);
			P shrinkPoint;
			if (shrinkRequired)
			{
				ShrinkResult shRes = findShrink(group);
				if (shRes.ticks > 7)
				{
					shrinkPoint = shRes.shrinkPoint;
				}
				else
				{
					shrinkRequired = false;
				}
				//LOG("SHRINK RES " << shRes.ticks << " " << shRes.endBBox << " P " << shRes.shrinkPoint);
			}
			//bool shrinkRequired = false;
			
			const double border = 20.0;
			P c = center;
			double R = 20 + unitVel(group.unitType) * 40;
			/*if (group.unitType == UnitType::ARV)
				R = 200.0;*/
			double ticks = R / unitVel(group.unitType);
			double curVal = attractionPoint(center, group, ticks, angryMode);
			P tp = center;
			bool found = false;
			bool canMoveFlag = false;
			
			for (int k = 0; k < 3.0; ++k)
			{
				ticks = R / unitVel(group.unitType);
				int di = -1;
				
				for (int i = 0; i < 20; ++i)
				{
					P p = c + P(PI * 2.0 / 20.0 * i) * R;
					if (p.x > border && p.y > border && p.x < (WIDTH - border) && p.y < (HEIGHT - border))
					{
						if (canMove(p - center, group))
						{
							canMoveFlag = true;
							double val = attractionPoint(p, group, ticks, angryMode);
							
							DebugAttractionPointsInfo debugInfo;
							debugInfo.point = c;
							debugInfo.dir = p - c;
							debugInfo.val = val;
							debugAttractionPoints.push_back(debugInfo);
							
							if (val > curVal)
							{
								curVal = val;
								di = i;
								tp = p;
								found = true;
							}
						}
					}
				}
				
				if (di >= 0)
				{
					c = tp;
				}
				
				R /= 1.5;
			}
			
			bool moveAway = false;
			P dirAway;
			P newShift;
			if (!canMoveFlag)
			{
				dirAway = P(0, 0);
				for (Group &othGroup : groups)
				{
					if (&group != &othGroup && isGroundUnit(group.unitType) == isGroundUnit(othGroup.unitType))
					{
						P dp = group.center - othGroup.center;
						double clen = dp.len();
						
						if (clen < 150.0)
							dirAway += dp / clen;
					}
				}
				
				dirAway *= 10;
				dirAway += P(tick % 7 - 3, tick % 9 - 5);
				moveAway = true;
			}
			else
			{
				newShift = P(0, 0);
				if (found)
					newShift = tp - center;
				
				if (newShift.len() < 15.0)
				{
					calcMicroShift(group, newShift);
				}
			}
			
			found = newShift.len2() > 0.1;
			
			if (found || shrinkRequired || moveAway)
			{
				if (!isSelected(group))
				{
					result = select(group);
					group.actionStarted = true;
				}
				else
				{
					if (shrinkRequired)
					{
						result.action = MyActionType::SCALE;
						result.factor = 0.2;
						result.p = shrinkPoint;
						group.lastShrinkTick = tick;
						group.shrinkActive = true;
					}
					else if (found)
					{
						result.action = MyActionType::MOVE;
						result.p = newShift;
						
						if (limitSpeed)
							result.maxSpeed = unitVel(group.unitType) * 0.6;
						
						/*if (!limitSpeed)
						{
							limitSpeed = !anyEnemiesNearbyByDangerDistr(group);
							if (limitSpeed)
								result.maxSpeed = unitVel(group.unitType) * 0.8;
						}*/
						
						/*if (group.unitType == UnitType::FIGHTER)
							result.maxSpeed = 1.0;*/
					}
					else
					{
						result.action = MyActionType::MOVE;
						result.p = dirAway;
						//std::cout << "AWAY " << dirAway.x << " " << dirAway.y << std::endl;
					}
					
					group.lastUpdateTick = tick;
					group.actionStarted = false;
				}
				
				break;
			}
		}
	}
	
	return result;
}

bool Strat::anyEnemiesNearbyByDangerDistr(const Group &group)
{
	int x = group.center.x / DISTR_MAT_CELL_SIZE;
	int y = group.center.y / DISTR_MAT_CELL_SIZE;
	for (int yy = -7; yy <= 7; ++yy)
	{
		for (int xx = -7; xx <= 7; ++xx)
		{
			if (xx * xx + yy * yy <= 50)
			{
				int x2 = x + xx;
				int y2 = y + yy;
				if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
				{
					DangerDistCell &dCell = dangerDistCells[y2 * DISTR_MAT_CELLS_X + x2];
					if (dCell.enemyDamage > 0.0 || dCell.totalMyDamage > 0.0)
					{
						return true;
					}
				}
			}
		}
	}
	
	return false;
}

void Strat::calcDangerDistCells(const Group &group)
{
	dngGr = &group;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
			double totalMyDamage = 0;
			
			for (int myType = 0; myType < 5; ++myType)
			{
				double typeDamage = 0;
				if (group.sizeByTypes[myType])
				{
					for (int enemyType = 0; enemyType < 5; ++enemyType)
					{
						if (cell.count[enemyType])
						{
							double dmg = getDamage((UnitType) myType, (UnitType) enemyType);
							typeDamage = std::max(typeDamage, std::min(dmg * group.sizeByTypes[myType], 120.0 * cell.count[enemyType]));
						}
					}
				}
				totalMyDamage += typeDamage;
			}
			
			double enemyDamage = 0;
			double enemyHealth = 0.0;
			double f2hDmg = 0.0;
			for (int enemyType = 0; enemyType < 5; ++enemyType)
			{
				double typeDamage = 0;
				if (cell.count[enemyType])
				{
					enemyHealth += cell.health[enemyType];
					for (int myType = 0; myType < 5; ++myType)
					{
						if (group.sizeByTypes[myType])
						{
							double dmg = getDamage((UnitType) enemyType, (UnitType) myType);
							typeDamage = std::max(typeDamage, std::min(dmg * cell.count[enemyType], 120.0 * group.sizeByTypes[myType]));
							
							if (enemyType == (int) UnitType::FIGHTER && myType == (int) UnitType::HELICOPTER)
								f2hDmg = typeDamage;
						}
					}
				}
				enemyDamage += typeDamage;
			}
			
			enemyDamage *= 1.4;
			
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			dCell.enemyDamage = enemyDamage;
			dCell.enemyHealth = enemyHealth;
			dCell.totalMyDamage = totalMyDamage;
			dCell.f2hDmg = f2hDmg;
		}
	}
}

double Strat::attractionPoint(const P from, const Group &group, double ticks, bool angryMode)
{
	double res = 0.0;
	double f2hDmgRes = 0.0;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			if (dCell.totalMyDamage > 0.0 || dCell.enemyDamage > 0.0)
			{
				//double pts = group.health / dCell.enemyDamage - dCell.enemyHealth / dCell.totalMyDamage;
				//double pts = (dCell.totalMyDamage - dCell.enemyDamage) / (group.size)/10;
				double alpha = 0.3;
				double alphaM1 = 0.7;
				double pts = (group.health * alphaM1 + dCell.enemyHealth * alpha) / (dCell.enemyHealth*0.01 + dCell.enemyDamage) 
				- (dCell.enemyHealth * alphaM1 + group.health * alpha) / (group.health * 0.01 + dCell.totalMyDamage);
				
				P p = P((x + 0.5) * DISTR_MAT_CELL_SIZE, (y + 0.5) * DISTR_MAT_CELL_SIZE);
				
				double dist2 = p.dist2(from);
				if (pts > 0.0)
				{
					double pp = 1.0/(1.0 + dist2);
					res += pts * pp;
				}
				else
				{
					double dangerRad = 150.0;
					double pn = (1.0 - std::min(1.0, dist2/(dangerRad*dangerRad)));
					res += pts * pn;
				}
				
				//res += pts / p.dist2(from);
			}
			
			f2hDmgRes += dCell.f2hDmg;
		}
	}
	
	if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		double L = 1.5 * WIDTH;
		if (f2hDmgRes > 0.0 && res < 0.0)
		{
			if (fivG && fivG->size > 10)
			{
				L = fivG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::IFV];
		
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 300.0)
			{
				res -= (l - 300.0);
			}
		}
	}
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::HELICOPTER);
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 400.0)
			{
				res -= (l - 400.0);
			}
		}
	}*/
	
	const double keepBorderDist = 40.0;
	double borderPen = std::max(
		std::max(
			std::max(keepBorderDist - from.x, 0.0),
			std::max(keepBorderDist - from.y, 0.0)
		),
		std::max(
			std::max(keepBorderDist - (WIDTH - from.x), 0.0),
			std::max(keepBorderDist - (HEIGHT - from.y), 0.0)
		)
	);
	
	res -= borderPen*10;
	
	return res;
}


ShrinkResult Strat::findShrink(Group &group)
{
	ShrinkResult result;
	
	Simulator sim;
	sim.tick = tick;
	std::copy(cells, cells + CELLS_X * CELLS_Y, sim.cells);
	
	BBox gbox = group.bbox;
	gbox.expand(2.0);
	for (const MyUnit &u : units)
	{
		if (gbox.inside(u.pos))
		{
			sim.units.push_back(u);
			MyUnit &tu = *sim.units.rbegin();
			tu.selected = group.check(tu);
			tu.vel = P(0, 0);
		}
	}
	sim.groups.push_back(group);
	
	double bestArea = WIDTH * HEIGHT;
	for (double x = -1; x <= 1; ++x)
	{
		for (double y = -1; y <= 1; ++y)
		{
			Simulator sim2 = sim;
			MyMove myMove;
			myMove.action = MyActionType::SCALE;
			myMove.p = group.center + P(x, y) * 10.0;
			myMove.factor = 0.2;
			sim2.registerMove(myMove, 0);
			
			int i = 0;
			for (; i < 20; ++i)
			{
				sim2.tick++;
				sim2.resetAxisSorts();
				sim2.applyMoves();
				int cnt = sim2.moveUnits();
				if (!cnt)
					break;
			}
			sim2.updateStats();
			
			double area = sim2.groups.rbegin()->bbox.area();
			if (area < bestArea)
			{
				bestArea = area;
				result.shrinkPoint = myMove.p;
				result.ticks = i;
				result.endBBox = sim2.groups.rbegin()->bbox;
			}
		}
	}
	
	return result;
}
}

namespace StratV14 {
	double groupPriority(const Group &g) {
	double res = 0.0;
	
	if (g.shrinkAfterNuke)
		res += 1100.0;
	else if (g.actionStarted)
		res += 1000.0;
	
	res -= g.lastUpdateTick;
	
	return res;
}

void DistributionMatrix::clear()
{
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		Cell &cell = cells[i];
		for (int j = 0; j < 5; ++j)
			cell.count[j] = 0;
		for (int j = 0; j < 5; ++j)
			cell.health[j] = 0;
	}
}

void DistributionMatrix::initialize(const Simulator &sim)
{
	clear();
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : sim.units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / DISTR_MAT_CELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / DISTR_MAT_CELL_SIZE;
				
				Cell &cell = getCell(x, y);
				int type = (int) u.type;
				cell.count[type] += 1.0 / K;
				cell.health[type] += u.durability / K;
			}
		}
	}
}

void DistributionMatrix::blur(DistributionMatrix &oth) const
{
	oth.clear();
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			Cell &resCell = oth.getCell(x, y);
			int cnt = 0;
			for (int yy = -1; yy <= 1; ++yy)
			{
				for (int xx = -1; xx <= 1; ++xx)
				{
					int x2 = x + xx;
					int y2 = y + yy;
					if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
					{
						++cnt;
						const Cell &cell = getCell(x2, y2);
						for (int i = 0; i < 5; ++i)
							resCell.count[i] += cell.count[i];
						for (int i = 0; i < 5; ++i)
							resCell.health[i] += cell.health[i];
					}
				}
			}
			
			const Cell &cell = getCell(x, y);
			for (int i = 0; i < 5; ++i)
			{
				if (!cell.count[i])
				{
					resCell.count[i] = 0;
					resCell.health[i] = 0;
				}
			}
			
			/*for (int i = 0; i < 5; ++i)
				resCell.count[i] += (resCell.count[i] + cnt - 1) / cnt;
			for (int i = 0; i < 5; ++i)
				resCell.health[i] /= cnt;*/
		}
	}
}

Strat::Strat::Strat()
{
	Group g;
	g.actionStarted = false;
	g.lastUpdateTick = 0;
	g.lastShrinkTick = 0;
	
	for (int i = 0; i < 1; ++i)
	{
		g.unitType = UnitType::HELICOPTER;
		//g.miniGroupInd = i;
		groups.push_back(g);
	}
	g.miniGroupInd = 0;
	
	g.unitType = UnitType::IFV;
	groups.push_back(g);
	
	g.unitType = UnitType::FIGHTER;
	groups.push_back(g);
	
	g.unitType = UnitType::TANK;
	groups.push_back(g);
	
	g.unitType = UnitType::ARV;
	groups.push_back(g);
}


void Strat::calcMicroShift(Group &group, P &shift)
{
	if (group.unitType != UnitType::TANK && group.unitType != UnitType::FIGHTER)
		return;
	
	BBox bbox = group.bbox;
	bbox.p1 += shift - P(35, 35);
	bbox.p2 += shift + P(35, 35);
	
	int finalScore = -100000;
	int currentScore = -100000;
	double ticks = 20.0;
	
	//if (group.unitType == UnitType::TANK)
	{
		std::vector<P> enemies[(int) UnitType::COUNT];
		std::vector<P> myUnits;
		
		for (const MyUnit &u : units)
		{
			if (u.side > 0 && (u.type == UnitType::TANK || u.type == UnitType::HELICOPTER || u.type == UnitType::IFV) && bbox.inside(u.pos))
			{
				enemies[(int) u.type].push_back(u.pos + u.vel * ticks);
			}
			else if (group.check(u))
			{
				myUnits.push_back(u.pos);
			}
		}
		
		double L = 8.0;
		P newShift = shift;
		for (int k = 0; k < 4; ++k)
		{
			P foundShift = newShift;
			int foundScore = -100000;
			for (int i = 0; i < 9; ++i)
			{
				P testShift = newShift + P(i / 3 - 1, i % 3 - 1) * L;
				int score = 0;
				bool isCurShift = shift.dist2(testShift) < 0.1;
				
				if (!isCurShift || currentScore == -100000)
				{
					const std::vector<MicroShiftValues> &pos = microShiftMatrix.pos[(int) group.unitType];
					if (!pos.empty())
					{
						for (const P &myP : myUnits)
						{
							P myPos = myP + testShift;
							bool found = false;
							for (const MicroShiftValues &vals : pos)
							{
								if (!found)
								{
									for (const P &enP : enemies[(int) vals.unitType])
									{
										if (myPos.dist2(enP) < vals.dist2)
										{
											score += vals.val;
											found = true;
											break;
										}
									}
								}
							}
						}
					}
					
					// !! decrease SCORE
					int posScore = score;
					score /= 2;
					
					const std::vector<MicroShiftValues> &neg = microShiftMatrix.neg[(int) group.unitType];
					if (!neg.empty())
					{
						for (const MicroShiftValues &vals : neg)
						{
							for (const P &enP : enemies[(int) vals.unitType])
							{
								for (const P &myP : myUnits)
								{
									P myPos = myP + testShift;
									if (myPos.dist2(enP) < vals.dist2)
									{
										score -= vals.val;
										break;
									}
								}
							}
						}
					}
					
					int negScore = posScore / 2 - score;
					
					if (foundScore < score)
					{
						foundShift = testShift;
						foundScore = score;
					}
					
					if (isCurShift)
						currentScore = score;
				}
			}
			
			L /= 2.0;
			newShift = foundShift;
			
			finalScore = foundScore;
		}
		
		if (currentScore >= finalScore)
			return;
		
		shift = newShift;
	}
}

void Strat::calcNuclearEfficiency()
{
	for (int i = 0; i < MICROCELLS_X * MICROCELLS_Y; i++)
		nuclearEfficiency[i] = 0;
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / MICROCELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / MICROCELL_SIZE;
				
				double efficiency = 100.0 / (10.0 + u.durability) / K;
				
				if (u.type == UnitType::ARV)
					efficiency /= 10.0;
				
				if (u.type == UnitType::FIGHTER || u.type == UnitType::HELICOPTER)
					efficiency /= 1.5;
				
				nuclearEfficiency[y * MICROCELLS_X + x] += efficiency;
			}
		}
	}
}

MyMove Strat::calcNextMove()
{
	MyMove result;
	result.action = MyActionType::NONE;
	
	int moves = getAvailableActions(12);
	if (moves < 1)
		return result;
	
	if (players[1].remainingNuclearStrikeCooldownTicks < 30)
	{
		if (moves < 3)
			return result;
	}
	
	updateStats();
	
	if (players[1].nextNuclearStrikeTick >= 0)
	{
		bool select = false;
		bool expand = false;
		BBox bbox;
		int evaidingGroups = 0;
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep == 0)
			{
				BBox gbox = g.bbox;
				gbox.expand(40.0);
				if (gbox.inside(players[1].nuclearStrike))
				{
					bbox.add(g.bbox);
					g.nukeEvadeStep = 1;
					select = true;
					++evaidingGroups;
				}
			}
			else if (g.nukeEvadeStep == 1)
			{
				expand = true;
				g.nukeEvadeStep = 2;
				++evaidingGroups;
			}
			else
			{
				++evaidingGroups;
			}
		}
		
		if (select)
		{
			result.action = MyActionType::CLEAR_AND_SELECT;
			result.p1 = bbox.p1;
			result.p2 = bbox.p2;
			
			return result;
		}
		else if (expand)
		{
			result.action = MyActionType::SCALE;
			result.p = players[1].nuclearStrike;
			result.factor = 9.0;
			return result;
		}
		
		if (evaidingGroups && moves < 3)
			return result;
	}
	else
	{
		for (Group &g : groups)
		{
			if (tick - g.lastShrinkTick > 30)
				g.shrinkAfterNuke = false;
			if (g.nukeEvadeStep > 0)
				g.shrinkAfterNuke = true;
			g.nukeEvadeStep = 0;
		}
	}
	
	/*if (tick > 200) {
		int moves = getAvailableActions(4, 10);
		if (moves < 1) // throttle
			return result;
	}*/
	
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType == UnitType::NONE)
		{
			result.action = MyActionType::SETUP_VEHICLE_PRODUCTION;
			result.facilityId = b.id;
			
			if (buildingCaptured % 2 == 0)
				result.unitType = UnitType::FIGHTER;
			else
				result.unitType = UnitType::IFV;
			
			++buildingCaptured;
			return result;
		}
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE && b.unitCount > 40 && b.productionProgress < 10 || b.createGroupStep > 0)
		{
			//LOG("MAKE GROUP");
			if (b.createGroupStep == 0 || b.createGroupStep == 1)
			{
				result.action = MyActionType::CLEAR_AND_SELECT;
				result.p1 = b.pos - P(32, 32);
				result.p2 = b.pos + P(32, 32);
				result.unitType = b.unitType;
				b.createGroupStep = 2;
				return result;
			}
			
			if (b.createGroupStep == 2)
			{
				// check if selected
				bool anySelected = false;
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.groups.count() == 0)
					{
						if (p.pos.x > b.pos.x - 32 && p.pos.x < b.pos.x + 32 && p.pos.y > b.pos.y - 32 && p.pos.y < b.pos.y + 32)
						{
							if (p.selected)
							{
								anySelected = true;
								break;
							}
						}
					}
				}
				
				if (anySelected)
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					b.createGroupStep = 3;
					
					Group newGroup;
					newGroup.unitType = b.unitType;
					newGroup.group = result.group;
					groups.push_back(newGroup);
					
					b.createGroupStep = 0;
					return result;
				}
				else
				{
					b.createGroupStep = 1;
				}
			}
		}
	}
	
		
	if (!initialGroupsGerationDone && !buildings.empty())
	{
		for (Group &g : groups)
		{
			/*if (g.unitType == UnitType::HELICOPTER && !g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					P allCenter = P(0, 0);
					int count = 0;
					for (const MyUnit &u : units)
					{
						if (u.side == 0 && u.type == g.unitType)
						{
							allCenter += u.pos;
							++count;
						}
					}
					
					allCenter /= count;
					
					if (g.miniGroupInd == 0)
					{
						result.p1 = P(0, 0);
						result.p2 = allCenter;
					}
					else if (g.miniGroupInd == 1)
					{
						result.p1 = P(allCenter.x, 0);
						result.p2 = P(WIDTH, allCenter.y);
					}
					else if (g.miniGroupInd == 2)
					{
						result.p1 = P(0, allCenter.y);
						result.p2 = P(allCenter.x, HEIGHT);
					}
					else if (g.miniGroupInd == 3)
					{
						result.p1 = allCenter;
						result.p2 = P(WIDTH, HEIGHT);
					}
					
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}*/
			
			if (!g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else if (g.enumGroupBuildStep == 1)
				{
					result.action = MyActionType::ASSIGN;
					g.enumGroupBuildStep++;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}
		}
		
		initialGroupsGerationDone = true;
		LOG("DONE");
	}
	
	bool angryMode = angryModeTill > tick;

	
	DistributionMatrix matr;
	matr.initialize(*this);
	matr.blur(distributionMatrix);
	
	/*if (tick > 532)
	{
		std::cout.width(2);
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &resCell = distributionMatrix.getCell(x, y);
				std::cout << " " << resCell.count[1];
			}
			std::cout << std::endl;
		}
		std::cout << std::endl;
	}*/
	
	if (players[0].remainingNuclearStrikeCooldownTicks == 0)
	{
		resetCells();
		calcNuclearEfficiency();
		
		auto getEnemySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0.0;
			
			return nuclearEfficiency[y * MICROCELLS_X + x];
		};
		
		auto getMySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0;
			
			return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
				+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
				+ (int) getMicrocell(x, y, 0, UnitType::ARV).size() / 2;
		};
		
		P bestTarget;
		int bestScore = 0;
		
		for (int y = 0; y < MICROCELLS_Y; ++y) {
			for (int x = 0; x < MICROCELLS_X; ++x) {
				double enemyN = getEnemySize(x, y);
				if (enemyN > 10)
				{
					int totalMyCount = 0;
					int myCenterCount = 0;
					for (int xx = -1; xx <= 1; ++xx)
					{
						for (int yy = -1; yy <= 1; ++yy)
						{
							totalMyCount += getMySize(x + xx, y + yy);
							if (xx == 0 && yy == 0)
								myCenterCount = totalMyCount;
						}
					}
					
					if (totalMyCount < 20 && myCenterCount == 0)
					{
						int totalMyExtCount = totalMyCount;
						if (totalMyExtCount == 0)
						{
							for (int xx = -2; xx <= 2; ++xx)
							{
								for (int yy = -2; yy <= 2; ++yy)
								{
									if (xx < -1 || xx > 1 || yy < -1 || yy > 1)
										totalMyExtCount += getMySize(x + xx, y + yy);
								}
							}
						}
						
						if (totalMyExtCount > 0)
						{
							double totalEnemyCount =
								enemyN +
								getEnemySize(x - 1, y)/2 +
								getEnemySize(x + 1, y)/2 + 
								getEnemySize(x, y - 1)/2 + 
								getEnemySize(x, y + 1)/2;
							
							if (bestScore < totalEnemyCount)
							{
								bestScore = totalEnemyCount;
								bestTarget = P(x + 0.5, y + 0.5) * MICROCELL_SIZE;
							}
						}
					}
				}
			}
		}
		
		if (bestScore > 0)
		{
			// Correct point
			P correctedBestTarget = bestTarget;
			
			std::vector<P> myVehicles;
			std::vector<P> enemyVehicles;
			int myInnerN = 0;
			int enemyInnerN = 0;
			for (const MyUnit &p : units)
			{
				double d2 = p.pos.dist2(bestTarget);
				if (d2 < (75.0*75.0))
				{
					if (d2 < (38.0*38.0))
					{
						if (p.side == 0)
							myInnerN++;
						else
							enemyInnerN++;
					}
					else
					{
						if (p.side == 0)
							myVehicles.push_back(p.pos);
						else
							enemyVehicles.push_back(p.pos);
					}
				}
			}
			
			int foundScore = -100000;
			for (int xx = -2; xx <= 2; ++xx)
			{
				for (int yy = -2; yy <= 2; ++yy)
				{
					P newTarget = bestTarget + P(xx, yy) * 6.0;
					
					int myNum = myInnerN*3;
					int myExtNum = 0;
					
					for (const P &p : myVehicles)
					{
						double d2 = p.dist2(newTarget);
						if (d2 < (50.0*50.0))
						{
							myNum++;
						}
						else if (d2 > (55.0*55.0) && d2 < (70.0*70.0))
						{
							myExtNum++;
						}
					}
					
					if (myExtNum > 0)
					{
						int enNum = enemyInnerN*3;
						for (const P &p : enemyVehicles)
						{
							double d2 = p.dist2(newTarget);
							if (d2 < (50.0*50.0))
							{
								enNum++;
							}
						}
						
						int score = enNum - myNum * 4;
						if (score > foundScore)
						{
							foundScore = score;
							correctedBestTarget = newTarget;
						}
					}
				}
			}
			
			if (foundScore > -100000)
			{
				auto getMySize = [this](int x, int y) {
					if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
						return 0;
					
					return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
						+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
						+ (int) getMicrocell(x, y, 0, UnitType::ARV).size();
				};
				
				int pts = 0;
				long bestId = -1;
				P vehPos;
				
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.durability > 70)
					{
						double dist2 = p.pos.dist2(correctedBestTarget);
						if (dist2 > 55*55 && dist2 < 70*70)
						{
							bool outOfRange = false;
							
							if (p.vel.len2() > 0.01)
							{
								MyUnit u = p;
								for (int i = 0; i < 30; ++i)
								{
									double visRange = getVisionRange(u) - unitVel(p.type) * 10;
									if (u.pos.dist2(correctedBestTarget) > sqr(visRange))
									{
										outOfRange = true;
										break;
									}
									u.pos += u.vel;
								}
							}
							
							if (outOfRange)
								continue;
							
							int xx = p.pos.x / MICROCELL_SIZE;
							int yy = p.pos.y / MICROCELL_SIZE;
							
							//int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 60.0);
							if (pts < newPts)
							{
								pts = newPts;
								bestId = p.id;
								vehPos = p.pos;
							}
						}
					}
				}
				
				if (bestId >= 0)
				{
					result.action = MyActionType::TACTICAL_NUCLEAR_STRIKE;
					result.vehicleId = bestId;
					result.p = correctedBestTarget;
					return result;
				}
			}
		}
	}
	
	groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group &g){return g.size == 0;}), groups.end());
	
	if (result.action == MyActionType::NONE && !groups.empty())
	{
		debugAttractionPoints.clear();
		
		std::sort(groups.begin(), groups.end(), [](const Group &g1, const Group &g2){
			double p1 = groupPriority(g1);
			double p2 = groupPriority(g2);
			return p2 < p1;
		});
		
		for (Group &group : groups)
		{
			if (tick - group.lastUpdateTick < 10 || group.nukeEvadeStep > 0)
				continue;
			
			if (group.shrinkActive)
			{
				if (tick - group.lastShrinkTick > 40 || !anyMoved(group))
				{
					group.shrinkActive = false;
				}
				else
				{
					continue;
				}
			}
			
			bool limitSpeed = false;
			if (nukeVehicleInd >= 0)
			{
				const MyUnit &u = units[nukeVehicleInd];
				if (group.check(u))
					limitSpeed = true;
			}
			
			calcDangerDistCells(group);
			
			P center = group.center;
			const BBox &bbox = group.bbox;
			int groupSize = group.size;
			double area = bbox.area();
			bool shrinkRequired = (area > groupSize * 40.0 || group.shrinkAfterNuke) && (((tick - group.lastUpdateTick) > 60 || group.shrinkAfterNuke) && (tick - group.lastShrinkTick) > 300);
			P shrinkPoint;
			if (shrinkRequired)
			{
				ShrinkResult shRes = findShrink(group);
				if (shRes.ticks > 7)
				{
					shrinkPoint = shRes.shrinkPoint;
				}
				else
				{
					shrinkRequired = false;
				}
				//LOG("SHRINK RES " << shRes.ticks << " " << shRes.endBBox << " P " << shRes.shrinkPoint);
			}
			//bool shrinkRequired = false;
			
			const double border = 20.0;
			P c = center;
			double R = 20 + unitVel(group.unitType) * 40;
			/*if (group.unitType == UnitType::ARV)
				R = 200.0;*/
			double ticks = R / unitVel(group.unitType);
			double curVal = attractionPoint(center, group, ticks, angryMode);
			P tp = center;
			bool found = false;
			bool canMoveFlag = false;
			
			for (int k = 0; k < 3.0; ++k)
			{
				ticks = R / unitVel(group.unitType);
				int di = -1;
				
				for (int i = 0; i < 20; ++i)
				{
					P p = c + P(PI * 2.0 / 20.0 * i) * R;
					if (p.x > border && p.y > border && p.x < (WIDTH - border) && p.y < (HEIGHT - border))
					{
						if (canMove(p - center, group))
						{
							canMoveFlag = true;
							double val = attractionPoint(p, group, ticks, angryMode);
							
							DebugAttractionPointsInfo debugInfo;
							debugInfo.point = c;
							debugInfo.dir = p - c;
							debugInfo.val = val;
							debugAttractionPoints.push_back(debugInfo);
							
							if (val > curVal)
							{
								curVal = val;
								di = i;
								tp = p;
								found = true;
							}
						}
					}
				}
				
				if (di >= 0)
				{
					c = tp;
				}
				
				R /= 1.5;
			}
			
			bool moveAway = false;
			P dirAway;
			P newShift;
			if (!canMoveFlag)
			{
				dirAway = P(0, 0);
				for (Group &othGroup : groups)
				{
					if (&group != &othGroup && isGroundUnit(group.unitType) == isGroundUnit(othGroup.unitType))
					{
						P dp = group.center - othGroup.center;
						double clen = dp.len();
						
						if (clen < 150.0)
							dirAway += dp / clen;
					}
				}
				
				dirAway *= 10;
				dirAway += P(tick % 7 - 3, tick % 9 - 5);
				moveAway = true;
			}
			else
			{
				newShift = P(0, 0);
				if (found)
					newShift = tp - center;
				
				if (newShift.len() < 15.0)
				{
					calcMicroShift(group, newShift);
				}
			}
			
			found = newShift.len2() > 0.1;
			
			if (found || shrinkRequired || moveAway)
			{
				if (!isSelected(group))
				{
					result = select(group);
					group.actionStarted = true;
				}
				else
				{
					if (shrinkRequired)
					{
						result.action = MyActionType::SCALE;
						result.factor = 0.2;
						result.p = shrinkPoint;
						group.lastShrinkTick = tick;
						group.shrinkActive = true;
					}
					else if (found)
					{
						result.action = MyActionType::MOVE;
						result.p = newShift;
						
						if (limitSpeed)
							result.maxSpeed = unitVel(group.unitType) * 0.6;
						
						/*if (!limitSpeed)
						{
							limitSpeed = !anyEnemiesNearbyByDangerDistr(group);
							if (limitSpeed)
								result.maxSpeed = unitVel(group.unitType) * 0.8;
						}*/
						
						/*if (group.unitType == UnitType::FIGHTER)
							result.maxSpeed = 1.0;*/
					}
					else
					{
						result.action = MyActionType::MOVE;
						result.p = dirAway;
						//std::cout << "AWAY " << dirAway.x << " " << dirAway.y << std::endl;
					}
					
					group.lastUpdateTick = tick;
					group.actionStarted = false;
				}
				
				break;
			}
		}
	}
	
	return result;
}

bool Strat::anyEnemiesNearbyByDangerDistr(const Group &group)
{
	int x = group.center.x / DISTR_MAT_CELL_SIZE;
	int y = group.center.y / DISTR_MAT_CELL_SIZE;
	for (int yy = -7; yy <= 7; ++yy)
	{
		for (int xx = -7; xx <= 7; ++xx)
		{
			if (xx * xx + yy * yy <= 50)
			{
				int x2 = x + xx;
				int y2 = y + yy;
				if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
				{
					DangerDistCell &dCell = dangerDistCells[y2 * DISTR_MAT_CELLS_X + x2];
					if (dCell.totalEnemyDamage > 0.0 || dCell.totalMyDamage > 0.0)
					{
						return true;
					}
				}
			}
		}
	}
	
	return false;
}

void Strat::calcDangerDistCells(const Group &group)
{
	dngGr = &group;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
			double totalMyDamage = 0;
			
			for (int myType = 0; myType < 5; ++myType)
			{
				double typeDamage = 0;
				if (group.sizeByTypes[myType])
				{
					for (int enemyType = 0; enemyType < 5; ++enemyType)
					{
						if (cell.count[enemyType])
						{
							double dmg = getDamage((UnitType) myType, (UnitType) enemyType);
							typeDamage = std::max(typeDamage, std::min(dmg * group.sizeByTypes[myType], 120.0 * cell.count[enemyType]));
						}
					}
				}
				totalMyDamage += typeDamage;
			}
			
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			double f2hDmg = 0.0;
			dCell.totalEnemyDamage = 0.0;
			dCell.totalEnemyHealth = 0.0;
			for (int enemyType = 0; enemyType < 5; ++enemyType)
			{
				dCell.enemyDamage[enemyType] = 0.0;
				dCell.enemyHealth[enemyType] = 0.0;
				
				if (cell.count[enemyType])
				{
					dCell.enemyHealth[enemyType] += cell.health[enemyType];
					for (int myType = 0; myType < 5; ++myType)
					{
						if (group.sizeByTypes[myType])
						{
							double dmg = getDamage((UnitType) enemyType, (UnitType) myType);
							dCell.enemyDamage[enemyType] = std::min(dmg * cell.count[enemyType], 120.0 * group.sizeByTypes[myType]);
							
							if (enemyType == (int) UnitType::FIGHTER && myType == (int) UnitType::HELICOPTER)
								f2hDmg = dCell.enemyDamage[enemyType];
						}
					}
				}
				
				dCell.enemyDamage[enemyType] *= 1.4;
				dCell.totalEnemyDamage += dCell.enemyDamage[enemyType];
				dCell.totalEnemyHealth += dCell.enemyHealth[enemyType];
			}
			
			dCell.totalMyDamage = totalMyDamage;
			dCell.f2hDmg = f2hDmg;
		}
	}
}

struct DangerDist {
	double dists[25];
	
	DangerDist() {
		dist(UnitType::ARV, UnitType::ARV) = 150.0;
		dist(UnitType::ARV, UnitType::FIGHTER) = 150.0;
		dist(UnitType::ARV, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::ARV, UnitType::IFV) = 150.0;
		dist(UnitType::ARV, UnitType::TANK) = 150.0;
		
		dist(UnitType::FIGHTER, UnitType::ARV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::IFV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::HELICOPTER, UnitType::ARV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::IFV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::IFV, UnitType::ARV) = 150.0;
		dist(UnitType::IFV, UnitType::FIGHTER) = 100.0;
		dist(UnitType::IFV, UnitType::HELICOPTER) = 100.0;
		dist(UnitType::IFV, UnitType::IFV) = 150.0;
		dist(UnitType::IFV, UnitType::TANK) = 150.0;
		
		dist(UnitType::TANK, UnitType::ARV) = 150.0;
		dist(UnitType::TANK, UnitType::FIGHTER) = 1.0;
		dist(UnitType::TANK, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::TANK, UnitType::IFV) = 150.0;
		dist(UnitType::TANK, UnitType::TANK) = 150.0;
		
		for (int i = 0; i < 25; ++i)
			dists[i] = sqr(dists[i]);
	}
	
	double &dist(UnitType from, UnitType to) {
		return dists[(int) from * 5 + (int) to];
	}
} DANGER_DISTS;

double Strat::attractionPoint(const P from, const Group &group, double ticks, bool angryMode)
{
	double res = 0.0;
	double f2hDmgRes = 0.0;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			if (dCell.totalMyDamage > 0.0 || dCell.totalEnemyDamage > 0.0)
			{
				double alpha = 0.3;
				double alphaM1 = 0.7;
				
				P p = P((x + 0.5) * DISTR_MAT_CELL_SIZE, (y + 0.5) * DISTR_MAT_CELL_SIZE);
				
				double dist2 = p.dist2(from);
				
				double pts = (group.health * alphaM1 + dCell.totalEnemyHealth * alpha) / (dCell.totalEnemyHealth*0.01 + dCell.totalEnemyDamage) 
				- (dCell.totalEnemyHealth * alphaM1 + group.health * alpha) / (group.health * 0.01 + dCell.totalMyDamage);
				
				if (pts > 0.0)
				{
					double pp = 1.0/(1.0 + dist2);
					res += pts * pp;
				}
				else
				{
					/*for (int mt = 0; mt < 5; ++mt)
					{
						if (group.healthByTypes[mt])
						{
							for (int et = 0; et < 5; ++et)
							{
								if (dCell.enemyHealth[et])
								{
									double rad2 = DANGER_DISTS.dist((UnitType) et, (UnitType) mt);
									//double rad2 = 150*150;
									if (rad2 > dist2)
									{
										double fraction = group.healthByTypes[mt] / group.health * dCell.enemyHealth[et] / dCell.totalEnemyHealth;
										double pn = (1.0 - std::min(1.0, dist2/rad2));
										res += pts * pn * fraction;
									}
								}
							}
						}
					}*/
					
					double pn = (1.0 - std::min(1.0, dist2/sqr(150)));
					res += pts * pn;
				}
				
				//res += pts / p.dist2(from);
			}
			
			f2hDmgRes += dCell.f2hDmg;
		}
	}
	
	if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		double L = 1.5 * WIDTH;
		if (f2hDmgRes > 0.0 && res < 0.0)
		{
			if (fivG && fivG->size > 10)
			{
				L = fivG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::IFV];
		
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 300.0)
			{
				res -= (l - 300.0);
			}
		}
	}
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::HELICOPTER);
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 400.0)
			{
				res -= (l - 400.0);
			}
		}
	}*/
	
	if (isGroundUnit(group.unitType))
	{
		for (const Building &b : buildings)
		{
			if (b.side == -1)
			{
				double d = from.dist(b.pos);
				res += group.health/(20 + d)*0.1;
			}
		}
	}
	
	const double keepBorderDist = 40.0;
	double borderPen = std::max(
		std::max(
			std::max(keepBorderDist - from.x, 0.0),
			std::max(keepBorderDist - from.y, 0.0)
		),
		std::max(
			std::max(keepBorderDist - (WIDTH - from.x), 0.0),
			std::max(keepBorderDist - (HEIGHT - from.y), 0.0)
		)
	);
	
	res -= borderPen*10;
	
	return res;
}


ShrinkResult Strat::findShrink(Group &group)
{
	ShrinkResult result;
	
	Simulator sim;
	sim.tick = tick;
	std::copy(cells, cells + CELLS_X * CELLS_Y, sim.cells);
	
	BBox gbox = group.bbox;
	gbox.expand(2.0);
	for (const MyUnit &u : units)
	{
		if (gbox.inside(u.pos))
		{
			sim.units.push_back(u);
			MyUnit &tu = *sim.units.rbegin();
			tu.selected = group.check(tu);
			tu.vel = P(0, 0);
		}
	}
	sim.groups.push_back(group);
	
	double bestArea = WIDTH * HEIGHT;
	for (double x = -1; x <= 1; ++x)
	{
		for (double y = -1; y <= 1; ++y)
		{
			Simulator sim2 = sim;
			MyMove myMove;
			myMove.action = MyActionType::SCALE;
			myMove.p = group.center + P(x, y) * 10.0;
			myMove.factor = 0.2;
			sim2.registerMove(myMove, 0);
			
			int i = 0;
			for (; i < 20; ++i)
			{
				sim2.tick++;
				sim2.resetAxisSorts();
				sim2.applyMoves();
				int cnt = sim2.moveUnits();
				if (!cnt)
					break;
			}
			sim2.updateStats();
			
			double area = sim2.groups.rbegin()->bbox.area();
			if (area < bestArea)
			{
				bestArea = area;
				result.shrinkPoint = myMove.p;
				result.ticks = i;
				result.endBBox = sim2.groups.rbegin()->bbox;
			}
		}
	}
	
	return result;
}
}


namespace StratV15 {
	double groupPriority(const Group &g) {
	double res = 0.0;
	
	if (g.shrinkAfterNuke)
		res += 1100.0;
	else if (g.actionStarted)
		res += 1000.0;
	
	res -= g.lastUpdateTick;
	
	return res;
}

void DistributionMatrix::clear()
{
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		Cell &cell = cells[i];
		for (int j = 0; j < 5; ++j)
			cell.count[j] = 0;
		for (int j = 0; j < 5; ++j)
			cell.health[j] = 0;
	}
}

void DistributionMatrix::initialize(const Simulator &sim)
{
	clear();
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : sim.units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / DISTR_MAT_CELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / DISTR_MAT_CELL_SIZE;
				
				Cell &cell = getCell(x, y);
				int type = (int) u.type;
				cell.count[type] += 1.0 / K;
				cell.health[type] += u.durability / K;
			}
		}
	}
}

void DistributionMatrix::blur(DistributionMatrix &oth) const
{
	oth.clear();
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			Cell &resCell = oth.getCell(x, y);
			int cnt = 0;
			for (int yy = -1; yy <= 1; ++yy)
			{
				for (int xx = -1; xx <= 1; ++xx)
				{
					int x2 = x + xx;
					int y2 = y + yy;
					if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
					{
						++cnt;
						const Cell &cell = getCell(x2, y2);
						for (int i = 0; i < 5; ++i)
							resCell.count[i] += cell.count[i];
						for (int i = 0; i < 5; ++i)
							resCell.health[i] += cell.health[i];
					}
				}
			}
			
			const Cell &cell = getCell(x, y);
			for (int i = 0; i < 5; ++i)
			{
				if (!cell.count[i])
				{
					resCell.count[i] = 0;
					resCell.health[i] = 0;
				}
			}
			
			/*for (int i = 0; i < 5; ++i)
				resCell.count[i] += (resCell.count[i] + cnt - 1) / cnt;
			for (int i = 0; i < 5; ++i)
				resCell.health[i] /= cnt;*/
		}
	}
}

Strat::Strat::Strat()
{
	Group g;
	g.actionStarted = false;
	g.lastUpdateTick = 0;
	g.lastShrinkTick = 0;
	
	for (int i = 0; i < 1; ++i)
	{
		g.unitType = UnitType::HELICOPTER;
		//g.miniGroupInd = i;
		g.internalId = internalGroupSeq++;
		groups.push_back(g);
	}
	g.miniGroupInd = 0;
	
	g.unitType = UnitType::IFV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::FIGHTER;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::TANK;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::ARV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
}


void Strat::calcMicroShift(Group &group, P &shift)
{
	if (group.unitType != UnitType::TANK && group.unitType != UnitType::FIGHTER)
		return;
	
	BBox bbox = group.bbox;
	bbox.p1 += shift - P(35, 35);
	bbox.p2 += shift + P(35, 35);
	
	int finalScore = -100000;
	int currentScore = -100000;
	double ticks = 20.0;
	
	//if (group.unitType == UnitType::TANK)
	{
		std::vector<P> enemies[(int) UnitType::COUNT];
		std::vector<P> myUnits;
		
		for (const MyUnit &u : units)
		{
			if (u.side > 0 && (u.type == UnitType::TANK || u.type == UnitType::HELICOPTER || u.type == UnitType::IFV) && bbox.inside(u.pos))
			{
				enemies[(int) u.type].push_back(u.pos + u.vel * ticks);
			}
			else if (group.check(u))
			{
				myUnits.push_back(u.pos);
			}
		}
		
		double L = 8.0;
		P newShift = shift;
		for (int k = 0; k < 4; ++k)
		{
			P foundShift = newShift;
			int foundScore = -100000;
			for (int i = 0; i < 9; ++i)
			{
				P testShift = newShift + P(i / 3 - 1, i % 3 - 1) * L;
				int score = 0;
				bool isCurShift = shift.dist2(testShift) < 0.1;
				
				if (!isCurShift || currentScore == -100000)
				{
					const std::vector<MicroShiftValues> &pos = microShiftMatrix.pos[(int) group.unitType];
					if (!pos.empty())
					{
						for (const P &myP : myUnits)
						{
							P myPos = myP + testShift;
							bool found = false;
							for (const MicroShiftValues &vals : pos)
							{
								if (!found)
								{
									for (const P &enP : enemies[(int) vals.unitType])
									{
										if (myPos.dist2(enP) < vals.dist2)
										{
											score += vals.val;
											found = true;
											break;
										}
									}
								}
							}
						}
					}
					
					// !! decrease SCORE
					int posScore = score;
					score /= 2;
					
					const std::vector<MicroShiftValues> &neg = microShiftMatrix.neg[(int) group.unitType];
					if (!neg.empty())
					{
						for (const MicroShiftValues &vals : neg)
						{
							for (const P &enP : enemies[(int) vals.unitType])
							{
								for (const P &myP : myUnits)
								{
									P myPos = myP + testShift;
									if (myPos.dist2(enP) < vals.dist2)
									{
										score -= vals.val;
										break;
									}
								}
							}
						}
					}
					
					int negScore = posScore / 2 - score;
					
					if (foundScore < score)
					{
						foundShift = testShift;
						foundScore = score;
					}
					
					if (isCurShift)
						currentScore = score;
				}
			}
			
			L /= 2.0;
			newShift = foundShift;
			
			finalScore = foundScore;
		}
		
		if (currentScore >= finalScore)
			return;
		
		shift = newShift;
	}
}

void Strat::calcNuclearEfficiency()
{
	for (int i = 0; i < MICROCELLS_X * MICROCELLS_Y; i++)
		nuclearEfficiency[i] = 0;
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / MICROCELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / MICROCELL_SIZE;
				
				double efficiency = 100.0 / (10.0 + u.durability) / K;
				
				if (u.type == UnitType::ARV)
					efficiency /= 10.0;
				
				if (u.type == UnitType::FIGHTER || u.type == UnitType::HELICOPTER)
					efficiency /= 1.5;
				
				nuclearEfficiency[y * MICROCELLS_X + x] += efficiency;
			}
		}
	}
}

MyMove Strat::calcNextMove()
{
	MyMove result;
	result.action = MyActionType::NONE;
	
	int moves = getAvailableActions(12);
	if (moves < 1)
		return result;
	
	if (players[1].remainingNuclearStrikeCooldownTicks < 30)
	{
		if (moves < 3)
			return result;
	}
	
	updateStats();
	
	if (players[1].nextNuclearStrikeTick >= 0)
	{
		bool select = false;
		bool expand = false;
		BBox bbox;
		int evaidingGroups = 0;
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep == 0)
			{
				BBox gbox = g.bbox;
				gbox.expand(40.0);
				if (gbox.inside(players[1].nuclearStrike))
				{
					bbox.add(g.bbox);
					g.nukeEvadeStep = 1;
					select = true;
					++evaidingGroups;
				}
			}
			else if (g.nukeEvadeStep == 1)
			{
				expand = true;
				g.nukeEvadeStep = 2;
				++evaidingGroups;
			}
			else
			{
				++evaidingGroups;
			}
		}
		
		if (select)
		{
			result.action = MyActionType::CLEAR_AND_SELECT;
			result.p1 = bbox.p1;
			result.p2 = bbox.p2;
			
			return result;
		}
		else if (expand)
		{
			result.action = MyActionType::SCALE;
			result.p = players[1].nuclearStrike;
			result.factor = 9.0;
			return result;
		}
		
		if (evaidingGroups && moves < 3)
			return result;
	}
	else
	{
		for (Group &g : groups)
		{
			if (tick - g.lastShrinkTick > 30)
				g.shrinkAfterNuke = false;
			if (g.nukeEvadeStep != 0)
			{
				g.shrinkAfterNuke = true;
				g.nukeEvadeStep = -1;
			}
		}
	}
	
	/*if (tick > 200) {
		int moves = getAvailableActions(4, 10);
		if (moves < 1) // throttle
			return result;
	}*/
	
	for (Building &b : buildings)
	{
		/*if (b.lastChangeUnitCount > b.unitCount)
			b.lastChangeUnitCount = b.unitCount;*/
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && (b.unitType == UnitType::NONE/* || (b.unitCount - b.lastChangeUnitCount) > 11*/))
		{
			//LOG("SVP " << buildingCaptured);
			result.action = MyActionType::SETUP_VEHICLE_PRODUCTION;
			result.facilityId = b.id;
			
			/*if (buildingCaptured % 2 == 0)
				result.unitType = UnitType::FIGHTER;
			else
				result.unitType = UnitType::IFV;*/
			
			result.unitType = UnitType::TANK;
			//result.unitType = (UnitType) (buildingCaptured % 5);
			//b.lastChangeUnitCount = b.unitCount;
			
			++buildingCaptured;
			return result;
		}
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE && b.unitCount > 40 && b.productionProgress < 30 || b.createGroupStep > 0)
		{
			//LOG("MAKE GROUP");
			if (b.createGroupStep == 0 || b.createGroupStep == 1)
			{
				result.action = MyActionType::CLEAR_AND_SELECT;
				result.p1 = b.pos - P(32, 32);
				result.p2 = b.pos + P(32, 32);
				result.unitType = b.unitType;
				b.createGroupStep = 2;
				return result;
			}
			
			if (b.createGroupStep == 2)
			{
				// check if selected
				bool anySelected = false;
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.groups.count() == 0)
					{
						if (p.pos.x > b.pos.x - 32 && p.pos.x < b.pos.x + 32 && p.pos.y > b.pos.y - 32 && p.pos.y < b.pos.y + 32)
						{
							if (p.selected)
							{
								anySelected = true;
								break;
							}
						}
					}
				}
				
				if (anySelected)
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					b.createGroupStep = 3;
					
					Group newGroup;
					//newGroup.unitType = b.unitType;
					newGroup.group = result.group;
					newGroup.internalId = internalGroupSeq++;
					groups.push_back(newGroup);
					
					b.createGroupStep = 0;
					return result;
				}
				else
				{
					b.createGroupStep = 1;
				}
			}
		}
	}
	
		
	if (!initialGroupsGerationDone && !buildings.empty())
	{
		for (Group &g : groups)
		{
			/*if (g.unitType == UnitType::HELICOPTER && !g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					P allCenter = P(0, 0);
					int count = 0;
					for (const MyUnit &u : units)
					{
						if (u.side == 0 && u.type == g.unitType)
						{
							allCenter += u.pos;
							++count;
						}
					}
					
					allCenter /= count;
					
					if (g.miniGroupInd == 0)
					{
						result.p1 = P(0, 0);
						result.p2 = allCenter;
					}
					else if (g.miniGroupInd == 1)
					{
						result.p1 = P(allCenter.x, 0);
						result.p2 = P(WIDTH, allCenter.y);
					}
					else if (g.miniGroupInd == 2)
					{
						result.p1 = P(0, allCenter.y);
						result.p2 = P(allCenter.x, HEIGHT);
					}
					else if (g.miniGroupInd == 3)
					{
						result.p1 = allCenter;
						result.p2 = P(WIDTH, HEIGHT);
					}
					
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}*/
			
			if (!g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else if (g.enumGroupBuildStep == 1)
				{
					result.action = MyActionType::ASSIGN;
					g.enumGroupBuildStep++;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}
		}
		
		initialGroupsGerationDone = true;
		LOG("DONE");
	}
	
	bool angryMode = angryModeTill > tick;

	
	DistributionMatrix matr;
	matr.initialize(*this);
	matr.blur(distributionMatrix);
	
	/*if (tick > 532)
	{
		std::cout.width(2);
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &resCell = distributionMatrix.getCell(x, y);
				std::cout << " " << resCell.count[1];
			}
			std::cout << std::endl;
		}
		std::cout << std::endl;
	}*/
	
	if (players[0].remainingNuclearStrikeCooldownTicks == 0)
	{
		resetCells();
		calcNuclearEfficiency();
		
		auto getEnemySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0.0;
			
			return nuclearEfficiency[y * MICROCELLS_X + x];
		};
		
		auto getMySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0;
			
			return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
				+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
				+ (int) getMicrocell(x, y, 0, UnitType::ARV).size() / 2;
		};
		
		P bestTarget;
		int bestScore = 0;
		
		for (int y = 0; y < MICROCELLS_Y; ++y) {
			for (int x = 0; x < MICROCELLS_X; ++x) {
				double enemyN = getEnemySize(x, y);
				if (enemyN > 10)
				{
					int totalMyCount = 0;
					int myCenterCount = 0;
					for (int xx = -1; xx <= 1; ++xx)
					{
						for (int yy = -1; yy <= 1; ++yy)
						{
							totalMyCount += getMySize(x + xx, y + yy);
							if (xx == 0 && yy == 0)
								myCenterCount = totalMyCount;
						}
					}
					
					if (totalMyCount < 20 && myCenterCount == 0)
					{
						int totalMyExtCount = totalMyCount;
						if (totalMyExtCount == 0)
						{
							for (int xx = -2; xx <= 2; ++xx)
							{
								for (int yy = -2; yy <= 2; ++yy)
								{
									if (xx < -1 || xx > 1 || yy < -1 || yy > 1)
										totalMyExtCount += getMySize(x + xx, y + yy);
								}
							}
						}
						
						if (totalMyExtCount > 0)
						{
							double totalEnemyCount =
								enemyN +
								getEnemySize(x - 1, y)/2 +
								getEnemySize(x + 1, y)/2 + 
								getEnemySize(x, y - 1)/2 + 
								getEnemySize(x, y + 1)/2;
							
							if (bestScore < totalEnemyCount)
							{
								bestScore = totalEnemyCount;
								bestTarget = P(x + 0.5, y + 0.5) * MICROCELL_SIZE;
							}
						}
					}
				}
			}
		}
		
		if (bestScore > 0)
		{
			// Correct point
			P correctedBestTarget = bestTarget;
			
			std::vector<P> myVehicles;
			std::vector<P> enemyVehicles;
			int myInnerN = 0;
			int enemyInnerN = 0;
			for (const MyUnit &p : units)
			{
				double d2 = p.pos.dist2(bestTarget);
				if (d2 < (75.0*75.0))
				{
					if (d2 < (38.0*38.0))
					{
						if (p.side == 0)
							myInnerN++;
						else
							enemyInnerN++;
					}
					else
					{
						if (p.side == 0)
							myVehicles.push_back(p.pos);
						else
							enemyVehicles.push_back(p.pos);
					}
				}
			}
			
			int foundScore = -100000;
			for (int xx = -2; xx <= 2; ++xx)
			{
				for (int yy = -2; yy <= 2; ++yy)
				{
					P newTarget = bestTarget + P(xx, yy) * 6.0;
					
					int myNum = myInnerN*3;
					int myExtNum = 0;
					
					for (const P &p : myVehicles)
					{
						double d2 = p.dist2(newTarget);
						if (d2 < (50.0*50.0))
						{
							myNum++;
						}
						else if (d2 > (55.0*55.0) && d2 < (70.0*70.0))
						{
							myExtNum++;
						}
					}
					
					if (myExtNum > 0)
					{
						int enNum = enemyInnerN*3;
						for (const P &p : enemyVehicles)
						{
							double d2 = p.dist2(newTarget);
							if (d2 < (50.0*50.0))
							{
								enNum++;
							}
						}
						
						int score = enNum - myNum * 4;
						if (score > foundScore)
						{
							foundScore = score;
							correctedBestTarget = newTarget;
						}
					}
				}
			}
			
			if (foundScore > -100000)
			{
				auto getMySize = [this](int x, int y) {
					if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
						return 0;
					
					return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
						+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
						+ (int) getMicrocell(x, y, 0, UnitType::ARV).size();
				};
				
				int pts = 0;
				long bestId = -1;
				P vehPos;
				
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.durability > 70)
					{
						double dist2 = p.pos.dist2(correctedBestTarget);
						if (dist2 > 55*55 && dist2 < 70*70)
						{
							bool outOfRange = false;
							
							if (p.vel.len2() > 0.01)
							{
								MyUnit u = p;
								for (int i = 0; i < 30; ++i)
								{
									double visRange = getVisionRange(u) - unitVel(p.type) * 10;
									if (u.pos.dist2(correctedBestTarget) > sqr(visRange))
									{
										outOfRange = true;
										break;
									}
									u.pos += u.vel;
								}
							}
							
							if (outOfRange)
								continue;
							
							int xx = p.pos.x / MICROCELL_SIZE;
							int yy = p.pos.y / MICROCELL_SIZE;
							
							//int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 60.0);
							if (pts < newPts)
							{
								pts = newPts;
								bestId = p.id;
								vehPos = p.pos;
							}
						}
					}
				}
				
				if (bestId >= 0)
				{
					result.action = MyActionType::TACTICAL_NUCLEAR_STRIKE;
					result.vehicleId = bestId;
					result.p = correctedBestTarget;
					return result;
				}
			}
		}
	}
	
	groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group &g){return g.size == 0;}), groups.end());
	
	if (result.action == MyActionType::NONE && !groups.empty())
	{
		debugAttractionPoints.clear();
		
		std::sort(groups.begin(), groups.end(), [](const Group &g1, const Group &g2){
			double p1 = groupPriority(g1);
			double p2 = groupPriority(g2);
			return p2 < p1;
		});
		
		assignBuildings();
		
		for (Group &group : groups)
		{
			if (tick - group.lastUpdateTick < 10 || group.nukeEvadeStep > 0)
				continue;
			
			if (group.shrinkActive)
			{
				if (tick - group.lastShrinkTick > 40 || !anyMoved(group))
				{
					group.shrinkActive = false;
				}
				else
				{
					continue;
				}
			}
			
			bool limitSpeed = false;
			if (nukeVehicleInd >= 0)
			{
				const MyUnit &u = units[nukeVehicleInd];
				if (group.check(u))
					limitSpeed = true;
			}
			
			calcDangerDistCells(group);
			
			P center = group.center;
			const BBox &bbox = group.bbox;
			int groupSize = group.size;
			double area = bbox.area();
			bool shrinkRequired = (area > groupSize * 40.0 || group.shrinkAfterNuke) && (((tick - group.lastUpdateTick) > 60 || group.shrinkAfterNuke) && (tick - group.lastShrinkTick) > 300);
			P shrinkPoint;
			if (shrinkRequired)
			{
				ShrinkResult shRes = findShrink(group);
				if (shRes.ticks > 7)
				{
					shrinkPoint = shRes.shrinkPoint;
				}
				else
				{
					shrinkRequired = false;
				}
				//LOG("SHRINK RES " << shRes.ticks << " " << shRes.endBBox << " P " << shRes.shrinkPoint);
			}
			//bool shrinkRequired = false;
			
			const double border = 20.0;
			P c = center;
			double R = 20 + unitVel(group.unitType) * 40;
			/*if (group.unitType == UnitType::ARV)
				R = 200.0;*/
			double ticks = R / unitVel(group.unitType);
			double curVal = attractionPoint(center, group, ticks, angryMode);
			P tp = center;
			bool found = false;
			bool canMoveFlag = false;
			
			for (int k = 0; k < 3.0; ++k)
			{
				ticks = R / unitVel(group.unitType);
				int di = -1;
				
				for (int i = 0; i < 20; ++i)
				{
					P p = c + P(PI * 2.0 / 20.0 * i) * R;
					if (p.x > border && p.y > border && p.x < (WIDTH - border) && p.y < (HEIGHT - border))
					{
						if (canMove(p - center, group))
						{
							canMoveFlag = true;
							double val = attractionPoint(p, group, ticks, angryMode);
							
							DebugAttractionPointsInfo debugInfo;
							debugInfo.point = c;
							debugInfo.dir = p - c;
							debugInfo.val = val;
							debugAttractionPoints.push_back(debugInfo);
							
							if (val > curVal)
							{
								curVal = val;
								di = i;
								tp = p;
								found = true;
							}
						}
					}
				}
				
				if (di >= 0)
				{
					c = tp;
				}
				
				R /= 1.5;
			}
			
			bool moveAway = false;
			P dirAway;
			P newShift;
			if (!canMoveFlag)
			{
				dirAway = P(0, 0);
				for (Group &othGroup : groups)
				{
					if (&group != &othGroup && isGroundUnit(group.unitType) == isGroundUnit(othGroup.unitType))
					{
						P dp = group.center - othGroup.center;
						double clen = dp.len();
						
						if (clen < 150.0)
							dirAway += dp / clen;
					}
				}
				
				dirAway *= 10;
				dirAway += P(tick % 7 - 3, tick % 9 - 5);
				moveAway = true;
			}
			else
			{
				newShift = P(0, 0);
				if (found)
					newShift = tp - center;
				
				if (newShift.len() < 15.0)
				{
					calcMicroShift(group, newShift);
				}
			}
			
			found = newShift.len2() > 0.1;
			
			if (found || shrinkRequired || moveAway)
			{
				if (!isSelected(group))
				{
					result = select(group);
					group.actionStarted = true;
				}
				else
				{
					if (shrinkRequired)
					{
						result.action = MyActionType::SCALE;
						result.factor = 0.2;
						result.p = shrinkPoint;
						group.lastShrinkTick = tick;
						group.shrinkActive = true;
						group.nukeEvadeStep = 0;
					}
					else if (found)
					{
						result.action = MyActionType::MOVE;
						result.p = newShift;
						
						if (limitSpeed)
							result.maxSpeed = unitVel(group.unitType) * 0.6;
						
						/*if (!limitSpeed)
						{
							limitSpeed = !anyEnemiesNearbyByDangerDistr(group);
							if (limitSpeed)
								result.maxSpeed = unitVel(group.unitType) * 0.8;
						}*/
						
						/*if (group.unitType == UnitType::FIGHTER)
							result.maxSpeed = 1.0;*/
					}
					else
					{
						result.action = MyActionType::MOVE;
						result.p = dirAway;
						//std::cout << "AWAY " << dirAway.x << " " << dirAway.y << std::endl;
					}
					
					group.lastUpdateTick = tick;
					group.actionStarted = false;
				}
				
				break;
			}
		}
	}
	
	return result;
}

void Strat::assignBuildings()
{
	std::set<Building *> pbuildings;
	
	for (Building &b : buildings)
	{
		if (b.side != 0)
		{
			pbuildings.insert(&b);
			b.assignedGroup = 0;
		}
	}
	
	std::set<Group *> pgroups;
	for (Group &g : groups)
	{
		if (isGroundUnit(g.unitType))
		{
			pgroups.insert(&g);
		}
	}
	
	size_t count = std::min(pbuildings.size(), pgroups.size());
	for (int i = 0; i < count; ++i)
	{
		double dist2 = sqr(100000.0);
		std::set<Building *>::iterator b = pbuildings.end();
		std::set<Group *>::iterator g = pgroups.end();
		for (std::set<Building *>::iterator bit = pbuildings.begin(); bit != pbuildings.end(); ++bit)
		{
			for (std::set<Group *>::iterator git = pgroups.begin(); git != pgroups.end(); ++git)
			{
				double d2 = (*bit)->pos.dist2((*git)->center);
				if (d2 < dist2)
				{
					dist2 = d2;
					b = bit;
					g = git;
				}
			}
		}
		
		if (b != pbuildings.end() && g != pgroups.end())
		{
			(*b)->assignedGroup = (*g)->internalId;
			pbuildings.erase(b);
			pgroups.erase(g);
		}
	}
}

bool Strat::anyEnemiesNearbyByDangerDistr(const Group &group)
{
	int x = group.center.x / DISTR_MAT_CELL_SIZE;
	int y = group.center.y / DISTR_MAT_CELL_SIZE;
	for (int yy = -7; yy <= 7; ++yy)
	{
		for (int xx = -7; xx <= 7; ++xx)
		{
			if (xx * xx + yy * yy <= 50)
			{
				int x2 = x + xx;
				int y2 = y + yy;
				if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
				{
					DangerDistCell &dCell = dangerDistCells[y2 * DISTR_MAT_CELLS_X + x2];
					if (dCell.totalEnemyDamage > 0.0 || dCell.totalMyDamage > 0.0)
					{
						return true;
					}
				}
			}
		}
	}
	
	return false;
}

void Strat::calcDangerDistCells(const Group &group)
{
	dngGr = &group;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
			double totalMyDamage = 0;
			
			for (int myType = 0; myType < 5; ++myType)
			{
				double typeDamage = 0;
				if (group.sizeByTypes[myType])
				{
					for (int enemyType = 0; enemyType < 5; ++enemyType)
					{
						if (cell.count[enemyType])
						{
							double dmg = getDamage((UnitType) myType, (UnitType) enemyType);
							typeDamage = std::max(typeDamage, std::min(dmg * group.sizeByTypes[myType], 120.0 * cell.count[enemyType]));
						}
					}
				}
				totalMyDamage += typeDamage;
			}
			
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			double f2hDmg = 0.0;
			dCell.totalEnemyDamage = 0.0;
			dCell.totalEnemyHealth = 0.0;
			for (int enemyType = 0; enemyType < 5; ++enemyType)
			{
				dCell.enemyDamage[enemyType] = 0.0;
				dCell.enemyHealth[enemyType] = 0.0;
				
				if (cell.count[enemyType])
				{
					dCell.enemyHealth[enemyType] += cell.health[enemyType];
					for (int myType = 0; myType < 5; ++myType)
					{
						if (group.sizeByTypes[myType])
						{
							double dmg = getDamage((UnitType) enemyType, (UnitType) myType);
							dCell.enemyDamage[enemyType] = std::min(dmg * cell.count[enemyType], 120.0 * group.sizeByTypes[myType]);
							
							if (enemyType == (int) UnitType::FIGHTER && myType == (int) UnitType::HELICOPTER)
								f2hDmg = dCell.enemyDamage[enemyType];
						}
					}
				}
				
				dCell.enemyDamage[enemyType] *= 1.4;
				dCell.totalEnemyDamage += dCell.enemyDamage[enemyType];
				dCell.totalEnemyHealth += dCell.enemyHealth[enemyType];
			}
			
			dCell.totalMyDamage = totalMyDamage;
			dCell.f2hDmg = f2hDmg;
		}
	}
}

struct DangerDist {
	double dists[25];
	
	DangerDist() {
		dist(UnitType::ARV, UnitType::ARV) = 150.0;
		dist(UnitType::ARV, UnitType::FIGHTER) = 150.0;
		dist(UnitType::ARV, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::ARV, UnitType::IFV) = 150.0;
		dist(UnitType::ARV, UnitType::TANK) = 150.0;
		
		dist(UnitType::FIGHTER, UnitType::ARV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::IFV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::HELICOPTER, UnitType::ARV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::IFV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::IFV, UnitType::ARV) = 150.0;
		dist(UnitType::IFV, UnitType::FIGHTER) = 100.0;
		dist(UnitType::IFV, UnitType::HELICOPTER) = 100.0;
		dist(UnitType::IFV, UnitType::IFV) = 150.0;
		dist(UnitType::IFV, UnitType::TANK) = 150.0;
		
		dist(UnitType::TANK, UnitType::ARV) = 150.0;
		dist(UnitType::TANK, UnitType::FIGHTER) = 1.0;
		dist(UnitType::TANK, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::TANK, UnitType::IFV) = 150.0;
		dist(UnitType::TANK, UnitType::TANK) = 150.0;
		
		for (int i = 0; i < 25; ++i)
			dists[i] = sqr(dists[i]);
	}
	
	double &dist(UnitType from, UnitType to) {
		return dists[(int) from * 5 + (int) to];
	}
} DANGER_DISTS;

double Strat::attractionPoint(const P from, const Group &group, double ticks, bool angryMode)
{
	double res = 0.0;
	double f2hDmgRes = 0.0;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			if (dCell.totalMyDamage > 0.0 || dCell.totalEnemyDamage > 0.0)
			{
				double alpha = 0.3;
				double alphaM1 = 0.7;
				
				P p = P((x + 0.5) * DISTR_MAT_CELL_SIZE, (y + 0.5) * DISTR_MAT_CELL_SIZE);
				
				double dist2 = p.dist2(from);
				
				double pts = (group.health * alphaM1 + dCell.totalEnemyHealth * alpha) / (dCell.totalEnemyHealth*0.01 + dCell.totalEnemyDamage) 
				- (dCell.totalEnemyHealth * alphaM1 + group.health * alpha) / (group.health * 0.01 + dCell.totalMyDamage);
				
				if (pts > 0.0)
				{
					double pp = 1.0/(1.0 + dist2);
					res += pts * pp;
				}
				else
				{
					/*for (int mt = 0; mt < 5; ++mt)
					{
						if (group.healthByTypes[mt])
						{
							for (int et = 0; et < 5; ++et)
							{
								if (dCell.enemyHealth[et])
								{
									double rad2 = DANGER_DISTS.dist((UnitType) et, (UnitType) mt);
									//double rad2 = 150*150;
									if (rad2 > dist2)
									{
										double fraction = group.healthByTypes[mt] / group.health * dCell.enemyHealth[et] / dCell.totalEnemyHealth;
										double pn = (1.0 - std::min(1.0, dist2/rad2));
										res += pts * pn * fraction;
									}
								}
							}
						}
					}*/
					
					double pn = (1.0 - std::min(1.0, dist2/sqr(150)));
					res += pts * pn;
				}
				
				//res += pts / p.dist2(from);
			}
			
			f2hDmgRes += dCell.f2hDmg;
		}
	}
	
	if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		double L = 1.5 * WIDTH;
		if (f2hDmgRes > 0.0 && res < 0.0)
		{
			if (fivG && fivG->size > 10)
			{
				L = fivG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::IFV];
		
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 300.0)
			{
				res -= (l - 300.0);
			}
		}
	}
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::HELICOPTER);
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 400.0)
			{
				res -= (l - 400.0);
			}
		}
	}*/
	
	if (isGroundUnit(group.unitType))
	{
		for (const Building &b : buildings)
		{
			if (b.side != 0)
			{
				double d = from.dist(b.pos);
				double coef = b.assignedGroup == group.internalId ? 2.0 : 1.0;
				res += coef*group.health/(20 + d)*0.1;
			}
		}
	}
	
	const double keepBorderDist = 40.0;
	double borderPen = std::max(
		std::max(
			std::max(keepBorderDist - from.x, 0.0),
			std::max(keepBorderDist - from.y, 0.0)
		),
		std::max(
			std::max(keepBorderDist - (WIDTH - from.x), 0.0),
			std::max(keepBorderDist - (HEIGHT - from.y), 0.0)
		)
	);
	
	res -= borderPen*10;
	
	return res;
}


ShrinkResult Strat::findShrink(Group &group)
{
	ShrinkResult result;
	
	Simulator sim;
	sim.tick = tick;
	std::copy(cells, cells + CELLS_X * CELLS_Y, sim.cells);
	
	BBox gbox = group.bbox;
	gbox.expand(2.0);
	for (const MyUnit &u : units)
	{
		if (gbox.inside(u.pos))
		{
			sim.units.push_back(u);
			MyUnit &tu = *sim.units.rbegin();
			tu.selected = group.check(tu);
			tu.vel = P(0, 0);
		}
	}
	sim.groups.push_back(group);
	
	double bestArea = WIDTH * HEIGHT;
	for (double x = -1; x <= 1; ++x)
	{
		for (double y = -1; y <= 1; ++y)
		{
			Simulator sim2 = sim;
			MyMove myMove;
			myMove.action = MyActionType::SCALE;
			myMove.p = group.center + P(x, y) * 10.0;
			myMove.factor = 0.2;
			sim2.registerMove(myMove, 0);
			
			int i = 0;
			for (; i < 20; ++i)
			{
				sim2.tick++;
				sim2.resetAxisSorts();
				sim2.applyMoves();
				int cnt = sim2.moveUnits();
				if (!cnt)
					break;
			}
			sim2.updateStats();
			
			double area = sim2.groups.rbegin()->bbox.area();
			if (area < bestArea)
			{
				bestArea = area;
				result.shrinkPoint = myMove.p;
				result.ticks = i;
				result.endBBox = sim2.groups.rbegin()->bbox;
			}
		}
	}
	
	return result;
}
}


namespace StratV16 {
	double groupPriority(const Group &g) {
	double res = 0.0;
	
	if (g.shrinkAfterNuke)
		res += 1100.0;
	else if (g.actionStarted)
		res += 1000.0;
	
	res -= g.lastUpdateTick;
	
	return res;
}

void DistributionMatrix::clear()
{
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		Cell &cell = cells[i];
		for (int j = 0; j < 5; ++j)
			cell.count[j] = 0;
		for (int j = 0; j < 5; ++j)
			cell.health[j] = 0;
	}
}

void DistributionMatrix::initialize(const Simulator &sim)
{
	clear();
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : sim.units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / DISTR_MAT_CELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / DISTR_MAT_CELL_SIZE;
				
				Cell &cell = getCell(x, y);
				int type = (int) u.type;
				cell.count[type] += 1.0 / K;
				cell.health[type] += u.durability / K;
			}
		}
	}
}

void DistributionMatrix::blur(DistributionMatrix &oth) const
{
	oth.clear();
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			Cell &resCell = oth.getCell(x, y);
			int cnt = 0;
			for (int yy = -1; yy <= 1; ++yy)
			{
				for (int xx = -1; xx <= 1; ++xx)
				{
					int x2 = x + xx;
					int y2 = y + yy;
					if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
					{
						++cnt;
						const Cell &cell = getCell(x2, y2);
						for (int i = 0; i < 5; ++i)
							resCell.count[i] += cell.count[i];
						for (int i = 0; i < 5; ++i)
							resCell.health[i] += cell.health[i];
					}
				}
			}
			
			const Cell &cell = getCell(x, y);
			for (int i = 0; i < 5; ++i)
			{
				if (!cell.count[i])
				{
					resCell.count[i] = 0;
					resCell.health[i] = 0;
				}
			}
			
			/*for (int i = 0; i < 5; ++i)
				resCell.count[i] += (resCell.count[i] + cnt - 1) / cnt;
			for (int i = 0; i < 5; ++i)
				resCell.health[i] /= cnt;*/
		}
	}
}

Strat::Strat::Strat()
{
	Group g;
	g.actionStarted = false;
	g.lastUpdateTick = 0;
	g.lastShrinkTick = 0;
	
	for (int i = 0; i < 1; ++i)
	{
		g.unitType = UnitType::HELICOPTER;
		//g.miniGroupInd = i;
		g.internalId = internalGroupSeq++;
		groups.push_back(g);
	}
	g.miniGroupInd = 0;
	
	g.unitType = UnitType::IFV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::FIGHTER;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::TANK;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::ARV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
}


void Strat::calcMicroShift(Group &group, P &shift)
{
	if (group.unitType != UnitType::TANK && group.unitType != UnitType::FIGHTER)
		return;
	
	BBox bbox = group.bbox;
	bbox.p1 += shift - P(35, 35);
	bbox.p2 += shift + P(35, 35);
	
	int finalScore = -100000;
	int currentScore = -100000;
	double ticks = 20.0;
	
	//if (group.unitType == UnitType::TANK)
	{
		std::vector<P> enemies[(int) UnitType::COUNT];
		std::vector<P> myUnits;
		
		for (const MyUnit &u : units)
		{
			if (u.side > 0 && (u.type == UnitType::TANK || u.type == UnitType::HELICOPTER || u.type == UnitType::IFV) && bbox.inside(u.pos))
			{
				enemies[(int) u.type].push_back(u.pos + u.vel * ticks);
			}
			else if (group.check(u))
			{
				myUnits.push_back(u.pos);
			}
		}
		
		double L = 8.0;
		P newShift = shift;
		for (int k = 0; k < 4; ++k)
		{
			P foundShift = newShift;
			int foundScore = -100000;
			for (int i = 0; i < 9; ++i)
			{
				P testShift = newShift + P(i / 3 - 1, i % 3 - 1) * L;
				int score = 0;
				bool isCurShift = shift.dist2(testShift) < 0.1;
				
				if (!isCurShift || currentScore == -100000)
				{
					const std::vector<MicroShiftValues> &pos = microShiftMatrix.pos[(int) group.unitType];
					if (!pos.empty())
					{
						for (const P &myP : myUnits)
						{
							P myPos = myP + testShift;
							bool found = false;
							for (const MicroShiftValues &vals : pos)
							{
								if (!found)
								{
									for (const P &enP : enemies[(int) vals.unitType])
									{
										if (myPos.dist2(enP) < vals.dist2)
										{
											score += vals.val;
											found = true;
											break;
										}
									}
								}
							}
						}
					}
					
					// !! decrease SCORE
					int posScore = score;
					score /= 2;
					
					const std::vector<MicroShiftValues> &neg = microShiftMatrix.neg[(int) group.unitType];
					if (!neg.empty())
					{
						for (const MicroShiftValues &vals : neg)
						{
							for (const P &enP : enemies[(int) vals.unitType])
							{
								for (const P &myP : myUnits)
								{
									P myPos = myP + testShift;
									if (myPos.dist2(enP) < vals.dist2)
									{
										score -= vals.val;
										break;
									}
								}
							}
						}
					}
					
					int negScore = posScore / 2 - score;
					
					if (foundScore < score)
					{
						foundShift = testShift;
						foundScore = score;
					}
					
					if (isCurShift)
						currentScore = score;
				}
			}
			
			L /= 2.0;
			newShift = foundShift;
			
			finalScore = foundScore;
		}
		
		if (currentScore >= finalScore)
			return;
		
		shift = newShift;
	}
}

void Strat::calcNuclearEfficiency()
{
	for (int i = 0; i < MICROCELLS_X * MICROCELLS_Y; i++)
		nuclearEfficiency[i] = 0;
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / MICROCELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / MICROCELL_SIZE;
				
				double efficiency = 100.0 / (10.0 + u.durability) / K;
				
				if (u.type == UnitType::ARV)
					efficiency /= 10.0;
				
				if (u.type == UnitType::FIGHTER || u.type == UnitType::HELICOPTER)
					efficiency /= 1.5;
				
				nuclearEfficiency[y * MICROCELLS_X + x] += efficiency;
			}
		}
	}
}

UnitType Strat::calcNextUnitTypeForConstruction(bool ground)
{
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType == UnitType::HELICOPTER)
		{
			return UnitType::TANK;
		}
	}
	
	if (enemyCount[UnitType::HELICOPTER] * 0.9 > myCount[UnitType::HELICOPTER])
	{
		return UnitType::HELICOPTER;
	}
	
	return UnitType::TANK;
	
	double score[5] = {};
	
	int enCnt = enemyCount[UnitType::HELICOPTER]*0.7 + enemyCount[UnitType::FIGHTER]*0.3;
	int myCnt = myCount[UnitType::FIGHTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::FIGHTER] += enCnt - myCnt;
	}
	score[(int) UnitType::FIGHTER] *= 0.6;
	
	enCnt = enemyCount[UnitType::TANK];
	myCnt = myCount[UnitType::HELICOPTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::HELICOPTER] += enCnt - myCnt;
	}
	
	score[(int) UnitType::HELICOPTER] *= 0.8;
	
	enCnt = enemyCount[UnitType::IFV];
	myCnt = myCount[UnitType::TANK];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::TANK] += enCnt - myCnt;
	}
	
	enCnt = enemyCount[UnitType::FIGHTER]*0.7 + enemyCount[UnitType::HELICOPTER]*0.3;
	myCnt = myCount[UnitType::IFV];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::IFV] += enCnt - myCnt;
	}
	
	
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE)
		{
			score[(int) b.unitType] -= 20;
		}
	}
	
	double grCount = 0;
	double airCount = 0;
	for (int i = 0; i < 5; ++i)
	{
		if (isGroundUnit((UnitType) i) && i != (int) UnitType::ARV)
		{
			grCount += myCount[(UnitType) i];
		}
		else
		{
			airCount += myCount[(UnitType) i];
		}
	}
	
	double totalCount = grCount + airCount;
	if (totalCount > 0)
	{
		for (int i = 0; i < 5; ++i)
		{
			if (isGroundUnit((UnitType) i))
			{
				score[i] *= airCount;
			}
			else
			{
				score[i] *= grCount;
			}
		}
	}
	
	int res = 0;
	int resType = -1;
	for (int i = 0; i < 5; ++i)
	{
		if (score[i] > res)
		{
			res = score[i];
			resType = i;
		}
	}
	
	if (resType >= 0)
		return (UnitType) resType;
	
	return UnitType::TANK;
}

MyMove Strat::calcNextMove()
{
	MyMove result;
	result.action = MyActionType::NONE;
	
	int moves = getAvailableActions(12);
	if (moves < 1)
		return result;
	
	if (players[1].remainingNuclearStrikeCooldownTicks < 30)
	{
		if (moves < 3)
			return result;
	}
	
	updateStats();
	
	if (players[1].nextNuclearStrikeTick >= 0)
	{
		bool select = false;
		bool expand = false;
		BBox bbox;
		int evaidingGroups = 0;
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep == 0)
			{
				BBox gbox = g.bbox;
				gbox.expand(40.0);
				if (gbox.inside(players[1].nuclearStrike))
				{
					bbox.add(g.bbox);
					g.nukeEvadeStep = 1;
					select = true;
					++evaidingGroups;
				}
			}
			else if (g.nukeEvadeStep == 1)
			{
				expand = true;
				g.nukeEvadeStep = 2;
				++evaidingGroups;
			}
			else
			{
				++evaidingGroups;
			}
		}
		
		if (select)
		{
			result.action = MyActionType::CLEAR_AND_SELECT;
			result.p1 = bbox.p1;
			result.p2 = bbox.p2;
			
			return result;
		}
		else if (expand)
		{
			result.action = MyActionType::SCALE;
			result.p = players[1].nuclearStrike;
			result.factor = 9.0;
			return result;
		}
		
		if (evaidingGroups && moves < 3)
			return result;
	}
	else
	{
		for (Group &g : groups)
		{
			if (tick - g.lastShrinkTick > 30)
				g.shrinkAfterNuke = false;
			if (g.nukeEvadeStep != 0)
			{
				g.shrinkAfterNuke = true;
				g.nukeEvadeStep = -1;
			}
		}
	}
	
	/*if (tick > 200) {
		int moves = getAvailableActions(4, 10);
		if (moves < 1) // throttle
			return result;
	}*/
	
	for (Building &b : buildings)
	{
		/*if (b.lastChangeUnitCount > b.unitCount)
			b.lastChangeUnitCount = b.unitCount;*/
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && (b.unitType == UnitType::NONE/* || (b.unitCount - b.lastChangeUnitCount) > 11*/))
		{
			//LOG("SVP " << buildingCaptured);
			result.action = MyActionType::SETUP_VEHICLE_PRODUCTION;
			result.facilityId = b.id;
			
			/*if (buildingCaptured % 2 == 0)
				result.unitType = UnitType::FIGHTER;
			else
				result.unitType = UnitType::IFV;*/
			
			//result.unitType = UnitType::TANK;
			result.unitType = calcNextUnitTypeForConstruction(false);
			//result.unitType = (UnitType) (buildingCaptured % 5);
			//b.lastChangeUnitCount = b.unitCount;
			
			++buildingCaptured;
			return result;
		}
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE && b.unitCount > 40 && b.productionProgress < 30 || b.createGroupStep > 0)
		{
			//LOG("MAKE GROUP");
			if (b.createGroupStep == 0 || b.createGroupStep == 1)
			{
				result.action = MyActionType::CLEAR_AND_SELECT;
				result.p1 = b.pos - P(32, 32);
				result.p2 = b.pos + P(32, 32);
				result.unitType = b.unitType;
				b.createGroupStep = 2;
				return result;
			}
			
			if (b.createGroupStep == 2)
			{
				// check if selected
				bool anySelected = false;
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.groups.count() == 0)
					{
						if (p.pos.x > b.pos.x - 32 && p.pos.x < b.pos.x + 32 && p.pos.y > b.pos.y - 32 && p.pos.y < b.pos.y + 32)
						{
							if (p.selected)
							{
								anySelected = true;
								break;
							}
						}
					}
				}
				
				if (anySelected)
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					b.createGroupStep = 3;
					
					Group newGroup;
					//newGroup.unitType = b.unitType;
					newGroup.group = result.group;
					newGroup.internalId = internalGroupSeq++;
					groups.push_back(newGroup);
					
					b.createGroupStep = 0;
					return result;
				}
				else
				{
					b.createGroupStep = 1;
				}
			}
		}
	}
	
		
	if (!initialGroupsGerationDone && !buildings.empty())
	{
		for (Group &g : groups)
		{
			/*if (g.unitType == UnitType::HELICOPTER && !g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					P allCenter = P(0, 0);
					int count = 0;
					for (const MyUnit &u : units)
					{
						if (u.side == 0 && u.type == g.unitType)
						{
							allCenter += u.pos;
							++count;
						}
					}
					
					allCenter /= count;
					
					if (g.miniGroupInd == 0)
					{
						result.p1 = P(0, 0);
						result.p2 = allCenter;
					}
					else if (g.miniGroupInd == 1)
					{
						result.p1 = P(allCenter.x, 0);
						result.p2 = P(WIDTH, allCenter.y);
					}
					else if (g.miniGroupInd == 2)
					{
						result.p1 = P(0, allCenter.y);
						result.p2 = P(allCenter.x, HEIGHT);
					}
					else if (g.miniGroupInd == 3)
					{
						result.p1 = allCenter;
						result.p2 = P(WIDTH, HEIGHT);
					}
					
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}*/
			
			if (!g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else if (g.enumGroupBuildStep == 1)
				{
					result.action = MyActionType::ASSIGN;
					g.enumGroupBuildStep++;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}
		}
		
		initialGroupsGerationDone = true;
		LOG("DONE");
	}
	
	bool angryMode = angryModeTill > tick;

	
	DistributionMatrix matr;
	matr.initialize(*this);
	matr.blur(distributionMatrix);
	
	/*if (tick > 532)
	{
		std::cout.width(2);
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &resCell = distributionMatrix.getCell(x, y);
				std::cout << " " << resCell.count[1];
			}
			std::cout << std::endl;
		}
		std::cout << std::endl;
	}*/
	
	if (players[0].remainingNuclearStrikeCooldownTicks == 0)
	{
		resetCells();
		calcNuclearEfficiency();
		
		auto getEnemySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0.0;
			
			return nuclearEfficiency[y * MICROCELLS_X + x];
		};
		
		auto getMySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0;
			
			return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
				+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
				+ (int) getMicrocell(x, y, 0, UnitType::ARV).size() / 2;
		};
		
		P bestTarget;
		int bestScore = 0;
		
		for (int y = 0; y < MICROCELLS_Y; ++y) {
			for (int x = 0; x < MICROCELLS_X; ++x) {
				double enemyN = getEnemySize(x, y);
				if (enemyN > 10)
				{
					int totalMyCount = 0;
					int myCenterCount = 0;
					for (int xx = -1; xx <= 1; ++xx)
					{
						for (int yy = -1; yy <= 1; ++yy)
						{
							totalMyCount += getMySize(x + xx, y + yy);
							if (xx == 0 && yy == 0)
								myCenterCount = totalMyCount;
						}
					}
					
					if (totalMyCount < 20 && myCenterCount == 0)
					{
						int totalMyExtCount = totalMyCount;
						if (totalMyExtCount == 0)
						{
							for (int xx = -2; xx <= 2; ++xx)
							{
								for (int yy = -2; yy <= 2; ++yy)
								{
									if (xx < -1 || xx > 1 || yy < -1 || yy > 1)
										totalMyExtCount += getMySize(x + xx, y + yy);
								}
							}
						}
						
						if (totalMyExtCount > 0)
						{
							double totalEnemyCount =
								enemyN +
								getEnemySize(x - 1, y)/2 +
								getEnemySize(x + 1, y)/2 + 
								getEnemySize(x, y - 1)/2 + 
								getEnemySize(x, y + 1)/2;
							
							if (bestScore < totalEnemyCount)
							{
								bestScore = totalEnemyCount;
								bestTarget = P(x + 0.5, y + 0.5) * MICROCELL_SIZE;
							}
						}
					}
				}
			}
		}
		
		if (bestScore > 0)
		{
			// Correct point
			P correctedBestTarget = bestTarget;
			
			std::vector<P> myVehicles;
			std::vector<P> enemyVehicles;
			int myInnerN = 0;
			int enemyInnerN = 0;
			for (const MyUnit &p : units)
			{
				double d2 = p.pos.dist2(bestTarget);
				if (d2 < (75.0*75.0))
				{
					if (d2 < (38.0*38.0))
					{
						if (p.side == 0)
							myInnerN++;
						else
							enemyInnerN++;
					}
					else
					{
						if (p.side == 0)
							myVehicles.push_back(p.pos);
						else
							enemyVehicles.push_back(p.pos);
					}
				}
			}
			
			int foundScore = -100000;
			for (int xx = -2; xx <= 2; ++xx)
			{
				for (int yy = -2; yy <= 2; ++yy)
				{
					P newTarget = bestTarget + P(xx, yy) * 6.0;
					
					int myNum = myInnerN*3;
					int myExtNum = 0;
					
					for (const P &p : myVehicles)
					{
						double d2 = p.dist2(newTarget);
						if (d2 < (50.0*50.0))
						{
							myNum++;
						}
						else if (d2 > (55.0*55.0) && d2 < (70.0*70.0))
						{
							myExtNum++;
						}
					}
					
					if (myExtNum > 0)
					{
						int enNum = enemyInnerN*3;
						for (const P &p : enemyVehicles)
						{
							double d2 = p.dist2(newTarget);
							if (d2 < (50.0*50.0))
							{
								enNum++;
							}
						}
						
						int score = enNum - myNum * 4;
						if (score > foundScore)
						{
							foundScore = score;
							correctedBestTarget = newTarget;
						}
					}
				}
			}
			
			if (foundScore > -100000)
			{
				auto getMySize = [this](int x, int y) {
					if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
						return 0;
					
					return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
						+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
						+ (int) getMicrocell(x, y, 0, UnitType::ARV).size();
				};
				
				int pts = 0;
				long bestId = -1;
				P vehPos;
				
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.durability > 70)
					{
						double dist2 = p.pos.dist2(correctedBestTarget);
						if (dist2 > 55*55 && dist2 < 70*70)
						{
							bool outOfRange = false;
							
							if (p.vel.len2() > 0.01)
							{
								MyUnit u = p;
								for (int i = 0; i < 30; ++i)
								{
									double visRange = getVisionRange(u) - unitVel(p.type) * 10;
									if (u.pos.dist2(correctedBestTarget) > sqr(visRange))
									{
										outOfRange = true;
										break;
									}
									u.pos += u.vel;
								}
							}
							
							if (outOfRange)
								continue;
							
							int xx = p.pos.x / MICROCELL_SIZE;
							int yy = p.pos.y / MICROCELL_SIZE;
							
							//int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 60.0);
							if (pts < newPts)
							{
								pts = newPts;
								bestId = p.id;
								vehPos = p.pos;
							}
						}
					}
				}
				
				if (bestId >= 0)
				{
					result.action = MyActionType::TACTICAL_NUCLEAR_STRIKE;
					result.vehicleId = bestId;
					result.p = correctedBestTarget;
					return result;
				}
			}
		}
	}
	
	groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group &g){return g.size == 0;}), groups.end());
	
	if (result.action == MyActionType::NONE && !groups.empty())
	{
		debugAttractionPoints.clear();
		
		std::sort(groups.begin(), groups.end(), [](const Group &g1, const Group &g2){
			double p1 = groupPriority(g1);
			double p2 = groupPriority(g2);
			return p2 < p1;
		});
		
		assignBuildings();
		
		for (Group &group : groups)
		{
			if (tick - group.lastUpdateTick < 10 || group.nukeEvadeStep > 0)
				continue;
			
			if (group.shrinkActive)
			{
				if (tick - group.lastShrinkTick > 40 || !anyMoved(group))
				{
					group.shrinkActive = false;
				}
				else
				{
					continue;
				}
			}
			
			bool limitSpeed = false;
			if (nukeVehicleInd >= 0)
			{
				const MyUnit &u = units[nukeVehicleInd];
				if (group.check(u))
					limitSpeed = true;
			}
			
			calcDangerDistCells(group);
			
			P center = group.center;
			const BBox &bbox = group.bbox;
			int groupSize = group.size;
			double area = bbox.area();
			bool shrinkRequired = (area > groupSize * 40.0 || group.shrinkAfterNuke) && (((tick - group.lastUpdateTick) > 60 || group.shrinkAfterNuke) && (tick - group.lastShrinkTick) > 300);
			P shrinkPoint;
			if (shrinkRequired)
			{
				ShrinkResult shRes = findShrink(group);
				if (shRes.ticks > 7)
				{
					shrinkPoint = shRes.shrinkPoint;
				}
				else
				{
					shrinkRequired = false;
				}
				//LOG("SHRINK RES " << shRes.ticks << " " << shRes.endBBox << " P " << shRes.shrinkPoint);
			}
			//bool shrinkRequired = false;
			
			P c = center;
			double R = 20 + unitVel(group.unitType) * 40;
			/*if (group.unitType == UnitType::ARV)
				R = 200.0;*/
			double ticks = R / unitVel(group.unitType);
			double curVal = attractionPoint(center, group, ticks, angryMode);
			P tp = center;
			bool found = false;
			bool canMoveFlag = false;
			
			P clampP1 = group.center - bbox.p1 + P(3.0, 3.0);
			P clampP2 = P(WIDTH - 3.0, HEIGHT - 3.0) + (group.center - bbox.p2);
			for (int k = 0; k < 3.0; ++k)
			{
				ticks = R / unitVel(group.unitType);
				int di = -1;
				
				for (int i = 0; i < 20; ++i)
				{
					P p = c + P(PI * 2.0 / 20.0 * i) * R;
					p = clampP(p, clampP1, clampP2);
					
					P shift = p - center;
					if (shift.len2() > 0.01)
					{
						if (canMove(p - center, group))
						{
							canMoveFlag = true;
							double val = attractionPoint(p, group, ticks, angryMode);
							
							DebugAttractionPointsInfo debugInfo;
							debugInfo.point = c;
							debugInfo.dir = p - c;
							debugInfo.val = val;
							debugAttractionPoints.push_back(debugInfo);
							
							if (val > curVal)
							{
								curVal = val;
								di = i;
								tp = p;
								found = true;
							}
						}
					}
				}
				
				if (di >= 0)
				{
					c = tp;
				}
				
				R /= 1.5;
			}
			
			bool moveAway = false;
			P dirAway;
			P newShift = P(0, 0);
			if (!canMoveFlag)
			{
				dirAway = P(0, 0);
				for (Group &othGroup : groups)
				{
					if (&group != &othGroup && isGroundUnit(group.unitType) == isGroundUnit(othGroup.unitType))
					{
						P dp = group.center - othGroup.center;
						double clen = dp.len();
						
						if (clen < 150.0)
							dirAway += dp / clen;
					}
				}
				
				dirAway *= 10;
				dirAway += P(tick % 7 - 3, tick % 9 - 5);
				moveAway = true;
			}
			else
			{
				if (found)
					newShift = tp - center;
				
				if (newShift.len() < 15.0)
				{
					calcMicroShift(group, newShift);
				}
			}
			
			found = newShift.len2() > 0.1;
			
			if (found || shrinkRequired || moveAway)
			{
				if (!isSelected(group))
				{
					result = select(group);
					group.actionStarted = true;
				}
				else
				{
					if (shrinkRequired)
					{
						result.action = MyActionType::SCALE;
						result.factor = 0.2;
						result.p = shrinkPoint;
						group.lastShrinkTick = tick;
						group.shrinkActive = true;
						group.nukeEvadeStep = 0;
					}
					else if (found)
					{
						result.action = MyActionType::MOVE;
						result.p = newShift;
						
						if (limitSpeed)
							result.maxSpeed = unitVel(group.unitType) * 0.6;
						
						/*if (!limitSpeed)
						{
							limitSpeed = !anyEnemiesNearbyByDangerDistr(group);
							if (limitSpeed)
								result.maxSpeed = unitVel(group.unitType) * 0.8;
						}*/
						
						/*if (group.unitType == UnitType::FIGHTER)
							result.maxSpeed = 1.0;*/
					}
					else
					{
						result.action = MyActionType::MOVE;
						result.p = dirAway;
						//std::cout << "AWAY " << dirAway.x << " " << dirAway.y << std::endl;
					}
					
					group.lastUpdateTick = tick;
					group.actionStarted = false;
				}
				
				break;
			}
		}
	}
	
	return result;
}

void Strat::assignBuildings()
{
	std::set<Building *> pbuildings;
	
	for (Building &b : buildings)
	{
		if (b.side != 0)
		{
			pbuildings.insert(&b);
			b.assignedGroup = 0;
		}
	}
	
	std::set<Group *> pgroups;
	for (Group &g : groups)
	{
		if (isGroundUnit(g.unitType))
		{
			pgroups.insert(&g);
		}
	}
	
	size_t count = std::min(pbuildings.size(), pgroups.size());
	for (int i = 0; i < count; ++i)
	{
		double dist2 = sqr(100000.0);
		std::set<Building *>::iterator b = pbuildings.end();
		std::set<Group *>::iterator g = pgroups.end();
		for (std::set<Building *>::iterator bit = pbuildings.begin(); bit != pbuildings.end(); ++bit)
		{
			for (std::set<Group *>::iterator git = pgroups.begin(); git != pgroups.end(); ++git)
			{
				double d2 = (*bit)->pos.dist2((*git)->center);
				if (d2 < dist2)
				{
					dist2 = d2;
					b = bit;
					g = git;
				}
			}
		}
		
		if (b != pbuildings.end() && g != pgroups.end())
		{
			(*b)->assignedGroup = (*g)->internalId;
			pbuildings.erase(b);
			pgroups.erase(g);
		}
	}
}

bool Strat::anyEnemiesNearbyByDangerDistr(const Group &group)
{
	int x = group.center.x / DISTR_MAT_CELL_SIZE;
	int y = group.center.y / DISTR_MAT_CELL_SIZE;
	for (int yy = -7; yy <= 7; ++yy)
	{
		for (int xx = -7; xx <= 7; ++xx)
		{
			if (xx * xx + yy * yy <= 50)
			{
				int x2 = x + xx;
				int y2 = y + yy;
				if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
				{
					DangerDistCell &dCell = dangerDistCells[y2 * DISTR_MAT_CELLS_X + x2];
					if (dCell.totalEnemyDamage > 0.0 || dCell.totalMyDamage > 0.0)
					{
						return true;
					}
				}
			}
		}
	}
	
	return false;
}

void Strat::calcDangerDistCells(const Group &group)
{
	dngGr = &group;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
			double totalMyDamage = 0;
			
			for (int myType = 0; myType < 5; ++myType)
			{
				double typeDamage = 0;
				if (group.sizeByTypes[myType])
				{
					for (int enemyType = 0; enemyType < 5; ++enemyType)
					{
						if (cell.count[enemyType])
						{
							double dmg = getDamage((UnitType) myType, (UnitType) enemyType);
							typeDamage = std::max(typeDamage, std::min(dmg * group.sizeByTypes[myType], 120.0 * cell.count[enemyType]));
						}
					}
				}
				totalMyDamage += typeDamage;
			}
			
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			double f2hDmg = 0.0;
			dCell.totalEnemyDamage = 0.0;
			dCell.totalEnemyHealth = 0.0;
			for (int enemyType = 0; enemyType < 5; ++enemyType)
			{
				dCell.enemyDamage[enemyType] = 0.0;
				dCell.enemyHealth[enemyType] = 0.0;
				
				if (cell.count[enemyType])
				{
					dCell.enemyHealth[enemyType] += cell.health[enemyType];
					for (int myType = 0; myType < 5; ++myType)
					{
						if (group.sizeByTypes[myType])
						{
							double dmg = getDamage((UnitType) enemyType, (UnitType) myType);
							dCell.enemyDamage[enemyType] = std::min(dmg * cell.count[enemyType], 120.0 * group.sizeByTypes[myType]);
							
							if (enemyType == (int) UnitType::FIGHTER && myType == (int) UnitType::HELICOPTER)
								f2hDmg = dCell.enemyDamage[enemyType];
						}
					}
				}
				
				dCell.enemyDamage[enemyType] *= 1.5;
				dCell.totalEnemyDamage += dCell.enemyDamage[enemyType];
				dCell.totalEnemyHealth += dCell.enemyHealth[enemyType];
			}
			
			dCell.totalMyDamage = totalMyDamage;
			dCell.f2hDmg = f2hDmg;
		}
	}
}

struct DangerDist {
	double dists[25];
	
	DangerDist() {
		dist(UnitType::ARV, UnitType::ARV) = 150.0;
		dist(UnitType::ARV, UnitType::FIGHTER) = 150.0;
		dist(UnitType::ARV, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::ARV, UnitType::IFV) = 150.0;
		dist(UnitType::ARV, UnitType::TANK) = 150.0;
		
		dist(UnitType::FIGHTER, UnitType::ARV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::IFV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::HELICOPTER, UnitType::ARV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::IFV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::IFV, UnitType::ARV) = 150.0;
		dist(UnitType::IFV, UnitType::FIGHTER) = 100.0;
		dist(UnitType::IFV, UnitType::HELICOPTER) = 100.0;
		dist(UnitType::IFV, UnitType::IFV) = 150.0;
		dist(UnitType::IFV, UnitType::TANK) = 150.0;
		
		dist(UnitType::TANK, UnitType::ARV) = 150.0;
		dist(UnitType::TANK, UnitType::FIGHTER) = 1.0;
		dist(UnitType::TANK, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::TANK, UnitType::IFV) = 150.0;
		dist(UnitType::TANK, UnitType::TANK) = 150.0;
		
		for (int i = 0; i < 25; ++i)
			dists[i] = sqr(dists[i]);
	}
	
	double &dist(UnitType from, UnitType to) {
		return dists[(int) from * 5 + (int) to];
	}
} DANGER_DISTS;


bool isClosedSpaceDanger(const P &myP, const P &enP, double myVel, double enemyVel, double ticks)
{
	double myR = myVel * ticks + 0.1;
	double dangerRad = 70.0;
	double enR = enemyVel * ticks + dangerRad;
	double d = myP.dist(enP);
	
	if (myR + d < enR)
		return true;
	
	const double borderDist = 40.0;
	if (myP.x > (borderDist + myR) && myP.x < (WIDTH - borderDist - myR) && myP.y > (borderDist + myR) && myP.y < (HEIGHT - borderDist - myR))
		return false;
	
	P myPn = myP;
	P enPn = enP;
	if (myPn.x > WIDTH / 2.0)
	{
		myPn.x = WIDTH - myPn.x;
		enPn.x = WIDTH - enPn.x;
	}
	
	if (myPn.y > HEIGHT / 2.0)
	{
		myPn.y = HEIGHT - myPn.y;
		enPn.y = HEIGHT - enPn.y;
	}
	
	if (myPn.x < myPn.y)
	{
		std::swap(myPn.x, myPn.y);
		std::swap(enPn.x, enPn.y);
	}
	
	double borderDistX = std::min(borderDist, myPn.x);
	double borderDistY = std::min(borderDist, myPn.y);
	
	double b = sqrt(sqr(myR) - sqr(myPn.y - borderDistY));
	double X = myPn.x + b;
	
	if (P(X, borderDistY).dist2(enPn) > sqr(enR))
		return false;
	
	if (myR > (myPn.x - borderDistX))
	{
		double Y = myPn.y + sqrt(sqr(myR) - sqr(myPn.x - borderDistX));
		
		if (P(borderDistX, Y).dist2(enPn) > sqr(enR))
			return false;
	}
	else
	{
		X = myPn.x - b;
	
		if (P(X, borderDistY).dist2(enPn) > sqr(enR))
			return false;
	}
	
	return true;
}

double captureTick(const P &myP, const P &enP, double myVel, double enemyVel)
{
	double dT = 125.0;
	double oldT = 0.0;
	for (double t = 0.0; t <= 625;)
	{
		if (!isClosedSpaceDanger(myP, enP, myVel, enemyVel, t))
		{
			oldT = t;
			t += dT;
		}
		else
		{
			if (dT <= 1.0)
				return t;
			
			t = oldT;
			dT /= 5.0;
			t += dT;
		}
	}
	
	return 625;
}

double Strat::attractionPoint(const P from, const Group &group, double ticks, bool angryMode)
{
	double res = 0.0;
	double f2hDmgRes = 0.0;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			if (dCell.totalMyDamage > 0.0 || dCell.totalEnemyDamage > 0.0)
			{
				double alpha = 0.3;
				double alphaM1 = 0.7;
				
				P p = P((x + 0.5) * DISTR_MAT_CELL_SIZE, (y + 0.5) * DISTR_MAT_CELL_SIZE);
				
				double dist2 = p.dist2(from);
				
				double pts = (group.health * alphaM1 + dCell.totalEnemyHealth * alpha) / (dCell.totalEnemyHealth*0.01 + dCell.totalEnemyDamage) 
				- (dCell.totalEnemyHealth * alphaM1 + group.health * alpha) / (group.health * 0.01 + dCell.totalMyDamage);
				
				if (pts != 0.0)
				{
					double enemyVel = 0.0;
					
					for (int i = 0; i < 5; ++i) 
					{ 
						if (dCell.enemyHealth[i]) 
							enemyVel += unitVel((UnitType) i) * (dCell.enemyHealth[i] / dCell.totalEnemyHealth); 
					}
					
					if (pts < 0.0)
					{
						double t = captureTick(from, p, unitVel(group.unitType), enemyVel);
						res += pts * (625 - t) / 625.0;
					}
					else
					{
						/*double t = captureTick(p, from, enemyVel, unitVel(group.unitType));
						res += 0.1* pts * (625 - t) / 625.0;*/
					}
				}
				
				if (pts > 0.0)
				{
					double pp = 1.0/(1.0 + dist2);
					res += pts * pp;
				}
				else
				{
					/*for (int mt = 0; mt < 5; ++mt)
					{
						if (group.healthByTypes[mt])
						{
							for (int et = 0; et < 5; ++et)
							{
								if (dCell.enemyHealth[et])
								{
									double rad2 = DANGER_DISTS.dist((UnitType) et, (UnitType) mt);
									//double rad2 = 150*150;
									if (rad2 > dist2)
									{
										double fraction = group.healthByTypes[mt] / group.health * dCell.enemyHealth[et] / dCell.totalEnemyHealth;
										double pn = (1.0 - std::min(1.0, dist2/rad2));
										res += pts * pn * fraction;
									}
								}
							}
						}
					}*/
					
					double pn = (1.0 - std::min(1.0, dist2/sqr(150)));
					res += pts * pn;
				}
				
				//res += pts / p.dist2(from);
			}
			
			f2hDmgRes += dCell.f2hDmg;
		}
	}
	
	/*if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		double L = 1.5 * WIDTH;
		if (f2hDmgRes > 0.0 && res < 0.0)
		{
			if (fivG && fivG->size > 10)
			{
				L = fivG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::IFV];
		
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 300.0)
			{
				res -= (l - 300.0);
			}
		}
	}*/
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::HELICOPTER);
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 400.0)
			{
				res -= (l - 400.0);
			}
		}
	}*/
	
	if (isGroundUnit(group.unitType))
	{
		for (const Building &b : buildings)
		{
			if (b.side != 0)
			{
				double d = from.dist(b.pos);
				double coef = b.assignedGroup == group.internalId ? 2.0 : 1.0;
				res += coef*group.health/(20 + d)*0.1;
			}
		}
	}
	
	/*const double keepBorderDist = 40.0;
	double borderPen = std::max(
		std::max(
			std::max(keepBorderDist - from.x, 0.0),
			std::max(keepBorderDist - from.y, 0.0)
		),
		std::max(
			std::max(keepBorderDist - (WIDTH - from.x), 0.0),
			std::max(keepBorderDist - (HEIGHT - from.y), 0.0)
		)
	);
	
	res -= borderPen*10;*/
	
	return res;
}


ShrinkResult Strat::findShrink(Group &group)
{
	ShrinkResult result;
	
	Simulator sim;
	sim.tick = tick;
	std::copy(cells, cells + CELLS_X * CELLS_Y, sim.cells);
	
	BBox gbox = group.bbox;
	gbox.expand(2.0);
	for (const MyUnit &u : units)
	{
		if (gbox.inside(u.pos))
		{
			sim.units.push_back(u);
			MyUnit &tu = *sim.units.rbegin();
			tu.selected = group.check(tu);
			tu.vel = P(0, 0);
		}
	}
	sim.groups.push_back(group);
	
	double bestArea = WIDTH * HEIGHT;
	for (double x = -1; x <= 1; ++x)
	{
		for (double y = -1; y <= 1; ++y)
		{
			Simulator sim2 = sim;
			MyMove myMove;
			myMove.action = MyActionType::SCALE;
			myMove.p = group.center + P(x, y) * 10.0;
			myMove.factor = 0.2;
			sim2.registerMove(myMove, 0);
			
			int i = 0;
			for (; i < 20; ++i)
			{
				sim2.tick++;
				sim2.resetAxisSorts();
				sim2.applyMoves();
				int cnt = sim2.moveUnits();
				if (!cnt)
					break;
			}
			sim2.updateStats();
			
			double area = sim2.groups.rbegin()->bbox.area();
			if (area < bestArea)
			{
				bestArea = area;
				result.shrinkPoint = myMove.p;
				result.ticks = i;
				result.endBBox = sim2.groups.rbegin()->bbox;
			}
		}
	}
	
	return result;
}
}

namespace StratV17 {
	double groupPriority(const Group &g) {
	double res = 0.0;
	
	if (g.shrinkAfterNuke)
		res += 1100.0;
	else if (g.actionStarted)
		res += 1000.0;
	
	res -= g.lastUpdateTick;
	
	return res;
}

void DistributionMatrix::clear()
{
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		Cell &cell = cells[i];
		for (int j = 0; j < 5; ++j)
			cell.count[j] = 0;
		for (int j = 0; j < 5; ++j)
			cell.health[j] = 0;
	}
}

void DistributionMatrix::initialize(const Simulator &sim)
{
	clear();
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : sim.units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / DISTR_MAT_CELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / DISTR_MAT_CELL_SIZE;
				
				Cell &cell = getCell(x, y);
				int type = (int) u.type;
				cell.count[type] += 1.0 / K;
				cell.health[type] += u.durability / K;
			}
		}
	}
}

void DistributionMatrix::blur(DistributionMatrix &oth) const
{
	oth.clear();
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			Cell &resCell = oth.getCell(x, y);
			int cnt = 0;
			for (int yy = -1; yy <= 1; ++yy)
			{
				for (int xx = -1; xx <= 1; ++xx)
				{
					int x2 = x + xx;
					int y2 = y + yy;
					if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
					{
						++cnt;
						const Cell &cell = getCell(x2, y2);
						for (int i = 0; i < 5; ++i)
							resCell.count[i] += cell.count[i];
						for (int i = 0; i < 5; ++i)
							resCell.health[i] += cell.health[i];
					}
				}
			}
			
			const Cell &cell = getCell(x, y);
			for (int i = 0; i < 5; ++i)
			{
				if (!cell.count[i])
				{
					resCell.count[i] = 0;
					resCell.health[i] = 0;
				}
			}
			
			/*for (int i = 0; i < 5; ++i)
				resCell.count[i] += (resCell.count[i] + cnt - 1) / cnt;
			for (int i = 0; i < 5; ++i)
				resCell.health[i] /= cnt;*/
		}
	}
}

Strat::Strat::Strat()
{
	Group g;
	g.actionStarted = false;
	g.lastUpdateTick = 0;
	g.lastShrinkTick = 0;
	
	for (int i = 0; i < 1; ++i)
	{
		g.unitType = UnitType::HELICOPTER;
		//g.miniGroupInd = i;
		g.internalId = internalGroupSeq++;
		groups.push_back(g);
	}
	g.miniGroupInd = 0;
	
	g.unitType = UnitType::IFV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::FIGHTER;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::TANK;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::ARV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
}


void Strat::calcMicroShift(Group &group, P &shift)
{
	if (group.unitType != UnitType::TANK && group.unitType != UnitType::FIGHTER)
		return;
	
	BBox bbox = group.bbox;
	bbox.p1 += shift - P(35, 35);
	bbox.p2 += shift + P(35, 35);
	
	int finalScore = -100000;
	int currentScore = -100000;
	double ticks = 20.0;
	
	//if (group.unitType == UnitType::TANK)
	{
		std::vector<P> enemies[(int) UnitType::COUNT];
		std::vector<P> myUnits;
		
		for (const MyUnit &u : units)
		{
			if (u.side > 0 && (u.type == UnitType::TANK || u.type == UnitType::HELICOPTER || u.type == UnitType::IFV) && bbox.inside(u.pos))
			{
				enemies[(int) u.type].push_back(u.pos + u.vel * ticks);
			}
			else if (group.check(u))
			{
				myUnits.push_back(u.pos);
			}
		}
		
		double L = 8.0;
		P newShift = shift;
		for (int k = 0; k < 4; ++k)
		{
			P foundShift = newShift;
			int foundScore = -100000;
			for (int i = 0; i < 9; ++i)
			{
				P testShift = newShift + P(i / 3 - 1, i % 3 - 1) * L;
				int score = 0;
				bool isCurShift = shift.dist2(testShift) < 0.1;
				
				if (!isCurShift || currentScore == -100000)
				{
					const std::vector<MicroShiftValues> &pos = microShiftMatrix.pos[(int) group.unitType];
					if (!pos.empty())
					{
						for (const P &myP : myUnits)
						{
							P myPos = myP + testShift;
							bool found = false;
							for (const MicroShiftValues &vals : pos)
							{
								if (!found)
								{
									for (const P &enP : enemies[(int) vals.unitType])
									{
										if (myPos.dist2(enP) < vals.dist2)
										{
											score += vals.val;
											found = true;
											break;
										}
									}
								}
							}
						}
					}
					
					// !! decrease SCORE
					int posScore = score;
					score /= 2;
					
					const std::vector<MicroShiftValues> &neg = microShiftMatrix.neg[(int) group.unitType];
					if (!neg.empty())
					{
						for (const MicroShiftValues &vals : neg)
						{
							for (const P &enP : enemies[(int) vals.unitType])
							{
								for (const P &myP : myUnits)
								{
									P myPos = myP + testShift;
									if (myPos.dist2(enP) < vals.dist2)
									{
										score -= vals.val;
										break;
									}
								}
							}
						}
					}
					
					int negScore = posScore / 2 - score;
					
					if (foundScore < score)
					{
						foundShift = testShift;
						foundScore = score;
					}
					
					if (isCurShift)
						currentScore = score;
				}
			}
			
			L /= 2.0;
			newShift = foundShift;
			
			finalScore = foundScore;
		}
		
		if (currentScore >= finalScore)
			return;
		
		shift = newShift;
	}
}

void Strat::calcNuclearEfficiency()
{
	for (int i = 0; i < MICROCELLS_X * MICROCELLS_Y; i++)
		nuclearEfficiency[i] = 0;
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / MICROCELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / MICROCELL_SIZE;
				
				double efficiency = 100.0 / (10.0 + u.durability) / K;
				
				if (u.type == UnitType::ARV)
					efficiency /= 10.0;
				
				if (u.type == UnitType::FIGHTER || u.type == UnitType::HELICOPTER)
					efficiency /= 1.5;
				
				nuclearEfficiency[y * MICROCELLS_X + x] += efficiency;
			}
		}
	}
}

UnitType Strat::calcNextUnitTypeForConstruction(bool ground)
{
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType == UnitType::HELICOPTER)
		{
			return UnitType::TANK;
		}
	}
	
	if (enemyCount[UnitType::HELICOPTER] * 0.9 > myCount[UnitType::HELICOPTER])
	{
		return UnitType::HELICOPTER;
	}
	
	return UnitType::TANK;
	
	double score[5] = {};
	
	int enCnt = enemyCount[UnitType::HELICOPTER]*0.7 + enemyCount[UnitType::FIGHTER]*0.3;
	int myCnt = myCount[UnitType::FIGHTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::FIGHTER] += enCnt - myCnt;
	}
	score[(int) UnitType::FIGHTER] *= 0.6;
	
	enCnt = enemyCount[UnitType::TANK];
	myCnt = myCount[UnitType::HELICOPTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::HELICOPTER] += enCnt - myCnt;
	}
	
	score[(int) UnitType::HELICOPTER] *= 0.8;
	
	enCnt = enemyCount[UnitType::IFV];
	myCnt = myCount[UnitType::TANK];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::TANK] += enCnt - myCnt;
	}
	
	enCnt = enemyCount[UnitType::FIGHTER]*0.7 + enemyCount[UnitType::HELICOPTER]*0.3;
	myCnt = myCount[UnitType::IFV];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::IFV] += enCnt - myCnt;
	}
	
	
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE)
		{
			score[(int) b.unitType] -= 20;
		}
	}
	
	double grCount = 0;
	double airCount = 0;
	for (int i = 0; i < 5; ++i)
	{
		if (isGroundUnit((UnitType) i) && i != (int) UnitType::ARV)
		{
			grCount += myCount[(UnitType) i];
		}
		else
		{
			airCount += myCount[(UnitType) i];
		}
	}
	
	double totalCount = grCount + airCount;
	if (totalCount > 0)
	{
		for (int i = 0; i < 5; ++i)
		{
			if (isGroundUnit((UnitType) i))
			{
				score[i] *= airCount;
			}
			else
			{
				score[i] *= grCount;
			}
		}
	}
	
	int res = 0;
	int resType = -1;
	for (int i = 0; i < 5; ++i)
	{
		if (score[i] > res)
		{
			res = score[i];
			resType = i;
		}
	}
	
	if (resType >= 0)
		return (UnitType) resType;
	
	return UnitType::TANK;
}

MyMove Strat::calcNextMove()
{
	MyMove result;
	result.action = MyActionType::NONE;
	
	int moves = getAvailableActions(12);
	if (moves < 1)
		return result;
	
	if (players[1].remainingNuclearStrikeCooldownTicks < 30)
	{
		if (moves < 3)
			return result;
	}
	
	updateStats();
	
	if (players[1].nextNuclearStrikeTick >= 0)
	{
		bool select = false;
		bool expand = false;
		BBox bbox;
		int evaidingGroups = 0;
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep == 0)
			{
				BBox gbox = g.bbox;
				gbox.expand(40.0);
				if (gbox.inside(players[1].nuclearStrike))
				{
					bbox.add(g.bbox);
					g.nukeEvadeStep = 1;
					select = true;
					++evaidingGroups;
				}
			}
			else if (g.nukeEvadeStep == 1)
			{
				expand = true;
				g.nukeEvadeStep = 2;
				++evaidingGroups;
			}
			else
			{
				++evaidingGroups;
			}
		}
		
		if (select)
		{
			result.action = MyActionType::CLEAR_AND_SELECT;
			result.p1 = bbox.p1;
			result.p2 = bbox.p2;
			
			return result;
		}
		else if (expand)
		{
			result.action = MyActionType::SCALE;
			result.p = players[1].nuclearStrike;
			result.factor = 9.0;
			return result;
		}
		
		if (evaidingGroups && moves < 3)
			return result;
	}
	else
	{
		for (Group &g : groups)
		{
			if (tick - g.lastShrinkTick > 30)
				g.shrinkAfterNuke = false;
			if (g.nukeEvadeStep != 0)
			{
				g.shrinkAfterNuke = true;
				g.nukeEvadeStep = -1;
			}
		}
	}
	
	/*if (tick > 200) {
		int moves = getAvailableActions(4, 10);
		if (moves < 1) // throttle
			return result;
	}*/
	
	for (Building &b : buildings)
	{
		/*if (b.lastChangeUnitCount > b.unitCount)
			b.lastChangeUnitCount = b.unitCount;*/
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && (b.unitType == UnitType::NONE/* || (b.unitCount - b.lastChangeUnitCount) > 11*/))
		{
			//LOG("SVP " << buildingCaptured);
			result.action = MyActionType::SETUP_VEHICLE_PRODUCTION;
			result.facilityId = b.id;
			
			/*if (buildingCaptured % 2 == 0)
				result.unitType = UnitType::FIGHTER;
			else
				result.unitType = UnitType::IFV;*/
			
			//result.unitType = UnitType::TANK;
			result.unitType = calcNextUnitTypeForConstruction(false);
			//result.unitType = (UnitType) (buildingCaptured % 5);
			//b.lastChangeUnitCount = b.unitCount;
			
			++buildingCaptured;
			return result;
		}
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE && b.unitCount > 40 && b.productionProgress < 30 || b.createGroupStep > 0)
		{
			//LOG("MAKE GROUP");
			if (b.createGroupStep == 0 || b.createGroupStep == 1)
			{
				result.action = MyActionType::CLEAR_AND_SELECT;
				result.p1 = b.pos - P(32, 32);
				result.p2 = b.pos + P(32, 32);
				result.unitType = b.unitType;
				b.createGroupStep = 2;
				return result;
			}
			
			if (b.createGroupStep == 2)
			{
				// check if selected
				bool anySelected = false;
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.groups.count() == 0)
					{
						if (p.pos.x > b.pos.x - 32 && p.pos.x < b.pos.x + 32 && p.pos.y > b.pos.y - 32 && p.pos.y < b.pos.y + 32)
						{
							if (p.selected)
							{
								anySelected = true;
								break;
							}
						}
					}
				}
				
				if (anySelected)
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					b.createGroupStep = 3;
					
					Group newGroup;
					//newGroup.unitType = b.unitType;
					newGroup.group = result.group;
					newGroup.internalId = internalGroupSeq++;
					groups.push_back(newGroup);
					
					b.createGroupStep = 0;
					return result;
				}
				else
				{
					b.createGroupStep = 1;
				}
			}
		}
	}
	
		
	if (!initialGroupsGerationDone && !buildings.empty())
	{
		for (Group &g : groups)
		{
			/*if (g.unitType == UnitType::HELICOPTER && !g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					P allCenter = P(0, 0);
					int count = 0;
					for (const MyUnit &u : units)
					{
						if (u.side == 0 && u.type == g.unitType)
						{
							allCenter += u.pos;
							++count;
						}
					}
					
					allCenter /= count;
					
					if (g.miniGroupInd == 0)
					{
						result.p1 = P(0, 0);
						result.p2 = allCenter;
					}
					else if (g.miniGroupInd == 1)
					{
						result.p1 = P(allCenter.x, 0);
						result.p2 = P(WIDTH, allCenter.y);
					}
					else if (g.miniGroupInd == 2)
					{
						result.p1 = P(0, allCenter.y);
						result.p2 = P(allCenter.x, HEIGHT);
					}
					else if (g.miniGroupInd == 3)
					{
						result.p1 = allCenter;
						result.p2 = P(WIDTH, HEIGHT);
					}
					
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}*/
			
			if (!g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else if (g.enumGroupBuildStep == 1)
				{
					result.action = MyActionType::ASSIGN;
					g.enumGroupBuildStep++;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}
		}
		
		initialGroupsGerationDone = true;
		LOG("DONE");
	}
	
	bool angryMode = angryModeTill > tick;

	
	DistributionMatrix matr;
	matr.initialize(*this);
	matr.blur(distributionMatrix);
	
	/*if (tick > 532)
	{
		std::cout.width(2);
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &resCell = distributionMatrix.getCell(x, y);
				std::cout << " " << resCell.count[1];
			}
			std::cout << std::endl;
		}
		std::cout << std::endl;
	}*/
	
	if (players[0].remainingNuclearStrikeCooldownTicks == 0)
	{
		resetCells();
		calcNuclearEfficiency();
		
		auto getEnemySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0.0;
			
			return nuclearEfficiency[y * MICROCELLS_X + x];
		};
		
		auto getMySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0;
			
			return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
				+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
				+ (int) getMicrocell(x, y, 0, UnitType::ARV).size() / 2;
		};
		
		P bestTarget;
		int bestScore = 0;
		
		for (int y = 0; y < MICROCELLS_Y; ++y) {
			for (int x = 0; x < MICROCELLS_X; ++x) {
				double enemyN = getEnemySize(x, y);
				if (enemyN > 10)
				{
					int totalMyCount = 0;
					int myCenterCount = 0;
					for (int xx = -1; xx <= 1; ++xx)
					{
						for (int yy = -1; yy <= 1; ++yy)
						{
							totalMyCount += getMySize(x + xx, y + yy);
							if (xx == 0 && yy == 0)
								myCenterCount = totalMyCount;
						}
					}
					
					if (totalMyCount < 20 && myCenterCount == 0)
					{
						int totalMyExtCount = totalMyCount;
						if (totalMyExtCount == 0)
						{
							for (int xx = -2; xx <= 2; ++xx)
							{
								for (int yy = -2; yy <= 2; ++yy)
								{
									if (xx < -1 || xx > 1 || yy < -1 || yy > 1)
										totalMyExtCount += getMySize(x + xx, y + yy);
								}
							}
						}
						
						if (totalMyExtCount > 0)
						{
							double totalEnemyCount =
								enemyN +
								getEnemySize(x - 1, y)/2 +
								getEnemySize(x + 1, y)/2 + 
								getEnemySize(x, y - 1)/2 + 
								getEnemySize(x, y + 1)/2;
							
							if (bestScore < totalEnemyCount)
							{
								bestScore = totalEnemyCount;
								bestTarget = P(x + 0.5, y + 0.5) * MICROCELL_SIZE;
							}
						}
					}
				}
			}
		}
		
		if (bestScore > 0)
		{
			// Correct point
			P correctedBestTarget = bestTarget;
			
			std::vector<P> myVehicles;
			std::vector<P> enemyVehicles;
			int myInnerN = 0;
			int enemyInnerN = 0;
			for (const MyUnit &p : units)
			{
				double d2 = p.pos.dist2(bestTarget);
				if (d2 < (75.0*75.0))
				{
					if (d2 < (38.0*38.0))
					{
						if (p.side == 0)
							myInnerN++;
						else
							enemyInnerN++;
					}
					else
					{
						if (p.side == 0)
							myVehicles.push_back(p.pos);
						else
							enemyVehicles.push_back(p.pos);
					}
				}
			}
			
			int foundScore = -100000;
			for (int xx = -2; xx <= 2; ++xx)
			{
				for (int yy = -2; yy <= 2; ++yy)
				{
					P newTarget = bestTarget + P(xx, yy) * 6.0;
					
					int myNum = myInnerN*3;
					int myExtNum = 0;
					
					for (const P &p : myVehicles)
					{
						double d2 = p.dist2(newTarget);
						if (d2 < (50.0*50.0))
						{
							myNum++;
						}
						else if (d2 > (55.0*55.0) && d2 < (70.0*70.0))
						{
							myExtNum++;
						}
					}
					
					if (myExtNum > 0)
					{
						int enNum = enemyInnerN*3;
						for (const P &p : enemyVehicles)
						{
							double d2 = p.dist2(newTarget);
							if (d2 < (50.0*50.0))
							{
								enNum++;
							}
						}
						
						int score = enNum - myNum * 4;
						if (score > foundScore)
						{
							foundScore = score;
							correctedBestTarget = newTarget;
						}
					}
				}
			}
			
			if (foundScore > -100000)
			{
				auto getMySize = [this](int x, int y) {
					if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
						return 0;
					
					return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
						+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
						+ (int) getMicrocell(x, y, 0, UnitType::ARV).size();
				};
				
				int pts = 0;
				long bestId = -1;
				P vehPos;
				
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.durability > 70)
					{
						double dist2 = p.pos.dist2(correctedBestTarget);
						if (dist2 > 55*55 && dist2 < 70*70)
						{
							bool outOfRange = false;
							
							if (p.vel.len2() > 0.01)
							{
								MyUnit u = p;
								for (int i = 0; i < 30; ++i)
								{
									double visRange = getVisionRange(u) - unitVel(p.type) * 10;
									if (u.pos.dist2(correctedBestTarget) > sqr(visRange))
									{
										outOfRange = true;
										break;
									}
									u.pos += u.vel;
								}
							}
							
							if (outOfRange)
								continue;
							
							int xx = p.pos.x / MICROCELL_SIZE;
							int yy = p.pos.y / MICROCELL_SIZE;
							
							//int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 60.0);
							if (pts < newPts)
							{
								pts = newPts;
								bestId = p.id;
								vehPos = p.pos;
							}
						}
					}
				}
				
				if (bestId >= 0)
				{
					result.action = MyActionType::TACTICAL_NUCLEAR_STRIKE;
					result.vehicleId = bestId;
					result.p = correctedBestTarget;
					return result;
				}
			}
		}
	}
	
	groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group &g){return g.size == 0;}), groups.end());
	
	if (result.action == MyActionType::NONE && !groups.empty())
	{
		debugAttractionPoints.clear();
		
		std::sort(groups.begin(), groups.end(), [](const Group &g1, const Group &g2){
			double p1 = groupPriority(g1);
			double p2 = groupPriority(g2);
			return p2 < p1;
		});
		
		assignBuildings();
		
		for (Group &group : groups)
		{
			if (tick - group.lastUpdateTick < 10 || group.nukeEvadeStep > 0)
				continue;
			
			if (group.shrinkActive)
			{
				if (tick - group.lastShrinkTick > 40 || !anyMoved(group))
				{
					group.shrinkActive = false;
				}
				else
				{
					continue;
				}
			}
			
			bool limitSpeed = false;
			if (nukeVehicleInd >= 0)
			{
				const MyUnit &u = units[nukeVehicleInd];
				if (group.check(u))
					limitSpeed = true;
			}
			
			calcDangerDistCells(group);
			
			P center = group.center;
			const BBox &bbox = group.bbox;
			int groupSize = group.size;
			double area = bbox.area();
			bool shrinkRequired = (area > groupSize * 40.0 || group.shrinkAfterNuke) && (((tick - group.lastUpdateTick) > 60 || group.shrinkAfterNuke) && (tick - group.lastShrinkTick) > 300);
			P shrinkPoint;
			if (shrinkRequired)
			{
				ShrinkResult shRes = findShrink(group);
				if (shRes.ticks > 7)
				{
					shrinkPoint = shRes.shrinkPoint;
				}
				else
				{
					shrinkRequired = false;
				}
				//LOG("SHRINK RES " << shRes.ticks << " " << shRes.endBBox << " P " << shRes.shrinkPoint);
			}
			//bool shrinkRequired = false;
			
			const double border = 20.0;
			P c = center;
			double R = 20 + unitVel(group.unitType) * 40;
			/*if (group.unitType == UnitType::ARV)
				R = 200.0;*/
			double ticks = R / unitVel(group.unitType);
			double curVal = attractionPoint(center, group, ticks, angryMode);
			P tp = center;
			bool found = false;
			bool canMoveFlag = false;
			
			P clampP1 = group.center - bbox.p1 + P(3.0, 3.0);
			P clampP2 = P(WIDTH - 3.0, HEIGHT - 3.0) + (group.center - bbox.p2);
			for (int k = 0; k < 3.0; ++k)
			{
				ticks = R / unitVel(group.unitType);
				int di = -1;
				
				for (int i = 0; i < 20; ++i)
				{
					P p = c + P(PI * 2.0 / 20.0 * i) * R;
					p = clampP(p, clampP1, clampP2);
					
					P shift = p - center;
					if (shift.len2() > 0.01)
					{
						if (canMove(p - center, group))
						{
							canMoveFlag = true;
							double val = attractionPoint(p, group, ticks, angryMode);
							
							DebugAttractionPointsInfo debugInfo;
							debugInfo.point = c;
							debugInfo.dir = p - c;
							debugInfo.val = val;
							debugAttractionPoints.push_back(debugInfo);
							
							if (val > curVal)
							{
								curVal = val;
								di = i;
								tp = p;
								found = true;
							}
						}
					}
				}
				
				if (di >= 0)
				{
					c = tp;
				}
				
				R /= 1.5;
			}
			
			bool moveAway = false;
			P dirAway;
			P newShift = P(0, 0);
			if (!canMoveFlag)
			{
				dirAway = P(0, 0);
				for (Group &othGroup : groups)
				{
					if (&group != &othGroup && isGroundUnit(group.unitType) == isGroundUnit(othGroup.unitType))
					{
						P dp = group.center - othGroup.center;
						double clen = dp.len();
						
						if (clen < 150.0)
							dirAway += dp / clen;
					}
				}
				
				dirAway *= 10;
				dirAway += P(tick % 7 - 3, tick % 9 - 5);
				moveAway = true;
			}
			else
			{
				if (found)
					newShift = tp - center;
				
				if (newShift.len() < 15.0)
				{
					calcMicroShift(group, newShift);
				}
			}
			
			found = newShift.len2() > 0.1;
			
			if (found || shrinkRequired || moveAway)
			{
				if (!isSelected(group))
				{
					result = select(group);
					group.actionStarted = true;
				}
				else
				{
					if (shrinkRequired)
					{
						result.action = MyActionType::SCALE;
						result.factor = 0.2;
						result.p = shrinkPoint;
						group.lastShrinkTick = tick;
						group.shrinkActive = true;
						group.nukeEvadeStep = 0;
					}
					else if (found)
					{
						result.action = MyActionType::MOVE;
						result.p = newShift;
						
						if (limitSpeed)
							result.maxSpeed = unitVel(group.unitType) * 0.6;
						
						/*if (!limitSpeed)
						{
							limitSpeed = !anyEnemiesNearbyByDangerDistr(group);
							if (limitSpeed)
								result.maxSpeed = unitVel(group.unitType) * 0.8;
						}*/
						
						/*if (group.unitType == UnitType::FIGHTER)
							result.maxSpeed = 1.0;*/
					}
					else
					{
						result.action = MyActionType::MOVE;
						result.p = dirAway;
						//std::cout << "AWAY " << dirAway.x << " " << dirAway.y << std::endl;
					}
					
					group.lastUpdateTick = tick;
					group.actionStarted = false;
				}
				
				break;
			}
			else if (!found)
			{
				group.lastUpdateTick = tick;
			}
		}
	}
	
	return result;
}

void Strat::assignBuildings()
{
	std::set<Building *> pbuildings;
	
	for (Building &b : buildings)
	{
		if (b.side != 0)
		{
			pbuildings.insert(&b);
			b.assignedGroup = 0;
		}
	}
	
	std::set<Group *> pgroups;
	for (Group &g : groups)
	{
		if (isGroundUnit(g.unitType))
		{
			pgroups.insert(&g);
		}
	}
	
	size_t count = std::min(pbuildings.size(), pgroups.size());
	for (int i = 0; i < count; ++i)
	{
		double dist2 = sqr(100000.0);
		std::set<Building *>::iterator b = pbuildings.end();
		std::set<Group *>::iterator g = pgroups.end();
		for (std::set<Building *>::iterator bit = pbuildings.begin(); bit != pbuildings.end(); ++bit)
		{
			for (std::set<Group *>::iterator git = pgroups.begin(); git != pgroups.end(); ++git)
			{
				double d2 = (*bit)->pos.dist2((*git)->center);
				if (d2 < dist2)
				{
					dist2 = d2;
					b = bit;
					g = git;
				}
			}
		}
		
		if (b != pbuildings.end() && g != pgroups.end())
		{
			(*b)->assignedGroup = (*g)->internalId;
			pbuildings.erase(b);
			pgroups.erase(g);
		}
	}
}

bool Strat::anyEnemiesNearbyByDangerDistr(const Group &group)
{
	int x = group.center.x / DISTR_MAT_CELL_SIZE;
	int y = group.center.y / DISTR_MAT_CELL_SIZE;
	for (int yy = -7; yy <= 7; ++yy)
	{
		for (int xx = -7; xx <= 7; ++xx)
		{
			if (xx * xx + yy * yy <= 50)
			{
				int x2 = x + xx;
				int y2 = y + yy;
				if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
				{
					DangerDistCell &dCell = dangerDistCells[y2 * DISTR_MAT_CELLS_X + x2];
					if (dCell.totalEnemyDamage > 0.0 || dCell.totalMyDamage > 0.0)
					{
						return true;
					}
				}
			}
		}
	}
	
	return false;
}

void Strat::calcDangerDistCells(const Group &group)
{
	dngGr = &group;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
			double totalMyDamage = 0;
			
			for (int myType = 0; myType < 5; ++myType)
			{
				double typeDamage = 0;
				if (group.sizeByTypes[myType])
				{
					for (int enemyType = 0; enemyType < 5; ++enemyType)
					{
						if (cell.count[enemyType])
						{
							double dmg = getDamage((UnitType) myType, (UnitType) enemyType);
							typeDamage = std::max(typeDamage, std::min(dmg * group.sizeByTypes[myType], 120.0 * cell.count[enemyType]));
						}
					}
				}
				totalMyDamage += typeDamage;
			}
			
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			double f2hDmg = 0.0;
			dCell.totalEnemyDamage = 0.0;
			dCell.totalEnemyHealth = 0.0;
			for (int enemyType = 0; enemyType < 5; ++enemyType)
			{
				dCell.enemyDamage[enemyType] = 0.0;
				dCell.enemyHealth[enemyType] = 0.0;
				
				if (cell.count[enemyType])
				{
					dCell.enemyHealth[enemyType] += cell.health[enemyType];
					for (int myType = 0; myType < 5; ++myType)
					{
						if (group.sizeByTypes[myType])
						{
							double dmg = getDamage((UnitType) enemyType, (UnitType) myType);
							dCell.enemyDamage[enemyType] = std::min(dmg * cell.count[enemyType], 120.0 * group.sizeByTypes[myType]);
							
							if (enemyType == (int) UnitType::FIGHTER && myType == (int) UnitType::HELICOPTER)
								f2hDmg = dCell.enemyDamage[enemyType];
						}
					}
				}
				
				dCell.enemyDamage[enemyType] *= 1.5;
				dCell.totalEnemyDamage += dCell.enemyDamage[enemyType];
				dCell.totalEnemyHealth += dCell.enemyHealth[enemyType];
			}
			
			dCell.totalMyDamage = totalMyDamage;
			dCell.f2hDmg = f2hDmg;
		}
	}
}

struct DangerDist {
	double dists[25];
	
	DangerDist() {
		dist(UnitType::ARV, UnitType::ARV) = 150.0;
		dist(UnitType::ARV, UnitType::FIGHTER) = 150.0;
		dist(UnitType::ARV, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::ARV, UnitType::IFV) = 150.0;
		dist(UnitType::ARV, UnitType::TANK) = 150.0;
		
		dist(UnitType::FIGHTER, UnitType::ARV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::IFV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::HELICOPTER, UnitType::ARV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::IFV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::IFV, UnitType::ARV) = 150.0;
		dist(UnitType::IFV, UnitType::FIGHTER) = 100.0;
		dist(UnitType::IFV, UnitType::HELICOPTER) = 100.0;
		dist(UnitType::IFV, UnitType::IFV) = 150.0;
		dist(UnitType::IFV, UnitType::TANK) = 150.0;
		
		dist(UnitType::TANK, UnitType::ARV) = 150.0;
		dist(UnitType::TANK, UnitType::FIGHTER) = 1.0;
		dist(UnitType::TANK, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::TANK, UnitType::IFV) = 150.0;
		dist(UnitType::TANK, UnitType::TANK) = 150.0;
		
		for (int i = 0; i < 25; ++i)
			dists[i] = sqr(dists[i]);
	}
	
	double &dist(UnitType from, UnitType to) {
		return dists[(int) from * 5 + (int) to];
	}
} DANGER_DISTS;


bool isClosedSpaceDanger(const P &myP, const P &enP, double myVel, double enemyVel, double ticks)
{
	double myR = myVel * ticks + 0.1;
	double dangerRad = 70.0;
	double enR = enemyVel * ticks + dangerRad;
	double d = myP.dist(enP);
	
	if (myR + d < enR)
		return true;
	
	const double borderDist = 40.0;
	if (myP.x > (borderDist + myR) && myP.x < (WIDTH - borderDist - myR) && myP.y > (borderDist + myR) && myP.y < (HEIGHT - borderDist - myR))
		return false;
	
	P myPn = myP;
	P enPn = enP;
	if (myPn.x > WIDTH / 2.0)
	{
		myPn.x = WIDTH - myPn.x;
		enPn.x = WIDTH - enPn.x;
	}
	
	if (myPn.y > HEIGHT / 2.0)
	{
		myPn.y = HEIGHT - myPn.y;
		enPn.y = HEIGHT - enPn.y;
	}
	
	if (myPn.x < myPn.y)
	{
		std::swap(myPn.x, myPn.y);
		std::swap(enPn.x, enPn.y);
	}
	
	double borderDistX = std::min(borderDist, myPn.x);
	double borderDistY = std::min(borderDist, myPn.y);
	
	double b = sqrt(sqr(myR) - sqr(myPn.y - borderDistY));
	double X = myPn.x + b;
	
	if (P(X, borderDistY).dist2(enPn) > sqr(enR))
		return false;
	
	if (myR > (myPn.x - borderDistX))
	{
		double Y = myPn.y + sqrt(sqr(myR) - sqr(myPn.x - borderDistX));
		
		if (P(borderDistX, Y).dist2(enPn) > sqr(enR))
			return false;
	}
	else
	{
		X = myPn.x - b;
	
		if (P(X, borderDistY).dist2(enPn) > sqr(enR))
			return false;
	}
	
	return true;
}

double captureTick(const P &myP, const P &enP, double myVel, double enemyVel)
{
	double dT = 125.0;
	double oldT = 0.0;
	for (double t = 0.0; t <= 625;)
	{
		if (!isClosedSpaceDanger(myP, enP, myVel, enemyVel, t))
		{
			oldT = t;
			t += dT;
		}
		else
		{
			if (dT <= 1.0)
				return t;
			
			t = oldT;
			dT /= 5.0;
			t += dT;
		}
	}
	
	return 625;
}

double Strat::attractionPoint(const P from, const Group &group, double ticks, bool angryMode)
{
	double res = 0.0;
	double f2hDmgRes = 0.0;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			if (dCell.totalMyDamage > 0.0 || dCell.totalEnemyDamage > 0.0)
			{
				double alpha = 0.3;
				double alphaM1 = 0.7;
				
				P p = P((x + 0.5) * DISTR_MAT_CELL_SIZE, (y + 0.5) * DISTR_MAT_CELL_SIZE);
				
				double dist2 = p.dist2(from);
				
				double pts = (group.health * alphaM1 + dCell.totalEnemyHealth * alpha) / (dCell.totalEnemyHealth*0.01 + dCell.totalEnemyDamage) 
				- (dCell.totalEnemyHealth * alphaM1 + group.health * alpha) / (group.health * 0.01 + dCell.totalMyDamage);
				
				if (pts != 0.0)
				{
					double enemyVel = 0.0;
					
					for (int i = 0; i < 5; ++i) 
					{ 
						if (dCell.enemyHealth[i]) 
							enemyVel += unitVel((UnitType) i) * (dCell.enemyHealth[i] / dCell.totalEnemyHealth); 
					}
					
					if (pts < 0.0)
					{
						double t = captureTick(from, p, unitVel(group.unitType), enemyVel);
						res += pts * (625 - t) / 625.0;
					}
					else
					{
						/*double t = captureTick(p, from, enemyVel, unitVel(group.unitType));
						res += 0.1* pts * (625 - t) / 625.0;*/
					}
				}
				
				if (pts > 0.0)
				{
					double pp = 1.0/(1.0 + dist2);
					res += pts * pp;
				}
				else
				{
					/*for (int mt = 0; mt < 5; ++mt)
					{
						if (group.healthByTypes[mt])
						{
							for (int et = 0; et < 5; ++et)
							{
								if (dCell.enemyHealth[et])
								{
									double rad2 = DANGER_DISTS.dist((UnitType) et, (UnitType) mt);
									//double rad2 = 150*150;
									if (rad2 > dist2)
									{
										double fraction = group.healthByTypes[mt] / group.health * dCell.enemyHealth[et] / dCell.totalEnemyHealth;
										double pn = (1.0 - std::min(1.0, dist2/rad2));
										res += pts * pn * fraction;
									}
								}
							}
						}
					}*/
					
					double pn = (1.0 - std::min(1.0, dist2/sqr(150)));
					res += pts * pn;
				}
				
				//res += pts / p.dist2(from);
			}
			
			f2hDmgRes += dCell.f2hDmg;
		}
	}
	
	/*if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		double L = 1.5 * WIDTH;
		if (f2hDmgRes > 0.0 && res < 0.0)
		{
			if (fivG && fivG->size > 10)
			{
				L = fivG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::IFV];
		
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 300.0)
			{
				res -= (l - 300.0);
			}
		}
	}*/
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::HELICOPTER);
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 400.0)
			{
				res -= (l - 400.0);
			}
		}
	}*/
	
	if (isGroundUnit(group.unitType))
	{
		for (const Building &b : buildings)
		{
			if (b.side != 0)
			{
				double d = from.dist(b.pos);
				double coef = b.assignedGroup == group.internalId ? 2.0 : 1.0;
				res += coef*group.health/(20 + d)*0.1;
			}
		}
	}
	
	return res;
}


ShrinkResult Strat::findShrink(Group &group)
{
	ShrinkResult result;
	
	Simulator sim;
	sim.tick = tick;
	std::copy(cells, cells + CELLS_X * CELLS_Y, sim.cells);
	
	BBox gbox = group.bbox;
	gbox.expand(2.0);
	for (const MyUnit &u : units)
	{
		if (gbox.inside(u.pos))
		{
			sim.units.push_back(u);
			MyUnit &tu = *sim.units.rbegin();
			tu.selected = group.check(tu);
			tu.vel = P(0, 0);
		}
	}
	sim.groups.push_back(group);
	
	double bestArea = WIDTH * HEIGHT;
	for (double x = -1; x <= 1; ++x)
	{
		for (double y = -1; y <= 1; ++y)
		{
			Simulator sim2 = sim;
			MyMove myMove;
			myMove.action = MyActionType::SCALE;
			myMove.p = group.center + P(x, y) * 10.0;
			myMove.factor = 0.2;
			sim2.registerMove(myMove, 0);
			
			int i = 0;
			for (; i < 20; ++i)
			{
				sim2.tick++;
				sim2.resetAxisSorts();
				sim2.applyMoves();
				int cnt = sim2.moveUnits();
				if (!cnt)
					break;
			}
			sim2.updateStats();
			
			double area = sim2.groups.rbegin()->bbox.area();
			if (area < bestArea)
			{
				bestArea = area;
				result.shrinkPoint = myMove.p;
				result.ticks = i;
				result.endBBox = sim2.groups.rbegin()->bbox;
			}
		}
	}
	
	return result;
}
}

namespace StratV18 {
	double groupPriority(const Group &g) {
	double res = 0.0;
	
	if (g.shrinkAfterNuke)
		res += 1100.0;
	else if (g.actionStarted)
		res += 1000.0;
	
	res -= g.lastUpdateTick;
	
	return res;
}

void DistributionMatrix::clear()
{
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		Cell &cell = cells[i];
		for (int j = 0; j < 5; ++j)
			cell.count[j] = 0;
		for (int j = 0; j < 5; ++j)
			cell.health[j] = 0;
	}
}

void DistributionMatrix::initialize(const Simulator &sim)
{
	clear();
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : sim.units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / DISTR_MAT_CELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / DISTR_MAT_CELL_SIZE;
				
				Cell &cell = getCell(x, y);
				int type = (int) u.type;
				cell.count[type] += 1.0 / K;
				cell.health[type] += u.durability / K;
			}
		}
	}
}

void DistributionMatrix::blur(DistributionMatrix &oth) const
{
	oth.clear();
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			Cell &resCell = oth.getCell(x, y);
			int cnt = 0;
			for (int yy = -1; yy <= 1; ++yy)
			{
				for (int xx = -1; xx <= 1; ++xx)
				{
					int x2 = x + xx;
					int y2 = y + yy;
					if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
					{
						++cnt;
						const Cell &cell = getCell(x2, y2);
						for (int i = 0; i < 5; ++i)
							resCell.count[i] += cell.count[i];
						for (int i = 0; i < 5; ++i)
							resCell.health[i] += cell.health[i];
					}
				}
			}
			
			const Cell &cell = getCell(x, y);
			for (int i = 0; i < 5; ++i)
			{
				if (!cell.count[i])
				{
					resCell.count[i] = 0;
					resCell.health[i] = 0;
				}
			}
			
			/*for (int i = 0; i < 5; ++i)
				resCell.count[i] += (resCell.count[i] + cnt - 1) / cnt;
			for (int i = 0; i < 5; ++i)
				resCell.health[i] /= cnt;*/
		}
	}
}

Strat::Strat::Strat()
{
	Group g;
	g.actionStarted = false;
	g.lastUpdateTick = 0;
	g.lastShrinkTick = 0;
	
	for (int i = 0; i < 1; ++i)
	{
		g.unitType = UnitType::HELICOPTER;
		//g.miniGroupInd = i;
		g.internalId = internalGroupSeq++;
		groups.push_back(g);
	}
	g.miniGroupInd = 0;
	
	g.unitType = UnitType::IFV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::FIGHTER;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::TANK;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::ARV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
}


void Strat::calcMicroShift(Group &group, P &shift)
{
	if (group.unitType != UnitType::TANK && group.unitType != UnitType::FIGHTER)
		return;
	
	BBox bbox = group.bbox;
	bbox.p1 += shift - P(35, 35);
	bbox.p2 += shift + P(35, 35);
	
	int finalScore = -100000;
	int currentScore = -100000;
	double ticks = 20.0;
	
	//if (group.unitType == UnitType::TANK)
	{
		std::vector<P> enemies[(int) UnitType::COUNT];
		std::vector<P> myUnits;
		
		for (const MyUnit &u : units)
		{
			if (u.side > 0 && (u.type == UnitType::TANK || u.type == UnitType::HELICOPTER || u.type == UnitType::IFV) && bbox.inside(u.pos))
			{
				enemies[(int) u.type].push_back(u.pos + u.vel * ticks);
			}
			else if (group.check(u))
			{
				myUnits.push_back(u.pos);
			}
		}
		
		double L = 8.0;
		P newShift = shift;
		for (int k = 0; k < 4; ++k)
		{
			P foundShift = newShift;
			int foundScore = -100000;
			for (int i = 0; i < 9; ++i)
			{
				P testShift = newShift + P(i / 3 - 1, i % 3 - 1) * L;
				int score = 0;
				bool isCurShift = shift.dist2(testShift) < 0.1;
				
				if (!isCurShift || currentScore == -100000)
				{
					const std::vector<MicroShiftValues> &pos = microShiftMatrix.pos[(int) group.unitType];
					if (!pos.empty())
					{
						for (const P &myP : myUnits)
						{
							P myPos = myP + testShift;
							bool found = false;
							for (const MicroShiftValues &vals : pos)
							{
								if (!found)
								{
									for (const P &enP : enemies[(int) vals.unitType])
									{
										if (myPos.dist2(enP) < vals.dist2)
										{
											score += vals.val;
											found = true;
											break;
										}
									}
								}
							}
						}
					}
					
					// !! decrease SCORE
					int posScore = score;
					score /= 2;
					
					const std::vector<MicroShiftValues> &neg = microShiftMatrix.neg[(int) group.unitType];
					if (!neg.empty())
					{
						for (const MicroShiftValues &vals : neg)
						{
							for (const P &enP : enemies[(int) vals.unitType])
							{
								for (const P &myP : myUnits)
								{
									P myPos = myP + testShift;
									if (myPos.dist2(enP) < vals.dist2)
									{
										score -= vals.val;
										break;
									}
								}
							}
						}
					}
					
					int negScore = posScore / 2 - score;
					
					if (foundScore < score)
					{
						foundShift = testShift;
						foundScore = score;
					}
					
					if (isCurShift)
						currentScore = score;
				}
			}
			
			L /= 2.0;
			newShift = foundShift;
			
			finalScore = foundScore;
		}
		
		if (currentScore >= finalScore)
			return;
		
		shift = newShift;
	}
}

void Strat::calcNuclearEfficiency()
{
	for (int i = 0; i < MICROCELLS_X * MICROCELLS_Y; i++)
		nuclearEfficiency[i] = 0;
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / MICROCELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / MICROCELL_SIZE;
				
				double efficiency = 100.0 / (10.0 + u.durability) / K;
				
				if (u.type == UnitType::ARV)
					efficiency /= 10.0;
				
				if (u.type == UnitType::FIGHTER || u.type == UnitType::HELICOPTER)
					efficiency /= 1.5;
				
				nuclearEfficiency[y * MICROCELLS_X + x] += efficiency;
			}
		}
	}
}

UnitType Strat::calcNextUnitTypeForConstruction(bool ground)
{
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType == UnitType::HELICOPTER)
		{
			return UnitType::TANK;
		}
	}
	
	if (enemyCount[UnitType::HELICOPTER] * 0.9 > myCount[UnitType::HELICOPTER])
	{
		return UnitType::HELICOPTER;
	}
	
	return UnitType::TANK;
	
	double score[5] = {};
	
	int enCnt = enemyCount[UnitType::HELICOPTER]*0.7 + enemyCount[UnitType::FIGHTER]*0.3;
	int myCnt = myCount[UnitType::FIGHTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::FIGHTER] += enCnt - myCnt;
	}
	score[(int) UnitType::FIGHTER] *= 0.6;
	
	enCnt = enemyCount[UnitType::TANK];
	myCnt = myCount[UnitType::HELICOPTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::HELICOPTER] += enCnt - myCnt;
	}
	
	score[(int) UnitType::HELICOPTER] *= 0.8;
	
	enCnt = enemyCount[UnitType::IFV];
	myCnt = myCount[UnitType::TANK];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::TANK] += enCnt - myCnt;
	}
	
	enCnt = enemyCount[UnitType::FIGHTER]*0.7 + enemyCount[UnitType::HELICOPTER]*0.3;
	myCnt = myCount[UnitType::IFV];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::IFV] += enCnt - myCnt;
	}
	
	
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE)
		{
			score[(int) b.unitType] -= 20;
		}
	}
	
	double grCount = 0;
	double airCount = 0;
	for (int i = 0; i < 5; ++i)
	{
		if (isGroundUnit((UnitType) i) && i != (int) UnitType::ARV)
		{
			grCount += myCount[(UnitType) i];
		}
		else
		{
			airCount += myCount[(UnitType) i];
		}
	}
	
	double totalCount = grCount + airCount;
	if (totalCount > 0)
	{
		for (int i = 0; i < 5; ++i)
		{
			if (isGroundUnit((UnitType) i))
			{
				score[i] *= airCount;
			}
			else
			{
				score[i] *= grCount;
			}
		}
	}
	
	int res = 0;
	int resType = -1;
	for (int i = 0; i < 5; ++i)
	{
		if (score[i] > res)
		{
			res = score[i];
			resType = i;
		}
	}
	
	if (resType >= 0)
		return (UnitType) resType;
	
	return UnitType::TANK;
}

MyMove Strat::calcNextMove()
{
	MyMove result;
	result.action = MyActionType::NONE;
	
	int moves = getAvailableActions(12);
	if (moves < 1)
		return result;
	
	if (players[1].remainingNuclearStrikeCooldownTicks < 30)
	{
		if (moves < 3)
			return result;
	}
	
	updateStats();
	
	if (players[1].nextNuclearStrikeTick >= 0)
	{
		bool select = false;
		bool expand = false;
		BBox bbox;
		int evaidingGroups = 0;
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep == 0)
			{
				BBox gbox = g.bbox;
				gbox.expand(40.0);
				if (gbox.inside(players[1].nuclearStrike))
				{
					bbox.add(g.bbox);
					g.nukeEvadeStep = 1;
					select = true;
					++evaidingGroups;
				}
			}
			else if (g.nukeEvadeStep == 1)
			{
				expand = true;
				g.nukeEvadeStep = 2;
				++evaidingGroups;
			}
			else
			{
				++evaidingGroups;
			}
		}
		
		if (select)
		{
			result.action = MyActionType::CLEAR_AND_SELECT;
			result.p1 = bbox.p1;
			result.p2 = bbox.p2;
			
			return result;
		}
		else if (expand)
		{
			result.action = MyActionType::SCALE;
			result.p = players[1].nuclearStrike;
			result.factor = 9.0;
			return result;
		}
		
		if (evaidingGroups && moves < 3)
			return result;
	}
	else
	{
		for (Group &g : groups)
		{
			if (tick - g.lastShrinkTick > 30)
				g.shrinkAfterNuke = false;
			if (g.nukeEvadeStep != 0)
			{
				g.shrinkAfterNuke = true;
				g.nukeEvadeStep = -1;
			}
		}
	}
	
	/*if (tick > 200) {
		int moves = getAvailableActions(4, 10);
		if (moves < 1) // throttle
			return result;
	}*/
	
	for (Building &b : buildings)
	{
		/*if (b.lastChangeUnitCount > b.unitCount)
			b.lastChangeUnitCount = b.unitCount;*/
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && (b.unitType == UnitType::NONE/* || (b.unitCount - b.lastChangeUnitCount) > 11*/))
		{
			//LOG("SVP " << buildingCaptured);
			result.action = MyActionType::SETUP_VEHICLE_PRODUCTION;
			result.facilityId = b.id;
			
			/*if (buildingCaptured % 2 == 0)
				result.unitType = UnitType::FIGHTER;
			else
				result.unitType = UnitType::IFV;*/
			
			//result.unitType = UnitType::TANK;
			result.unitType = calcNextUnitTypeForConstruction(false);
			//result.unitType = (UnitType) (buildingCaptured % 5);
			//b.lastChangeUnitCount = b.unitCount;
			
			++buildingCaptured;
			return result;
		}
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE && b.unitCount > 40 && b.productionProgress < 30 || b.createGroupStep > 0)
		{
			//LOG("MAKE GROUP");
			if (b.createGroupStep == 0 || b.createGroupStep == 1)
			{
				result.action = MyActionType::CLEAR_AND_SELECT;
				result.p1 = b.pos - P(32, 32);
				result.p2 = b.pos + P(32, 32);
				result.unitType = b.unitType;
				b.createGroupStep = 2;
				return result;
			}
			
			if (b.createGroupStep == 2)
			{
				// check if selected
				bool anySelected = false;
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.groups.count() == 0)
					{
						if (p.pos.x > b.pos.x - 32 && p.pos.x < b.pos.x + 32 && p.pos.y > b.pos.y - 32 && p.pos.y < b.pos.y + 32)
						{
							if (p.selected)
							{
								anySelected = true;
								break;
							}
						}
					}
				}
				
				if (anySelected)
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					b.createGroupStep = 3;
					
					Group newGroup;
					//newGroup.unitType = b.unitType;
					newGroup.group = result.group;
					newGroup.internalId = internalGroupSeq++;
					groups.push_back(newGroup);
					
					b.createGroupStep = 0;
					return result;
				}
				else
				{
					b.createGroupStep = 1;
				}
			}
		}
	}
	
		
	if (!initialGroupsGerationDone && !buildings.empty())
	{
		for (Group &g : groups)
		{
			/*if (g.unitType == UnitType::HELICOPTER && !g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					P allCenter = P(0, 0);
					int count = 0;
					for (const MyUnit &u : units)
					{
						if (u.side == 0 && u.type == g.unitType)
						{
							allCenter += u.pos;
							++count;
						}
					}
					
					allCenter /= count;
					
					if (g.miniGroupInd == 0)
					{
						result.p1 = P(0, 0);
						result.p2 = allCenter;
					}
					else if (g.miniGroupInd == 1)
					{
						result.p1 = P(allCenter.x, 0);
						result.p2 = P(WIDTH, allCenter.y);
					}
					else if (g.miniGroupInd == 2)
					{
						result.p1 = P(0, allCenter.y);
						result.p2 = P(allCenter.x, HEIGHT);
					}
					else if (g.miniGroupInd == 3)
					{
						result.p1 = allCenter;
						result.p2 = P(WIDTH, HEIGHT);
					}
					
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}*/
			
			if (!g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else if (g.enumGroupBuildStep == 1)
				{
					result.action = MyActionType::ASSIGN;
					g.enumGroupBuildStep++;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}
		}
		
		initialGroupsGerationDone = true;
		LOG("DONE");
	}
	
	bool angryMode = angryModeTill > tick;

	
	DistributionMatrix matr;
	matr.initialize(*this);
	matr.blur(distributionMatrix);
	
	/*if (tick > 532)
	{
		std::cout.width(2);
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &resCell = distributionMatrix.getCell(x, y);
				std::cout << " " << resCell.count[1];
			}
			std::cout << std::endl;
		}
		std::cout << std::endl;
	}*/
	
	if (players[0].remainingNuclearStrikeCooldownTicks == 0)
	{
		resetCells();
		calcNuclearEfficiency();
		
		auto getEnemySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0.0;
			
			return nuclearEfficiency[y * MICROCELLS_X + x];
		};
		
		auto getMySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0;
			
			return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
				+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
				+ (int) getMicrocell(x, y, 0, UnitType::ARV).size() / 2;
		};
		
		P bestTarget;
		int bestScore = 0;
		
		for (int y = 0; y < MICROCELLS_Y; ++y) {
			for (int x = 0; x < MICROCELLS_X; ++x) {
				double enemyN = getEnemySize(x, y);
				if (enemyN > 10)
				{
					int totalMyCount = 0;
					int myCenterCount = 0;
					for (int xx = -1; xx <= 1; ++xx)
					{
						for (int yy = -1; yy <= 1; ++yy)
						{
							totalMyCount += getMySize(x + xx, y + yy);
							if (xx == 0 && yy == 0)
								myCenterCount = totalMyCount;
						}
					}
					
					if (totalMyCount < 20 && myCenterCount == 0)
					{
						int totalMyExtCount = totalMyCount;
						if (totalMyExtCount == 0)
						{
							for (int xx = -2; xx <= 2; ++xx)
							{
								for (int yy = -2; yy <= 2; ++yy)
								{
									if (xx < -1 || xx > 1 || yy < -1 || yy > 1)
										totalMyExtCount += getMySize(x + xx, y + yy);
								}
							}
						}
						
						if (totalMyExtCount > 0)
						{
							double totalEnemyCount =
								enemyN +
								getEnemySize(x - 1, y)/2 +
								getEnemySize(x + 1, y)/2 + 
								getEnemySize(x, y - 1)/2 + 
								getEnemySize(x, y + 1)/2;
							
							if (bestScore < totalEnemyCount)
							{
								bestScore = totalEnemyCount;
								bestTarget = P(x + 0.5, y + 0.5) * MICROCELL_SIZE;
							}
						}
					}
				}
			}
		}
		
		if (bestScore > 0)
		{
			// Correct point
			P correctedBestTarget = bestTarget;
			
			std::vector<P> myVehicles;
			std::vector<P> enemyVehicles;
			int myInnerN = 0;
			int enemyInnerN = 0;
			for (const MyUnit &p : units)
			{
				double d2 = p.pos.dist2(bestTarget);
				if (d2 < (75.0*75.0))
				{
					if (d2 < (38.0*38.0))
					{
						if (p.side == 0)
							myInnerN++;
						else
							enemyInnerN++;
					}
					else
					{
						if (p.side == 0)
							myVehicles.push_back(p.pos);
						else
							enemyVehicles.push_back(p.pos);
					}
				}
			}
			
			int foundScore = -100000;
			for (int xx = -2; xx <= 2; ++xx)
			{
				for (int yy = -2; yy <= 2; ++yy)
				{
					P newTarget = bestTarget + P(xx, yy) * 6.0;
					
					int myNum = myInnerN*3;
					int myExtNum = 0;
					
					for (const P &p : myVehicles)
					{
						double d2 = p.dist2(newTarget);
						if (d2 < (50.0*50.0))
						{
							myNum++;
						}
						else if (d2 > (55.0*55.0) && d2 < (70.0*70.0))
						{
							myExtNum++;
						}
					}
					
					if (myExtNum > 0)
					{
						int enNum = enemyInnerN*3;
						for (const P &p : enemyVehicles)
						{
							double d2 = p.dist2(newTarget);
							if (d2 < (50.0*50.0))
							{
								enNum++;
							}
						}
						
						int score = enNum - myNum * 4;
						if (score > foundScore)
						{
							foundScore = score;
							correctedBestTarget = newTarget;
						}
					}
				}
			}
			
			if (foundScore > -100000)
			{
				auto getMySize = [this](int x, int y) {
					if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
						return 0;
					
					return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
						+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
						+ (int) getMicrocell(x, y, 0, UnitType::ARV).size();
				};
				
				int pts = 0;
				long bestId = -1;
				P vehPos;
				
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.durability > 70)
					{
						double dist2 = p.pos.dist2(correctedBestTarget);
						if (dist2 > 55*55 && dist2 < 70*70)
						{
							bool outOfRange = false;
							
							if (p.vel.len2() > 0.01)
							{
								MyUnit u = p;
								for (int i = 0; i < 30; ++i)
								{
									double visRange = getVisionRange(u) - unitVel(p.type) * 10;
									if (u.pos.dist2(correctedBestTarget) > sqr(visRange))
									{
										outOfRange = true;
										break;
									}
									u.pos += u.vel;
								}
							}
							
							if (outOfRange)
								continue;
							
							int xx = p.pos.x / MICROCELL_SIZE;
							int yy = p.pos.y / MICROCELL_SIZE;
							
							//int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 60.0);
							if (pts < newPts)
							{
								pts = newPts;
								bestId = p.id;
								vehPos = p.pos;
							}
						}
					}
				}
				
				if (bestId >= 0)
				{
					result.action = MyActionType::TACTICAL_NUCLEAR_STRIKE;
					result.vehicleId = bestId;
					result.p = correctedBestTarget;
					return result;
				}
			}
		}
	}
	
	groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group &g){return g.size == 0;}), groups.end());
	
	if (result.action == MyActionType::NONE && !groups.empty())
	{
		debugAttractionPoints.clear();
		
		std::sort(groups.begin(), groups.end(), [](const Group &g1, const Group &g2){
			double p1 = groupPriority(g1);
			double p2 = groupPriority(g2);
			return p2 < p1;
		});
		
		assignBuildings();
		
		for (Group &group : groups)
		{
			if (tick - group.lastUpdateTick < 10 || group.nukeEvadeStep > 0)
				continue;
			
			if (group.shrinkActive)
			{
				if (tick - group.lastShrinkTick > 40 || !anyMoved(group))
				{
					group.shrinkActive = false;
				}
				else
				{
					continue;
				}
			}
			
			bool limitSpeed = false;
			if (nukeVehicleInd >= 0)
			{
				const MyUnit &u = units[nukeVehicleInd];
				if (group.check(u))
					limitSpeed = true;
			}
			
			calcDangerDistCells(group);
			
			P center = group.center;
			const BBox &bbox = group.bbox;
			int groupSize = group.size;
			double area = bbox.area();
			bool shrinkRequired = (area > groupSize * 40.0 || group.shrinkAfterNuke) && (((tick - group.lastUpdateTick) > 60 || group.shrinkAfterNuke) && (tick - group.lastShrinkTick) > 300);
			P shrinkPoint;
			if (shrinkRequired)
			{
				ShrinkResult shRes = findShrink(group);
				if (shRes.ticks > 7)
				{
					shrinkPoint = shRes.shrinkPoint;
				}
				else
				{
					shrinkRequired = false;
				}
				//LOG("SHRINK RES " << shRes.ticks << " " << shRes.endBBox << " P " << shRes.shrinkPoint);
			}
			//bool shrinkRequired = false;
			
			const double border = 20.0;
			P c = center;
			double R = 20 + unitVel(group.unitType) * 40;
			/*if (group.unitType == UnitType::ARV)
				R = 200.0;*/
			double ticks = R / unitVel(group.unitType);
			double curVal = attractionPoint(center, group, ticks, angryMode);
			P tp = center;
			bool found = false;
			bool canMoveFlag = false;
			
			P clampP1 = group.center - bbox.p1 + P(3.0, 3.0);
			P clampP2 = P(WIDTH - 3.0, HEIGHT - 3.0) + (group.center - bbox.p2);
			for (int k = 0; k < 3.0; ++k)
			{
				ticks = R / unitVel(group.unitType);
				int di = -1;
				
				for (int i = 0; i < 20; ++i)
				{
					P p = c + P(PI * 2.0 / 20.0 * i) * R;
					p = clampP(p, clampP1, clampP2);
					
					P shift = p - center;
					if (shift.len2() > 0.01)
					{
						if (canMove(p - center, group))
						{
							canMoveFlag = true;
							double val = attractionPoint(p, group, ticks, angryMode);
							
							DebugAttractionPointsInfo debugInfo;
							debugInfo.point = c;
							debugInfo.dir = p - c;
							debugInfo.val = val;
							debugAttractionPoints.push_back(debugInfo);
							
							if (val > curVal)
							{
								curVal = val;
								di = i;
								tp = p;
								found = true;
							}
						}
					}
				}
				
				if (di >= 0)
				{
					c = tp;
				}
				
				R /= 1.5;
			}
			
			bool moveAway = false;
			P dirAway;
			P newShift = P(0, 0);
			if (!canMoveFlag)
			{
				dirAway = P(0, 0);
				for (Group &othGroup : groups)
				{
					if (&group != &othGroup && isGroundUnit(group.unitType) == isGroundUnit(othGroup.unitType))
					{
						P dp = group.center - othGroup.center;
						double clen = dp.len();
						
						if (clen < 150.0)
							dirAway += dp / clen;
					}
				}
				
				dirAway *= 10;
				dirAway += P(tick % 7 - 3, tick % 9 - 5);
				moveAway = true;
			}
			else
			{
				if (found)
					newShift = tp - center;
				
				if (newShift.len() < 15.0)
				{
					calcMicroShift(group, newShift);
				}
			}
			
			found = newShift.len2() > 0.1;
			
			if (found || shrinkRequired || moveAway)
			{
				if (!isSelected(group))
				{
					result = select(group);
					group.actionStarted = true;
				}
				else
				{
					if (shrinkRequired)
					{
						result.action = MyActionType::SCALE;
						result.factor = 0.2;
						result.p = shrinkPoint;
						group.lastShrinkTick = tick;
						group.shrinkActive = true;
						group.nukeEvadeStep = 0;
					}
					else if (found)
					{
						result.action = MyActionType::MOVE;
						result.p = newShift;
						
						if (limitSpeed)
							result.maxSpeed = unitVel(group.unitType) * 0.6;
						
						/*if (!limitSpeed)
						{
							limitSpeed = !anyEnemiesNearbyByDangerDistr(group);
							if (limitSpeed)
								result.maxSpeed = unitVel(group.unitType) * 0.8;
						}*/
						
						/*if (group.unitType == UnitType::FIGHTER)
							result.maxSpeed = 1.0;*/
					}
					else
					{
						result.action = MyActionType::MOVE;
						result.p = dirAway;
						//std::cout << "AWAY " << dirAway.x << " " << dirAway.y << std::endl;
					}
					
					group.lastUpdateTick = tick;
					group.actionStarted = false;
				}
				
				break;
			}
			else if (!found)
			{
				group.lastUpdateTick = tick;
			}
		}
	}
	
	return result;
}

void Strat::assignBuildings()
{
	std::set<Building *> pbuildings;
	
	for (Building &b : buildings)
	{
		if (b.side != 0)
		{
			pbuildings.insert(&b);
			b.assignedGroup = 0;
		}
	}
	
	std::set<Group *> pgroups;
	for (Group &g : groups)
	{
		if (isGroundUnit(g.unitType))
		{
			pgroups.insert(&g);
		}
	}
	
	size_t count = std::min(pbuildings.size(), pgroups.size());
	for (int i = 0; i < count; ++i)
	{
		double dist2 = sqr(100000.0);
		std::set<Building *>::iterator b = pbuildings.end();
		std::set<Group *>::iterator g = pgroups.end();
		for (std::set<Building *>::iterator bit = pbuildings.begin(); bit != pbuildings.end(); ++bit)
		{
			for (std::set<Group *>::iterator git = pgroups.begin(); git != pgroups.end(); ++git)
			{
				double d2 = (*bit)->pos.dist2((*git)->center);
				if (d2 < dist2)
				{
					dist2 = d2;
					b = bit;
					g = git;
				}
			}
		}
		
		if (b != pbuildings.end() && g != pgroups.end())
		{
			(*b)->assignedGroup = (*g)->internalId;
			pbuildings.erase(b);
			pgroups.erase(g);
		}
	}
}

bool Strat::anyEnemiesNearbyByDangerDistr(const Group &group)
{
	int x = group.center.x / DISTR_MAT_CELL_SIZE;
	int y = group.center.y / DISTR_MAT_CELL_SIZE;
	for (int yy = -7; yy <= 7; ++yy)
	{
		for (int xx = -7; xx <= 7; ++xx)
		{
			if (xx * xx + yy * yy <= 50)
			{
				int x2 = x + xx;
				int y2 = y + yy;
				if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
				{
					DangerDistCell &dCell = dangerDistCells[y2 * DISTR_MAT_CELLS_X + x2];
					if (dCell.totalEnemyDamage > 0.0 || dCell.totalMyDamage > 0.0)
					{
						return true;
					}
				}
			}
		}
	}
	
	return false;
}

void Strat::calcDangerDistCells(const Group &group)
{
	dngGr = &group;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
			double totalMyDamage = 0;
			
			for (int myType = 0; myType < 5; ++myType)
			{
				double typeDamage = 0;
				if (group.sizeByTypes[myType])
				{
					for (int enemyType = 0; enemyType < 5; ++enemyType)
					{
						if (cell.count[enemyType])
						{
							double dmg = getDamage((UnitType) myType, (UnitType) enemyType);
							typeDamage = std::max(typeDamage, std::min(dmg * group.sizeByTypes[myType], 120.0 * cell.count[enemyType]));
						}
					}
				}
				totalMyDamage += typeDamage;
			}
			
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			double f2hDmg = 0.0;
			dCell.totalEnemyDamage = 0.0;
			dCell.totalEnemyHealth = 0.0;
			for (int enemyType = 0; enemyType < 5; ++enemyType)
			{
				dCell.enemyDamage[enemyType] = 0.0;
				dCell.enemyHealth[enemyType] = 0.0;
				
				if (cell.count[enemyType])
				{
					dCell.enemyHealth[enemyType] += cell.health[enemyType];
					for (int myType = 0; myType < 5; ++myType)
					{
						if (group.sizeByTypes[myType])
						{
							double dmg = getDamage((UnitType) enemyType, (UnitType) myType);
							dCell.enemyDamage[enemyType] = std::min(dmg * cell.count[enemyType], 120.0 * group.sizeByTypes[myType]);
							
							if (enemyType == (int) UnitType::FIGHTER && myType == (int) UnitType::HELICOPTER)
								f2hDmg = dCell.enemyDamage[enemyType];
						}
					}
				}
				
				dCell.enemyDamage[enemyType] *= 1.5;
				dCell.totalEnemyDamage += dCell.enemyDamage[enemyType];
				dCell.totalEnemyHealth += dCell.enemyHealth[enemyType];
			}
			
			dCell.totalMyDamage = totalMyDamage;
			dCell.f2hDmg = f2hDmg;
		}
	}
}

struct DangerDist {
	double dists[25];
	
	DangerDist() {
		dist(UnitType::ARV, UnitType::ARV) = 150.0;
		dist(UnitType::ARV, UnitType::FIGHTER) = 150.0;
		dist(UnitType::ARV, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::ARV, UnitType::IFV) = 150.0;
		dist(UnitType::ARV, UnitType::TANK) = 150.0;
		
		dist(UnitType::FIGHTER, UnitType::ARV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::IFV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::HELICOPTER, UnitType::ARV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::IFV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::IFV, UnitType::ARV) = 150.0;
		dist(UnitType::IFV, UnitType::FIGHTER) = 100.0;
		dist(UnitType::IFV, UnitType::HELICOPTER) = 100.0;
		dist(UnitType::IFV, UnitType::IFV) = 150.0;
		dist(UnitType::IFV, UnitType::TANK) = 150.0;
		
		dist(UnitType::TANK, UnitType::ARV) = 150.0;
		dist(UnitType::TANK, UnitType::FIGHTER) = 1.0;
		dist(UnitType::TANK, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::TANK, UnitType::IFV) = 150.0;
		dist(UnitType::TANK, UnitType::TANK) = 150.0;
		
		for (int i = 0; i < 25; ++i)
			dists[i] = sqr(dists[i]);
	}
	
	double &dist(UnitType from, UnitType to) {
		return dists[(int) from * 5 + (int) to];
	}
} DANGER_DISTS;


bool isClosedSpaceDanger(const P &myP, const P &enP, double myVel, double enemyVel, double ticks)
{
	double myR = myVel * ticks + 0.1;
	double dangerRad = 70.0;
	double enR = enemyVel * ticks + dangerRad;
	double d = myP.dist(enP);
	
	if (myR + d < enR)
		return true;
	
	const double borderDist = 40.0;
	if (myP.x > (borderDist + myR) && myP.x < (WIDTH - borderDist - myR) && myP.y > (borderDist + myR) && myP.y < (HEIGHT - borderDist - myR))
		return false;
	
	P myPn = myP;
	P enPn = enP;
	if (myPn.x > WIDTH / 2.0)
	{
		myPn.x = WIDTH - myPn.x;
		enPn.x = WIDTH - enPn.x;
	}
	
	if (myPn.y > HEIGHT / 2.0)
	{
		myPn.y = HEIGHT - myPn.y;
		enPn.y = HEIGHT - enPn.y;
	}
	
	if (myPn.x < myPn.y)
	{
		std::swap(myPn.x, myPn.y);
		std::swap(enPn.x, enPn.y);
	}
	
	double borderDistX = std::min(borderDist, myPn.x);
	double borderDistY = std::min(borderDist, myPn.y);
	
	double b = sqrt(sqr(myR) - sqr(myPn.y - borderDistY));
	double X = myPn.x + b;
	
	if (P(X, borderDistY).dist2(enPn) > sqr(enR))
		return false;
	
	if (myR > (myPn.x - borderDistX))
	{
		double Y = myPn.y + sqrt(sqr(myR) - sqr(myPn.x - borderDistX));
		
		if (P(borderDistX, Y).dist2(enPn) > sqr(enR))
			return false;
	}
	else
	{
		X = myPn.x - b;
	
		if (P(X, borderDistY).dist2(enPn) > sqr(enR))
			return false;
	}
	
	return true;
}

double captureTick(const P &myP, const P &enP, double myVel, double enemyVel)
{
	double dT = 125.0;
	double oldT = 0.0;
	for (double t = 0.0; t <= 625;)
	{
		if (!isClosedSpaceDanger(myP, enP, myVel, enemyVel, t))
		{
			oldT = t;
			t += dT;
		}
		else
		{
			if (dT <= 1.0)
				return t;
			
			t = oldT;
			dT /= 5.0;
			t += dT;
		}
	}
	
	return 625;
}

double Strat::attractionPoint(const P from, const Group &group, double ticks, bool angryMode)
{
	double res = 0.0;
	double f2hDmgRes = 0.0;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			if (dCell.totalMyDamage > 0.0 || dCell.totalEnemyDamage > 0.0)
			{
				double alpha = 0.3;
				double alphaM1 = 0.7;
				
				P p = P((x + 0.5) * DISTR_MAT_CELL_SIZE, (y + 0.5) * DISTR_MAT_CELL_SIZE);
				
				double dist2 = p.dist2(from);
				
				double pts = (group.health * alphaM1 + dCell.totalEnemyHealth * alpha) / (dCell.totalEnemyHealth*0.01 + dCell.totalEnemyDamage) 
				- (dCell.totalEnemyHealth * alphaM1 + group.health * alpha) / (group.health * 0.01 + dCell.totalMyDamage);
				
				pts *= (1.0 + dCell.totalEnemyHealth*0.0003);
				
				if (pts != 0.0)
				{
					double enemyVel = 0.0;
					
					for (int i = 0; i < 5; ++i) 
					{ 
						if (dCell.enemyHealth[i]) 
							enemyVel += unitVel((UnitType) i) * (dCell.enemyHealth[i] / dCell.totalEnemyHealth); 
					}
					
					if (pts < 0.0)
					{
						double t = captureTick(from, p, unitVel(group.unitType), enemyVel);
						res += pts * (625 - t) / 625.0;
					}
					else
					{
						/*double t = captureTick(p, from, enemyVel, unitVel(group.unitType));
						res += 0.1* pts * (625 - t) / 625.0;*/
					}
				}
				
				if (pts > 0.0)
				{
					double pp = 1.0/(1.0 + dist2);
					res += pts * pp;
				}
				else
				{
					/*for (int mt = 0; mt < 5; ++mt)
					{
						if (group.healthByTypes[mt])
						{
							for (int et = 0; et < 5; ++et)
							{
								if (dCell.enemyHealth[et])
								{
									double rad2 = DANGER_DISTS.dist((UnitType) et, (UnitType) mt);
									//double rad2 = 150*150;
									if (rad2 > dist2)
									{
										double fraction = group.healthByTypes[mt] / group.health * dCell.enemyHealth[et] / dCell.totalEnemyHealth;
										double pn = (1.0 - std::min(1.0, dist2/rad2));
										res += pts * pn * fraction;
									}
								}
							}
						}
					}*/
					
					double pn = (1.0 - std::min(1.0, dist2/sqr(150)));
					res += pts * pn;
				}
				
				//res += pts / p.dist2(from);
			}
			
			f2hDmgRes += dCell.f2hDmg;
		}
	}
	
	if (group.unitType == UnitType::FIGHTER)
	{
		double L = 1.5 * WIDTH;
		if (group.size * 80 > group.health)
		{
			Group *arvG = getGroup(UnitType::ARV);
			if (arvG && arvG->size > 20)
			{
				L = arvG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::ARV] * (80 - group.health/group.size)*0.2;
	}
	
	/*if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		double L = 1.5 * WIDTH;
		if (f2hDmgRes > 0.0 && res < 0.0)
		{
			if (fivG && fivG->size > 10)
			{
				L = fivG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::IFV];
		
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 300.0)
			{
				res -= (l - 300.0);
			}
		}
	}*/
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::HELICOPTER);
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 400.0)
			{
				res -= (l - 400.0);
			}
		}
	}*/
	
	if (isGroundUnit(group.unitType))
	{
		for (const Building &b : buildings)
		{
			if (b.side != 0)
			{
				double d = from.dist(b.pos);
				double coef = b.assignedGroup == group.internalId ? 2.0 : 1.0;
				res += coef*group.health/(20 + d)*0.1;
			}
		}
	}
	
	return res;
}


ShrinkResult Strat::findShrink(Group &group)
{
	ShrinkResult result;
	
	Simulator sim;
	sim.tick = tick;
	std::copy(cells, cells + CELLS_X * CELLS_Y, sim.cells);
	
	BBox gbox = group.bbox;
	gbox.expand(2.0);
	for (const MyUnit &u : units)
	{
		if (gbox.inside(u.pos))
		{
			sim.units.push_back(u);
			MyUnit &tu = *sim.units.rbegin();
			tu.selected = group.check(tu);
			tu.vel = P(0, 0);
		}
	}
	sim.groups.push_back(group);
	
	double bestArea = WIDTH * HEIGHT;
	for (double x = -1; x <= 1; ++x)
	{
		for (double y = -1; y <= 1; ++y)
		{
			Simulator sim2 = sim;
			MyMove myMove;
			myMove.action = MyActionType::SCALE;
			myMove.p = group.center + P(x, y) * 10.0;
			myMove.factor = 0.2;
			sim2.registerMove(myMove, 0);
			
			int i = 0;
			for (; i < 20; ++i)
			{
				sim2.tick++;
				sim2.resetAxisSorts();
				sim2.applyMoves();
				int cnt = sim2.moveUnits();
				if (!cnt)
					break;
			}
			sim2.updateStats();
			
			double area = sim2.groups.rbegin()->bbox.area();
			if (area < bestArea)
			{
				bestArea = area;
				result.shrinkPoint = myMove.p;
				result.ticks = i;
				result.endBBox = sim2.groups.rbegin()->bbox;
			}
		}
	}
	
	return result;
}

}


namespace StratV19 {
	double groupPriority(const Group &g) {
	double res = 0.0;
	
	if (g.shrinkAfterNuke)
		res += 1100.0;
	else if (g.actionStarted)
		res += 1000.0;
	
	res -= g.lastUpdateTick;
	
	return res;
}

void DistributionMatrix::clear()
{
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		Cell &cell = cells[i];
		for (int j = 0; j < 5; ++j)
			cell.count[j] = 0;
		for (int j = 0; j < 5; ++j)
			cell.health[j] = 0;
	}
}

void DistributionMatrix::initialize(const Simulator &sim)
{
	clear();
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : sim.units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / DISTR_MAT_CELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / DISTR_MAT_CELL_SIZE;
				
				Cell &cell = getCell(x, y);
				int type = (int) u.type;
				cell.count[type] += 1.0 / K;
				cell.health[type] += u.durability / K;
			}
		}
	}
}

void DistributionMatrix::blur(DistributionMatrix &oth) const
{
	oth.clear();
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			Cell &resCell = oth.getCell(x, y);
			int cnt = 0;
			for (int yy = -1; yy <= 1; ++yy)
			{
				for (int xx = -1; xx <= 1; ++xx)
				{
					int x2 = x + xx;
					int y2 = y + yy;
					if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
					{
						++cnt;
						const Cell &cell = getCell(x2, y2);
						for (int i = 0; i < 5; ++i)
							resCell.count[i] += cell.count[i];
						for (int i = 0; i < 5; ++i)
							resCell.health[i] += cell.health[i];
					}
				}
			}
			
			const Cell &cell = getCell(x, y);
			for (int i = 0; i < 5; ++i)
			{
				if (!cell.count[i])
				{
					resCell.count[i] = 0;
					resCell.health[i] = 0;
				}
			}
			
			/*for (int i = 0; i < 5; ++i)
				resCell.count[i] += (resCell.count[i] + cnt - 1) / cnt;
			for (int i = 0; i < 5; ++i)
				resCell.health[i] /= cnt;*/
		}
	}
}

Strat::Strat::Strat()
{
	Group g;
	g.actionStarted = false;
	g.lastUpdateTick = 0;
	g.lastShrinkTick = 0;
	
	for (int i = 0; i < 1; ++i)
	{
		g.unitType = UnitType::HELICOPTER;
		//g.miniGroupInd = i;
		g.internalId = internalGroupSeq++;
		groups.push_back(g);
	}
	g.miniGroupInd = 0;
	
	g.unitType = UnitType::IFV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::FIGHTER;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::TANK;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::ARV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
}


void Strat::calcMicroShift(Group &group, P &shift)
{
	if (group.unitType != UnitType::TANK && group.unitType != UnitType::FIGHTER)
		return;
	
	BBox bbox = group.bbox;
	bbox.p1 += shift - P(35, 35);
	bbox.p2 += shift + P(35, 35);
	
	int finalScore = -100000;
	int currentScore = -100000;
	double ticks = 20.0;
	
	//if (group.unitType == UnitType::TANK)
	{
		std::vector<P> enemies[(int) UnitType::COUNT];
		std::vector<P> myUnits;
		
		for (const MyUnit &u : units)
		{
			if (u.side > 0 && (u.type == UnitType::TANK || u.type == UnitType::HELICOPTER || u.type == UnitType::IFV) && bbox.inside(u.pos))
			{
				enemies[(int) u.type].push_back(u.pos + u.vel * ticks);
			}
			else if (group.check(u))
			{
				myUnits.push_back(u.pos);
			}
		}
		
		double L = 8.0;
		P newShift = shift;
		for (int k = 0; k < 4; ++k)
		{
			P foundShift = newShift;
			int foundScore = -100000;
			for (int i = 0; i < 9; ++i)
			{
				P testShift = newShift + P(i / 3 - 1, i % 3 - 1) * L;
				int score = 0;
				bool isCurShift = shift.dist2(testShift) < 0.1;
				
				if (!isCurShift || currentScore == -100000)
				{
					const std::vector<MicroShiftValues> &pos = microShiftMatrix.pos[(int) group.unitType];
					if (!pos.empty())
					{
						for (const P &myP : myUnits)
						{
							P myPos = myP + testShift;
							bool found = false;
							for (const MicroShiftValues &vals : pos)
							{
								if (!found)
								{
									for (const P &enP : enemies[(int) vals.unitType])
									{
										if (myPos.dist2(enP) < vals.dist2)
										{
											score += vals.val;
											found = true;
											break;
										}
									}
								}
							}
						}
					}
					
					// !! decrease SCORE
					int posScore = score;
					score /= 2;
					
					const std::vector<MicroShiftValues> &neg = microShiftMatrix.neg[(int) group.unitType];
					if (!neg.empty())
					{
						for (const MicroShiftValues &vals : neg)
						{
							for (const P &enP : enemies[(int) vals.unitType])
							{
								for (const P &myP : myUnits)
								{
									P myPos = myP + testShift;
									if (myPos.dist2(enP) < vals.dist2)
									{
										score -= vals.val;
										break;
									}
								}
							}
						}
					}
					
					int negScore = posScore / 2 - score;
					
					if (foundScore < score)
					{
						foundShift = testShift;
						foundScore = score;
					}
					
					if (isCurShift)
						currentScore = score;
				}
			}
			
			L /= 2.0;
			newShift = foundShift;
			
			finalScore = foundScore;
		}
		
		if (currentScore >= finalScore)
			return;
		
		shift = newShift;
	}
}

void Strat::calcNuclearEfficiency()
{
	for (int i = 0; i < MICROCELLS_X * MICROCELLS_Y; i++)
		nuclearEfficiency[i] = 0;
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / MICROCELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / MICROCELL_SIZE;
				
				double efficiency = 100.0 / (10.0 + u.durability) / K;
				
				if (u.type == UnitType::ARV)
					efficiency /= 10.0;
				
				if (u.type == UnitType::FIGHTER || u.type == UnitType::HELICOPTER)
					efficiency /= 1.5;
				
				nuclearEfficiency[y * MICROCELLS_X + x] += efficiency;
			}
		}
	}
}

UnitType Strat::calcNextUnitTypeForConstruction(bool ground)
{
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType == UnitType::HELICOPTER)
		{
			return UnitType::TANK;
		}
	}
	
	if (enemyCount[UnitType::HELICOPTER] * 0.9 > myCount[UnitType::HELICOPTER])
	{
		return UnitType::HELICOPTER;
	}
	
	return UnitType::TANK;
	
	double score[5] = {};
	
	int enCnt = enemyCount[UnitType::HELICOPTER]*0.7 + enemyCount[UnitType::FIGHTER]*0.3;
	int myCnt = myCount[UnitType::FIGHTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::FIGHTER] += enCnt - myCnt;
	}
	score[(int) UnitType::FIGHTER] *= 0.6;
	
	enCnt = enemyCount[UnitType::TANK];
	myCnt = myCount[UnitType::HELICOPTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::HELICOPTER] += enCnt - myCnt;
	}
	
	score[(int) UnitType::HELICOPTER] *= 0.8;
	
	enCnt = enemyCount[UnitType::IFV];
	myCnt = myCount[UnitType::TANK];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::TANK] += enCnt - myCnt;
	}
	
	enCnt = enemyCount[UnitType::FIGHTER]*0.7 + enemyCount[UnitType::HELICOPTER]*0.3;
	myCnt = myCount[UnitType::IFV];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::IFV] += enCnt - myCnt;
	}
	
	
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE)
		{
			score[(int) b.unitType] -= 20;
		}
	}
	
	double grCount = 0;
	double airCount = 0;
	for (int i = 0; i < 5; ++i)
	{
		if (isGroundUnit((UnitType) i) && i != (int) UnitType::ARV)
		{
			grCount += myCount[(UnitType) i];
		}
		else
		{
			airCount += myCount[(UnitType) i];
		}
	}
	
	double totalCount = grCount + airCount;
	if (totalCount > 0)
	{
		for (int i = 0; i < 5; ++i)
		{
			if (isGroundUnit((UnitType) i))
			{
				score[i] *= airCount;
			}
			else
			{
				score[i] *= grCount;
			}
		}
	}
	
	int res = 0;
	int resType = -1;
	for (int i = 0; i < 5; ++i)
	{
		if (score[i] > res)
		{
			res = score[i];
			resType = i;
		}
	}
	
	if (resType >= 0)
		return (UnitType) resType;
	
	return UnitType::TANK;
}

MyMove Strat::calcNextMove()
{
	MyMove result;
	result.action = MyActionType::NONE;
	
	int moves = getAvailableActions(12);
	if (moves < 1)
		return result;
	
	if (players[1].remainingNuclearStrikeCooldownTicks < 30)
	{
		if (moves < 3)
			return result;
	}
	
	updateStats();
	
	if (players[1].nextNuclearStrikeTick >= 0)
	{
		bool select = false;
		bool expand = false;
		BBox bbox;
		int evaidingGroups = 0;
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep == 0)
			{
				BBox gbox = g.bbox;
				gbox.expand(40.0);
				if (gbox.inside(players[1].nuclearStrike))
				{
					bbox.add(g.bbox);
					g.nukeEvadeStep = 1;
					select = true;
					++evaidingGroups;
				}
			}
			else if (g.nukeEvadeStep == 1)
			{
				expand = true;
				g.nukeEvadeStep = 2;
				++evaidingGroups;
			}
			else
			{
				++evaidingGroups;
			}
		}
		
		if (select)
		{
			result.action = MyActionType::CLEAR_AND_SELECT;
			result.p1 = bbox.p1;
			result.p2 = bbox.p2;
			
			return result;
		}
		else if (expand)
		{
			result.action = MyActionType::SCALE;
			result.p = players[1].nuclearStrike;
			result.factor = 9.0;
			return result;
		}
		
		if (evaidingGroups && moves < 3)
			return result;
	}
	else
	{
		for (Group &g : groups)
		{
			if (tick - g.lastShrinkTick > 30)
				g.shrinkAfterNuke = false;
			if (g.nukeEvadeStep != 0)
			{
				g.shrinkAfterNuke = true;
				g.nukeEvadeStep = -1;
			}
		}
	}
	
	/*if (tick > 200) {
		int moves = getAvailableActions(4, 10);
		if (moves < 1) // throttle
			return result;
	}*/
	
	for (Building &b : buildings)
	{
		/*if (b.lastChangeUnitCount > b.unitCount)
			b.lastChangeUnitCount = b.unitCount;*/
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && (b.unitType == UnitType::NONE/* || (b.unitCount - b.lastChangeUnitCount) > 11*/))
		{
			//LOG("SVP " << buildingCaptured);
			result.action = MyActionType::SETUP_VEHICLE_PRODUCTION;
			result.facilityId = b.id;
			
			/*if (buildingCaptured % 2 == 0)
				result.unitType = UnitType::FIGHTER;
			else
				result.unitType = UnitType::IFV;*/
			
			//result.unitType = UnitType::TANK;
			result.unitType = calcNextUnitTypeForConstruction(false);
			//result.unitType = (UnitType) (buildingCaptured % 5);
			//b.lastChangeUnitCount = b.unitCount;
			
			++buildingCaptured;
			return result;
		}
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE && b.unitCount > 40 && b.productionProgress < 30 || b.createGroupStep > 0)
		{
			//LOG("MAKE GROUP");
			if (b.createGroupStep == 0 || b.createGroupStep == 1)
			{
				result.action = MyActionType::CLEAR_AND_SELECT;
				result.p1 = b.pos - P(32, 32);
				result.p2 = b.pos + P(32, 32);
				result.unitType = b.unitType;
				b.createGroupStep = 2;
				return result;
			}
			
			if (b.createGroupStep == 2)
			{
				// check if selected
				bool anySelected = false;
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.groups.count() == 0)
					{
						if (p.pos.x > b.pos.x - 32 && p.pos.x < b.pos.x + 32 && p.pos.y > b.pos.y - 32 && p.pos.y < b.pos.y + 32)
						{
							if (p.selected)
							{
								anySelected = true;
								break;
							}
						}
					}
				}
				
				if (anySelected)
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					b.createGroupStep = 3;
					
					Group newGroup;
					//newGroup.unitType = b.unitType;
					newGroup.group = result.group;
					newGroup.internalId = internalGroupSeq++;
					groups.push_back(newGroup);
					
					b.createGroupStep = 0;
					return result;
				}
				else
				{
					b.createGroupStep = 1;
				}
			}
		}
	}
	
		
	if (!initialGroupsGerationDone && !buildings.empty())
	{
		for (Group &g : groups)
		{
			/*if (g.unitType == UnitType::HELICOPTER && !g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					P allCenter = P(0, 0);
					int count = 0;
					for (const MyUnit &u : units)
					{
						if (u.side == 0 && u.type == g.unitType)
						{
							allCenter += u.pos;
							++count;
						}
					}
					
					allCenter /= count;
					
					if (g.miniGroupInd == 0)
					{
						result.p1 = P(0, 0);
						result.p2 = allCenter;
					}
					else if (g.miniGroupInd == 1)
					{
						result.p1 = P(allCenter.x, 0);
						result.p2 = P(WIDTH, allCenter.y);
					}
					else if (g.miniGroupInd == 2)
					{
						result.p1 = P(0, allCenter.y);
						result.p2 = P(allCenter.x, HEIGHT);
					}
					else if (g.miniGroupInd == 3)
					{
						result.p1 = allCenter;
						result.p2 = P(WIDTH, HEIGHT);
					}
					
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}*/
			
			if (!g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else if (g.enumGroupBuildStep == 1)
				{
					result.action = MyActionType::ASSIGN;
					g.enumGroupBuildStep++;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}
		}
		
		initialGroupsGerationDone = true;
		LOG("DONE");
	}
	
	bool angryMode = angryModeTill > tick;

	
	DistributionMatrix matr;
	matr.initialize(*this);
	matr.blur(distributionMatrix);
	
	/*if (tick > 532)
	{
		std::cout.width(2);
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &resCell = distributionMatrix.getCell(x, y);
				std::cout << " " << resCell.count[1];
			}
			std::cout << std::endl;
		}
		std::cout << std::endl;
	}*/
	
	if (players[0].remainingNuclearStrikeCooldownTicks == 0)
	{
		resetCells();
		calcNuclearEfficiency();
		
		auto getEnemySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0.0;
			
			return nuclearEfficiency[y * MICROCELLS_X + x];
		};
		
		auto getMySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0;
			
			return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
				+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
				+ (int) getMicrocell(x, y, 0, UnitType::ARV).size() / 2;
		};
		
		P bestTarget;
		int bestScore = 0;
		
		for (int y = 0; y < MICROCELLS_Y; ++y) {
			for (int x = 0; x < MICROCELLS_X; ++x) {
				double enemyN = getEnemySize(x, y);
				if (enemyN > 10)
				{
					int totalMyCount = 0;
					int myCenterCount = 0;
					for (int xx = -1; xx <= 1; ++xx)
					{
						for (int yy = -1; yy <= 1; ++yy)
						{
							totalMyCount += getMySize(x + xx, y + yy);
							if (xx == 0 && yy == 0)
								myCenterCount = totalMyCount;
						}
					}
					
					if (totalMyCount < 20 && myCenterCount == 0)
					{
						int totalMyExtCount = totalMyCount;
						if (totalMyExtCount == 0)
						{
							for (int xx = -2; xx <= 2; ++xx)
							{
								for (int yy = -2; yy <= 2; ++yy)
								{
									if (xx < -1 || xx > 1 || yy < -1 || yy > 1)
										totalMyExtCount += getMySize(x + xx, y + yy);
								}
							}
						}
						
						if (totalMyExtCount > 0)
						{
							double totalEnemyCount =
								enemyN +
								getEnemySize(x - 1, y)/2 +
								getEnemySize(x + 1, y)/2 + 
								getEnemySize(x, y - 1)/2 + 
								getEnemySize(x, y + 1)/2;
							
							if (bestScore < totalEnemyCount)
							{
								bestScore = totalEnemyCount;
								bestTarget = P(x + 0.5, y + 0.5) * MICROCELL_SIZE;
							}
						}
					}
				}
			}
		}
		
		if (bestScore > 0)
		{
			// Correct point
			P correctedBestTarget = bestTarget;
			
			std::vector<P> myVehicles;
			std::vector<P> enemyVehicles;
			int myInnerN = 0;
			int enemyInnerN = 0;
			for (const MyUnit &p : units)
			{
				double d2 = p.pos.dist2(bestTarget);
				if (d2 < (75.0*75.0))
				{
					if (d2 < (38.0*38.0))
					{
						if (p.side == 0)
							myInnerN++;
						else
							enemyInnerN++;
					}
					else
					{
						if (p.side == 0)
							myVehicles.push_back(p.pos);
						else
							enemyVehicles.push_back(p.pos);
					}
				}
			}
			
			int foundScore = -100000;
			for (int xx = -2; xx <= 2; ++xx)
			{
				for (int yy = -2; yy <= 2; ++yy)
				{
					P newTarget = bestTarget + P(xx, yy) * 6.0;
					
					int myNum = myInnerN*3;
					int myExtNum = 0;
					
					for (const P &p : myVehicles)
					{
						double d2 = p.dist2(newTarget);
						if (d2 < (50.0*50.0))
						{
							myNum++;
						}
						else if (d2 > (55.0*55.0) && d2 < (70.0*70.0))
						{
							myExtNum++;
						}
					}
					
					if (myExtNum > 0)
					{
						int enNum = enemyInnerN*3;
						for (const P &p : enemyVehicles)
						{
							double d2 = p.dist2(newTarget);
							if (d2 < (50.0*50.0))
							{
								enNum++;
							}
						}
						
						int score = enNum - myNum * 4;
						if (score > foundScore)
						{
							foundScore = score;
							correctedBestTarget = newTarget;
						}
					}
				}
			}
			
			if (foundScore > -100000)
			{
				auto getMySize = [this](int x, int y) {
					if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
						return 0;
					
					return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
						+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
						+ (int) getMicrocell(x, y, 0, UnitType::ARV).size();
				};
				
				int pts = 0;
				long bestId = -1;
				P vehPos;
				
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.durability > 70)
					{
						double dist2 = p.pos.dist2(correctedBestTarget);
						if (dist2 > 55*55 && dist2 < 70*70)
						{
							bool outOfRange = false;
							
							if (p.vel.len2() > 0.01)
							{
								MyUnit u = p;
								for (int i = 0; i < 30; ++i)
								{
									double visRange = getVisionRange(u) - unitVel(p.type) * 10;
									if (u.pos.dist2(correctedBestTarget) > sqr(visRange))
									{
										outOfRange = true;
										break;
									}
									u.pos += u.vel;
								}
							}
							
							if (outOfRange)
								continue;
							
							int xx = p.pos.x / MICROCELL_SIZE;
							int yy = p.pos.y / MICROCELL_SIZE;
							
							//int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 60.0);
							if (pts < newPts)
							{
								pts = newPts;
								bestId = p.id;
								vehPos = p.pos;
							}
						}
					}
				}
				
				if (bestId >= 0)
				{
					result.action = MyActionType::TACTICAL_NUCLEAR_STRIKE;
					result.vehicleId = bestId;
					result.p = correctedBestTarget;
					return result;
				}
			}
		}
	}
	
	groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group &g){return g.size == 0;}), groups.end());
	
	if (result.action == MyActionType::NONE && !groups.empty())
	{
		debugAttractionPoints.clear();
		
		std::sort(groups.begin(), groups.end(), [](const Group &g1, const Group &g2){
			double p1 = groupPriority(g1);
			double p2 = groupPriority(g2);
			return p2 < p1;
		});
		
		assignBuildings();
		
		for (Group &group : groups)
		{
			if (tick - group.lastUpdateTick < 10 || group.nukeEvadeStep > 0)
				continue;
			
			if (group.shrinkActive)
			{
				if (tick - group.lastShrinkTick > 40 || !anyMoved(group))
				{
					group.shrinkActive = false;
				}
				else
				{
					continue;
				}
			}
			
			bool limitSpeed = false;
			if (nukeVehicleInd >= 0)
			{
				const MyUnit &u = units[nukeVehicleInd];
				if (group.check(u))
					limitSpeed = true;
			}
			
			calcDangerDistCells(group);
			
			P center = group.center;
			const BBox &bbox = group.bbox;
			int groupSize = group.size;
			double area = bbox.area();
			bool shrinkRequired = (area > groupSize * 40.0 || group.shrinkAfterNuke) && (((tick - group.lastUpdateTick) > 60 || group.shrinkAfterNuke) && (tick - group.lastShrinkTick) > 300);
			P shrinkPoint;
			if (shrinkRequired)
			{
				ShrinkResult shRes = findShrink(group);
				if (shRes.ticks > 7)
				{
					shrinkPoint = shRes.shrinkPoint;
				}
				else
				{
					shrinkRequired = false;
				}
				//LOG("SHRINK RES " << shRes.ticks << " " << shRes.endBBox << " P " << shRes.shrinkPoint);
			}
			//bool shrinkRequired = false;
			
			const double border = 20.0;
			P c = center;
			double R = 20 + unitVel(group.unitType) * 40;
			/*if (group.unitType == UnitType::ARV)
				R = 200.0;*/
			double ticks = R / unitVel(group.unitType);
			double curVal = attractionPoint(center, group, ticks, angryMode);
			P tp = center;
			bool found = false;
			
			bool canMoveFlag = false;
			
			P clampP1 = group.center - bbox.p1 + P(3.0, 3.0);
			P clampP2 = P(WIDTH - 3.0, HEIGHT - 3.0) + (group.center - bbox.p2);
			for (int k = 0; k < 3.0; ++k)
			{
				ticks = R / unitVel(group.unitType);
				int di = -1;
				
				for (int i = 0; i < 20; ++i)
				{
					P p = c + P(PI * 2.0 / 20.0 * i) * R;
					p = clampP(p, clampP1, clampP2);
					
					P shift = p - center;
					if (shift.len2() > 0.01)
					{
						if (canMove(p - center, group))
						{
							canMoveFlag = true;
							double val = attractionPoint(p, group, ticks, angryMode);
							
							DebugAttractionPointsInfo debugInfo;
							debugInfo.point = c;
							debugInfo.dir = p - c;
							debugInfo.val = val;
							debugAttractionPoints.push_back(debugInfo);
							
							if (val > curVal)
							{
								curVal = val;
								di = i;
								tp = p;
								found = true;
							}
						}
					}
				}
				
				if (di >= 0)
				{
					c = tp;
				}
				
				R /= 1.5;
			}
			
			if (!canMoveFlag)
			{
				double R = 20 + unitVel(group.unitType) * 40;
				
				std::vector<const MyUnit *> groupUnits;
				std::vector<const MyUnit *> otherUnits;
				
				BBox bbox = group.bbox;
				bbox.p1 -= P(2.0*R, 2.0*R);
				bbox.p2 += P(2.0*R, 2.0*R);
				
				for (const MyUnit &u : units)
				{
					if (group.check(u))
					{
						groupUnits.push_back(&u);
					}
					else if (group.canIntersectWith(u) && bbox.inside(u.pos))
					{
						otherUnits.push_back(&u);
					}
				}
				
				for (int k = 0; k < 3.0; ++k)
				{
					ticks = R / unitVel(group.unitType);
					int di = -1;
					
					for (int i = 0; i < 19; ++i)
					{
						P p = c + P(PI * 2.0 / 19.0 * i) * R;
						p = clampP(p, clampP1, clampP2);
						
						P shift = p - center;
						if (shift.len2() > 0.01)
						{
							if (canMoveDetailed(p - center, group, groupUnits, otherUnits))
							{
								canMoveFlag = true;
								double val = attractionPoint(p, group, ticks, angryMode);
								
								DebugAttractionPointsInfo debugInfo;
								debugInfo.point = c;
								debugInfo.dir = p - c;
								debugInfo.val = val;
								debugAttractionPoints.push_back(debugInfo);
								
								if (val > curVal)
								{
									curVal = val;
									di = i;
									tp = p;
									found = true;
								}
							}
						}
					}
					
					if (di >= 0)
					{
						c = tp;
					}
					
					R /= 1.5;
				}
			}
			
			bool moveAway = false;
			P dirAway;
			P newShift = P(0, 0);
			if (!canMoveFlag)
			{
				dirAway = P(0, 0);
				for (Group &othGroup : groups)
				{
					if (&group != &othGroup && isGroundUnit(group.unitType) == isGroundUnit(othGroup.unitType))
					{
						P dp = group.center - othGroup.center;
						double clen = dp.len();
						
						if (clen < 150.0)
							dirAway += dp / clen;
					}
				}
				
				dirAway *= 10;
				dirAway += P(tick % 7 - 3, tick % 9 - 5);
				moveAway = true;
			}
			else
			{
				if (found)
					newShift = tp - center;
				
				if (newShift.len() < 15.0)
				{
					calcMicroShift(group, newShift);
				}
			}
			
			found = newShift.len2() > 0.1;
			
			if (found || shrinkRequired || moveAway)
			{
				if (!isSelected(group))
				{
					result = select(group);
					group.actionStarted = true;
				}
				else
				{
					if (shrinkRequired)
					{
						result.action = MyActionType::SCALE;
						result.factor = 0.2;
						result.p = shrinkPoint;
						group.lastShrinkTick = tick;
						group.shrinkActive = true;
						group.nukeEvadeStep = 0;
					}
					else if (found)
					{
						result.action = MyActionType::MOVE;
						result.p = newShift;
						
						if (limitSpeed)
							result.maxSpeed = unitVel(group.unitType) * 0.6;
						
						/*if (!limitSpeed)
						{
							limitSpeed = !anyEnemiesNearbyByDangerDistr(group);
							if (limitSpeed)
								result.maxSpeed = unitVel(group.unitType) * 0.8;
						}*/
						
						/*if (group.unitType == UnitType::FIGHTER)
							result.maxSpeed = 1.0;*/
					}
					else
					{
						result.action = MyActionType::MOVE;
						result.p = dirAway;
						//std::cout << "AWAY " << dirAway.x << " " << dirAway.y << std::endl;
					}
					
					group.lastUpdateTick = tick;
					group.actionStarted = false;
				}
				
				break;
			}
			else if (!found)
			{
				group.lastUpdateTick = tick;
			}
		}
	}
	
	return result;
}

void Strat::assignBuildings()
{
	std::set<Building *> pbuildings;
	
	for (Building &b : buildings)
	{
		if (b.side != 0)
		{
			pbuildings.insert(&b);
			b.assignedGroup = 0;
		}
	}
	
	std::set<Group *> pgroups;
	for (Group &g : groups)
	{
		if (isGroundUnit(g.unitType))
		{
			pgroups.insert(&g);
		}
	}
	
	size_t count = std::min(pbuildings.size(), pgroups.size());
	for (int i = 0; i < count; ++i)
	{
		double dist2 = sqr(100000.0);
		std::set<Building *>::iterator b = pbuildings.end();
		std::set<Group *>::iterator g = pgroups.end();
		for (std::set<Building *>::iterator bit = pbuildings.begin(); bit != pbuildings.end(); ++bit)
		{
			for (std::set<Group *>::iterator git = pgroups.begin(); git != pgroups.end(); ++git)
			{
				double d2 = (*bit)->pos.dist2((*git)->center);
				if (d2 < dist2)
				{
					dist2 = d2;
					b = bit;
					g = git;
				}
			}
		}
		
		if (b != pbuildings.end() && g != pgroups.end())
		{
			(*b)->assignedGroup = (*g)->internalId;
			pbuildings.erase(b);
			pgroups.erase(g);
		}
	}
}

bool Strat::anyEnemiesNearbyByDangerDistr(const Group &group)
{
	int x = group.center.x / DISTR_MAT_CELL_SIZE;
	int y = group.center.y / DISTR_MAT_CELL_SIZE;
	for (int yy = -7; yy <= 7; ++yy)
	{
		for (int xx = -7; xx <= 7; ++xx)
		{
			if (xx * xx + yy * yy <= 50)
			{
				int x2 = x + xx;
				int y2 = y + yy;
				if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
				{
					DangerDistCell &dCell = dangerDistCells[y2 * DISTR_MAT_CELLS_X + x2];
					if (dCell.totalEnemyDamage > 0.0 || dCell.totalMyDamage > 0.0)
					{
						return true;
					}
				}
			}
		}
	}
	
	return false;
}

void Strat::calcDangerDistCells(const Group &group)
{
	dngGr = &group;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
			double totalMyDamage = 0;
			
			for (int myType = 0; myType < 5; ++myType)
			{
				double typeDamage = 0;
				if (group.sizeByTypes[myType])
				{
					for (int enemyType = 0; enemyType < 5; ++enemyType)
					{
						if (cell.count[enemyType])
						{
							double dmg = getDamage((UnitType) myType, (UnitType) enemyType);
							typeDamage = std::max(typeDamage, std::min(dmg * group.sizeByTypes[myType], 120.0 * cell.count[enemyType]));
						}
					}
				}
				totalMyDamage += typeDamage;
			}
			
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			double f2hDmg = 0.0;
			dCell.totalEnemyDamage = 0.0;
			dCell.totalEnemyHealth = 0.0;
			for (int enemyType = 0; enemyType < 5; ++enemyType)
			{
				dCell.enemyDamage[enemyType] = 0.0;
				dCell.enemyHealth[enemyType] = 0.0;
				
				if (cell.count[enemyType])
				{
					dCell.enemyHealth[enemyType] += cell.health[enemyType];
					for (int myType = 0; myType < 5; ++myType)
					{
						if (group.sizeByTypes[myType])
						{
							double dmg = getDamage((UnitType) enemyType, (UnitType) myType);
							dCell.enemyDamage[enemyType] = std::min(dmg * cell.count[enemyType], 120.0 * group.sizeByTypes[myType]);
							
							if (enemyType == (int) UnitType::FIGHTER && myType == (int) UnitType::HELICOPTER)
								f2hDmg = dCell.enemyDamage[enemyType];
						}
					}
				}
				
				dCell.enemyDamage[enemyType] *= 1.5;
				dCell.totalEnemyDamage += dCell.enemyDamage[enemyType];
				dCell.totalEnemyHealth += dCell.enemyHealth[enemyType];
			}
			
			dCell.totalMyDamage = totalMyDamage;
			dCell.f2hDmg = f2hDmg;
		}
	}
}

struct DangerDist {
	double dists[25];
	
	DangerDist() {
		dist(UnitType::ARV, UnitType::ARV) = 150.0;
		dist(UnitType::ARV, UnitType::FIGHTER) = 150.0;
		dist(UnitType::ARV, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::ARV, UnitType::IFV) = 150.0;
		dist(UnitType::ARV, UnitType::TANK) = 150.0;
		
		dist(UnitType::FIGHTER, UnitType::ARV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::IFV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::HELICOPTER, UnitType::ARV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::IFV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::IFV, UnitType::ARV) = 150.0;
		dist(UnitType::IFV, UnitType::FIGHTER) = 100.0;
		dist(UnitType::IFV, UnitType::HELICOPTER) = 100.0;
		dist(UnitType::IFV, UnitType::IFV) = 150.0;
		dist(UnitType::IFV, UnitType::TANK) = 150.0;
		
		dist(UnitType::TANK, UnitType::ARV) = 150.0;
		dist(UnitType::TANK, UnitType::FIGHTER) = 1.0;
		dist(UnitType::TANK, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::TANK, UnitType::IFV) = 150.0;
		dist(UnitType::TANK, UnitType::TANK) = 150.0;
		
		for (int i = 0; i < 25; ++i)
			dists[i] = sqr(dists[i]);
	}
	
	double &dist(UnitType from, UnitType to) {
		return dists[(int) from * 5 + (int) to];
	}
} DANGER_DISTS;


bool isClosedSpaceDanger(const P &myP, const P &enP, double myVel, double enemyVel, double ticks)
{
	double myR = myVel * ticks + 0.1;
	double dangerRad = 70.0;
	double enR = enemyVel * ticks + dangerRad;
	double d = myP.dist(enP);
	
	if (myR + d < enR)
		return true;
	
	const double borderDist = 40.0;
	if (myP.x > (borderDist + myR) && myP.x < (WIDTH - borderDist - myR) && myP.y > (borderDist + myR) && myP.y < (HEIGHT - borderDist - myR))
		return false;
	
	P myPn = myP;
	P enPn = enP;
	if (myPn.x > WIDTH / 2.0)
	{
		myPn.x = WIDTH - myPn.x;
		enPn.x = WIDTH - enPn.x;
	}
	
	if (myPn.y > HEIGHT / 2.0)
	{
		myPn.y = HEIGHT - myPn.y;
		enPn.y = HEIGHT - enPn.y;
	}
	
	if (myPn.x < myPn.y)
	{
		std::swap(myPn.x, myPn.y);
		std::swap(enPn.x, enPn.y);
	}
	
	double borderDistX = std::min(borderDist, myPn.x);
	double borderDistY = std::min(borderDist, myPn.y);
	
	double b = sqrt(sqr(myR) - sqr(myPn.y - borderDistY));
	double X = myPn.x + b;
	
	if (P(X, borderDistY).dist2(enPn) > sqr(enR))
		return false;
	
	if (myR > (myPn.x - borderDistX))
	{
		double Y = myPn.y + sqrt(sqr(myR) - sqr(myPn.x - borderDistX));
		
		if (P(borderDistX, Y).dist2(enPn) > sqr(enR))
			return false;
	}
	else
	{
		X = myPn.x - b;
	
		if (P(X, borderDistY).dist2(enPn) > sqr(enR))
			return false;
	}
	
	return true;
}

double captureTick(const P &myP, const P &enP, double myVel, double enemyVel)
{
	double dT = 125.0;
	double oldT = 0.0;
	for (double t = 0.0; t <= 625;)
	{
		if (!isClosedSpaceDanger(myP, enP, myVel, enemyVel, t))
		{
			oldT = t;
			t += dT;
		}
		else
		{
			if (dT <= 1.0)
				return t;
			
			t = oldT;
			dT /= 5.0;
			t += dT;
		}
	}
	
	return 625;
}

double Strat::attractionPoint(const P from, const Group &group, double ticks, bool angryMode)
{
	double res = 0.0;
	double f2hDmgRes = 0.0;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			if (dCell.totalMyDamage > 0.0 || dCell.totalEnemyDamage > 0.0)
			{
				double alpha = 0.3;
				double alphaM1 = 0.7;
				
				P p = P((x + 0.5) * DISTR_MAT_CELL_SIZE, (y + 0.5) * DISTR_MAT_CELL_SIZE);
				
				double dist2 = p.dist2(from);
				
				double pts = (group.health * alphaM1 + dCell.totalEnemyHealth * alpha) / (dCell.totalEnemyHealth*0.01 + dCell.totalEnemyDamage) 
				- (dCell.totalEnemyHealth * alphaM1 + group.health * alpha) / (group.health * 0.01 + dCell.totalMyDamage);
				
				pts *= (1.0 + dCell.totalEnemyHealth*0.0003);
				
				if (pts != 0.0)
				{
					double enemyVel = 0.0;
					
					for (int i = 0; i < 5; ++i) 
					{ 
						if (dCell.enemyHealth[i]) 
							enemyVel += unitVel((UnitType) i) * (dCell.enemyHealth[i] / dCell.totalEnemyHealth); 
					}
					
					if (pts < 0.0)
					{
						double t = captureTick(from, p, unitVel(group.unitType), enemyVel);
						res += pts * (625 - t) / 625.0;
					}
					else
					{
						/*double t = captureTick(p, from, enemyVel, unitVel(group.unitType));
						res += 0.1* pts * (625 - t) / 625.0;*/
					}
				}
				
				if (pts > 0.0)
				{
					double pp = 1.0/(1.0 + dist2);
					res += pts * pp;
				}
				else
				{
					/*for (int mt = 0; mt < 5; ++mt)
					{
						if (group.healthByTypes[mt])
						{
							for (int et = 0; et < 5; ++et)
							{
								if (dCell.enemyHealth[et])
								{
									double rad2 = DANGER_DISTS.dist((UnitType) et, (UnitType) mt);
									//double rad2 = 150*150;
									if (rad2 > dist2)
									{
										double fraction = group.healthByTypes[mt] / group.health * dCell.enemyHealth[et] / dCell.totalEnemyHealth;
										double pn = (1.0 - std::min(1.0, dist2/rad2));
										res += pts * pn * fraction;
									}
								}
							}
						}
					}*/
					
					double pn = (1.0 - std::min(1.0, dist2/sqr(150)));
					res += pts * pn;
				}
				
				//res += pts / p.dist2(from);
			}
			
			f2hDmgRes += dCell.f2hDmg;
		}
	}
	
	if (group.unitType == UnitType::FIGHTER)
	{
		double L = 1.5 * WIDTH;
		if (group.size * 80 > group.health)
		{
			Group *arvG = getGroup(UnitType::ARV);
			if (arvG && arvG->size > 20)
			{
				L = arvG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::ARV] * (80 - group.health/group.size)*0.2;
	}
	
	/*if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		double L = 1.5 * WIDTH;
		if (f2hDmgRes > 0.0 && res < 0.0)
		{
			if (fivG && fivG->size > 10)
			{
				L = fivG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::IFV];
		
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 300.0)
			{
				res -= (l - 300.0);
			}
		}
	}*/
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::HELICOPTER);
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 400.0)
			{
				res -= (l - 400.0);
			}
		}
	}*/
	
	if (isGroundUnit(group.unitType))
	{
		for (const Building &b : buildings)
		{
			if (b.side != 0)
			{
				double d = from.dist(b.pos);
				double coef = b.assignedGroup == group.internalId ? 2.0 : 1.0;
				res += coef*group.health/(20 + d)*0.1;
			}
		}
	}
	
	return res;
}


ShrinkResult Strat::findShrink(Group &group)
{
	ShrinkResult result;
	
	Simulator sim;
	sim.tick = tick;
	std::copy(cells, cells + CELLS_X * CELLS_Y, sim.cells);
	
	BBox gbox = group.bbox;
	gbox.expand(2.0);
	for (const MyUnit &u : units)
	{
		if (gbox.inside(u.pos))
		{
			sim.units.push_back(u);
			MyUnit &tu = *sim.units.rbegin();
			tu.selected = group.check(tu);
			tu.vel = P(0, 0);
		}
	}
	sim.groups.push_back(group);
	
	double bestArea = WIDTH * HEIGHT;
	for (double x = -1; x <= 1; ++x)
	{
		for (double y = -1; y <= 1; ++y)
		{
			Simulator sim2 = sim;
			MyMove myMove;
			myMove.action = MyActionType::SCALE;
			myMove.p = group.center + P(x, y) * 10.0;
			myMove.factor = 0.2;
			sim2.registerMove(myMove, 0);
			
			int i = 0;
			for (; i < 20; ++i)
			{
				sim2.tick++;
				sim2.resetAxisSorts();
				sim2.applyMoves();
				int cnt = sim2.moveUnits();
				if (!cnt)
					break;
			}
			sim2.updateStats();
			
			double area = sim2.groups.rbegin()->bbox.area();
			if (area < bestArea)
			{
				bestArea = area;
				result.shrinkPoint = myMove.p;
				result.ticks = i;
				result.endBBox = sim2.groups.rbegin()->bbox;
			}
		}
	}
	
	return result;
}
}


namespace StratV20 {
	double groupPriority(const Group &g) {
	double res = 0.0;
	
	if (g.shrinkAfterNuke)
		res += 1100.0;
	else if (g.actionStarted)
		res += 1000.0;
	
	res -= g.lastUpdateTick;
	
	return res;
}

void DistributionMatrix::clear()
{
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		Cell &cell = cells[i];
		for (int j = 0; j < 5; ++j)
			cell.count[j] = 0;
		for (int j = 0; j < 5; ++j)
			cell.health[j] = 0;
	}
}

void DistributionMatrix::initialize(const Simulator &sim)
{
	clear();
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : sim.units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / DISTR_MAT_CELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / DISTR_MAT_CELL_SIZE;
				
				Cell &cell = getCell(x, y);
				int type = (int) u.type;
				cell.count[type] += 1.0 / K;
				cell.health[type] += u.durability / K;
			}
		}
	}
}

void DistributionMatrix::blur(DistributionMatrix &oth) const
{
	oth.clear();
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			Cell &resCell = oth.getCell(x, y);
			int cnt = 0;
			for (int yy = -1; yy <= 1; ++yy)
			{
				for (int xx = -1; xx <= 1; ++xx)
				{
					int x2 = x + xx;
					int y2 = y + yy;
					if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
					{
						++cnt;
						const Cell &cell = getCell(x2, y2);
						for (int i = 0; i < 5; ++i)
							resCell.count[i] += cell.count[i];
						for (int i = 0; i < 5; ++i)
							resCell.health[i] += cell.health[i];
					}
				}
			}
			
			const Cell &cell = getCell(x, y);
			for (int i = 0; i < 5; ++i)
			{
				if (!cell.count[i])
				{
					resCell.count[i] = 0;
					resCell.health[i] = 0;
				}
			}
			
			/*for (int i = 0; i < 5; ++i)
				resCell.count[i] += (resCell.count[i] + cnt - 1) / cnt;
			for (int i = 0; i < 5; ++i)
				resCell.health[i] /= cnt;*/
		}
	}
}

Strat::Strat::Strat()
{
	Group g;
	g.actionStarted = false;
	g.lastUpdateTick = 0;
	g.lastShrinkTick = 0;
	
	for (int i = 0; i < 1; ++i)
	{
		g.unitType = UnitType::HELICOPTER;
		//g.miniGroupInd = i;
		g.internalId = internalGroupSeq++;
		groups.push_back(g);
	}
	g.miniGroupInd = 0;
	
	g.unitType = UnitType::IFV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::FIGHTER;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::TANK;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::ARV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
}


void Strat::calcMicroShift(Group &group, P &shift)
{
	if (group.unitType != UnitType::TANK && group.unitType != UnitType::FIGHTER)
		return;
	
	BBox bbox = group.bbox;
	bbox.p1 += shift - P(35, 35);
	bbox.p2 += shift + P(35, 35);
	
	int finalScore = -100000;
	int currentScore = -100000;
	double ticks = 20.0;
	
	//if (group.unitType == UnitType::TANK)
	{
		std::vector<P> enemies[(int) UnitType::COUNT];
		std::vector<P> myUnits;
		
		for (const MyUnit &u : units)
		{
			if (u.side > 0 && (u.type == UnitType::TANK || u.type == UnitType::HELICOPTER || u.type == UnitType::IFV) && bbox.inside(u.pos))
			{
				enemies[(int) u.type].push_back(u.pos + u.vel * ticks);
			}
			else if (group.check(u))
			{
				myUnits.push_back(u.pos);
			}
		}
		
		double L = 8.0;
		P newShift = shift;
		for (int k = 0; k < 4; ++k)
		{
			P foundShift = newShift;
			int foundScore = -100000;
			for (int i = 0; i < 9; ++i)
			{
				P testShift = newShift + P(i / 3 - 1, i % 3 - 1) * L;
				int score = 0;
				bool isCurShift = shift.dist2(testShift) < 0.1;
				
				if (!isCurShift || currentScore == -100000)
				{
					const std::vector<MicroShiftValues> &pos = microShiftMatrix.pos[(int) group.unitType];
					if (!pos.empty())
					{
						for (const P &myP : myUnits)
						{
							P myPos = myP + testShift;
							bool found = false;
							for (const MicroShiftValues &vals : pos)
							{
								if (!found)
								{
									for (const P &enP : enemies[(int) vals.unitType])
									{
										if (myPos.dist2(enP) < vals.dist2)
										{
											score += vals.val;
											found = true;
											break;
										}
									}
								}
							}
						}
					}
					
					// !! decrease SCORE
					int posScore = score;
					score /= 2;
					
					const std::vector<MicroShiftValues> &neg = microShiftMatrix.neg[(int) group.unitType];
					if (!neg.empty())
					{
						for (const MicroShiftValues &vals : neg)
						{
							for (const P &enP : enemies[(int) vals.unitType])
							{
								for (const P &myP : myUnits)
								{
									P myPos = myP + testShift;
									if (myPos.dist2(enP) < vals.dist2)
									{
										score -= vals.val;
										break;
									}
								}
							}
						}
					}
					
					int negScore = posScore / 2 - score;
					
					if (foundScore < score)
					{
						foundShift = testShift;
						foundScore = score;
					}
					
					if (isCurShift)
						currentScore = score;
				}
			}
			
			L /= 2.0;
			newShift = foundShift;
			
			finalScore = foundScore;
		}
		
		if (currentScore >= finalScore)
			return;
		
		shift = newShift;
	}
}

void Strat::calcNuclearEfficiency()
{
	for (int i = 0; i < MICROCELLS_X * MICROCELLS_Y; i++)
		nuclearEfficiency[i] = 0;
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / MICROCELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / MICROCELL_SIZE;
				
				double efficiency = 100.0 / (10.0 + u.durability) / K;
				
				if (u.type == UnitType::ARV)
					efficiency /= 10.0;
				
				if (u.type == UnitType::FIGHTER || u.type == UnitType::HELICOPTER)
					efficiency /= 1.5;
				
				nuclearEfficiency[y * MICROCELLS_X + x] += efficiency;
			}
		}
	}
}

UnitType Strat::calcNextUnitTypeForConstruction(bool ground)
{
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType == UnitType::HELICOPTER)
		{
			return UnitType::TANK;
		}
	}
	
	if (enemyCount[UnitType::HELICOPTER] * 0.9 > myCount[UnitType::HELICOPTER])
	{
		return UnitType::HELICOPTER;
	}
	
	return UnitType::TANK;
	
	double score[5] = {};
	
	int enCnt = enemyCount[UnitType::HELICOPTER]*0.7 + enemyCount[UnitType::FIGHTER]*0.3;
	int myCnt = myCount[UnitType::FIGHTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::FIGHTER] += enCnt - myCnt;
	}
	score[(int) UnitType::FIGHTER] *= 0.6;
	
	enCnt = enemyCount[UnitType::TANK];
	myCnt = myCount[UnitType::HELICOPTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::HELICOPTER] += enCnt - myCnt;
	}
	
	score[(int) UnitType::HELICOPTER] *= 0.8;
	
	enCnt = enemyCount[UnitType::IFV];
	myCnt = myCount[UnitType::TANK];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::TANK] += enCnt - myCnt;
	}
	
	enCnt = enemyCount[UnitType::FIGHTER]*0.7 + enemyCount[UnitType::HELICOPTER]*0.3;
	myCnt = myCount[UnitType::IFV];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::IFV] += enCnt - myCnt;
	}
	
	
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE)
		{
			score[(int) b.unitType] -= 20;
		}
	}
	
	double grCount = 0;
	double airCount = 0;
	for (int i = 0; i < 5; ++i)
	{
		if (isGroundUnit((UnitType) i) && i != (int) UnitType::ARV)
		{
			grCount += myCount[(UnitType) i];
		}
		else
		{
			airCount += myCount[(UnitType) i];
		}
	}
	
	double totalCount = grCount + airCount;
	if (totalCount > 0)
	{
		for (int i = 0; i < 5; ++i)
		{
			if (isGroundUnit((UnitType) i))
			{
				score[i] *= airCount;
			}
			else
			{
				score[i] *= grCount;
			}
		}
	}
	
	int res = 0;
	int resType = -1;
	for (int i = 0; i < 5; ++i)
	{
		if (score[i] > res)
		{
			res = score[i];
			resType = i;
		}
	}
	
	if (resType >= 0)
		return (UnitType) resType;
	
	return UnitType::TANK;
}

MyMove Strat::calcNextMove()
{
	MyMove result;
	result.action = MyActionType::NONE;
	
	int moves = getAvailableActions(12);
	if (moves < 1)
		return result;
	
	if (players[1].remainingNuclearStrikeCooldownTicks < 30)
	{
		if (moves < 3)
			return result;
	}
	
	updateStats();
	
	if (players[1].nextNuclearStrikeTick >= 0)
	{
		bool select = false;
		bool expand = false;
		BBox bbox;
		int evaidingGroups = 0;
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep == 0)
			{
				BBox gbox = g.bbox;
				gbox.expand(40.0);
				if (gbox.inside(players[1].nuclearStrike))
				{
					bbox.add(g.bbox);
					g.nukeEvadeStep = 1;
					select = true;
					++evaidingGroups;
				}
			}
			else if (g.nukeEvadeStep == 1)
			{
				expand = true;
				g.nukeEvadeStep = 2;
				++evaidingGroups;
			}
			else
			{
				++evaidingGroups;
			}
		}
		
		if (select)
		{
			result.action = MyActionType::CLEAR_AND_SELECT;
			result.p1 = bbox.p1;
			result.p2 = bbox.p2;
			
			return result;
		}
		else if (expand)
		{
			result.action = MyActionType::SCALE;
			result.p = players[1].nuclearStrike;
			result.factor = 9.0;
			return result;
		}
		
		if (evaidingGroups && moves < 3)
			return result;
	}
	else
	{
		for (Group &g : groups)
		{
			if (tick - g.lastShrinkTick > 30)
				g.shrinkAfterNuke = false;
			if (g.nukeEvadeStep != 0)
			{
				g.shrinkAfterNuke = true;
				g.nukeEvadeStep = -1;
			}
		}
	}
	
	/*if (tick > 200) {
		int moves = getAvailableActions(4, 10);
		if (moves < 1) // throttle
			return result;
	}*/
	
	for (Building &b : buildings)
	{
		/*if (b.lastChangeUnitCount > b.unitCount)
			b.lastChangeUnitCount = b.unitCount;*/
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && (b.unitType == UnitType::NONE/* || (b.unitCount - b.lastChangeUnitCount) > 11*/))
		{
			//LOG("SVP " << buildingCaptured);
			result.action = MyActionType::SETUP_VEHICLE_PRODUCTION;
			result.facilityId = b.id;
			
			/*if (buildingCaptured % 2 == 0)
				result.unitType = UnitType::FIGHTER;
			else
				result.unitType = UnitType::IFV;*/
			
			//result.unitType = UnitType::TANK;
			result.unitType = calcNextUnitTypeForConstruction(false);
			//result.unitType = (UnitType) (buildingCaptured % 5);
			//b.lastChangeUnitCount = b.unitCount;
			
			++buildingCaptured;
			return result;
		}
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE && b.unitCount > 40 && b.productionProgress < 30 || b.createGroupStep > 0)
		{
			//LOG("MAKE GROUP");
			if (b.createGroupStep == 0 || b.createGroupStep == 1)
			{
				result.action = MyActionType::CLEAR_AND_SELECT;
				result.p1 = b.pos - P(32, 32);
				result.p2 = b.pos + P(32, 32);
				result.unitType = b.unitType;
				b.createGroupStep = 2;
				return result;
			}
			
			if (b.createGroupStep == 2)
			{
				// check if selected
				bool anySelected = false;
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.groups.count() == 0)
					{
						if (p.pos.x > b.pos.x - 32 && p.pos.x < b.pos.x + 32 && p.pos.y > b.pos.y - 32 && p.pos.y < b.pos.y + 32)
						{
							if (p.selected)
							{
								anySelected = true;
								break;
							}
						}
					}
				}
				
				if (anySelected)
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					b.createGroupStep = 3;
					
					Group newGroup;
					newGroup.unitType = b.unitType;
					newGroup.group = result.group;
					newGroup.internalId = internalGroupSeq++;
					groups.push_back(newGroup);
					
					b.createGroupStep = 0;
					return result;
				}
				else
				{
					b.createGroupStep = 1;
				}
			}
		}
	}
	
		
	if (!initialGroupsGerationDone && !buildings.empty())
	{
		for (Group &g : groups)
		{
			/*if (g.unitType == UnitType::HELICOPTER && !g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					P allCenter = P(0, 0);
					int count = 0;
					for (const MyUnit &u : units)
					{
						if (u.side == 0 && u.type == g.unitType)
						{
							allCenter += u.pos;
							++count;
						}
					}
					
					allCenter /= count;
					
					if (g.miniGroupInd == 0)
					{
						result.p1 = P(0, 0);
						result.p2 = allCenter;
					}
					else if (g.miniGroupInd == 1)
					{
						result.p1 = P(allCenter.x, 0);
						result.p2 = P(WIDTH, allCenter.y);
					}
					else if (g.miniGroupInd == 2)
					{
						result.p1 = P(0, allCenter.y);
						result.p2 = P(allCenter.x, HEIGHT);
					}
					else if (g.miniGroupInd == 3)
					{
						result.p1 = allCenter;
						result.p2 = P(WIDTH, HEIGHT);
					}
					
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}*/
			
			if (!g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else if (g.enumGroupBuildStep == 1)
				{
					result.action = MyActionType::ASSIGN;
					g.enumGroupBuildStep++;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}
		}
		
		initialGroupsGerationDone = true;
		LOG("DONE");
	}
	
	bool angryMode = angryModeTill > tick;

	
	DistributionMatrix matr;
	matr.initialize(*this);
	matr.blur(distributionMatrix);
	
	/*if (tick > 532)
	{
		std::cout.width(2);
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &resCell = distributionMatrix.getCell(x, y);
				std::cout << " " << resCell.count[1];
			}
			std::cout << std::endl;
		}
		std::cout << std::endl;
	}*/
	
	if (players[0].remainingNuclearStrikeCooldownTicks == 0)
	{
		resetCells();
		calcNuclearEfficiency();
		
		auto getEnemySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0.0;
			
			return nuclearEfficiency[y * MICROCELLS_X + x];
		};
		
		auto getMySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0;
			
			return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
				+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
				+ (int) getMicrocell(x, y, 0, UnitType::ARV).size() / 2;
		};
		
		P bestTarget;
		int bestScore = 0;
		
		for (int y = 0; y < MICROCELLS_Y; ++y) {
			for (int x = 0; x < MICROCELLS_X; ++x) {
				double enemyN = getEnemySize(x, y);
				if (enemyN > 10)
				{
					int totalMyCount = 0;
					int myCenterCount = 0;
					for (int xx = -1; xx <= 1; ++xx)
					{
						for (int yy = -1; yy <= 1; ++yy)
						{
							totalMyCount += getMySize(x + xx, y + yy);
							if (xx == 0 && yy == 0)
								myCenterCount = totalMyCount;
						}
					}
					
					if (totalMyCount < 20 && myCenterCount == 0)
					{
						int totalMyExtCount = totalMyCount;
						if (totalMyExtCount == 0)
						{
							for (int xx = -2; xx <= 2; ++xx)
							{
								for (int yy = -2; yy <= 2; ++yy)
								{
									if (xx < -1 || xx > 1 || yy < -1 || yy > 1)
										totalMyExtCount += getMySize(x + xx, y + yy);
								}
							}
						}
						
						if (totalMyExtCount > 0)
						{
							double totalEnemyCount =
								enemyN +
								getEnemySize(x - 1, y)/2 +
								getEnemySize(x + 1, y)/2 + 
								getEnemySize(x, y - 1)/2 + 
								getEnemySize(x, y + 1)/2;
							
							if (bestScore < totalEnemyCount)
							{
								bestScore = totalEnemyCount;
								bestTarget = P(x + 0.5, y + 0.5) * MICROCELL_SIZE;
							}
						}
					}
				}
			}
		}
		
		if (bestScore > 0)
		{
			// Correct point
			P correctedBestTarget = bestTarget;
			
			std::vector<P> myVehicles;
			std::vector<P> enemyVehicles;
			int myInnerN = 0;
			int enemyInnerN = 0;
			for (const MyUnit &p : units)
			{
				double d2 = p.pos.dist2(bestTarget);
				if (d2 < (75.0*75.0))
				{
					if (d2 < (38.0*38.0))
					{
						if (p.side == 0)
							myInnerN++;
						else
							enemyInnerN++;
					}
					else
					{
						if (p.side == 0)
							myVehicles.push_back(p.pos);
						else
							enemyVehicles.push_back(p.pos);
					}
				}
			}
			
			int foundScore = -100000;
			for (int xx = -2; xx <= 2; ++xx)
			{
				for (int yy = -2; yy <= 2; ++yy)
				{
					P newTarget = bestTarget + P(xx, yy) * 6.0;
					
					int myNum = myInnerN*3;
					int myExtNum = 0;
					
					for (const P &p : myVehicles)
					{
						double d2 = p.dist2(newTarget);
						if (d2 < (50.0*50.0))
						{
							myNum++;
						}
						else if (d2 > (55.0*55.0) && d2 < (70.0*70.0))
						{
							myExtNum++;
						}
					}
					
					if (myExtNum > 0)
					{
						int enNum = enemyInnerN*3;
						for (const P &p : enemyVehicles)
						{
							double d2 = p.dist2(newTarget);
							if (d2 < (50.0*50.0))
							{
								enNum++;
							}
						}
						
						int score = enNum - myNum * 4;
						if (score > foundScore)
						{
							foundScore = score;
							correctedBestTarget = newTarget;
						}
					}
				}
			}
			
			if (foundScore > -100000)
			{
				auto getMySize = [this](int x, int y) {
					if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
						return 0;
					
					return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
						+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
						+ (int) getMicrocell(x, y, 0, UnitType::ARV).size();
				};
				
				int pts = 0;
				long bestId = -1;
				P vehPos;
				
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.durability > 70)
					{
						double dist2 = p.pos.dist2(correctedBestTarget);
						if (dist2 > 55*55 && dist2 < 70*70)
						{
							bool outOfRange = false;
							
							if (p.vel.len2() > 0.01)
							{
								MyUnit u = p;
								for (int i = 0; i < 30; ++i)
								{
									double visRange = getVisionRange(u) - unitVel(p.type) * 10;
									if (u.pos.dist2(correctedBestTarget) > sqr(visRange))
									{
										outOfRange = true;
										break;
									}
									u.pos += u.vel;
								}
							}
							
							if (outOfRange)
								continue;
							
							int xx = p.pos.x / MICROCELL_SIZE;
							int yy = p.pos.y / MICROCELL_SIZE;
							
							//int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 60.0);
							if (pts < newPts)
							{
								pts = newPts;
								bestId = p.id;
								vehPos = p.pos;
							}
						}
					}
				}
				
				if (bestId >= 0)
				{
					result.action = MyActionType::TACTICAL_NUCLEAR_STRIKE;
					result.vehicleId = bestId;
					result.p = correctedBestTarget;
					return result;
				}
			}
		}
	}
	
	groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group &g){return g.size == 0;}), groups.end());
	
	if (result.action == MyActionType::NONE && !groups.empty())
	{
		debugAttractionPoints.clear();
		
		std::sort(groups.begin(), groups.end(), [](const Group &g1, const Group &g2){
			double p1 = groupPriority(g1);
			double p2 = groupPriority(g2);
			return p2 < p1;
		});
		
		assignBuildings();
		
		for (Group &group : groups)
		{
			if (tick - group.lastUpdateTick < 10 || group.nukeEvadeStep > 0)
				continue;
			
			if (group.shrinkActive)
			{
				if (tick - group.lastShrinkTick > 40 || !anyMoved(group))
				{
					group.shrinkActive = false;
				}
				else
				{
					continue;
				}
			}
			
			bool limitSpeed = false;
			if (nukeVehicleInd >= 0)
			{
				const MyUnit &u = units[nukeVehicleInd];
				if (group.check(u))
					limitSpeed = true;
			}
			
			calcDangerDistCells(group);
			
			P center = group.center;
			const BBox &bbox = group.bbox;
			int groupSize = group.size;
			double area = bbox.area();
			bool shrinkRequired = (area > groupSize * 40.0 || group.shrinkAfterNuke) && (((tick - group.lastUpdateTick) > 60 || group.shrinkAfterNuke) && (tick - group.lastShrinkTick) > 300);
			P shrinkPoint;
			if (shrinkRequired)
			{
				ShrinkResult shRes = findShrink(group);
				if (shRes.ticks > 7)
				{
					shrinkPoint = shRes.shrinkPoint;
				}
				else
				{
					shrinkRequired = false;
				}
				//LOG("SHRINK RES " << shRes.ticks << " " << shRes.endBBox << " P " << shRes.shrinkPoint);
			}
			//bool shrinkRequired = false;
			
			const double border = 20.0;
			P c = center;
			double R = 20 + unitVel(group.unitType) * 40;
			/*if (group.unitType == UnitType::ARV)
				R = 200.0;*/
			double ticks = R / unitVel(group.unitType);
			double curVal = attractionPoint(center, group, ticks, angryMode);
			P tp = center;
			bool found = false;
			
			bool canMoveFlag = false;
			
			P clampP1 = group.center - bbox.p1 + P(3.0, 3.0);
			P clampP2 = P(WIDTH - 3.0, HEIGHT - 3.0) + (group.center - bbox.p2);
			for (int k = 0; k < 3.0; ++k)
			{
				ticks = R / unitVel(group.unitType);
				int di = -1;
				
				for (int i = 0; i < 20; ++i)
				{
					P p = c + P(PI * 2.0 / 20.0 * i) * R;
					p = clampP(p, clampP1, clampP2);
					
					P shift = p - center;
					if (shift.len2() > 0.01)
					{
						if (canMove(p - center, group))
						{
							canMoveFlag = true;
							double val = attractionPoint(p, group, ticks, angryMode);
							
							DebugAttractionPointsInfo debugInfo;
							debugInfo.point = c;
							debugInfo.dir = p - c;
							debugInfo.val = val;
							debugAttractionPoints.push_back(debugInfo);
							
							if (val > curVal)
							{
								curVal = val;
								di = i;
								tp = p;
								found = true;
							}
						}
					}
				}
				
				if (di >= 0)
				{
					c = tp;
				}
				
				R /= 1.5;
			}
			
			if (!canMoveFlag)
			{
				double R = 20 + unitVel(group.unitType) * 40;
				
				std::vector<const MyUnit *> groupUnits;
				std::vector<const MyUnit *> otherUnits;
				
				BBox bbox = group.bbox;
				bbox.p1 -= P(2.0*R, 2.0*R);
				bbox.p2 += P(2.0*R, 2.0*R);
				
				for (const MyUnit &u : units)
				{
					if (group.check(u))
					{
						groupUnits.push_back(&u);
					}
					else if (group.canIntersectWith(u) && bbox.inside(u.pos))
					{
						otherUnits.push_back(&u);
					}
				}
				
				for (int k = 0; k < 3.0; ++k)
				{
					ticks = R / unitVel(group.unitType);
					int di = -1;
					
					for (int i = 0; i < 19; ++i)
					{
						P p = c + P(PI * 2.0 / 19.0 * i) * R;
						p = clampP(p, clampP1, clampP2);
						
						P shift = p - center;
						if (shift.len2() > 0.01)
						{
							if (canMoveDetailed(p - center, group, groupUnits, otherUnits))
							{
								canMoveFlag = true;
								double val = attractionPoint(p, group, ticks, angryMode);
								
								DebugAttractionPointsInfo debugInfo;
								debugInfo.point = c;
								debugInfo.dir = p - c;
								debugInfo.val = val;
								debugAttractionPoints.push_back(debugInfo);
								
								if (val > curVal)
								{
									curVal = val;
									di = i;
									tp = p;
									found = true;
								}
							}
						}
					}
					
					if (di >= 0)
					{
						c = tp;
					}
					
					R /= 1.5;
				}
				
				/*if (!canMoveFlag)
				{
					UnitType typeToSelect = UnitType::NONE;
					bool selected = false;
					GroupId otherGr = 0;
					for (const MyUnit *o : otherUnits)
					{
						if (group.bbox.inside(o->pos) && o->side == 0)
						{
							typeToSelect = o->type;
							if (o->selected)
								selected = true;
							
							if (o->groups.any())
							{
								for (Group &oth : groups)
								{
									if (oth.group != group.group && o->hasGroup(oth.group))
									{
										otherGr = oth.group;
										break;
									}
								}
								if (otherGr > 0)
									break;
							}
						}
					}
					
					if (typeToSelect != UnitType::NONE)
					{
						if (!selected)
						{
							result.p1 = group.bbox.p1;
							result.p2 = group.bbox.p2;
							result.unitType = typeToSelect;
							result.action = MyActionType::CLEAR_AND_SELECT;
							
							group.actionStarted = true;
							
							LOG("SELECT TO " << (int) group.group << " " << group.center);
							return result;
						}
						
						if (otherGr > 0)
						{
							result.action = MyActionType::DISMISS;
							result.group = otherGr;
							LOG("DISMISS TO " << (int) otherGr << " " << group.center);
							return result;
						}
						else
						{
							result.action = MyActionType::ASSIGN;
							result.group = group.group;
							LOG("ASSIGN TO " << (int) group.group << " " << group.center);
							return result;
						}
					}
				}*/
			}
			
			bool moveAway = false;
			P dirAway;
			P newShift = P(0, 0);
			if (!canMoveFlag)
			{
				//LOG("CANT MOVE " << group.center);
				group.canMove = false;
				
				dirAway = P(0, 0);
				for (Group &othGroup : groups)
				{
					if (&group != &othGroup && isGroundUnit(group.unitType) == isGroundUnit(othGroup.unitType))
					{
						P dp = group.center - othGroup.center;
						double clen = dp.len();
						
						if (clen < 150.0)
							dirAway += dp / clen;
					}
				}
				
				dirAway *= 10;
				dirAway += P(tick % 7 - 3, tick % 9 - 5);
				moveAway = true;
			}
			else
			{
				group.canMove = true;
				
				if (found)
					newShift = tp - center;
				
				if (newShift.len() < 15.0)
				{
					calcMicroShift(group, newShift);
				}
			}
			
			found = newShift.len2() > 0.1;
			
			if (found || shrinkRequired || moveAway)
			{
				if (!isSelected(group))
				{
					result = select(group);
					group.actionStarted = true;
				}
				else
				{
					if (shrinkRequired)
					{
						result.action = MyActionType::SCALE;
						result.factor = 0.2;
						result.p = shrinkPoint;
						group.lastShrinkTick = tick;
						group.shrinkActive = true;
						group.nukeEvadeStep = 0;
					}
					else if (found)
					{
						result.action = MyActionType::MOVE;
						result.p = newShift;
						
						if (limitSpeed)
							result.maxSpeed = unitVel(group.unitType) * 0.6;
						
						/*if (!limitSpeed)
						{
							limitSpeed = !anyEnemiesNearbyByDangerDistr(group);
							if (limitSpeed)
								result.maxSpeed = unitVel(group.unitType) * 0.8;
						}*/
						
						/*if (group.unitType == UnitType::FIGHTER)
							result.maxSpeed = 1.0;*/
					}
					else
					{
						result.action = MyActionType::MOVE;
						result.p = dirAway;
						//std::cout << "AWAY " << dirAway.x << " " << dirAway.y << std::endl;
					}
					
					group.lastUpdateTick = tick;
					group.actionStarted = false;
				}
				
				break;
			}
			else if (!found)
			{
				group.lastUpdateTick = tick;
			}
		}
	}
	
	return result;
}

void Strat::assignBuildings()
{
	std::set<Building *> pbuildings;
	
	for (Building &b : buildings)
	{
		if (b.side != 0)
		{
			pbuildings.insert(&b);
			b.assignedGroup = 0;
		}
	}
	
	std::set<Group *> pgroups;
	for (Group &g : groups)
	{
		if (isGroundUnit(g.unitType))
		{
			pgroups.insert(&g);
		}
	}
	
	size_t count = std::min(pbuildings.size(), pgroups.size());
	for (int i = 0; i < count; ++i)
	{
		double dist2 = sqr(100000.0);
		std::set<Building *>::iterator b = pbuildings.end();
		std::set<Group *>::iterator g = pgroups.end();
		for (std::set<Building *>::iterator bit = pbuildings.begin(); bit != pbuildings.end(); ++bit)
		{
			for (std::set<Group *>::iterator git = pgroups.begin(); git != pgroups.end(); ++git)
			{
				double d2 = (*bit)->pos.dist2((*git)->center);
				if (d2 < dist2)
				{
					dist2 = d2;
					b = bit;
					g = git;
				}
			}
		}
		
		if (b != pbuildings.end() && g != pgroups.end())
		{
			(*b)->assignedGroup = (*g)->internalId;
			pbuildings.erase(b);
			pgroups.erase(g);
		}
	}
}

bool Strat::anyEnemiesNearbyByDangerDistr(const Group &group)
{
	int x = group.center.x / DISTR_MAT_CELL_SIZE;
	int y = group.center.y / DISTR_MAT_CELL_SIZE;
	for (int yy = -7; yy <= 7; ++yy)
	{
		for (int xx = -7; xx <= 7; ++xx)
		{
			if (xx * xx + yy * yy <= 50)
			{
				int x2 = x + xx;
				int y2 = y + yy;
				if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
				{
					DangerDistCell &dCell = dangerDistCells[y2 * DISTR_MAT_CELLS_X + x2];
					if (dCell.totalEnemyDamage > 0.0 || dCell.totalMyDamage > 0.0)
					{
						return true;
					}
				}
			}
		}
	}
	
	return false;
}

void Strat::calcDangerDistCells(const Group &group)
{
	dngGr = &group;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
			double totalMyDamage = 0;
			
			for (int myType = 0; myType < 5; ++myType)
			{
				double typeDamage = 0;
				if (group.sizeByTypes[myType])
				{
					for (int enemyType = 0; enemyType < 5; ++enemyType)
					{
						if (cell.count[enemyType])
						{
							double dmg = getDamage((UnitType) myType, (UnitType) enemyType);
							typeDamage = std::max(typeDamage, std::min(dmg * group.sizeByTypes[myType], 120.0 * cell.count[enemyType]));
						}
					}
				}
				totalMyDamage += typeDamage;
			}
			
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			double f2hDmg = 0.0;
			dCell.totalEnemyDamage = 0.0;
			dCell.totalEnemyHealth = 0.0;
			for (int enemyType = 0; enemyType < 5; ++enemyType)
			{
				dCell.enemyDamage[enemyType] = 0.0;
				dCell.enemyHealth[enemyType] = 0.0;
				
				if (cell.count[enemyType])
				{
					dCell.enemyHealth[enemyType] += cell.health[enemyType];
					for (int myType = 0; myType < 5; ++myType)
					{
						if (group.sizeByTypes[myType])
						{
							double dmg = getDamage((UnitType) enemyType, (UnitType) myType);
							dCell.enemyDamage[enemyType] = std::min(dmg * cell.count[enemyType], 120.0 * group.sizeByTypes[myType]);
							
							if (enemyType == (int) UnitType::FIGHTER && myType == (int) UnitType::HELICOPTER)
								f2hDmg = dCell.enemyDamage[enemyType];
						}
					}
				}
				
				dCell.enemyDamage[enemyType] *= 1.5;
				dCell.totalEnemyDamage += dCell.enemyDamage[enemyType];
				dCell.totalEnemyHealth += dCell.enemyHealth[enemyType];
			}
			
			dCell.totalMyDamage = totalMyDamage;
			dCell.f2hDmg = f2hDmg;
		}
	}
}

struct DangerDist {
	double dists[25];
	
	DangerDist() {
		dist(UnitType::ARV, UnitType::ARV) = 150.0;
		dist(UnitType::ARV, UnitType::FIGHTER) = 150.0;
		dist(UnitType::ARV, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::ARV, UnitType::IFV) = 150.0;
		dist(UnitType::ARV, UnitType::TANK) = 150.0;
		
		dist(UnitType::FIGHTER, UnitType::ARV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::IFV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::HELICOPTER, UnitType::ARV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::IFV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::IFV, UnitType::ARV) = 150.0;
		dist(UnitType::IFV, UnitType::FIGHTER) = 100.0;
		dist(UnitType::IFV, UnitType::HELICOPTER) = 100.0;
		dist(UnitType::IFV, UnitType::IFV) = 150.0;
		dist(UnitType::IFV, UnitType::TANK) = 150.0;
		
		dist(UnitType::TANK, UnitType::ARV) = 150.0;
		dist(UnitType::TANK, UnitType::FIGHTER) = 1.0;
		dist(UnitType::TANK, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::TANK, UnitType::IFV) = 150.0;
		dist(UnitType::TANK, UnitType::TANK) = 150.0;
		
		for (int i = 0; i < 25; ++i)
			dists[i] = sqr(dists[i]);
	}
	
	double &dist(UnitType from, UnitType to) {
		return dists[(int) from * 5 + (int) to];
	}
} DANGER_DISTS;


bool isClosedSpaceDanger(const P &myP, const P &enP, double myVel, double enemyVel, double ticks)
{
	double myR = myVel * ticks + 0.1;
	double dangerRad = 70.0;
	double enR = enemyVel * ticks + dangerRad;
	double d = myP.dist(enP);
	
	if (myR + d < enR)
		return true;
	
	const double borderDist = 40.0;
	if (myP.x > (borderDist + myR) && myP.x < (WIDTH - borderDist - myR) && myP.y > (borderDist + myR) && myP.y < (HEIGHT - borderDist - myR))
		return false;
	
	P myPn = myP;
	P enPn = enP;
	if (myPn.x > WIDTH / 2.0)
	{
		myPn.x = WIDTH - myPn.x;
		enPn.x = WIDTH - enPn.x;
	}
	
	if (myPn.y > HEIGHT / 2.0)
	{
		myPn.y = HEIGHT - myPn.y;
		enPn.y = HEIGHT - enPn.y;
	}
	
	if (myPn.x < myPn.y)
	{
		std::swap(myPn.x, myPn.y);
		std::swap(enPn.x, enPn.y);
	}
	
	double borderDistX = std::min(borderDist, myPn.x);
	double borderDistY = std::min(borderDist, myPn.y);
	
	double b = sqrt(sqr(myR) - sqr(myPn.y - borderDistY));
	double X = myPn.x + b;
	
	if (P(X, borderDistY).dist2(enPn) > sqr(enR))
		return false;
	
	if (myR > (myPn.x - borderDistX))
	{
		double Y = myPn.y + sqrt(sqr(myR) - sqr(myPn.x - borderDistX));
		
		if (P(borderDistX, Y).dist2(enPn) > sqr(enR))
			return false;
	}
	else
	{
		X = myPn.x - b;
	
		if (P(X, borderDistY).dist2(enPn) > sqr(enR))
			return false;
	}
	
	return true;
}

double captureTick(const P &myP, const P &enP, double myVel, double enemyVel)
{
	double dT = 125.0;
	double oldT = 0.0;
	for (double t = 0.0; t <= 625;)
	{
		if (!isClosedSpaceDanger(myP, enP, myVel, enemyVel, t))
		{
			oldT = t;
			t += dT;
		}
		else
		{
			if (dT <= 1.0)
				return t;
			
			t = oldT;
			dT /= 5.0;
			t += dT;
		}
	}
	
	return 625;
}

double Strat::attractionPoint(const P from, const Group &group, double ticks, bool angryMode)
{
	double res = 0.0;
	double f2hDmgRes = 0.0;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			if (dCell.totalMyDamage > 0.0 || dCell.totalEnemyDamage > 0.0)
			{
				double alpha = 0.3;
				double alphaM1 = 0.7;
				
				P p = P((x + 0.5) * DISTR_MAT_CELL_SIZE, (y + 0.5) * DISTR_MAT_CELL_SIZE);
				
				double dist2 = p.dist2(from);
				
				double pts = (group.health * alphaM1 + dCell.totalEnemyHealth * alpha) / (dCell.totalEnemyHealth*0.01 + dCell.totalEnemyDamage) 
				- (dCell.totalEnemyHealth * alphaM1 + group.health * alpha) / (group.health * 0.01 + dCell.totalMyDamage);
				
				pts *= (1.0 + dCell.totalEnemyHealth*0.0003);
				
				if (pts != 0.0)
				{
					double enemyVel = 0.0;
					
					for (int i = 0; i < 5; ++i) 
					{ 
						if (dCell.enemyHealth[i]) 
							enemyVel += unitVel((UnitType) i) * (dCell.enemyHealth[i] / dCell.totalEnemyHealth); 
					}
					
					if (pts < 0.0)
					{
						double t = captureTick(from, p, unitVel(group.unitType), enemyVel);
						res += pts * (625 - t) / 625.0;
					}
					else
					{
						/*double t = captureTick(p, from, enemyVel, unitVel(group.unitType));
						res += 0.1* pts * (625 - t) / 625.0;*/
					}
				}
				
				if (pts > 0.0)
				{
					double pp = 1.0/(1.0 + dist2);
					res += pts * pp;
				}
				else
				{
					/*for (int mt = 0; mt < 5; ++mt)
					{
						if (group.healthByTypes[mt])
						{
							for (int et = 0; et < 5; ++et)
							{
								if (dCell.enemyHealth[et])
								{
									double rad2 = DANGER_DISTS.dist((UnitType) et, (UnitType) mt);
									//double rad2 = 150*150;
									if (rad2 > dist2)
									{
										double fraction = group.healthByTypes[mt] / group.health * dCell.enemyHealth[et] / dCell.totalEnemyHealth;
										double pn = (1.0 - std::min(1.0, dist2/rad2));
										res += pts * pn * fraction;
									}
								}
							}
						}
					}*/
					
					double pn = (1.0 - std::min(1.0, dist2/sqr(150)));
					res += pts * pn;
				}
				
				//res += pts / p.dist2(from);
			}
			
			f2hDmgRes += dCell.f2hDmg;
		}
	}
	
	if (group.unitType == UnitType::FIGHTER)
	{
		double L = 1.5 * WIDTH;
		if (group.size * 80 > group.health)
		{
			Group *arvG = getGroup(UnitType::ARV);
			if (arvG && arvG->size > 20)
			{
				L = arvG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::ARV] * (80 - group.health/group.size)*0.2;
	}
	
	/*if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		double L = 1.5 * WIDTH;
		if (f2hDmgRes > 0.0 && res < 0.0)
		{
			if (fivG && fivG->size > 10)
			{
				L = fivG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::IFV];
		
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 300.0)
			{
				res -= (l - 300.0);
			}
		}
	}*/
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::HELICOPTER);
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 400.0)
			{
				res -= (l - 400.0);
			}
		}
	}*/
	
	if (isGroundUnit(group.unitType))
	{
		for (const Building &b : buildings)
		{
			if (b.side != 0)
			{
				double d = from.dist(b.pos);
				double coef = b.assignedGroup == group.internalId ? 2.0 : 1.0;
				res += coef*group.health/(20 + d)*0.1;
			}
		}
	}
	
	return res;
}


ShrinkResult Strat::findShrink(Group &group)
{
	ShrinkResult result;
	
	Simulator sim;
	sim.tick = tick;
	std::copy(cells, cells + CELLS_X * CELLS_Y, sim.cells);
	
	BBox gbox = group.bbox;
	gbox.expand(2.0);
	for (const MyUnit &u : units)
	{
		if (gbox.inside(u.pos))
		{
			sim.units.push_back(u);
			MyUnit &tu = *sim.units.rbegin();
			tu.selected = group.check(tu);
			tu.vel = P(0, 0);
		}
	}
	sim.groups.push_back(group);
	
	double bestArea = WIDTH * HEIGHT;
	for (double x = -1; x <= 1; ++x)
	{
		for (double y = -1; y <= 1; ++y)
		{
			Simulator sim2 = sim;
			MyMove myMove;
			myMove.action = MyActionType::SCALE;
			myMove.p = group.center + P(x, y) * 10.0;
			myMove.factor = 0.2;
			sim2.registerMove(myMove, 0);
			
			int i = 0;
			for (; i < 20; ++i)
			{
				sim2.tick++;
				sim2.resetAxisSorts();
				sim2.applyMoves();
				int cnt = sim2.moveUnits();
				if (!cnt)
					break;
			}
			sim2.updateStats();
			
			double area = sim2.groups.rbegin()->bbox.area();
			if (area < bestArea)
			{
				bestArea = area;
				result.shrinkPoint = myMove.p;
				result.ticks = i;
				result.endBBox = sim2.groups.rbegin()->bbox;
			}
		}
	}
	
	return result;
}

}


namespace StratV21 {
	double groupPriority(const Group &g) {
	double res = 0.0;
	
	if (g.shrinkAfterNuke)
		res += 1100.0;
	else if (g.actionStarted)
		res += 1000.0;
	
	res -= g.lastUpdateTick;
	
	return res;
}

void DistributionMatrix::clear()
{
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		Cell &cell = cells[i];
		for (int j = 0; j < 5; ++j)
			cell.count[j] = 0;
		for (int j = 0; j < 5; ++j)
			cell.health[j] = 0;
	}
}

void DistributionMatrix::initialize(const Simulator &sim)
{
	clear();
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : sim.units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / DISTR_MAT_CELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / DISTR_MAT_CELL_SIZE;
				
				Cell &cell = getCell(x, y);
				int type = (int) u.type;
				cell.count[type] += 1.0 / K;
				cell.health[type] += u.durability / K;
			}
		}
	}
}

void DistributionMatrix::blur(DistributionMatrix &oth) const
{
	oth.clear();
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			Cell &resCell = oth.getCell(x, y);
			int cnt = 0;
			for (int yy = -1; yy <= 1; ++yy)
			{
				for (int xx = -1; xx <= 1; ++xx)
				{
					int x2 = x + xx;
					int y2 = y + yy;
					if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
					{
						++cnt;
						const Cell &cell = getCell(x2, y2);
						for (int i = 0; i < 5; ++i)
							resCell.count[i] += cell.count[i];
						for (int i = 0; i < 5; ++i)
							resCell.health[i] += cell.health[i];
					}
				}
			}
			
			const Cell &cell = getCell(x, y);
			for (int i = 0; i < 5; ++i)
			{
				if (!cell.count[i])
				{
					resCell.count[i] = 0;
					resCell.health[i] = 0;
				}
			}
			
			/*for (int i = 0; i < 5; ++i)
				resCell.count[i] += (resCell.count[i] + cnt - 1) / cnt;
			for (int i = 0; i < 5; ++i)
				resCell.health[i] /= cnt;*/
		}
	}
}

Strat::Strat::Strat()
{
	Group g;
	g.actionStarted = false;
	g.lastUpdateTick = 0;
	g.lastShrinkTick = 0;
	
	for (int i = 0; i < 1; ++i)
	{
		g.unitType = UnitType::HELICOPTER;
		//g.miniGroupInd = i;
		g.internalId = internalGroupSeq++;
		groups.push_back(g);
	}
	g.miniGroupInd = 0;
	
	g.unitType = UnitType::IFV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::FIGHTER;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::TANK;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::ARV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
}


void Strat::calcMicroShift(Group &group, P &shift)
{
	if (group.unitType != UnitType::TANK && group.unitType != UnitType::FIGHTER)
		return;
	
	BBox bbox = group.bbox;
	bbox.p1 += shift - P(35, 35);
	bbox.p2 += shift + P(35, 35);
	
	int finalScore = -100000;
	int currentScore = -100000;
	double ticks = 20.0;
	
	//if (group.unitType == UnitType::TANK)
	{
		std::vector<P> enemies[(int) UnitType::COUNT];
		std::vector<P> myUnits;
		
		for (const MyUnit &u : units)
		{
			if (u.side > 0 && (u.type == UnitType::TANK || u.type == UnitType::HELICOPTER || u.type == UnitType::IFV) && bbox.inside(u.pos))
			{
				enemies[(int) u.type].push_back(u.pos + u.vel * ticks);
			}
			else if (group.check(u))
			{
				myUnits.push_back(u.pos);
			}
		}
		
		double L = 8.0;
		P newShift = shift;
		for (int k = 0; k < 4; ++k)
		{
			P foundShift = newShift;
			int foundScore = -100000;
			for (int i = 0; i < 9; ++i)
			{
				P testShift = newShift + P(i / 3 - 1, i % 3 - 1) * L;
				int score = 0;
				bool isCurShift = shift.dist2(testShift) < 0.1;
				
				if (!isCurShift || currentScore == -100000)
				{
					const std::vector<MicroShiftValues> &pos = microShiftMatrix.pos[(int) group.unitType];
					if (!pos.empty())
					{
						for (const P &myP : myUnits)
						{
							P myPos = myP + testShift;
							bool found = false;
							for (const MicroShiftValues &vals : pos)
							{
								if (!found)
								{
									for (const P &enP : enemies[(int) vals.unitType])
									{
										if (myPos.dist2(enP) < vals.dist2)
										{
											score += vals.val;
											found = true;
											break;
										}
									}
								}
							}
						}
					}
					
					// !! decrease SCORE
					int posScore = score;
					score /= 2;
					
					const std::vector<MicroShiftValues> &neg = microShiftMatrix.neg[(int) group.unitType];
					if (!neg.empty())
					{
						for (const MicroShiftValues &vals : neg)
						{
							for (const P &enP : enemies[(int) vals.unitType])
							{
								for (const P &myP : myUnits)
								{
									P myPos = myP + testShift;
									if (myPos.dist2(enP) < vals.dist2)
									{
										score -= vals.val;
										break;
									}
								}
							}
						}
					}
					
					int negScore = posScore / 2 - score;
					
					if (foundScore < score)
					{
						foundShift = testShift;
						foundScore = score;
					}
					
					if (isCurShift)
						currentScore = score;
				}
			}
			
			L /= 2.0;
			newShift = foundShift;
			
			finalScore = foundScore;
		}
		
		if (currentScore >= finalScore)
			return;
		
		shift = newShift;
	}
}

void Strat::calcNuclearEfficiency()
{
	for (int i = 0; i < MICROCELLS_X * MICROCELLS_Y; i++)
		nuclearEfficiency[i] = 0;
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / MICROCELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / MICROCELL_SIZE;
				
				double efficiency = 100.0 / (10.0 + u.durability) / K;
				
				if (u.type == UnitType::ARV)
					efficiency /= 10.0;
				
				if (u.type == UnitType::FIGHTER || u.type == UnitType::HELICOPTER)
					efficiency /= 1.5;
				
				nuclearEfficiency[y * MICROCELLS_X + x] += efficiency;
			}
		}
	}
}

UnitType Strat::calcNextUnitTypeForConstruction(bool ground)
{
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType == UnitType::HELICOPTER)
		{
			return UnitType::TANK;
		}
	}
	
	if (enemyCount[UnitType::HELICOPTER] * 0.9 > myCount[UnitType::HELICOPTER])
	{
		return UnitType::HELICOPTER;
	}
	
	return UnitType::TANK;
	
	double score[5] = {};
	
	int enCnt = enemyCount[UnitType::HELICOPTER]*0.7 + enemyCount[UnitType::FIGHTER]*0.3;
	int myCnt = myCount[UnitType::FIGHTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::FIGHTER] += enCnt - myCnt;
	}
	score[(int) UnitType::FIGHTER] *= 0.6;
	
	enCnt = enemyCount[UnitType::TANK];
	myCnt = myCount[UnitType::HELICOPTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::HELICOPTER] += enCnt - myCnt;
	}
	
	score[(int) UnitType::HELICOPTER] *= 0.8;
	
	enCnt = enemyCount[UnitType::IFV];
	myCnt = myCount[UnitType::TANK];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::TANK] += enCnt - myCnt;
	}
	
	enCnt = enemyCount[UnitType::FIGHTER]*0.7 + enemyCount[UnitType::HELICOPTER]*0.3;
	myCnt = myCount[UnitType::IFV];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::IFV] += enCnt - myCnt;
	}
	
	
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE)
		{
			score[(int) b.unitType] -= 20;
		}
	}
	
	double grCount = 0;
	double airCount = 0;
	for (int i = 0; i < 5; ++i)
	{
		if (isGroundUnit((UnitType) i) && i != (int) UnitType::ARV)
		{
			grCount += myCount[(UnitType) i];
		}
		else
		{
			airCount += myCount[(UnitType) i];
		}
	}
	
	double totalCount = grCount + airCount;
	if (totalCount > 0)
	{
		for (int i = 0; i < 5; ++i)
		{
			if (isGroundUnit((UnitType) i))
			{
				score[i] *= airCount;
			}
			else
			{
				score[i] *= grCount;
			}
		}
	}
	
	int res = 0;
	int resType = -1;
	for (int i = 0; i < 5; ++i)
	{
		if (score[i] > res)
		{
			res = score[i];
			resType = i;
		}
	}
	
	if (resType >= 0)
		return (UnitType) resType;
	
	return UnitType::TANK;
}

MyMove Strat::calcNextMove()
{
	MyMove result;
	result.action = MyActionType::NONE;
	
	int moves = getAvailableActions(12);
	if (moves < 1)
		return result;
	
	if (players[1].remainingNuclearStrikeCooldownTicks < 30)
	{
		if (moves < 3)
			return result;
	}
	
	updateStats();
	
	if (players[1].nextNuclearStrikeTick >= 0)
	{
		bool select = false;
		bool expand = false;
		BBox bbox;
		int evaidingGroups = 0;
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep == 0)
			{
				BBox gbox = g.bbox;
				gbox.expand(40.0);
				if (gbox.inside(players[1].nuclearStrike))
				{
					bbox.add(g.bbox);
					g.nukeEvadeStep = 1;
					select = true;
					++evaidingGroups;
				}
			}
			else if (g.nukeEvadeStep == 1)
			{
				expand = true;
				g.nukeEvadeStep = 2;
				++evaidingGroups;
			}
			else
			{
				++evaidingGroups;
			}
		}
		
		if (select)
		{
			result.action = MyActionType::CLEAR_AND_SELECT;
			result.p1 = bbox.p1;
			result.p2 = bbox.p2;
			
			return result;
		}
		else if (expand)
		{
			result.action = MyActionType::SCALE;
			result.p = players[1].nuclearStrike;
			result.factor = 9.0;
			return result;
		}
		
		if (evaidingGroups && moves < 3)
			return result;
	}
	else
	{
		for (Group &g : groups)
		{
			if (tick - g.lastShrinkTick > 30)
				g.shrinkAfterNuke = false;
			if (g.nukeEvadeStep != 0)
			{
				g.shrinkAfterNuke = true;
				g.nukeEvadeStep = -1;
			}
		}
	}
	
	/*if (tick > 200) {
		int moves = getAvailableActions(4, 10);
		if (moves < 1) // throttle
			return result;
	}*/
	
	for (Building &b : buildings)
	{
		/*if (b.lastChangeUnitCount > b.unitCount)
			b.lastChangeUnitCount = b.unitCount;*/
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && (b.unitType == UnitType::NONE/* || (b.unitCount - b.lastChangeUnitCount) > 11*/))
		{
			//LOG("SVP " << buildingCaptured);
			result.action = MyActionType::SETUP_VEHICLE_PRODUCTION;
			result.facilityId = b.id;
			
			/*if (buildingCaptured % 2 == 0)
				result.unitType = UnitType::FIGHTER;
			else
				result.unitType = UnitType::IFV;*/
			
			//result.unitType = UnitType::TANK;
			result.unitType = calcNextUnitTypeForConstruction(false);
			//result.unitType = (UnitType) (buildingCaptured % 5);
			//b.lastChangeUnitCount = b.unitCount;
			
			++buildingCaptured;
			return result;
		}
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE && b.unitCount > 40 && b.productionProgress < 30
			|| b.createGroupStep > 0
			//|| b.side != 0 && b.unitCount > 0 && b.type == BuildingType::VEHICLE_FACTORY
		)
		{
			//LOG("MAKE GROUP");
			if (b.createGroupStep == 0 || b.createGroupStep == 1)
			{
				BBox bbox;
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && !p.groups.any() && b.checkPoint(p.pos))
					{
						bbox.add(p.pos);
					}
				}
				
				result.p1 = bbox.p1 - P(1, 1);
				result.p2 = bbox.p2 + P(1, 1);
				
				result.action = MyActionType::CLEAR_AND_SELECT;
				/*result.p1 = b.pos - P(32, 32); 
				result.p2 = b.pos + P(32, 32); */
				
				result.unitType = b.unitType;
				b.createGroupStep = 2;
				return result;
			}
			
			if (b.createGroupStep == 2)
			{
				// check if selected
				bool anySelected = false;
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.groups.count() == 0)
					{
						if (p.pos.x > b.pos.x - 32 && p.pos.x < b.pos.x + 32 && p.pos.y > b.pos.y - 32 && p.pos.y < b.pos.y + 32)
						{
							if (p.selected)
							{
								anySelected = true;
								break;
							}
						}
					}
				}
				
				if (anySelected)
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					b.createGroupStep = 3;
					
					Group newGroup;
					newGroup.unitType = b.unitType;
					newGroup.group = result.group;
					newGroup.internalId = internalGroupSeq++;
					groups.push_back(newGroup);
					
					b.createGroupStep = 0;
					return result;
				}
				else
				{
					b.createGroupStep = 1;
				}
			}
		}
	}
	
		
	if (!initialGroupsGerationDone && !buildings.empty())
	{
		for (Group &g : groups)
		{
			/*if (g.unitType == UnitType::HELICOPTER && !g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					P allCenter = P(0, 0);
					int count = 0;
					for (const MyUnit &u : units)
					{
						if (u.side == 0 && u.type == g.unitType)
						{
							allCenter += u.pos;
							++count;
						}
					}
					
					allCenter /= count;
					
					if (g.miniGroupInd == 0)
					{
						result.p1 = P(0, 0);
						result.p2 = allCenter;
					}
					else if (g.miniGroupInd == 1)
					{
						result.p1 = P(allCenter.x, 0);
						result.p2 = P(WIDTH, allCenter.y);
					}
					else if (g.miniGroupInd == 2)
					{
						result.p1 = P(0, allCenter.y);
						result.p2 = P(allCenter.x, HEIGHT);
					}
					else if (g.miniGroupInd == 3)
					{
						result.p1 = allCenter;
						result.p2 = P(WIDTH, HEIGHT);
					}
					
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}*/
			
			if (!g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else if (g.enumGroupBuildStep == 1)
				{
					result.action = MyActionType::ASSIGN;
					g.enumGroupBuildStep++;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}
		}
		
		initialGroupsGerationDone = true;
		LOG("DONE");
	}
	
	bool angryMode = angryModeTill > tick;

	
	DistributionMatrix matr;
	matr.initialize(*this);
	matr.blur(distributionMatrix);
	
	/*if (tick > 532)
	{
		std::cout.width(2);
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &resCell = distributionMatrix.getCell(x, y);
				std::cout << " " << resCell.count[1];
			}
			std::cout << std::endl;
		}
		std::cout << std::endl;
	}*/
	
	if (players[0].remainingNuclearStrikeCooldownTicks == 0)
	{
		resetCells();
		calcNuclearEfficiency();
		
		auto getEnemySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0.0;
			
			return nuclearEfficiency[y * MICROCELLS_X + x];
		};
		
		auto getMySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0;
			
			return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
				+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
				+ (int) getMicrocell(x, y, 0, UnitType::ARV).size() / 2;
		};
		
		P bestTarget;
		int bestScore = 0;
		
		for (int y = 0; y < MICROCELLS_Y; ++y) {
			for (int x = 0; x < MICROCELLS_X; ++x) {
				double enemyN = getEnemySize(x, y);
				if (enemyN > 10)
				{
					int totalMyCount = 0;
					int myCenterCount = 0;
					for (int xx = -1; xx <= 1; ++xx)
					{
						for (int yy = -1; yy <= 1; ++yy)
						{
							totalMyCount += getMySize(x + xx, y + yy);
							if (xx == 0 && yy == 0)
								myCenterCount = totalMyCount;
						}
					}
					
					if (totalMyCount < 20 && myCenterCount == 0)
					{
						int totalMyExtCount = totalMyCount;
						if (totalMyExtCount == 0)
						{
							for (int xx = -2; xx <= 2; ++xx)
							{
								for (int yy = -2; yy <= 2; ++yy)
								{
									if (xx < -1 || xx > 1 || yy < -1 || yy > 1)
										totalMyExtCount += getMySize(x + xx, y + yy);
								}
							}
						}
						
						if (totalMyExtCount > 0)
						{
							double totalEnemyCount =
								enemyN +
								getEnemySize(x - 1, y)/2 +
								getEnemySize(x + 1, y)/2 + 
								getEnemySize(x, y - 1)/2 + 
								getEnemySize(x, y + 1)/2;
							
							if (bestScore < totalEnemyCount)
							{
								bestScore = totalEnemyCount;
								bestTarget = P(x + 0.5, y + 0.5) * MICROCELL_SIZE;
							}
						}
					}
				}
			}
		}
		
		if (bestScore > 0)
		{
			// Correct point
			P correctedBestTarget = bestTarget;
			
			std::vector<P> myVehicles;
			std::vector<P> enemyVehicles;
			int myInnerN = 0;
			int enemyInnerN = 0;
			for (const MyUnit &p : units)
			{
				double d2 = p.pos.dist2(bestTarget);
				if (d2 < (75.0*75.0))
				{
					if (d2 < (38.0*38.0))
					{
						if (p.side == 0)
							myInnerN++;
						else
							enemyInnerN++;
					}
					else
					{
						if (p.side == 0)
							myVehicles.push_back(p.pos);
						else
							enemyVehicles.push_back(p.pos);
					}
				}
			}
			
			int foundScore = -100000;
			for (int xx = -2; xx <= 2; ++xx)
			{
				for (int yy = -2; yy <= 2; ++yy)
				{
					P newTarget = bestTarget + P(xx, yy) * 6.0;
					
					int myNum = myInnerN*3;
					int myExtNum = 0;
					
					for (const P &p : myVehicles)
					{
						double d2 = p.dist2(newTarget);
						if (d2 < (50.0*50.0))
						{
							myNum++;
						}
						else if (d2 > (55.0*55.0) && d2 < (70.0*70.0))
						{
							myExtNum++;
						}
					}
					
					if (myExtNum > 0)
					{
						int enNum = enemyInnerN*3;
						for (const P &p : enemyVehicles)
						{
							double d2 = p.dist2(newTarget);
							if (d2 < (50.0*50.0))
							{
								enNum++;
							}
						}
						
						int score = enNum - myNum * 4;
						if (score > foundScore)
						{
							foundScore = score;
							correctedBestTarget = newTarget;
						}
					}
				}
			}
			
			if (foundScore > -100000)
			{
				auto getMySize = [this](int x, int y) {
					if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
						return 0;
					
					return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
						+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
						+ (int) getMicrocell(x, y, 0, UnitType::ARV).size();
				};
				
				int pts = 0;
				long bestId = -1;
				P vehPos;
				
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.durability > 70)
					{
						double dist2 = p.pos.dist2(correctedBestTarget);
						if (dist2 > 55*55 && dist2 < 70*70)
						{
							bool outOfRange = false;
							
							if (p.vel.len2() > 0.01)
							{
								MyUnit u = p;
								for (int i = 0; i < 30; ++i)
								{
									double visRange = getVisionRange(u) - unitVel(p.type) * 10;
									if (u.pos.dist2(correctedBestTarget) > sqr(visRange))
									{
										outOfRange = true;
										break;
									}
									u.pos += u.vel;
								}
							}
							
							if (outOfRange)
								continue;
							
							int xx = p.pos.x / MICROCELL_SIZE;
							int yy = p.pos.y / MICROCELL_SIZE;
							
							//int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 60.0);
							if (pts < newPts)
							{
								pts = newPts;
								bestId = p.id;
								vehPos = p.pos;
							}
						}
					}
				}
				
				if (bestId >= 0)
				{
					result.action = MyActionType::TACTICAL_NUCLEAR_STRIKE;
					result.vehicleId = bestId;
					result.p = correctedBestTarget;
					return result;
				}
			}
		}
	}
	
	groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group &g){return g.size == 0;}), groups.end());
	
	if (result.action == MyActionType::NONE && !groups.empty())
	{
		debugAttractionPoints.clear();
		
		std::sort(groups.begin(), groups.end(), [](const Group &g1, const Group &g2){
			double p1 = groupPriority(g1);
			double p2 = groupPriority(g2);
			return p2 < p1;
		});
		
		assignBuildings();
		updateGroupAttraction();
		
		for (auto groupIt = groups.begin(); groupIt != groups.end(); ++groupIt)
		{
			Group &group = *groupIt;
			
			if (tick - group.lastComputeTick < 10 || group.nukeEvadeStep > 0)
				continue;
			
			if (group.shrinkActive)
			{
				if (tick - group.lastShrinkTick > 40 || !anyMoved(group))
				{
					group.shrinkActive = false;
				}
				else
				{
					continue;
				}
			}
			
			if (group.attractedToGroup >= 0)
			{
				Group &othG = groups[group.attractedToGroup];
				double dist2 = othG.center.dist2(group.center);
				if (dist2 < sqr(40))
				{
					if (!isSelected(group))
					{
						result = select(group);
						group.actionStarted = true;
						return result;
					}
					
					result.action = MyActionType::ASSIGN;
					result.group = othG.group;
					group.group = 0;
					group.unitType = UnitType::NONE;
					
					groups.erase(groupIt);
					//LOG("JOIN GROUPS ASSIGN " << (int) othG.group);
					return result;
				}
			}
			
			bool limitSpeed = false;
			if (nukeVehicleInd >= 0)
			{
				const MyUnit &u = units[nukeVehicleInd];
				if (group.check(u))
					limitSpeed = true;
			}
			
			calcDangerDistCells(group);
			
			P center = group.center;
			const BBox &bbox = group.bbox;
			int groupSize = group.size;
			double area = bbox.area();
			bool shrinkRequired = (area > groupSize * 40.0 || group.shrinkAfterNuke) && (((tick - group.lastUpdateTick) > 60 || group.shrinkAfterNuke) && (tick - group.lastShrinkTick) > 300);
			P shrinkPoint;
			if (shrinkRequired)
			{
				ShrinkResult shRes = findShrink(group);
				if (shRes.ticks > 7)
				{
					shrinkPoint = shRes.shrinkPoint;
				}
				else
				{
					shrinkRequired = false;
				}
				//LOG("SHRINK RES " << shRes.ticks << " " << shRes.endBBox << " P " << shRes.shrinkPoint);
			}
			//bool shrinkRequired = false;
			
			const double border = 20.0;
			P c = center;
			double R = 20 + unitVel(group.unitType) * 40;
			/*if (group.unitType == UnitType::ARV)
				R = 200.0;*/
			double ticks = R / unitVel(group.unitType);
			double curVal = attractionPoint(center, group, ticks, angryMode);
			P tp = center;
			bool found = false;
			
			bool canMoveFlag = false;
			
			P clampP1 = group.center - bbox.p1 + P(3.0, 3.0);
			P clampP2 = P(WIDTH - 3.0, HEIGHT - 3.0) + (group.center - bbox.p2);
			for (int k = 0; k < 3.0; ++k)
			{
				ticks = R / unitVel(group.unitType);
				int di = -1;
				
				for (int i = 0; i < 20; ++i)
				{
					P p = c + P(PI * 2.0 / 20.0 * i) * R;
					p = clampP(p, clampP1, clampP2);
					
					P shift = p - center;
					if (shift.len2() > 0.01)
					{
						if (canMove(p - center, group))
						{
							canMoveFlag = true;
							double val = attractionPoint(p, group, ticks, angryMode);
							
							DebugAttractionPointsInfo debugInfo;
							debugInfo.point = c;
							debugInfo.dir = p - c;
							debugInfo.val = val;
							debugAttractionPoints.push_back(debugInfo);
							
							if (val > curVal)
							{
								curVal = val;
								di = i;
								tp = p;
								found = true;
							}
						}
					}
				}
				
				if (di >= 0)
				{
					c = tp;
				}
				
				R /= 1.5;
			}
			
			if (!canMoveFlag)
			{
				double R = 20 + unitVel(group.unitType) * 40;
				
				std::vector<const MyUnit *> groupUnits;
				std::vector<const MyUnit *> otherUnits;
				
				BBox bbox = group.bbox;
				bbox.p1 -= P(2.0*R, 2.0*R);
				bbox.p2 += P(2.0*R, 2.0*R);
				
				for (const MyUnit &u : units)
				{
					if (group.check(u))
					{
						groupUnits.push_back(&u);
					}
					else if (group.canIntersectWith(u) && bbox.inside(u.pos))
					{
						otherUnits.push_back(&u);
					}
				}
				
				for (int k = 0; k < 3.0; ++k)
				{
					ticks = R / unitVel(group.unitType);
					int di = -1;
					
					for (int i = 0; i < 19; ++i)
					{
						P p = c + P(PI * 2.0 / 19.0 * i) * R;
						p = clampP(p, clampP1, clampP2);
						
						P shift = p - center;
						if (shift.len2() > 0.01)
						{
							if (canMoveDetailed(p - center, group, groupUnits, otherUnits))
							{
								canMoveFlag = true;
								double val = attractionPoint(p, group, ticks, angryMode);
								
								DebugAttractionPointsInfo debugInfo;
								debugInfo.point = c;
								debugInfo.dir = p - c;
								debugInfo.val = val;
								debugAttractionPoints.push_back(debugInfo);
								
								if (val > curVal)
								{
									curVal = val;
									di = i;
									tp = p;
									found = true;
								}
							}
						}
					}
					
					if (di >= 0)
					{
						c = tp;
					}
					
					R /= 1.5;
				}
				
				/*if (!canMoveFlag)
				{
					UnitType typeToSelect = UnitType::NONE;
					bool selected = false;
					GroupId otherGr = 0;
					for (const MyUnit *o : otherUnits)
					{
						if (group.bbox.inside(o->pos) && o->side == 0)
						{
							typeToSelect = o->type;
							if (o->selected)
								selected = true;
							
							if (o->groups.any())
							{
								for (Group &oth : groups)
								{
									if (oth.group != group.group && o->hasGroup(oth.group))
									{
										otherGr = oth.group;
										break;
									}
								}
								if (otherGr > 0)
									break;
							}
						}
					}
					
					if (typeToSelect != UnitType::NONE)
					{
						if (!selected)
						{
							result.p1 = group.bbox.p1;
							result.p2 = group.bbox.p2;
							result.unitType = typeToSelect;
							result.action = MyActionType::CLEAR_AND_SELECT;
							
							group.actionStarted = true;
							
							LOG("SELECT TO " << (int) group.group << " " << group.center);
							return result;
						}
						
						if (otherGr > 0)
						{
							result.action = MyActionType::DISMISS;
							result.group = otherGr;
							LOG("DISMISS TO " << (int) otherGr << " " << group.center);
							return result;
						}
						else
						{
							result.action = MyActionType::ASSIGN;
							result.group = group.group;
							LOG("ASSIGN TO " << (int) group.group << " " << group.center);
							return result;
						}
					}
				}*/
			}
			
			bool moveAway = false;
			P dirAway;
			P newShift = P(0, 0);
			if (!canMoveFlag)
			{
				//LOG("CANT MOVE " << group.center);
				group.canMove = false;
				
				dirAway = P(0, 0);
				for (Group &othGroup : groups)
				{
					if (&group != &othGroup && isGroundUnit(group.unitType) == isGroundUnit(othGroup.unitType))
					{
						P dp = group.center - othGroup.center;
						double clen = dp.len();
						
						if (clen < 150.0)
							dirAway += dp / clen;
					}
				}
				
				dirAway *= 10;
				dirAway += P(tick % 7 - 3, tick % 9 - 5);
				moveAway = true;
			}
			else
			{
				group.canMove = true;
				
				if (found)
					newShift = tp - center;
				
				if (newShift.len() < 15.0)
				{
					calcMicroShift(group, newShift);
				}
			}
			
			found = newShift.len2() > 0.1;
			
			if (found || shrinkRequired || moveAway)
			{
				if (!isSelected(group))
				{
					result = select(group);
					group.actionStarted = true;
				}
				else
				{
					if (shrinkRequired)
					{
						result.action = MyActionType::SCALE;
						result.factor = 0.2;
						result.p = shrinkPoint;
						group.lastShrinkTick = tick;
						group.shrinkActive = true;
						group.nukeEvadeStep = 0;
					}
					else if (found)
					{
						result.action = MyActionType::MOVE;
						result.p = newShift;
						
						if (limitSpeed)
							result.maxSpeed = unitVel(group.unitType) * 0.6;
						
						/*if (!limitSpeed)
						{
							limitSpeed = !anyEnemiesNearbyByDangerDistr(group);
							if (limitSpeed)
								result.maxSpeed = unitVel(group.unitType) * 0.8;
						}*/
						
						/*if (group.unitType == UnitType::FIGHTER)
							result.maxSpeed = 1.0;*/
					}
					else
					{
						result.action = MyActionType::MOVE;
						result.p = dirAway;
						//std::cout << "AWAY " << dirAway.x << " " << dirAway.y << std::endl;
					}
					
					group.lastUpdateTick = tick;
					group.lastComputeTick = tick;
					group.actionStarted = false;
				}
				
				break;
			}
			else if (!found)
			{
				group.lastComputeTick = tick;
				group.lastUpdateTick = tick; // TODO ???
			}
		}
	}
	
	return result;
}

void Strat::assignBuildings()
{
	std::set<Building *> pbuildings;
	
	for (Building &b : buildings)
	{
		if (b.side != 0)
		{
			pbuildings.insert(&b);
			b.assignedGroup = 0;
		}
	}
	
	std::set<Group *> pgroups;
	for (Group &g : groups)
	{
		if (isGroundUnit(g.unitType))
		{
			pgroups.insert(&g);
		}
	}
	
	size_t count = std::min(pbuildings.size(), pgroups.size());
	for (int i = 0; i < count; ++i)
	{
		double dist2 = sqr(100000.0);
		std::set<Building *>::iterator b = pbuildings.end();
		std::set<Group *>::iterator g = pgroups.end();
		for (std::set<Building *>::iterator bit = pbuildings.begin(); bit != pbuildings.end(); ++bit)
		{
			for (std::set<Group *>::iterator git = pgroups.begin(); git != pgroups.end(); ++git)
			{
				double d2 = (*bit)->pos.dist2((*git)->center);
				if (d2 < dist2)
				{
					dist2 = d2;
					b = bit;
					g = git;
				}
			}
		}
		
		if (b != pbuildings.end() && g != pgroups.end())
		{
			(*b)->assignedGroup = (*g)->internalId;
			pbuildings.erase(b);
			pgroups.erase(g);
		}
	}
}

void Strat::updateGroupAttraction()
{
	if (buildings.empty())
		return;
	
	std::vector<Group *> grps[2];
	
	int grCount[5] = {};
	for (Group &g : groups)
	{
		g.attractedToGroup = -1;
		
		if (g.unitType != UnitType::NONE)
		{
			if (isGroundUnit(g.unitType))
				grps[0].push_back(&g);
			else
				grps[1].push_back(&g);
			
			grCount[(int) g.unitType]++;
		}
	}
	
	constexpr int MAX_GROUPS = 5;
	for (int i = 0; i < 2; ++i)
	{
		if (grps[i].size() <= MAX_GROUPS)
			continue;
		
		/*std::sort(grps[i].begin(), grps[i].end(), [](const Group *g1, const Group *g2) {
			return g1->health < g2->health;
		});*/
	
		double pts = 1e10;
		Group *bestG = nullptr;
		for (Group *g : grps[i])
		{
			if (grCount[(int) g->unitType] > 1)
			{
				double minDist2 = 1e8;
				int targetK = -1;
				
				for (int k = 0; k < groups.size(); ++k)
				{
					Group &othG = groups[k];
					
					if (&othG != g && othG.unitType == g->unitType)
					{
						double dist2 = othG.center.dist2(g->center);
						
						if (dist2 < minDist2)
						{
							minDist2 = dist2;
							targetK = k;
						}
					}
				}
				
				if (targetK >= 0 && minDist2 < sqr(300))
				{
					g->attractedToGroup = targetK;
					double curPts = sqrt(minDist2)*5 + g->health;
					if (curPts < pts)
					{
						pts = curPts;
						bestG = g;
					}
				}
			}
		}
		
		for (Group *g : grps[i])
		{
			if (g != bestG)
				g->attractedToGroup = -1;
		}
	}
}

bool Strat::anyEnemiesNearbyByDangerDistr(const Group &group)
{
	int x = group.center.x / DISTR_MAT_CELL_SIZE;
	int y = group.center.y / DISTR_MAT_CELL_SIZE;
	for (int yy = -7; yy <= 7; ++yy)
	{
		for (int xx = -7; xx <= 7; ++xx)
		{
			if (xx * xx + yy * yy <= 50)
			{
				int x2 = x + xx;
				int y2 = y + yy;
				if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
				{
					DangerDistCell &dCell = dangerDistCells[y2 * DISTR_MAT_CELLS_X + x2];
					if (dCell.totalEnemyDamage > 0.0 || dCell.totalMyDamage > 0.0)
					{
						return true;
					}
				}
			}
		}
	}
	
	return false;
}

void Strat::calcDangerDistCells(const Group &group)
{
	dngGr = &group;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
			double totalMyDamage = 0;
			
			for (int myType = 0; myType < 5; ++myType)
			{
				double typeDamage = 0;
				if (group.sizeByTypes[myType])
				{
					for (int enemyType = 0; enemyType < 5; ++enemyType)
					{
						if (cell.count[enemyType])
						{
							double dmg = getDamage((UnitType) myType, (UnitType) enemyType);
							typeDamage = std::max(typeDamage, std::min(dmg * group.sizeByTypes[myType], 120.0 * cell.count[enemyType]));
						}
					}
				}
				totalMyDamage += typeDamage;
			}
			
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			double f2hDmg = 0.0;
			dCell.totalEnemyDamage = 0.0;
			dCell.totalEnemyHealth = 0.0;
			for (int enemyType = 0; enemyType < 5; ++enemyType)
			{
				dCell.enemyDamage[enemyType] = 0.0;
				dCell.enemyHealth[enemyType] = 0.0;
				
				if (cell.count[enemyType])
				{
					dCell.enemyHealth[enemyType] += cell.health[enemyType];
					for (int myType = 0; myType < 5; ++myType)
					{
						if (group.sizeByTypes[myType])
						{
							double dmg = getDamage((UnitType) enemyType, (UnitType) myType);
							dCell.enemyDamage[enemyType] = std::min(dmg * cell.count[enemyType], 120.0 * group.sizeByTypes[myType]);
							
							if (enemyType == (int) UnitType::FIGHTER && myType == (int) UnitType::HELICOPTER)
								f2hDmg = dCell.enemyDamage[enemyType];
						}
					}
				}
				
				dCell.enemyDamage[enemyType] *= 1.5;
				dCell.totalEnemyDamage += dCell.enemyDamage[enemyType];
				dCell.totalEnemyHealth += dCell.enemyHealth[enemyType];
			}
			
			dCell.totalMyDamage = totalMyDamage;
			dCell.f2hDmg = f2hDmg;
		}
	}
}

struct DangerDist {
	double dists[25];
	
	DangerDist() {
		dist(UnitType::ARV, UnitType::ARV) = 150.0;
		dist(UnitType::ARV, UnitType::FIGHTER) = 150.0;
		dist(UnitType::ARV, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::ARV, UnitType::IFV) = 150.0;
		dist(UnitType::ARV, UnitType::TANK) = 150.0;
		
		dist(UnitType::FIGHTER, UnitType::ARV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::IFV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::HELICOPTER, UnitType::ARV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::IFV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::IFV, UnitType::ARV) = 150.0;
		dist(UnitType::IFV, UnitType::FIGHTER) = 100.0;
		dist(UnitType::IFV, UnitType::HELICOPTER) = 100.0;
		dist(UnitType::IFV, UnitType::IFV) = 150.0;
		dist(UnitType::IFV, UnitType::TANK) = 150.0;
		
		dist(UnitType::TANK, UnitType::ARV) = 150.0;
		dist(UnitType::TANK, UnitType::FIGHTER) = 1.0;
		dist(UnitType::TANK, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::TANK, UnitType::IFV) = 150.0;
		dist(UnitType::TANK, UnitType::TANK) = 150.0;
		
		for (int i = 0; i < 25; ++i)
			dists[i] = sqr(dists[i]);
	}
	
	double &dist(UnitType from, UnitType to) {
		return dists[(int) from * 5 + (int) to];
	}
} DANGER_DISTS;


bool isClosedSpaceDanger(const P &myP, const P &enP, double myVel, double enemyVel, double ticks)
{
	double myR = myVel * ticks + 0.1;
	double dangerRad = 70.0;
	double enR = enemyVel * ticks + dangerRad;
	double d = myP.dist(enP);
	
	if (myR + d < enR)
		return true;
	
	const double borderDist = 40.0;
	if (myP.x > (borderDist + myR) && myP.x < (WIDTH - borderDist - myR) && myP.y > (borderDist + myR) && myP.y < (HEIGHT - borderDist - myR))
		return false;
	
	P myPn = myP;
	P enPn = enP;
	if (myPn.x > WIDTH / 2.0)
	{
		myPn.x = WIDTH - myPn.x;
		enPn.x = WIDTH - enPn.x;
	}
	
	if (myPn.y > HEIGHT / 2.0)
	{
		myPn.y = HEIGHT - myPn.y;
		enPn.y = HEIGHT - enPn.y;
	}
	
	if (myPn.x < myPn.y)
	{
		std::swap(myPn.x, myPn.y);
		std::swap(enPn.x, enPn.y);
	}
	
	double borderDistX = std::min(borderDist, myPn.x);
	double borderDistY = std::min(borderDist, myPn.y);
	
	double b = sqrt(sqr(myR) - sqr(myPn.y - borderDistY));
	double X = myPn.x + b;
	
	if (P(X, borderDistY).dist2(enPn) > sqr(enR))
		return false;
	
	if (myR > (myPn.x - borderDistX))
	{
		double Y = myPn.y + sqrt(sqr(myR) - sqr(myPn.x - borderDistX));
		
		if (P(borderDistX, Y).dist2(enPn) > sqr(enR))
			return false;
	}
	else
	{
		X = myPn.x - b;
	
		if (P(X, borderDistY).dist2(enPn) > sqr(enR))
			return false;
	}
	
	return true;
}

double captureTick(const P &myP, const P &enP, double myVel, double enemyVel)
{
	double dT = 125.0;
	double oldT = 0.0;
	for (double t = 0.0; t <= 625;)
	{
		if (!isClosedSpaceDanger(myP, enP, myVel, enemyVel, t))
		{
			oldT = t;
			t += dT;
		}
		else
		{
			if (dT <= 1.0)
				return t;
			
			t = oldT;
			dT /= 5.0;
			t += dT;
		}
	}
	
	return 625;
}

double Strat::attractionPoint(const P from, const Group &group, double ticks, bool angryMode)
{
	double res = 0.0;
	double f2hDmgRes = 0.0;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			if (dCell.totalMyDamage > 0.0 || dCell.totalEnemyDamage > 0.0)
			{
				double alpha = 0.3;
				double alphaM1 = 0.7;
				
				P p = P((x + 0.5) * DISTR_MAT_CELL_SIZE, (y + 0.5) * DISTR_MAT_CELL_SIZE);
				
				double dist2 = p.dist2(from);
				
				double pts = (group.health * alphaM1 + dCell.totalEnemyHealth * alpha) / (dCell.totalEnemyHealth*0.01 + dCell.totalEnemyDamage) 
				- (dCell.totalEnemyHealth * alphaM1 + group.health * alpha) / (group.health * 0.01 + dCell.totalMyDamage);
				
				pts *= (1.0 + dCell.totalEnemyHealth*0.0003);
				
				if (pts != 0.0)
				{
					double enemyVel = 0.0;
					
					for (int i = 0; i < 5; ++i) 
					{ 
						if (dCell.enemyHealth[i]) 
							enemyVel += unitVel((UnitType) i) * (dCell.enemyHealth[i] / dCell.totalEnemyHealth); 
					}
					
					if (pts < 0.0)
					{
						double t = captureTick(from, p, unitVel(group.unitType), enemyVel);
						res += pts * (625 - t) / 625.0;
					}
					else
					{
						/*double t = captureTick(p, from, enemyVel, unitVel(group.unitType));
						res += 0.1* pts * (625 - t) / 625.0;*/
					}
				}
				
				if (pts > 0.0)
				{
					double pp = 1.0/(1.0 + dist2);
					res += pts * pp;
				}
				else
				{
					/*for (int mt = 0; mt < 5; ++mt)
					{
						if (group.healthByTypes[mt])
						{
							for (int et = 0; et < 5; ++et)
							{
								if (dCell.enemyHealth[et])
								{
									double rad2 = DANGER_DISTS.dist((UnitType) et, (UnitType) mt);
									//double rad2 = 150*150;
									if (rad2 > dist2)
									{
										double fraction = group.healthByTypes[mt] / group.health * dCell.enemyHealth[et] / dCell.totalEnemyHealth;
										double pn = (1.0 - std::min(1.0, dist2/rad2));
										res += pts * pn * fraction;
									}
								}
							}
						}
					}*/
					
					double pn = (1.0 - std::min(1.0, dist2/sqr(150)));
					res += pts * pn;
				}
				
				//res += pts / p.dist2(from);
			}
			
			f2hDmgRes += dCell.f2hDmg;
		}
	}
	
	if (group.unitType == UnitType::FIGHTER)
	{
		double L = 1.5 * WIDTH;
		if (group.size * 80 > group.health)
		{
			Group *arvG = getGroup(UnitType::ARV);
			if (arvG && arvG->size > 20)
			{
				L = arvG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::ARV] * (80 - group.health/group.size)*0.2;
	}
	
	/*if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		double L = 1.5 * WIDTH;
		if (f2hDmgRes > 0.0 && res < 0.0)
		{
			if (fivG && fivG->size > 10)
			{
				L = fivG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::IFV];
		
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 300.0)
			{
				res -= (l - 300.0);
			}
		}
	}*/
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::HELICOPTER);
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 400.0)
			{
				res -= (l - 400.0);
			}
		}
	}*/
	
	if (isGroundUnit(group.unitType))
	{
		for (const Building &b : buildings)
		{
			if (b.side != 0)
			{
				double d = from.dist(b.pos);
				double coef = b.assignedGroup == group.internalId ? 2.0 : 1.0;
				res += coef*group.health/(20 + d)*0.1;
			}
		}
	}
	
	if (group.attractedToGroup >= 0)
	{
		Group &othG = groups[group.attractedToGroup];
		double d = from.dist(othG.center);
		res += group.health/(20 + d)*0.3;
	}
	
	return res;
}


ShrinkResult Strat::findShrink(Group &group)
{
	ShrinkResult result;
	
	Simulator sim;
	sim.tick = tick;
	std::copy(cells, cells + CELLS_X * CELLS_Y, sim.cells);
	
	BBox gbox = group.bbox;
	gbox.expand(2.0);
	for (const MyUnit &u : units)
	{
		if (gbox.inside(u.pos))
		{
			sim.units.push_back(u);
			MyUnit &tu = *sim.units.rbegin();
			tu.selected = group.check(tu);
			tu.vel = P(0, 0);
		}
	}
	sim.groups.push_back(group);
	
	double bestArea = WIDTH * HEIGHT;
	for (double x = -1; x <= 1; ++x)
	{
		for (double y = -1; y <= 1; ++y)
		{
			Simulator sim2 = sim;
			MyMove myMove;
			myMove.action = MyActionType::SCALE;
			myMove.p = group.center + P(x, y) * 10.0;
			myMove.factor = 0.2;
			sim2.registerMove(myMove, 0);
			
			int i = 0;
			for (; i < 20; ++i)
			{
				sim2.tick++;
				sim2.resetAxisSorts();
				sim2.applyMoves();
				int cnt = sim2.moveUnits();
				if (!cnt)
					break;
			}
			sim2.updateStats();
			
			double area = sim2.groups.rbegin()->bbox.area();
			if (area < bestArea)
			{
				bestArea = area;
				result.shrinkPoint = myMove.p;
				result.ticks = i;
				result.endBBox = sim2.groups.rbegin()->bbox;
			}
		}
	}
	
	return result;
}
}


namespace TestV1 {
	double groupPriority(const Group &g) {
	double res = 0.0;
	
	if (g.shrinkAfterNuke)
		res += 1100.0;
	else if (g.actionStarted)
		res += 1000.0;
	
	res -= g.lastUpdateTick;
	
	return res;
}

void DistributionMatrix::clear()
{
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		Cell &cell = cells[i];
		for (int j = 0; j < 5; ++j)
			cell.count[j] = 0;
		for (int j = 0; j < 5; ++j)
			cell.health[j] = 0;
	}
}

void DistributionMatrix::initialize(const Simulator &sim, bool firstTick)
{
	clear();
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : sim.units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / DISTR_MAT_CELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / DISTR_MAT_CELL_SIZE;
				
				Cell &cell = getCell(x, y);
				int type = (int) u.type;
				cell.count[type] += 1.0 / K;
				cell.health[type] += u.durability / K;
			}
		}
		
		/*if (sim.enableFOW && firstTick && u.side == 0)
		{
			P pos = P(WIDTH, HEIGHT) - u.pos;
			int x = pos.x / DISTR_MAT_CELL_SIZE;
			int y = pos.y / DISTR_MAT_CELL_SIZE;
			Cell &cell = getCell(x, y);
			int type = (int) u.type;
			cell.count[type] += 1.0;
			cell.health[type] += u.durability;
		}*/
	}
}

void DistributionMatrix::blur(DistributionMatrix &oth) const
{
	oth.clear();
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			Cell &resCell = oth.getCell(x, y);
			int cnt = 0;
			for (int yy = -1; yy <= 1; ++yy)
			{
				for (int xx = -1; xx <= 1; ++xx)
				{
					int x2 = x + xx;
					int y2 = y + yy;
					if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
					{
						++cnt;
						const Cell &cell = getCell(x2, y2);
						for (int i = 0; i < 5; ++i)
							resCell.count[i] += cell.count[i];
						for (int i = 0; i < 5; ++i)
							resCell.health[i] += cell.health[i];
					}
				}
			}
			
			const Cell &cell = getCell(x, y);
			for (int i = 0; i < 5; ++i)
			{
				if (!cell.count[i])
				{
					resCell.count[i] = 0;
					resCell.health[i] = 0;
				}
			}
			
			/*for (int i = 0; i < 5; ++i)
				resCell.count[i] += (resCell.count[i] + cnt - 1) / cnt;
			for (int i = 0; i < 5; ++i)
				resCell.health[i] /= cnt;*/
		}
	}
}

Strat::Strat::Strat()
{
	Group g;
	g.actionStarted = false;
	g.lastUpdateTick = 0;
	g.lastShrinkTick = 0;
	
	for (int i = 0; i < 1; ++i)
	{
		g.unitType = UnitType::HELICOPTER;
		//g.miniGroupInd = i;
		g.internalId = internalGroupSeq++;
		groups.push_back(g);
	}
	g.miniGroupInd = 0;
	
	g.unitType = UnitType::IFV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::FIGHTER;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::TANK;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.unitType = UnitType::ARV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	
	// TODO
	/*g.unitType = UnitType::FIGHTER;
	g.internalId = internalGroupSeq++;
	g.group = 90;
	groups.push_back(g);
	
	g.unitType = UnitType::FIGHTER;
	g.internalId = internalGroupSeq++;
	g.group = 91;
	groups.push_back(g);
	
	g.unitType = UnitType::FIGHTER;
	g.internalId = internalGroupSeq++;
	g.group = 92;
	groups.push_back(g);*/
}


void Strat::calcMicroShift(Group &group, P &shift)
{
	if (group.unitType != UnitType::TANK && group.unitType != UnitType::FIGHTER)
		return;
	
	BBox bbox = group.bbox;
	bbox.p1 += shift - P(35, 35);
	bbox.p2 += shift + P(35, 35);
	
	int finalScore = -100000;
	int currentScore = -100000;
	double ticks = 20.0;
	
	//if (group.unitType == UnitType::TANK)
	{
		std::vector<P> enemies[(int) UnitType::COUNT];
		std::vector<P> myUnits;
		
		for (const MyUnit &u : units)
		{
			if (u.side > 0 && (u.type == UnitType::TANK || u.type == UnitType::HELICOPTER || u.type == UnitType::IFV) && bbox.inside(u.pos))
			{
				enemies[(int) u.type].push_back(u.pos + u.vel * ticks);
			}
			else if (group.check(u))
			{
				myUnits.push_back(u.pos);
			}
		}
		
		double L = 8.0;
		P newShift = shift;
		for (int k = 0; k < 4; ++k)
		{
			P foundShift = newShift;
			int foundScore = -100000;
			for (int i = 0; i < 9; ++i)
			{
				P testShift = newShift + P(i / 3 - 1, i % 3 - 1) * L;
				int score = 0;
				bool isCurShift = shift.dist2(testShift) < 0.1;
				
				if (!isCurShift || currentScore == -100000)
				{
					const std::vector<MicroShiftValues> &pos = microShiftMatrix.pos[(int) group.unitType];
					if (!pos.empty())
					{
						for (const P &myP : myUnits)
						{
							P myPos = myP + testShift;
							bool found = false;
							for (const MicroShiftValues &vals : pos)
							{
								if (!found)
								{
									for (const P &enP : enemies[(int) vals.unitType])
									{
										if (myPos.dist2(enP) < vals.dist2)
										{
											score += vals.val;
											found = true;
											break;
										}
									}
								}
							}
						}
					}
					
					// !! decrease SCORE
					int posScore = score;
					score /= 2;
					
					const std::vector<MicroShiftValues> &neg = microShiftMatrix.neg[(int) group.unitType];
					if (!neg.empty())
					{
						for (const MicroShiftValues &vals : neg)
						{
							for (const P &enP : enemies[(int) vals.unitType])
							{
								for (const P &myP : myUnits)
								{
									P myPos = myP + testShift;
									if (myPos.dist2(enP) < vals.dist2)
									{
										score -= vals.val;
										break;
									}
								}
							}
						}
					}
					
					int negScore = posScore / 2 - score;
					
					if (foundScore < score)
					{
						foundShift = testShift;
						foundScore = score;
					}
					
					if (isCurShift)
						currentScore = score;
				}
			}
			
			L /= 2.0;
			newShift = foundShift;
			
			finalScore = foundScore;
		}
		
		if (currentScore >= finalScore)
			return;
		
		shift = newShift;
	}
}

void Strat::calcNuclearEfficiency()
{
	for (int i = 0; i < MICROCELLS_X * MICROCELLS_Y; i++)
		nuclearEfficiency[i] = 0;
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / MICROCELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / MICROCELL_SIZE;
				
				double efficiency = 100.0 / (10.0 + u.durability) / K;
				
				if (u.type == UnitType::ARV)
					efficiency /= 10.0;
				
				if (u.type == UnitType::FIGHTER || u.type == UnitType::HELICOPTER)
					efficiency /= 1.5;
				
				nuclearEfficiency[y * MICROCELLS_X + x] += efficiency;
			}
		}
	}
}

UnitType Strat::calcNextUnitTypeForConstruction(bool ground)
{
	/*double totalCount = 0;
	double assetCount[5] = {};
	for (int i = 0; i < 5; ++i)
	{
		totalCount += myCount[(UnitType) i];
		assetCount[i] = myCount[(UnitType) i];
	}
	
	double totalAssets = totalCount;
	for (const Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE)
		{
			int cnt = std::max(0, 40 - b.unitCount);
			totalAssets += cnt;
			assetCount[(int) b.unitType] += cnt;
		}
	}
	
	if (assetCount[(int) UnitType::TANK] > 300)
	{
		if (assetCount[(int) UnitType::FIGHTER] < totalAssets * 0.20)
		{
			LOG("CREATE FIGHTER");
			return UnitType::FIGHTER;
		}
		
		if (assetCount[(int) UnitType::HELICOPTER] < totalAssets * 0.20)
		{
			LOG("CREATE HELICOPTER");
			return UnitType::HELICOPTER;
		}
	}
	LOG("CREATE TANK");
	return UnitType::TANK;*/
	
	////////////
	
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType == UnitType::HELICOPTER)
		{
			return UnitType::TANK;
		}
	}
	
	if (enemyCount[UnitType::HELICOPTER] * 0.9 > myCount[UnitType::HELICOPTER])
	{
		return UnitType::HELICOPTER;
	}
	
	return UnitType::TANK;
	
	/*double score[5] = {};
	
	int enCnt = enemyCount[UnitType::HELICOPTER]*0.7 + enemyCount[UnitType::FIGHTER]*0.3;
	int myCnt = myCount[UnitType::FIGHTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::FIGHTER] += enCnt - myCnt;
	}
	score[(int) UnitType::FIGHTER] *= 0.6;
	
	enCnt = enemyCount[UnitType::TANK];
	myCnt = myCount[UnitType::HELICOPTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::HELICOPTER] += enCnt - myCnt;
	}
	
	score[(int) UnitType::HELICOPTER] *= 0.8;
	
	enCnt = enemyCount[UnitType::IFV];
	myCnt = myCount[UnitType::TANK];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::TANK] += enCnt - myCnt;
	}
	
	enCnt = enemyCount[UnitType::FIGHTER]*0.7 + enemyCount[UnitType::HELICOPTER]*0.3;
	myCnt = myCount[UnitType::IFV];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::IFV] += enCnt - myCnt;
	}
	
	
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE)
		{
			score[(int) b.unitType] -= 20;
		}
	}
	
	double grCount = 0;
	double airCount = 0;
	for (int i = 0; i < 5; ++i)
	{
		if (isGroundUnit((UnitType) i) && i != (int) UnitType::ARV)
		{
			grCount += myCount[(UnitType) i];
		}
		else
		{
			airCount += myCount[(UnitType) i];
		}
	}
	
	double totalCount = grCount + airCount;
	if (totalCount > 0)
	{
		for (int i = 0; i < 5; ++i)
		{
			if (isGroundUnit((UnitType) i))
			{
				score[i] *= airCount;
			}
			else
			{
				score[i] *= grCount;
			}
		}
	}
	
	int res = 0;
	int resType = -1;
	for (int i = 0; i < 5; ++i)
	{
		if (score[i] > res)
		{
			res = score[i];
			resType = i;
		}
	}
	
	if (resType >= 0)
		return (UnitType) resType;
	
	return UnitType::TANK;*/
}

MyMove Strat::calcNextMove()
{
	MyMove result;
	result.action = MyActionType::NONE;
	
	int moves = getAvailableActions(12);
	if (moves < 1)
		return result;
	
	if (players[1].remainingNuclearStrikeCooldownTicks < 30)
	{
		if (moves < 3)
			return result;
	}
	
	updateStats();
	
	if (players[1].nextNuclearStrikeTick >= 0)
	{
		bool select = false;
		bool expand = false;
		BBox bbox;
		int evaidingGroups = 0;
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep == 0)
			{
				BBox gbox = g.bbox;
				gbox.expand(40.0);
				if (gbox.inside(players[1].nuclearStrike))
				{
					bbox.add(g.bbox);
					g.nukeEvadeStep = 1;
					select = true;
					++evaidingGroups;
				}
			}
			else if (g.nukeEvadeStep == 1)
			{
				expand = true;
				g.nukeEvadeStep = 2;
				++evaidingGroups;
			}
			else
			{
				++evaidingGroups;
			}
		}
		
		if (select)
		{
			result.action = MyActionType::CLEAR_AND_SELECT;
			result.p1 = bbox.p1;
			result.p2 = bbox.p2;
			
			return result;
		}
		else if (expand)
		{
			result.action = MyActionType::SCALE;
			result.p = players[1].nuclearStrike;
			result.factor = 9.0;
			return result;
		}
		
		if (evaidingGroups && moves < 3)
			return result;
	}
	else
	{
		for (Group &g : groups)
		{
			if (tick - g.lastShrinkTick > 30)
				g.shrinkAfterNuke = false;
			if (g.nukeEvadeStep != 0)
			{
				g.shrinkAfterNuke = true;
				g.nukeEvadeStep = -1;
			}
		}
	}
	
	/*if (tick > 200) {
		int moves = getAvailableActions(4, 10);
		if (moves < 1) // throttle
			return result;
	}*/
	
	for (Building &b : buildings)
	{
		/*if (b.lastChangeUnitCount > b.unitCount)
			b.lastChangeUnitCount = b.unitCount;*/
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && (b.unitType == UnitType::NONE/* || (b.unitCount - b.lastChangeUnitCount) > 11*/))
		{
			//LOG("SVP " << buildingCaptured);
			result.action = MyActionType::SETUP_VEHICLE_PRODUCTION;
			result.facilityId = b.id;
			
			/*if (buildingCaptured % 2 == 0)
				result.unitType = UnitType::FIGHTER;
			else
				result.unitType = UnitType::IFV;*/
			
			//result.unitType = UnitType::TANK;
			result.unitType = calcNextUnitTypeForConstruction(false);
			//result.unitType = (UnitType) (buildingCaptured % 5);
			//b.lastChangeUnitCount = b.unitCount;
			
			++buildingCaptured;
			return result;
		}
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE && b.unitCount > 54 && b.productionProgress < 30
			|| b.createGroupStep > 0
			//|| b.side != 0 && b.unitCount > 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE && !isGroundUnit(b.unitType)
		)
		{
			UnitType unitType = UnitType::NONE;
			int cnt = 0;
			for (int i = 0; i < 5; ++i)
			{
				if (cnt < b.unitCountByType[i])
				{
					cnt = b.unitCountByType[i];
					unitType = (UnitType) i;
				}
			}
			
			UnitType newUnitType = calcNextUnitTypeForConstruction(false);
			if (b.unitType != newUnitType && unitType != newUnitType)
			{
				result.action = MyActionType::SETUP_VEHICLE_PRODUCTION;
				result.facilityId = b.id;
				result.unitType = newUnitType;
				return result;
			}
			
			//LOG("MAKE GROUP");
			if (b.createGroupStep == 0 || b.createGroupStep == 1)
			{
				BBox bbox;
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && !p.groups.any() && b.checkPoint(p.pos))
					{
						bbox.add(p.pos);
					}
				}
				
				result.p1 = bbox.p1 - P(1, 1);
				result.p2 = bbox.p2 + P(1, 1);
				
				result.action = MyActionType::CLEAR_AND_SELECT;
				/*result.p1 = b.pos - P(32, 32); 
				result.p2 = b.pos + P(32, 32); */
				
				result.unitType = unitType;
				b.createGroupStep = 2;
				return result;
			}
			
			if (b.createGroupStep == 2)
			{
				// check if selected
				bool anySelected = false;
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.groups.count() == 0 && p.type == unitType)
					{
						if (p.pos.x > b.pos.x - 32 && p.pos.x < b.pos.x + 32 && p.pos.y > b.pos.y - 32 && p.pos.y < b.pos.y + 32)
						{
							if (p.selected)
							{
								anySelected = true;
								break;
							}
						}
					}
				}
				
				if (anySelected)
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					b.createGroupStep = 3;
					
					Group newGroup;
					newGroup.unitType = unitType;
					newGroup.group = result.group;
					newGroup.internalId = internalGroupSeq++;
					groups.push_back(newGroup);
					
					b.createGroupStep = 0;
					return result;
				}
				else
				{
					b.createGroupStep = 1;
				}
			}
		}
	}
	
		
	if (!initialGroupsGerationDone && !buildings.empty())
	{
		for (Group &g : groups)
		{
			/*if (g.unitType == UnitType::HELICOPTER && !g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					P allCenter = P(0, 0);
					int count = 0;
					for (const MyUnit &u : units)
					{
						if (u.side == 0 && u.type == g.unitType)
						{
							allCenter += u.pos;
							++count;
						}
					}
					
					allCenter /= count;
					
					if (g.miniGroupInd == 0)
					{
						result.p1 = P(0, 0);
						result.p2 = allCenter;
					}
					else if (g.miniGroupInd == 1)
					{
						result.p1 = P(allCenter.x, 0);
						result.p2 = P(WIDTH, allCenter.y);
					}
					else if (g.miniGroupInd == 2)
					{
						result.p1 = P(0, allCenter.y);
						result.p2 = P(allCenter.x, HEIGHT);
					}
					else if (g.miniGroupInd == 3)
					{
						result.p1 = allCenter;
						result.p2 = P(WIDTH, HEIGHT);
					}
					
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}*/
			
			if (!g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					
					//result.p1 = g.center - P(30, 30); // TODO
					//result.p2 = g.center + P(30, 30);
					
					g.enumGroupBuildStep++;
					return result;
				}
				else if (g.enumGroupBuildStep == 1)
				{
					result.action = MyActionType::ASSIGN;
					g.enumGroupBuildStep++;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}
		}
		
		initialGroupsGerationDone = true;
		LOG("DONE");
	}
	
	bool angryMode = angryModeTill > tick;

	
	DistributionMatrix matr;
	matr.initialize(*this, !distributionMatrixInitialized);
	
	//if (!enableFOW || !distributionMatrixInitialized)
	{
		matr.blur(distributionMatrix);
		distributionMatrixInitialized = true;
	}
	/*else
	{
		DistributionMatrix othMatr;
		matr.blur(othMatr);
		
		spreadDistributionMatrix();
		
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &dcell = distributionMatrix.getCell(x, y);
				DistributionMatrix::Cell &simDcell = othMatr.getCell(x, y);
				double visFactor = visibilityFactors[y * DISTR_MAT_CELLS_X + x];
				
				if (visFactor > 0.8)
				{
					dcell = simDcell;
					dcell.updateTick = tick;
					dcell.realUpdateTick = tick;
				}
				else
				{
					for (int i = 0; i < 5; ++i)
					{
						dcell.health[i] = std::max(dcell.health[i], simDcell.health[i]);
						dcell.count[i] = std::max(dcell.count[i], simDcell.count[i]);
					}
					
					if (visFactor > 0.5)
					{
						dcell.updateTick = tick;
						dcell.realUpdateTick = tick;
					}
				}
			}
		}
	}*/
	
	/*if (tick > 532)
	{
		std::cout.width(2);
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &resCell = distributionMatrix.getCell(x, y);
				std::cout << " " << resCell.count[1];
			}
			std::cout << std::endl;
		}
		std::cout << std::endl;
	}*/
	
	if (players[0].remainingNuclearStrikeCooldownTicks == 0)
	{
		resetCells();
		calcNuclearEfficiency();
		
		auto getEnemySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0.0;
			
			return nuclearEfficiency[y * MICROCELLS_X + x];
		};
		
		auto getMySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0;
			
			return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
				+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
				+ (int) getMicrocell(x, y, 0, UnitType::ARV).size() / 2;
		};
		
		P bestTarget;
		int bestScore = 0;
		
		for (int y = 0; y < MICROCELLS_Y; ++y) {
			for (int x = 0; x < MICROCELLS_X; ++x) {
				double enemyN = getEnemySize(x, y);
				if (enemyN > 10)
				{
					int totalMyCount = 0;
					int myCenterCount = 0;
					for (int xx = -1; xx <= 1; ++xx)
					{
						for (int yy = -1; yy <= 1; ++yy)
						{
							totalMyCount += getMySize(x + xx, y + yy);
							if (xx == 0 && yy == 0)
								myCenterCount = totalMyCount;
						}
					}
					
					if (totalMyCount < 20 && myCenterCount == 0)
					{
						int totalMyExtCount = totalMyCount;
						if (totalMyExtCount == 0)
						{
							for (int xx = -2; xx <= 2; ++xx)
							{
								for (int yy = -2; yy <= 2; ++yy)
								{
									if (xx < -1 || xx > 1 || yy < -1 || yy > 1)
										totalMyExtCount += getMySize(x + xx, y + yy);
								}
							}
						}
						
						if (totalMyExtCount > 0)
						{
							double totalEnemyCount =
								enemyN +
								getEnemySize(x - 1, y)/2 +
								getEnemySize(x + 1, y)/2 + 
								getEnemySize(x, y - 1)/2 + 
								getEnemySize(x, y + 1)/2;
							
							if (bestScore < totalEnemyCount)
							{
								bestScore = totalEnemyCount;
								bestTarget = P(x + 0.5, y + 0.5) * MICROCELL_SIZE;
							}
						}
					}
				}
			}
		}
		
		if (bestScore > 0)
		{
			// Correct point
			P correctedBestTarget = bestTarget;
			
			std::vector<P> myVehicles;
			std::vector<P> enemyVehicles;
			int myInnerN = 0;
			int enemyInnerN = 0;
			for (const MyUnit &p : units)
			{
				double d2 = p.pos.dist2(bestTarget);
				if (d2 < (75.0*75.0))
				{
					if (d2 < (38.0*38.0))
					{
						if (p.side == 0)
							myInnerN++;
						else
							enemyInnerN++;
					}
					else
					{
						if (p.side == 0)
							myVehicles.push_back(p.pos);
						else
							enemyVehicles.push_back(p.pos);
					}
				}
			}
			
			int foundScore = -100000;
			for (int xx = -2; xx <= 2; ++xx)
			{
				for (int yy = -2; yy <= 2; ++yy)
				{
					P newTarget = bestTarget + P(xx, yy) * 6.0;
					
					int myNum = myInnerN*3;
					int myExtNum = 0;
					
					for (const P &p : myVehicles)
					{
						double d2 = p.dist2(newTarget);
						if (d2 < (50.0*50.0))
						{
							myNum++;
						}
						else if (d2 > (55.0*55.0) && d2 < (70.0*70.0))
						{
							myExtNum++;
						}
					}
					
					if (myExtNum > 0)
					{
						int enNum = enemyInnerN*3;
						for (const P &p : enemyVehicles)
						{
							double d2 = p.dist2(newTarget);
							if (d2 < (50.0*50.0))
							{
								enNum++;
							}
						}
						
						int score = enNum - myNum * 4;
						if (score > foundScore)
						{
							foundScore = score;
							correctedBestTarget = newTarget;
						}
					}
				}
			}
			
			if (foundScore > -100000)
			{
				auto getMySize = [this](int x, int y) {
					if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
						return 0;
					
					return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
						+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
						+ (int) getMicrocell(x, y, 0, UnitType::ARV).size();
				};
				
				int pts = 0;
				long bestId = -1;
				P vehPos;
				
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.durability > 70)
					{
						double dist2 = p.pos.dist2(correctedBestTarget);
						if (dist2 > 55*55 && dist2 < 70*70)
						{
							bool outOfRange = false;
							
							if (p.vel.len2() > 0.01)
							{
								MyUnit u = p;
								for (int i = 0; i < 30; ++i)
								{
									double visRange = getVisionRange(u) - unitVel(p.type) * 10;
									if (u.pos.dist2(correctedBestTarget) > sqr(visRange))
									{
										outOfRange = true;
										break;
									}
									u.pos += u.vel;
								}
							}
							
							if (outOfRange)
								continue;
							
							int xx = p.pos.x / MICROCELL_SIZE;
							int yy = p.pos.y / MICROCELL_SIZE;
							
							//int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 60.0);
							if (pts < newPts)
							{
								pts = newPts;
								bestId = p.id;
								vehPos = p.pos;
							}
						}
					}
				}
				
				if (bestId >= 0)
				{
					result.action = MyActionType::TACTICAL_NUCLEAR_STRIKE;
					result.vehicleId = bestId;
					result.p = correctedBestTarget;
					return result;
				}
			}
		}
	}
	
	groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group &g){return g.size == 0;}), groups.end());
	
	if (result.action == MyActionType::NONE && !groups.empty())
	{
		debugAttractionPoints.clear();
		
		std::sort(groups.begin(), groups.end(), [](const Group &g1, const Group &g2){
			double p1 = groupPriority(g1);
			double p2 = groupPriority(g2);
			return p2 < p1;
		});
		
		assignBuildings();
		updateGroupAttraction();
		
		if (enableFOW)
			calcVisibilityFactors();
		
		for (auto groupIt = groups.begin(); groupIt != groups.end(); ++groupIt)
		{
			Group &group = *groupIt;
			
			if (tick - group.lastComputeTick < 10 || group.nukeEvadeStep > 0)
				continue;
			
			if (group.shrinkActive)
			{
				if (tick - group.lastShrinkTick > 40 || !anyMoved(group))
				{
					group.shrinkActive = false;
				}
				else
				{
					continue;
				}
			}
			
			if (group.attractedToGroup >= 0)
			{
				Group &othG = groups[group.attractedToGroup];
				double dist2 = othG.center.dist2(group.center);
				if (dist2 < sqr(40))
				{
					if (!isSelected(group))
					{
						result = select(group);
						group.actionStarted = true;
						return result;
					}
					
					result.action = MyActionType::ASSIGN;
					result.group = othG.group;
					group.group = 0;
					group.unitType = UnitType::NONE;
					
					groups.erase(groupIt);
					//LOG("JOIN GROUPS ASSIGN " << (int) othG.group);
					return result;
				}
			}
			
			bool limitSpeed = false;
			if (nukeVehicleInd >= 0)
			{
				const MyUnit &u = units[nukeVehicleInd];
				if (group.check(u))
					limitSpeed = true;
			}
			
			calcDangerDistCells(group);
			
			P center = group.center;
			const BBox &bbox = group.bbox;
			int groupSize = group.size;
			double area = bbox.area();
			bool shrinkRequired = (area > groupSize * 40.0 || group.shrinkAfterNuke) && (((tick - group.lastUpdateTick) > 60 || group.shrinkAfterNuke) && (tick - group.lastShrinkTick) > 300);
			P shrinkPoint;
			if (shrinkRequired)
			{
				ShrinkResult shRes = findShrink(group);
				if (shRes.ticks > 7)
				{
					shrinkPoint = shRes.shrinkPoint;
				}
				else
				{
					shrinkRequired = false;
				}
				//LOG("SHRINK RES " << shRes.ticks << " " << shRes.endBBox << " P " << shRes.shrinkPoint);
			}
			//bool shrinkRequired = false;
			
			const double border = 20.0;
			P c = center;
			double R = 20 + unitVel(group.unitType) * 40;
			/*if (group.unitType == UnitType::ARV)
				R = 200.0;*/
			double ticks = R / unitVel(group.unitType);
			double curVal = attractionPoint(center, group, ticks, angryMode);
			P tp = center;
			bool found = false;
			
			bool canMoveFlag = false;
			
			P clampP1 = group.center - bbox.p1 + P(3.0, 3.0);
			P clampP2 = P(WIDTH - 3.0, HEIGHT - 3.0) + (group.center - bbox.p2);
			for (int k = 0; k < 3.0; ++k)
			{
				ticks = R / unitVel(group.unitType);
				int di = -1;
				
				for (int i = 0; i < 20; ++i)
				{
					P p = c + P(PI * 2.0 / 20.0 * i) * R;
					p = clampP(p, clampP1, clampP2);
					
					P shift = p - center;
					if (shift.len2() > 0.01)
					{
						if (canMove(p - center, group))
						{
							canMoveFlag = true;
							double val = attractionPoint(p, group, ticks, angryMode);
							
							DebugAttractionPointsInfo debugInfo;
							debugInfo.point = c;
							debugInfo.dir = p - c;
							debugInfo.val = val;
							debugAttractionPoints.push_back(debugInfo);
							
							if (val > curVal)
							{
								curVal = val;
								di = i;
								tp = p;
								found = true;
							}
						}
					}
				}
				
				if (di >= 0)
				{
					c = tp;
				}
				
				R /= 1.5;
			}
			
			if (!canMoveFlag)
			{
				double R = 20 + unitVel(group.unitType) * 40;
				
				std::vector<const MyUnit *> groupUnits;
				std::vector<const MyUnit *> otherUnits;
				
				BBox bbox = group.bbox;
				bbox.p1 -= P(2.0*R, 2.0*R);
				bbox.p2 += P(2.0*R, 2.0*R);
				
				for (const MyUnit &u : units)
				{
					if (group.check(u))
					{
						groupUnits.push_back(&u);
					}
					else if (group.canIntersectWith(u) && bbox.inside(u.pos))
					{
						otherUnits.push_back(&u);
					}
				}
				
				for (int k = 0; k < 3.0; ++k)
				{
					ticks = R / unitVel(group.unitType);
					int di = -1;
					
					for (int i = 0; i < 19; ++i)
					{
						P p = c + P(PI * 2.0 / 19.0 * i) * R;
						p = clampP(p, clampP1, clampP2);
						
						P shift = p - center;
						if (shift.len2() > 0.01)
						{
							if (canMoveDetailed(p - center, group, groupUnits, otherUnits))
							{
								canMoveFlag = true;
								double val = attractionPoint(p, group, ticks, angryMode);
								
								DebugAttractionPointsInfo debugInfo;
								debugInfo.point = c;
								debugInfo.dir = p - c;
								debugInfo.val = val;
								debugAttractionPoints.push_back(debugInfo);
								
								if (val > curVal)
								{
									curVal = val;
									di = i;
									tp = p;
									found = true;
								}
							}
						}
					}
					
					if (di >= 0)
					{
						c = tp;
					}
					
					R /= 1.5;
				}
				
				/*if (!canMoveFlag)
				{
					UnitType typeToSelect = UnitType::NONE;
					bool selected = false;
					GroupId otherGr = 0;
					for (const MyUnit *o : otherUnits)
					{
						if (group.bbox.inside(o->pos) && o->side == 0)
						{
							typeToSelect = o->type;
							if (o->selected)
								selected = true;
							
							if (o->groups.any())
							{
								for (Group &oth : groups)
								{
									if (oth.group != group.group && o->hasGroup(oth.group))
									{
										otherGr = oth.group;
										break;
									}
								}
								if (otherGr > 0)
									break;
							}
						}
					}
					
					if (typeToSelect != UnitType::NONE)
					{
						if (!selected)
						{
							result.p1 = group.bbox.p1;
							result.p2 = group.bbox.p2;
							result.unitType = typeToSelect;
							result.action = MyActionType::CLEAR_AND_SELECT;
							
							group.actionStarted = true;
							
							LOG("SELECT TO " << (int) group.group << " " << group.center);
							return result;
						}
						
						if (otherGr > 0)
						{
							result.action = MyActionType::DISMISS;
							result.group = otherGr;
							LOG("DISMISS TO " << (int) otherGr << " " << group.center);
							return result;
						}
						else
						{
							result.action = MyActionType::ASSIGN;
							result.group = group.group;
							LOG("ASSIGN TO " << (int) group.group << " " << group.center);
							return result;
						}
					}
				}*/
			}
			
			bool moveAway = false;
			P dirAway;
			P newShift = P(0, 0);
			if (!canMoveFlag)
			{
				//LOG("CANT MOVE " << group.center);
				group.canMove = false;
				
				dirAway = P(0, 0);
				for (Group &othGroup : groups)
				{
					if (&group != &othGroup && isGroundUnit(group.unitType) == isGroundUnit(othGroup.unitType))
					{
						P dp = group.center - othGroup.center;
						double clen = dp.len();
						
						if (clen < 150.0)
							dirAway += dp / clen;
					}
				}
				
				dirAway *= 10;
				dirAway += P(tick % 7 - 3, tick % 9 - 5);
				moveAway = true;
			}
			else
			{
				group.canMove = true;
				
				if (found)
					newShift = tp - center;
				
				if (newShift.len() < 15.0)
				{
					calcMicroShift(group, newShift);
				}
			}
			
			found = newShift.len2() > 0.1;
			
			if (found || shrinkRequired || moveAway)
			{
				if (!isSelected(group))
				{
					result = select(group);
					group.actionStarted = true;
				}
				else
				{
					if (shrinkRequired)
					{
						result.action = MyActionType::SCALE;
						result.factor = 0.2;
						result.p = shrinkPoint;
						group.lastShrinkTick = tick;
						group.shrinkActive = true;
						group.nukeEvadeStep = 0;
					}
					else if (found)
					{
						result.action = MyActionType::MOVE;
						result.p = newShift;
						
						if (limitSpeed)
							result.maxSpeed = unitVel(group.unitType) * 0.6;
						
						/*if (!limitSpeed)
						{
							limitSpeed = !anyEnemiesNearbyByDangerDistr(group);
							if (limitSpeed)
								result.maxSpeed = unitVel(group.unitType) * 0.8;
						}*/
						
						/*if (group.unitType == UnitType::FIGHTER)
							result.maxSpeed = 1.0;*/
					}
					else
					{
						result.action = MyActionType::MOVE;
						result.p = dirAway;
						//std::cout << "AWAY " << dirAway.x << " " << dirAway.y << std::endl;
					}
					
					group.lastUpdateTick = tick;
					group.lastComputeTick = tick;
					group.actionStarted = false;
				}
				
				break;
			}
			else if (!found)
			{
				group.lastComputeTick = tick;
				group.lastUpdateTick = tick; // TODO ???
			}
		}
	}
	
	return result;
}

void Strat::assignBuildings()
{
	std::set<Building *> pbuildings;
	
	for (Building &b : buildings)
	{
		if (b.side != 0)
		{
			pbuildings.insert(&b);
			b.assignedGroup = 0;
		}
	}
	
	std::set<Group *> pgroups;
	for (Group &g : groups)
	{
		if (isGroundUnit(g.unitType))
		{
			pgroups.insert(&g);
		}
	}
	
	size_t count = std::min(pbuildings.size(), pgroups.size());
	for (int i = 0; i < count; ++i)
	{
		double dist2 = sqr(100000.0);
		std::set<Building *>::iterator b = pbuildings.end();
		std::set<Group *>::iterator g = pgroups.end();
		for (std::set<Building *>::iterator bit = pbuildings.begin(); bit != pbuildings.end(); ++bit)
		{
			for (std::set<Group *>::iterator git = pgroups.begin(); git != pgroups.end(); ++git)
			{
				double d2 = (*bit)->pos.dist2((*git)->center);
				if (d2 < dist2)
				{
					dist2 = d2;
					b = bit;
					g = git;
				}
			}
		}
		
		if (b != pbuildings.end() && g != pgroups.end())
		{
			(*b)->assignedGroup = (*g)->internalId;
			pbuildings.erase(b);
			pgroups.erase(g);
		}
	}
}

void Strat::updateGroupAttraction()
{
	if (buildings.empty())
		return;
	
	std::vector<Group *> grps[2];
	
	int grCount[5] = {};
	for (Group &g : groups)
	{
		g.attractedToGroup = -1;
		
		if (g.unitType != UnitType::NONE)
		{
			if (isGroundUnit(g.unitType))
				grps[0].push_back(&g);
			else
				grps[1].push_back(&g);
			
			grCount[(int) g.unitType]++;
		}
	}
	
	constexpr int MAX_GROUPS = 5;
	for (int i = 0; i < 2; ++i)
	{
		if (grps[i].size() <= MAX_GROUPS)
			continue;
		
		/*std::sort(grps[i].begin(), grps[i].end(), [](const Group *g1, const Group *g2) {
			return g1->health < g2->health;
		});*/
	
		double pts = 1e10;
		Group *bestG = nullptr;
		for (Group *g : grps[i])
		{
			if (grCount[(int) g->unitType] > 1)
			{
				double minDist2 = 1e8;
				int targetK = -1;
				
				for (int k = 0; k < groups.size(); ++k)
				{
					Group &othG = groups[k];
					
					if (&othG != g && othG.unitType == g->unitType)
					{
						double dist2 = othG.center.dist2(g->center);
						
						if (dist2 < minDist2)
						{
							minDist2 = dist2;
							targetK = k;
						}
					}
				}
				
				if (targetK >= 0 && minDist2 < sqr(300))
				{
					g->attractedToGroup = targetK;
					double curPts = sqrt(minDist2)*5 + g->health;
					if (curPts < pts)
					{
						pts = curPts;
						bestG = g;
					}
				}
			}
		}
		
		for (Group *g : grps[i])
		{
			if (g != bestG)
				g->attractedToGroup = -1;
		}
	}
}

void Strat::spreadDistributionMatrix()
{
	double horTicks[5];
	double diagTicks[5];
	int xx[8] = {-1, 1,  0,  0,  -1, -1, 1,  1};
	int yy[8] = { 0, 0, -1, -1,  -1,  1, 1, -1};
	for (int i = 0; i < 5; ++i)
	{
		double vel = unitVel((UnitType) i);
		horTicks[i] = DISTR_MAT_CELL_SIZE / vel;
		diagTicks[i] = horTicks[i] * 1.4;
	}
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DistributionMatrix::Cell &dcell = distributionMatrix.getCell(x, y);
			
			for (int i = 0; i < 5; ++i)
			{
				for (int j = 0; j < 8; ++j)
				{
					double dt = j < 4 ? horTicks[i] : diagTicks[i];
					if (dcell.updateTick + dt < tick)
					{
						int x2 = x + xx[j];
						int y2 = y + yy[j];
						if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
						{
							DistributionMatrix::Cell &othCell = distributionMatrix.getCell(x2, y2);
							double visFactor = visibilityFactors[y2 * DISTR_MAT_CELLS_X + x2];
							if (visFactor < 0.7)
							{
								othCell.health[i] = std::max(othCell.health[i], dcell.health[i]);
								othCell.count[i] = std::max(othCell.count[i], dcell.count[i]);
							}
							othCell.updateTick = std::max(othCell.updateTick, dcell.updateTick + dt);
							othCell.realUpdateTick = std::max(othCell.realUpdateTick, dcell.realUpdateTick);
						}
					}
				}
			}
		}
	}
}

bool Strat::anyEnemiesNearbyByDangerDistr(const Group &group)
{
	int x = group.center.x / DISTR_MAT_CELL_SIZE;
	int y = group.center.y / DISTR_MAT_CELL_SIZE;
	for (int yy = -7; yy <= 7; ++yy)
	{
		for (int xx = -7; xx <= 7; ++xx)
		{
			if (xx * xx + yy * yy <= 50)
			{
				int x2 = x + xx;
				int y2 = y + yy;
				if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
				{
					DangerDistCell &dCell = dangerDistCells[y2 * DISTR_MAT_CELLS_X + x2];
					if (dCell.totalEnemyDamage > 0.0 || dCell.totalMyDamage > 0.0)
					{
						return true;
					}
				}
			}
		}
	}
	
	return false;
}

void Strat::calcDangerDistCells(const Group &group)
{
	dngGr = &group;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
			double totalMyDamage = 0;
			
			for (int myType = 0; myType < 5; ++myType)
			{
				double typeDamage = 0;
				if (group.sizeByTypes[myType])
				{
					for (int enemyType = 0; enemyType < 5; ++enemyType)
					{
						if (cell.count[enemyType])
						{
							double dmg = getDamage((UnitType) myType, (UnitType) enemyType);
							typeDamage = std::max(typeDamage, std::min(dmg * group.sizeByTypes[myType], 120.0 * cell.count[enemyType]));
						}
					}
				}
				totalMyDamage += typeDamage;
			}
			
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			double f2hDmg = 0.0;
			dCell.totalEnemyDamage = 0.0;
			dCell.totalEnemyHealth = 0.0;
			for (int enemyType = 0; enemyType < 5; ++enemyType)
			{
				dCell.enemyDamage[enemyType] = 0.0;
				dCell.enemyHealth[enemyType] = 0.0;
				
				if (cell.count[enemyType])
				{
					dCell.enemyHealth[enemyType] += cell.health[enemyType];
					for (int myType = 0; myType < 5; ++myType)
					{
						if (group.sizeByTypes[myType])
						{
							double dmg = getDamage((UnitType) enemyType, (UnitType) myType);
							dCell.enemyDamage[enemyType] = std::min(dmg * cell.count[enemyType], 120.0 * group.sizeByTypes[myType]);
							
							if (enemyType == (int) UnitType::FIGHTER && myType == (int) UnitType::HELICOPTER)
								f2hDmg = dCell.enemyDamage[enemyType];
						}
					}
				}
				
				dCell.enemyDamage[enemyType] *= 1.5;
				dCell.totalEnemyDamage += dCell.enemyDamage[enemyType];
				dCell.totalEnemyHealth += dCell.enemyHealth[enemyType];
			}
			
			dCell.totalMyDamage = totalMyDamage;
			dCell.f2hDmg = f2hDmg;
		}
	}
}

void Strat::calcVisibilityFactors()
{
	updateVisionRangeAndStealthFactor();
	
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		visibilityFactors[i] = 0.0;
	}
	
	for (MyUnit &u : units)
	{
		if (u.side == 0)
		{
			double range = u.visionRange;
			int minX = std::max(0.0, (u.pos.x - range)) / DISTR_MAT_CELL_SIZE;
			int maxX = std::min(WIDTH - 1.0, (u.pos.x + range)) / DISTR_MAT_CELL_SIZE;
			int minY = std::max(0.0, (u.pos.y - range)) / DISTR_MAT_CELL_SIZE;
			int maxY = std::min(HEIGHT - 1.0, (u.pos.y + range)) / DISTR_MAT_CELL_SIZE;
			double visionRange2 = sqr(u.visionRange);
			
			for (int y = minY; y <= maxY; ++y)
			{
				for (int x = minX; x <= maxX; ++x)
				{
					double &visFactor = visibilityFactors[y * DISTR_MAT_CELLS_X + x];
					if (visFactor == 1.0)
						continue;
					
					P center = P(x + 0.5, y + 0.5) * DISTR_MAT_CELL_SIZE;
					P nearPoint = center;
					P farPoint = center;
					if (center.x < u.pos.x)
					{
						nearPoint.x += DISTR_MAT_CELL_SIZE * 0.5;
						farPoint.x  -= DISTR_MAT_CELL_SIZE * 0.5;
					}
					else
					{
						nearPoint.x -= DISTR_MAT_CELL_SIZE * 0.5;
						farPoint.x  += DISTR_MAT_CELL_SIZE * 0.5;
					}
					
					if (center.y < u.pos.y)
					{
						nearPoint.y += DISTR_MAT_CELL_SIZE * 0.5;
						farPoint.y  -= DISTR_MAT_CELL_SIZE * 0.5;
					}
					else
					{
						nearPoint.y -= DISTR_MAT_CELL_SIZE * 0.5;
						farPoint.y  += DISTR_MAT_CELL_SIZE * 0.5;
					}
					
					if (nearPoint.dist2(u.pos) < visionRange2)
					{
						const Cell &cell = this->cell(x * DISTR_MAT_CELL_SIZE / CELL_SIZE, y * DISTR_MAT_CELL_SIZE / CELL_SIZE);
						double minStealth = 1.0;
						if (cell.groundType == GroundType::FOREST)
							minStealth = 0.6;
						else if (cell.weatherType == MyWeatherType::RAIN)
							minStealth = 0.6;
						else if (cell.weatherType == MyWeatherType::CLOUDY)
							minStealth = 0.8;
						
						double visibilityFactor = (range * minStealth) / farPoint.dist(u.pos);
						if (visibilityFactor > 1.0)
							visibilityFactor = 1.0;
						
						visFactor = std::max(visFactor, visibilityFactor);
					}
				}
			}
		}
	}
}

struct DangerDist {
	double dists[25];
	
	DangerDist() {
		dist(UnitType::ARV, UnitType::ARV) = 150.0;
		dist(UnitType::ARV, UnitType::FIGHTER) = 150.0;
		dist(UnitType::ARV, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::ARV, UnitType::IFV) = 150.0;
		dist(UnitType::ARV, UnitType::TANK) = 150.0;
		
		dist(UnitType::FIGHTER, UnitType::ARV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::IFV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::HELICOPTER, UnitType::ARV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::IFV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::IFV, UnitType::ARV) = 150.0;
		dist(UnitType::IFV, UnitType::FIGHTER) = 100.0;
		dist(UnitType::IFV, UnitType::HELICOPTER) = 100.0;
		dist(UnitType::IFV, UnitType::IFV) = 150.0;
		dist(UnitType::IFV, UnitType::TANK) = 150.0;
		
		dist(UnitType::TANK, UnitType::ARV) = 150.0;
		dist(UnitType::TANK, UnitType::FIGHTER) = 1.0;
		dist(UnitType::TANK, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::TANK, UnitType::IFV) = 150.0;
		dist(UnitType::TANK, UnitType::TANK) = 150.0;
		
		for (int i = 0; i < 25; ++i)
			dists[i] = sqr(dists[i]);
	}
	
	double &dist(UnitType from, UnitType to) {
		return dists[(int) from * 5 + (int) to];
	}
} DANGER_DISTS;


bool isClosedSpaceDanger(const P &myP, const P &enP, double myVel, double enemyVel, double ticks)
{
	double myR = myVel * ticks + 0.1;
	double dangerRad = 70.0;
	double enR = enemyVel * ticks + dangerRad;
	double d = myP.dist(enP);
	
	if (myR + d < enR)
		return true;
	
	const double borderDist = 40.0;
	if (myP.x > (borderDist + myR) && myP.x < (WIDTH - borderDist - myR) && myP.y > (borderDist + myR) && myP.y < (HEIGHT - borderDist - myR))
		return false;
	
	P myPn = myP;
	P enPn = enP;
	if (myPn.x > WIDTH / 2.0)
	{
		myPn.x = WIDTH - myPn.x;
		enPn.x = WIDTH - enPn.x;
	}
	
	if (myPn.y > HEIGHT / 2.0)
	{
		myPn.y = HEIGHT - myPn.y;
		enPn.y = HEIGHT - enPn.y;
	}
	
	if (myPn.x < myPn.y)
	{
		std::swap(myPn.x, myPn.y);
		std::swap(enPn.x, enPn.y);
	}
	
	double borderDistX = std::min(borderDist, myPn.x);
	double borderDistY = std::min(borderDist, myPn.y);
	
	double b = sqrt(sqr(myR) - sqr(myPn.y - borderDistY));
	double X = myPn.x + b;
	
	if (P(X, borderDistY).dist2(enPn) > sqr(enR))
		return false;
	
	if (myR > (myPn.x - borderDistX))
	{
		double Y = myPn.y + sqrt(sqr(myR) - sqr(myPn.x - borderDistX));
		
		if (P(borderDistX, Y).dist2(enPn) > sqr(enR))
			return false;
	}
	else
	{
		X = myPn.x - b;
	
		if (P(X, borderDistY).dist2(enPn) > sqr(enR))
			return false;
	}
	
	return true;
}

double captureTick(const P &myP, const P &enP, double myVel, double enemyVel)
{
	double dT = 125.0;
	double oldT = 0.0;
	for (double t = 0.0; t <= 625;)
	{
		if (!isClosedSpaceDanger(myP, enP, myVel, enemyVel, t))
		{
			oldT = t;
			t += dT;
		}
		else
		{
			if (dT <= 1.0)
				return t;
			
			t = oldT;
			dT /= 5.0;
			t += dT;
		}
	}
	
	return 625;
}

double Strat::attractionPoint(const P from, const Group &group, double ticks, bool angryMode)
{
	double res = 0.0;
	double f2hDmgRes = 0.0;
	bool spy = group.unitType == UnitType::FIGHTER && group.size == 1;
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			bool anyDamage = dCell.totalMyDamage > 0.0 || dCell.totalEnemyDamage > 0.0;
			
			if (anyDamage || (spy && dCell.totalEnemyHealth > 0))
			{
				double alpha = 0.3;
				double alphaM1 = 0.7;
				
				P p = P((x + 0.5) * DISTR_MAT_CELL_SIZE, (y + 0.5) * DISTR_MAT_CELL_SIZE);
				
				double dist2 = p.dist2(from);
				
				double pts = 0.0;
				
				if (anyDamage)
				{
					pts = (group.health * alphaM1 + dCell.totalEnemyHealth * alpha) / (dCell.totalEnemyHealth*0.01 + dCell.totalEnemyDamage) 
						- (dCell.totalEnemyHealth * alphaM1 + group.health * alpha) / (group.health * 0.01 + dCell.totalMyDamage);
				}
				else
				{
					if (dist2 > sqr(50))
						pts = 1.0;
				}
						
				
				/*if (enableFOW)
				{
					const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
					double dt = tick - cell.realUpdateTick;
		
					if (dt > 10)
					{
						pts /= sqr(dt / 10.0);
					}
				}*/
				
				pts *= (1.0 + dCell.totalEnemyHealth*0.0003);
				
				if (pts != 0.0)
				{
					double enemyVel = 0.0;
					
					for (int i = 0; i < 5; ++i) 
					{ 
						if (dCell.enemyHealth[i]) 
							enemyVel += unitVel((UnitType) i) * (dCell.enemyHealth[i] / dCell.totalEnemyHealth); 
					}
					
					if (pts < 0.0)
					{
						double t = captureTick(from, p, unitVel(group.unitType), enemyVel);
						res += pts * (625 - t) / 625.0;
					}
					else
					{
						/*double t = captureTick(p, from, enemyVel, unitVel(group.unitType));
						res += 0.1* pts * (625 - t) / 625.0;*/
					}
				}
				
				if (pts > 0.0)
				{
					double pp = 1.0/(1.0 + dist2);
					res += pts * pp;
				}
				else
				{
					/*for (int mt = 0; mt < 5; ++mt)
					{
						if (group.healthByTypes[mt])
						{
							for (int et = 0; et < 5; ++et)
							{
								if (dCell.enemyHealth[et])
								{
									double rad2 = DANGER_DISTS.dist((UnitType) et, (UnitType) mt);
									//double rad2 = 150*150;
									if (rad2 > dist2)
									{
										double fraction = group.healthByTypes[mt] / group.health * dCell.enemyHealth[et] / dCell.totalEnemyHealth;
										double pn = (1.0 - std::min(1.0, dist2/rad2));
										res += pts * pn * fraction;
									}
								}
							}
						}
					}*/
					
					double pn = (1.0 - std::min(1.0, dist2/sqr(150)));
					res += pts * pn;
				}
				
				//res += pts / p.dist2(from);
			}
			
			f2hDmgRes += dCell.f2hDmg;
		}
	}
	
	if (group.unitType == UnitType::FIGHTER)
	{
		double L = 1.5 * WIDTH;
		if (group.size * 80 > group.health)
		{
			Group *arvG = getGroup(UnitType::ARV);
			if (arvG && arvG->size > 20)
			{
				L = arvG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::ARV] * (80 - group.health/group.size)*0.2;
	}
	
	if (spy)
	{
		for (Group &g : groups)
		{
			if (&g != &group)
			{
				double dist2 = from.dist2(g.center);
				res -= 0.05 / (1.0 + dist2);
			}
		}
		
		if (enableFOW)
		{
			for (Building &b : buildings)
			{
				if (b.side != 0)
				{
					double dist2 = from.dist2(b.pos);
					double pp = 1.0/(1.0 + dist2);
					res += pp * 0.1;
				}
			}
		}
	}
	
	/*if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		double L = 1.5 * WIDTH;
		if (f2hDmgRes > 0.0 && res < 0.0)
		{
			if (fivG && fivG->size > 10)
			{
				L = fivG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::IFV];
		
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 300.0)
			{
				res -= (l - 300.0);
			}
		}
	}*/
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::HELICOPTER);
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 400.0)
			{
				res -= (l - 400.0);
			}
		}
	}*/
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::TANK);
		if (fivG) {
			double l = fivG->center.dist2(from);
			res += 0.0001 / (1.0 + l);
		}
	}
	
	if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		if (fivG) {
			double l = fivG->center.dist2(from);
			res += 0.0001 / (1.0 + l);
		}
	}*/
	
	if (isGroundUnit(group.unitType))
	{
		for (const Building &b : buildings)
		{
			if (b.side != 0)
			{
				double d = from.dist(b.pos);
				double coef = b.assignedGroup == group.internalId ? 2.0 : 1.0;
				res += coef*group.health/(20 + d)*0.1;
			}
		}
	}
	
	if (group.attractedToGroup >= 0)
	{
		Group &othG = groups[group.attractedToGroup];
		double d = from.dist(othG.center);
		res += group.health/(20 + d)*0.3;
	}
	
	return res;
}


ShrinkResult Strat::findShrink(Group &group)
{
	ShrinkResult result;
	
	Simulator sim;
	sim.tick = tick;
	std::copy(cells, cells + CELLS_X * CELLS_Y, sim.cells);
	
	BBox gbox = group.bbox;
	gbox.expand(2.0);
	for (const MyUnit &u : units)
	{
		if (gbox.inside(u.pos))
		{
			sim.units.push_back(u);
			MyUnit &tu = *sim.units.rbegin();
			tu.selected = group.check(tu);
			tu.vel = P(0, 0);
		}
	}
	sim.groups.push_back(group);
	
	double bestArea = WIDTH * HEIGHT;
	for (double x = -1; x <= 1; ++x)
	{
		for (double y = -1; y <= 1; ++y)
		{
			Simulator sim2 = sim;
			MyMove myMove;
			myMove.action = MyActionType::SCALE;
			myMove.p = group.center + P(x, y) * 10.0;
			myMove.factor = 0.2;
			sim2.registerMove(myMove, 0);
			
			int i = 0;
			for (; i < 20; ++i)
			{
				sim2.tick++;
				sim2.resetAxisSorts();
				sim2.applyMoves();
				int cnt = sim2.moveUnits();
				if (!cnt)
					break;
			}
			sim2.updateStats();
			
			double area = sim2.groups.rbegin()->bbox.area();
			if (area < bestArea)
			{
				bestArea = area;
				result.shrinkPoint = myMove.p;
				result.ticks = i;
				result.endBBox = sim2.groups.rbegin()->bbox;
			}
		}
	}
	
	return result;
}
}

namespace StratV22 {
	double groupPriority(const Group &g) {
	double res = 0.0;
	
	if (g.shrinkAfterNuke)
		res += 1100.0;
	else if (g.actionStarted)
		res += 1000.0;
	
	res -= g.lastUpdateTick;
	
	return res;
}

void DistributionMatrix::clear()
{
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		Cell &cell = cells[i];
		for (int j = 0; j < 5; ++j)
			cell.count[j] = 0;
		for (int j = 0; j < 5; ++j)
			cell.health[j] = 0;
	}
}

void DistributionMatrix::initialize(const Simulator &sim, bool firstTick)
{
	clear();
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : sim.units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / DISTR_MAT_CELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / DISTR_MAT_CELL_SIZE;
				
				Cell &cell = getCell(x, y);
				int type = (int) u.type;
				cell.count[type] += 1.0 / K;
				cell.health[type] += u.durability / K;
			}
		}
		
		/*if (sim.enableFOW && firstTick && u.side == 0)
		{
			P pos = P(WIDTH, HEIGHT) - u.pos;
			int x = pos.x / DISTR_MAT_CELL_SIZE;
			int y = pos.y / DISTR_MAT_CELL_SIZE;
			Cell &cell = getCell(x, y);
			int type = (int) u.type;
			cell.count[type] += 1.0;
			cell.health[type] += u.durability;
		}*/
	}
}

void DistributionMatrix::blur(DistributionMatrix &oth) const
{
	oth.clear();
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			Cell &resCell = oth.getCell(x, y);
			int cnt = 0;
			for (int yy = -1; yy <= 1; ++yy)
			{
				for (int xx = -1; xx <= 1; ++xx)
				{
					int x2 = x + xx;
					int y2 = y + yy;
					if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
					{
						++cnt;
						const Cell &cell = getCell(x2, y2);
						for (int i = 0; i < 5; ++i)
							resCell.count[i] += cell.count[i];
						for (int i = 0; i < 5; ++i)
							resCell.health[i] += cell.health[i];
					}
				}
			}
			
			const Cell &cell = getCell(x, y);
			for (int i = 0; i < 5; ++i)
			{
				if (!cell.count[i])
				{
					resCell.count[i] = 0;
					resCell.health[i] = 0;
				}
			}
			
			/*for (int i = 0; i < 5; ++i)
				resCell.count[i] += (resCell.count[i] + cnt - 1) / cnt;
			for (int i = 0; i < 5; ++i)
				resCell.health[i] /= cnt;*/
		}
	}
}

Strat::Strat::Strat()
{
	Group g;
	g.actionStarted = false;
	g.lastUpdateTick = 0;
	g.lastShrinkTick = 0;
	
	for (int i = 0; i < 1; ++i)
	{
		g.miniGroupInd = -1;
		g.unitType = UnitType::HELICOPTER;
		//g.miniGroupInd = i;
		g.internalId = internalGroupSeq++;
		groups.push_back(g);
	}
	g.miniGroupInd = -1;
	g.unitType = UnitType::IFV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	/*g.miniGroupInd = 1;
	g.unitType = UnitType::IFV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);*/
	
	g.miniGroupInd = -1;
	g.unitType = UnitType::FIGHTER;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.miniGroupInd = -1;
	g.unitType = UnitType::TANK;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.miniGroupInd = -1;
	g.unitType = UnitType::ARV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
}


void Strat::calcMicroShift(Group &group, P &shift)
{
	if (group.unitType != UnitType::TANK && group.unitType != UnitType::FIGHTER)
		return;
	
	BBox bbox = group.bbox;
	bbox.p1 += shift - P(35, 35);
	bbox.p2 += shift + P(35, 35);
	
	int finalScore = -100000;
	int currentScore = -100000;
	double ticks = 20.0;
	
	//if (group.unitType == UnitType::TANK)
	{
		std::vector<P> enemies[(int) UnitType::COUNT];
		std::vector<P> myUnits;
		
		for (const MyUnit &u : units)
		{
			if (u.side > 0 && (u.type == UnitType::TANK || u.type == UnitType::HELICOPTER || u.type == UnitType::IFV) && bbox.inside(u.pos))
			{
				enemies[(int) u.type].push_back(u.pos + u.vel * ticks);
			}
			else if (group.check(u))
			{
				myUnits.push_back(u.pos);
			}
		}
		
		double L = 8.0;
		P newShift = shift;
		for (int k = 0; k < 4; ++k)
		{
			P foundShift = newShift;
			int foundScore = -100000;
			for (int i = 0; i < 9; ++i)
			{
				P testShift = newShift + P(i / 3 - 1, i % 3 - 1) * L;
				int score = 0;
				bool isCurShift = shift.dist2(testShift) < 0.1;
				
				if (!isCurShift || currentScore == -100000)
				{
					const std::vector<MicroShiftValues> &pos = microShiftMatrix.pos[(int) group.unitType];
					if (!pos.empty())
					{
						for (const P &myP : myUnits)
						{
							P myPos = myP + testShift;
							bool found = false;
							for (const MicroShiftValues &vals : pos)
							{
								if (!found)
								{
									for (const P &enP : enemies[(int) vals.unitType])
									{
										if (myPos.dist2(enP) < vals.dist2)
										{
											score += vals.val;
											found = true;
											break;
										}
									}
								}
							}
						}
					}
					
					// !! decrease SCORE
					int posScore = score;
					score /= 2;
					
					const std::vector<MicroShiftValues> &neg = microShiftMatrix.neg[(int) group.unitType];
					if (!neg.empty())
					{
						for (const MicroShiftValues &vals : neg)
						{
							for (const P &enP : enemies[(int) vals.unitType])
							{
								for (const P &myP : myUnits)
								{
									P myPos = myP + testShift;
									if (myPos.dist2(enP) < vals.dist2)
									{
										score -= vals.val;
										break;
									}
								}
							}
						}
					}
					
					int negScore = posScore / 2 - score;
					
					if (foundScore < score)
					{
						foundShift = testShift;
						foundScore = score;
					}
					
					if (isCurShift)
						currentScore = score;
				}
			}
			
			L /= 2.0;
			newShift = foundShift;
			
			finalScore = foundScore;
		}
		
		if (currentScore >= finalScore)
			return;
		
		shift = newShift;
	}
}

void Strat::calcNuclearEfficiency()
{
	for (int i = 0; i < MICROCELLS_X * MICROCELLS_Y; i++)
		nuclearEfficiency[i] = 0;
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / MICROCELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / MICROCELL_SIZE;
				
				double efficiency = 100.0 / (10.0 + u.durability) / K;
				
				if (u.type == UnitType::ARV)
					efficiency /= 10.0;
				
				if (u.type == UnitType::FIGHTER || u.type == UnitType::HELICOPTER)
					efficiency /= 1.5;
				
				nuclearEfficiency[y * MICROCELLS_X + x] += efficiency;
			}
		}
	}
}

UnitType Strat::calcNextUnitTypeForConstruction(bool ground)
{
	/*double totalCount = 0;
	double assetCount[5] = {};
	for (int i = 0; i < 5; ++i)
	{
		totalCount += myCount[(UnitType) i];
		assetCount[i] = myCount[(UnitType) i];
	}
	
	double totalAssets = totalCount;
	for (const Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE)
		{
			int cnt = std::max(0, 40 - b.unitCount);
			totalAssets += cnt;
			assetCount[(int) b.unitType] += cnt;
		}
	}
	
	if (assetCount[(int) UnitType::TANK] > 300)
	{
		if (assetCount[(int) UnitType::FIGHTER] < totalAssets * 0.20)
		{
			LOG("CREATE FIGHTER");
			return UnitType::FIGHTER;
		}
		
		if (assetCount[(int) UnitType::HELICOPTER] < totalAssets * 0.20)
		{
			LOG("CREATE HELICOPTER");
			return UnitType::HELICOPTER;
		}
	}
	LOG("CREATE TANK");
	return UnitType::TANK;*/
	
	////////////
	
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType == UnitType::HELICOPTER)
		{
			return UnitType::TANK;
		}
	}
	
	if (enemyCount[UnitType::HELICOPTER] * 0.9 > myCount[UnitType::HELICOPTER])
	{
		return UnitType::HELICOPTER;
	}
	
	return UnitType::TANK;
	
	/*double score[5] = {};
	
	int enCnt = enemyCount[UnitType::HELICOPTER]*0.7 + enemyCount[UnitType::FIGHTER]*0.3;
	int myCnt = myCount[UnitType::FIGHTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::FIGHTER] += enCnt - myCnt;
	}
	score[(int) UnitType::FIGHTER] *= 0.6;
	
	enCnt = enemyCount[UnitType::TANK];
	myCnt = myCount[UnitType::HELICOPTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::HELICOPTER] += enCnt - myCnt;
	}
	
	score[(int) UnitType::HELICOPTER] *= 0.8;
	
	enCnt = enemyCount[UnitType::IFV];
	myCnt = myCount[UnitType::TANK];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::TANK] += enCnt - myCnt;
	}
	
	enCnt = enemyCount[UnitType::FIGHTER]*0.7 + enemyCount[UnitType::HELICOPTER]*0.3;
	myCnt = myCount[UnitType::IFV];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::IFV] += enCnt - myCnt;
	}
	
	
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE)
		{
			score[(int) b.unitType] -= 20;
		}
	}
	
	double grCount = 0;
	double airCount = 0;
	for (int i = 0; i < 5; ++i)
	{
		if (isGroundUnit((UnitType) i) && i != (int) UnitType::ARV)
		{
			grCount += myCount[(UnitType) i];
		}
		else
		{
			airCount += myCount[(UnitType) i];
		}
	}
	
	double totalCount = grCount + airCount;
	if (totalCount > 0)
	{
		for (int i = 0; i < 5; ++i)
		{
			if (isGroundUnit((UnitType) i))
			{
				score[i] *= airCount;
			}
			else
			{
				score[i] *= grCount;
			}
		}
	}
	
	int res = 0;
	int resType = -1;
	for (int i = 0; i < 5; ++i)
	{
		if (score[i] > res)
		{
			res = score[i];
			resType = i;
		}
	}
	
	if (resType >= 0)
		return (UnitType) resType;
	
	return UnitType::TANK;*/
}

MyMove Strat::calcNextMove()
{
	MyMove result;
	result.action = MyActionType::NONE;
	
	int moves = getAvailableActions(12);
	if (moves < 1)
		return result;
	
	if (players[1].remainingNuclearStrikeCooldownTicks < 30)
	{
		if (moves < 3)
			return result;
	}
	
	updateStats();
	
	if (players[1].nextNuclearStrikeTick >= 0)
	{
		bool select = false;
		bool expand = false;
		BBox bbox;
		int evaidingGroups = 0;
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep == 0)
			{
				BBox gbox = g.bbox;
				gbox.expand(40.0);
				if (gbox.inside(players[1].nuclearStrike))
				{
					bbox.add(g.bbox);
					g.nukeEvadeStep = 1;
					select = true;
					++evaidingGroups;
				}
			}
			else if (g.nukeEvadeStep == 1)
			{
				expand = true;
				g.nukeEvadeStep = 2;
				++evaidingGroups;
			}
			else
			{
				++evaidingGroups;
			}
		}
		
		if (select)
		{
			result.action = MyActionType::CLEAR_AND_SELECT;
			result.p1 = bbox.p1;
			result.p2 = bbox.p2;
			
			return result;
		}
		else if (expand)
		{
			result.action = MyActionType::SCALE;
			result.p = players[1].nuclearStrike;
			result.factor = 9.0;
			return result;
		}
		
		if (evaidingGroups && moves < 3)
			return result;
	}
	else
	{
		for (Group &g : groups)
		{
			if (tick - g.lastShrinkTick > 30)
				g.shrinkAfterNuke = false;
			if (g.nukeEvadeStep != 0)
			{
				g.shrinkAfterNuke = true;
				g.nukeEvadeStep = -1;
			}
		}
	}
	
	/*if (tick > 200) {
		int moves = getAvailableActions(4, 10);
		if (moves < 1) // throttle
			return result;
	}*/
	
	if (!initialGroupsGerationDone && !buildings.empty())
	{
		for (Group &g : groups)
		{
			/*if (g.unitType == UnitType::HELICOPTER && !g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					P allCenter = P(0, 0);
					int count = 0;
					for (const MyUnit &u : units)
					{
						if (u.side == 0 && u.type == g.unitType)
						{
							allCenter += u.pos;
							++count;
						}
					}
					
					allCenter /= count;
					
					if (g.miniGroupInd == 0)
					{
						result.p1 = P(0, 0);
						result.p2 = allCenter;
					}
					else if (g.miniGroupInd == 1)
					{
						result.p1 = P(allCenter.x, 0);
						result.p2 = P(WIDTH, allCenter.y);
					}
					else if (g.miniGroupInd == 2)
					{
						result.p1 = P(0, allCenter.y);
						result.p2 = P(allCenter.x, HEIGHT);
					}
					else if (g.miniGroupInd == 3)
					{
						result.p1 = allCenter;
						result.p2 = P(WIDTH, HEIGHT);
					}
					
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}*/
			
			if (!g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					
					if (g.miniGroupInd > -1)
					{
						P allCenter = P(0, 0);
						int count = 0;
						for (const MyUnit &u : units)
						{
							if (u.side == 0 && u.type == g.unitType)
							{
								allCenter += u.pos;
								++count;
							}
						}

						allCenter /= count;

						if (g.miniGroupInd == 0)
						{
							result.p2 = P(allCenter.x, HEIGHT);
						}
						else
						{
							result.p1 = P(allCenter.x, 0);
						}
					}
					//result.p1 = g.center - P(30, 30); // TODO
					//result.p2 = g.center + P(30, 30);
					
					g.enumGroupBuildStep++;
					return result;
				}
				else if (g.enumGroupBuildStep == 1)
				{
					result.action = MyActionType::ASSIGN;
					g.enumGroupBuildStep++;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}
		}
		
		initialGroupsGerationDone = true;
		LOG("DONE");
	}
	
	for (Building &b : buildings)
	{
		/*if (b.lastChangeUnitCount > b.unitCount)
			b.lastChangeUnitCount = b.unitCount;*/
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && (b.unitType == UnitType::NONE/* || (b.unitCount - b.lastChangeUnitCount) > 11*/))
		{
			//LOG("SVP " << buildingCaptured);
			result.action = MyActionType::SETUP_VEHICLE_PRODUCTION;
			result.facilityId = b.id;
			
			/*if (buildingCaptured % 2 == 0)
				result.unitType = UnitType::FIGHTER;
			else
				result.unitType = UnitType::IFV;*/
			
			//result.unitType = UnitType::TANK;
			result.unitType = calcNextUnitTypeForConstruction(false);
			//result.unitType = (UnitType) (buildingCaptured % 5);
			//b.lastChangeUnitCount = b.unitCount;
			
			++buildingCaptured;
			return result;
		}
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE && b.unitCount > 50 && b.productionProgress < 30
			|| b.createGroupStep > 0
			//|| b.side != 0 && b.unitCount > 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE && !isGroundUnit(b.unitType)
		)
		{
			UnitType unitType = UnitType::NONE;
			int cnt = 0;
			for (int i = 0; i < 5; ++i)
			{
				if (cnt < b.unitCountByType[i])
				{
					cnt = b.unitCountByType[i];
					unitType = (UnitType) i;
				}
			}
			
			UnitType newUnitType = calcNextUnitTypeForConstruction(false);
			if (b.unitType != newUnitType && unitType != newUnitType)
			{
				result.action = MyActionType::SETUP_VEHICLE_PRODUCTION;
				result.facilityId = b.id;
				result.unitType = newUnitType;
				return result;
			}
			
			//LOG("MAKE GROUP");
			if (b.createGroupStep == 0 || b.createGroupStep == 1)
			{
				BBox bbox;
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && !p.groups.any() && b.checkPoint(p.pos))
					{
						bbox.add(p.pos);
					}
				}
				
				result.p1 = bbox.p1 - P(1, 1);
				result.p2 = bbox.p2 + P(1, 1);
				
				result.action = MyActionType::CLEAR_AND_SELECT;
				/*result.p1 = b.pos - P(32, 32); 
				result.p2 = b.pos + P(32, 32); */
				
				result.unitType = unitType;
				b.createGroupStep = 2;
				return result;
			}
			
			if (b.createGroupStep == 2)
			{
				// check if selected
				bool anySelected = false;
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.groups.count() == 0 && p.type == unitType)
					{
						if (p.pos.x > b.pos.x - 32 && p.pos.x < b.pos.x + 32 && p.pos.y > b.pos.y - 32 && p.pos.y < b.pos.y + 32)
						{
							if (p.selected)
							{
								anySelected = true;
								break;
							}
						}
					}
				}
				
				if (anySelected)
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					b.createGroupStep = 3;
					
					Group newGroup;
					newGroup.unitType = unitType;
					newGroup.group = result.group;
					newGroup.internalId = internalGroupSeq++;
					groups.push_back(newGroup);
					
					b.createGroupStep = 0;
					return result;
				}
				else
				{
					b.createGroupStep = 1;
				}
			}
		}
	}
	
		
	
	
	bool angryMode = angryModeTill > tick;

	
	DistributionMatrix matr;
	matr.initialize(*this, !distributionMatrixInitialized);
	
	//if (!enableFOW || !distributionMatrixInitialized)
	{
		matr.blur(distributionMatrix);
		distributionMatrixInitialized = true;
	}
	/*else
	{
		DistributionMatrix othMatr;
		matr.blur(othMatr);
		
		spreadDistributionMatrix();
		
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &dcell = distributionMatrix.getCell(x, y);
				DistributionMatrix::Cell &simDcell = othMatr.getCell(x, y);
				double visFactor = visibilityFactors[y * DISTR_MAT_CELLS_X + x];
				
				if (visFactor > 0.8)
				{
					dcell = simDcell;
					dcell.updateTick = tick;
					dcell.realUpdateTick = tick;
				}
				else
				{
					for (int i = 0; i < 5; ++i)
					{
						dcell.health[i] = std::max(dcell.health[i], simDcell.health[i]);
						dcell.count[i] = std::max(dcell.count[i], simDcell.count[i]);
					}
					
					if (visFactor > 0.5)
					{
						dcell.updateTick = tick;
						dcell.realUpdateTick = tick;
					}
				}
			}
		}
	}*/
	
	/*if (tick > 532)
	{
		std::cout.width(2);
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &resCell = distributionMatrix.getCell(x, y);
				std::cout << " " << resCell.count[1];
			}
			std::cout << std::endl;
		}
		std::cout << std::endl;
	}*/
	
	if (players[0].remainingNuclearStrikeCooldownTicks == 0)
	{
		resetCells();
		calcNuclearEfficiency();
		
		auto getEnemySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0.0;
			
			return nuclearEfficiency[y * MICROCELLS_X + x];
		};
		
		auto getMySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0;
			
			return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
				+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
				+ (int) getMicrocell(x, y, 0, UnitType::ARV).size() / 2;
		};
		
		P bestTarget;
		int bestScore = 0;
		
		for (int y = 0; y < MICROCELLS_Y; ++y) {
			for (int x = 0; x < MICROCELLS_X; ++x) {
				double enemyN = getEnemySize(x, y);
				if (enemyN > 10)
				{
					int totalMyCount = 0;
					int myCenterCount = 0;
					for (int xx = -1; xx <= 1; ++xx)
					{
						for (int yy = -1; yy <= 1; ++yy)
						{
							totalMyCount += getMySize(x + xx, y + yy);
							if (xx == 0 && yy == 0)
								myCenterCount = totalMyCount;
						}
					}
					
					if (totalMyCount < 20 && myCenterCount == 0)
					{
						int totalMyExtCount = totalMyCount;
						if (totalMyExtCount == 0)
						{
							for (int xx = -2; xx <= 2; ++xx)
							{
								for (int yy = -2; yy <= 2; ++yy)
								{
									if (xx < -1 || xx > 1 || yy < -1 || yy > 1)
										totalMyExtCount += getMySize(x + xx, y + yy);
								}
							}
						}
						
						if (totalMyExtCount > 0)
						{
							double totalEnemyCount =
								enemyN +
								getEnemySize(x - 1, y)/2 +
								getEnemySize(x + 1, y)/2 + 
								getEnemySize(x, y - 1)/2 + 
								getEnemySize(x, y + 1)/2;
							
							if (bestScore < totalEnemyCount)
							{
								bestScore = totalEnemyCount;
								bestTarget = P(x + 0.5, y + 0.5) * MICROCELL_SIZE;
							}
						}
					}
				}
			}
		}
		
		if (bestScore > 0)
		{
			// Correct point
			P correctedBestTarget = bestTarget;
			
			std::vector<P> myVehicles;
			std::vector<P> enemyVehicles;
			int myInnerN = 0;
			int enemyInnerN = 0;
			for (const MyUnit &p : units)
			{
				double d2 = p.pos.dist2(bestTarget);
				if (d2 < (75.0*75.0))
				{
					if (d2 < (38.0*38.0))
					{
						if (p.side == 0)
							myInnerN++;
						else
							enemyInnerN++;
					}
					else
					{
						if (p.side == 0)
							myVehicles.push_back(p.pos);
						else
							enemyVehicles.push_back(p.pos);
					}
				}
			}
			
			int foundScore = -100000;
			for (int xx = -2; xx <= 2; ++xx)
			{
				for (int yy = -2; yy <= 2; ++yy)
				{
					P newTarget = bestTarget + P(xx, yy) * 6.0;
					
					int myNum = myInnerN*3;
					int myExtNum = 0;
					
					for (const P &p : myVehicles)
					{
						double d2 = p.dist2(newTarget);
						if (d2 < (50.0*50.0))
						{
							myNum++;
						}
						else if (d2 > (55.0*55.0) && d2 < (70.0*70.0))
						{
							myExtNum++;
						}
					}
					
					if (myExtNum > 0)
					{
						int enNum = enemyInnerN*3;
						for (const P &p : enemyVehicles)
						{
							double d2 = p.dist2(newTarget);
							if (d2 < (50.0*50.0))
							{
								enNum++;
							}
						}
						
						int score = enNum - myNum * 4;
						if (score > foundScore)
						{
							foundScore = score;
							correctedBestTarget = newTarget;
						}
					}
				}
			}
			
			if (foundScore > -100000)
			{
				auto getMySize = [this](int x, int y) {
					if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
						return 0;
					
					return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
						+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
						+ (int) getMicrocell(x, y, 0, UnitType::ARV).size();
				};
				
				int pts = -1000000;
				long bestId = -1;
				P vehPos;
				
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.durability > 70)
					{
						double dist2 = p.pos.dist2(correctedBestTarget);
						if (dist2 > 55*55 && dist2 < 70*70)
						{
							bool outOfRange = false;
							
							double vel2 = p.vel.len2();
							
							MyUnit u = p;
							for (int i = 0; i < 30; ++i)
							{
								double visRange = getVisionRange(u) - unitVel(p.type) * 10;
								if (u.pos.dist2(correctedBestTarget) > sqr(visRange))
								{
									outOfRange = true;
									break;
								}
								
								if (vel2 < 0.01)
									break;
								u.pos += u.vel;
							}
							
							if (outOfRange)
								continue;
							
							int xx = p.pos.x / MICROCELL_SIZE;
							int yy = p.pos.y / MICROCELL_SIZE;
							
							//int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							if (pts < newPts)
							{
								pts = newPts;
								bestId = p.id;
								vehPos = p.pos;
							}
						}
					}
				}
				
				if (bestId >= 0)
				{
					result.action = MyActionType::TACTICAL_NUCLEAR_STRIKE;
					result.vehicleId = bestId;
					result.p = correctedBestTarget;
					return result;
				}
			}
		}
	}
	
	groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group &g){return g.size == 0;}), groups.end());
	
	if (result.action == MyActionType::NONE && !groups.empty())
	{
		debugAttractionPoints.clear();
		
		std::sort(groups.begin(), groups.end(), [](const Group &g1, const Group &g2){
			double p1 = groupPriority(g1);
			double p2 = groupPriority(g2);
			return p2 < p1;
		});
		
		assignBuildings();
		updateGroupAttraction();
		
		if (enableFOW)
			calcVisibilityFactors();
		
		for (auto groupIt = groups.begin(); groupIt != groups.end(); ++groupIt)
		{
			Group &group = *groupIt;
			
			if (tick - group.lastComputeTick < 10 || group.nukeEvadeStep > 0)
				continue;
			
			if (group.shrinkActive)
			{
				if (tick - group.lastShrinkTick > 40 || !anyMoved(group))
				{
					group.shrinkActive = false;
				}
				else
				{
					continue;
				}
			}
			
			if (group.attractedToGroup >= 0)
			{
				Group &othG = groups[group.attractedToGroup];
				double dist2 = othG.center.dist2(group.center);
				if (dist2 < sqr(40))
				{
					if (!isSelected(group))
					{
						result = select(group);
						group.actionStarted = true;
						return result;
					}
					
					result.action = MyActionType::ASSIGN;
					result.group = othG.group;
					group.group = 0;
					group.unitType = UnitType::NONE;
					
					groups.erase(groupIt);
					//LOG("JOIN GROUPS ASSIGN " << (int) othG.group);
					return result;
				}
			}
			
			bool limitSpeed = false;
			if (nukeVehicleInd >= 0)
			{
				const MyUnit &u = units[nukeVehicleInd];
				if (group.check(u))
					limitSpeed = true;
			}
			
			calcDangerDistCells(group);
			
			P center = group.center;
			const BBox &bbox = group.bbox;
			int groupSize = group.size;
			double area = bbox.area();
			bool shrinkRequired = (area > groupSize * 40.0 || group.shrinkAfterNuke) && (((tick - group.lastUpdateTick) > 60 || group.shrinkAfterNuke) && (tick - group.lastShrinkTick) > 300);
			P shrinkPoint;
			if (shrinkRequired)
			{
				ShrinkResult shRes = findShrink(group);
				if (shRes.ticks > 7)
				{
					shrinkPoint = shRes.shrinkPoint;
				}
				else
				{
					shrinkRequired = false;
				}
				//LOG("SHRINK RES " << shRes.ticks << " " << shRes.endBBox << " P " << shRes.shrinkPoint);
			}
			//bool shrinkRequired = false;
			
			const double border = 20.0;
			P c = center;
			double R = 20 + unitVel(group.unitType) * 40;
			/*if (group.unitType == UnitType::ARV)
				R = 200.0;*/
			double ticks = R / unitVel(group.unitType);
			double curVal = attractionPoint(center, group, ticks, angryMode);
			P tp = center;
			bool found = false;
			
			bool canMoveFlag = false;
			
			P clampP1 = group.center - bbox.p1 + P(3.0, 3.0);
			P clampP2 = P(WIDTH - 3.0, HEIGHT - 3.0) + (group.center - bbox.p2);
			for (int k = 0; k < 3.0; ++k)
			{
				ticks = R / unitVel(group.unitType);
				int di = -1;
				
				for (int i = 0; i < 20; ++i)
				{
					P p = c + P(PI * 2.0 / 20.0 * i) * R;
					p = clampP(p, clampP1, clampP2);
					
					P shift = p - center;
					if (shift.len2() > 0.01)
					{
						if (canMove(p - center, group))
						{
							canMoveFlag = true;
							double val = attractionPoint(p, group, ticks, angryMode);
							
							DebugAttractionPointsInfo debugInfo;
							debugInfo.point = c;
							debugInfo.dir = p - c;
							debugInfo.val = val;
							debugAttractionPoints.push_back(debugInfo);
							
							if (val > curVal)
							{
								curVal = val;
								di = i;
								tp = p;
								found = true;
							}
						}
					}
				}
				
				if (di >= 0)
				{
					c = tp;
				}
				
				R /= 1.5;
			}
			
			if (!canMoveFlag)
			{
				double R = 20 + unitVel(group.unitType) * 40;
				
				std::vector<const MyUnit *> groupUnits;
				std::vector<const MyUnit *> otherUnits;
				
				BBox bbox = group.bbox;
				bbox.p1 -= P(2.0*R, 2.0*R);
				bbox.p2 += P(2.0*R, 2.0*R);
				
				for (const MyUnit &u : units)
				{
					if (group.check(u))
					{
						groupUnits.push_back(&u);
					}
					else if (group.canIntersectWith(u) && bbox.inside(u.pos))
					{
						otherUnits.push_back(&u);
					}
				}
				
				for (int k = 0; k < 3.0; ++k)
				{
					ticks = R / unitVel(group.unitType);
					int di = -1;
					
					for (int i = 0; i < 19; ++i)
					{
						P p = c + P(PI * 2.0 / 19.0 * i) * R;
						p = clampP(p, clampP1, clampP2);
						
						P shift = p - center;
						if (shift.len2() > 0.01)
						{
							if (canMoveDetailed(p - center, group, groupUnits, otherUnits))
							{
								canMoveFlag = true;
								double val = attractionPoint(p, group, ticks, angryMode);
								
								DebugAttractionPointsInfo debugInfo;
								debugInfo.point = c;
								debugInfo.dir = p - c;
								debugInfo.val = val;
								debugAttractionPoints.push_back(debugInfo);
								
								if (val > curVal)
								{
									curVal = val;
									di = i;
									tp = p;
									found = true;
								}
							}
						}
					}
					
					if (di >= 0)
					{
						c = tp;
					}
					
					R /= 1.5;
				}
				
				/*if (!canMoveFlag)
				{
					UnitType typeToSelect = UnitType::NONE;
					bool selected = false;
					GroupId otherGr = 0;
					for (const MyUnit *o : otherUnits)
					{
						if (group.bbox.inside(o->pos) && o->side == 0)
						{
							typeToSelect = o->type;
							if (o->selected)
								selected = true;
							
							if (o->groups.any())
							{
								for (Group &oth : groups)
								{
									if (oth.group != group.group && o->hasGroup(oth.group))
									{
										otherGr = oth.group;
										break;
									}
								}
								if (otherGr > 0)
									break;
							}
						}
					}
					
					if (typeToSelect != UnitType::NONE)
					{
						if (!selected)
						{
							result.p1 = group.bbox.p1;
							result.p2 = group.bbox.p2;
							result.unitType = typeToSelect;
							result.action = MyActionType::CLEAR_AND_SELECT;
							
							group.actionStarted = true;
							
							LOG("SELECT TO " << (int) group.group << " " << group.center);
							return result;
						}
						
						if (otherGr > 0)
						{
							result.action = MyActionType::DISMISS;
							result.group = otherGr;
							LOG("DISMISS TO " << (int) otherGr << " " << group.center);
							return result;
						}
						else
						{
							result.action = MyActionType::ASSIGN;
							result.group = group.group;
							LOG("ASSIGN TO " << (int) group.group << " " << group.center);
							return result;
						}
					}
				}*/
			}
			
			bool moveAway = false;
			P dirAway;
			P newShift = P(0, 0);
			if (!canMoveFlag)
			{
				//LOG("CANT MOVE " << group.center);
				group.canMove = false;
				
				dirAway = P(0, 0);
				for (Group &othGroup : groups)
				{
					if (&group != &othGroup && isGroundUnit(group.unitType) == isGroundUnit(othGroup.unitType))
					{
						P dp = group.center - othGroup.center;
						double clen = dp.len();
						
						if (clen < 150.0)
							dirAway += dp / clen;
					}
				}
				
				dirAway *= 10;
				dirAway += P(tick % 7 - 3, tick % 9 - 5);
				moveAway = true;
			}
			else
			{
				group.canMove = true;
				
				if (found)
					newShift = tp - center;
				
				if (newShift.len() < 15.0)
				{
					calcMicroShift(group, newShift);
				}
			}
			
			found = newShift.len2() > 0.1;
			
			if (found || shrinkRequired || moveAway)
			{
				if (!isSelected(group))
				{
					result = select(group);
					group.actionStarted = true;
				}
				else
				{
					if (shrinkRequired)
					{
						result.action = MyActionType::SCALE;
						result.factor = 0.2;
						result.p = shrinkPoint;
						group.lastShrinkTick = tick;
						group.shrinkActive = true;
						group.nukeEvadeStep = 0;
					}
					else if (found)
					{
						result.action = MyActionType::MOVE;
						result.p = newShift;
						
						if (limitSpeed)
							result.maxSpeed = unitVel(group.unitType) * 0.6;
						
						/*if (!limitSpeed)
						{
							limitSpeed = !anyEnemiesNearbyByDangerDistr(group);
							if (limitSpeed)
								result.maxSpeed = unitVel(group.unitType) * 0.8;
						}*/
						
						/*if (group.unitType == UnitType::FIGHTER)
							result.maxSpeed = 1.0;*/
					}
					else
					{
						result.action = MyActionType::MOVE;
						result.p = dirAway;
						//std::cout << "AWAY " << dirAway.x << " " << dirAway.y << std::endl;
					}
					
					group.lastUpdateTick = tick;
					group.lastComputeTick = tick;
					group.actionStarted = false;
				}
				
				break;
			}
			else if (!found)
			{
				group.lastComputeTick = tick;
				group.lastUpdateTick = tick; // TODO ???
			}
		}
	}
	
	return result;
}

void Strat::assignBuildings()
{
	std::set<Building *> pbuildings;
	
	for (Building &b : buildings)
	{
		if (b.side != 0)
		{
			pbuildings.insert(&b);
			b.assignedGroup = 0;
		}
	}
	
	std::set<Group *> pgroups;
	for (Group &g : groups)
	{
		if (isGroundUnit(g.unitType))
		{
			pgroups.insert(&g);
		}
	}
	
	size_t count = std::min(pbuildings.size(), pgroups.size());
	for (int i = 0; i < count; ++i)
	{
		double dist2 = sqr(100000.0);
		std::set<Building *>::iterator b = pbuildings.end();
		std::set<Group *>::iterator g = pgroups.end();
		for (std::set<Building *>::iterator bit = pbuildings.begin(); bit != pbuildings.end(); ++bit)
		{
			for (std::set<Group *>::iterator git = pgroups.begin(); git != pgroups.end(); ++git)
			{
				double d2 = (*bit)->pos.dist2((*git)->center);
				if (d2 < dist2)
				{
					dist2 = d2;
					b = bit;
					g = git;
				}
			}
		}
		
		if (b != pbuildings.end() && g != pgroups.end())
		{
			(*b)->assignedGroup = (*g)->internalId;
			pbuildings.erase(b);
			pgroups.erase(g);
		}
	}
}

void Strat::updateGroupAttraction()
{
	if (buildings.empty())
		return;
	
	std::vector<Group *> grps[2];
	
	int grCount[5] = {};
	for (Group &g : groups)
	{
		g.attractedToGroup = -1;
		
		if (g.unitType != UnitType::NONE)
		{
			if (isGroundUnit(g.unitType))
				grps[0].push_back(&g);
			else
				grps[1].push_back(&g);
			
			grCount[(int) g.unitType]++;
		}
	}
	
	constexpr int MAX_GROUPS = 5;
	for (int i = 0; i < 2; ++i)
	{
		if (grps[i].size() <= MAX_GROUPS)
			continue;
		
		/*std::sort(grps[i].begin(), grps[i].end(), [](const Group *g1, const Group *g2) {
			return g1->health < g2->health;
		});*/
	
		double pts = 1e10;
		Group *bestG = nullptr;
		for (Group *g : grps[i])
		{
			if (grCount[(int) g->unitType] > 1)
			{
				double minDist2 = 1e8;
				int targetK = -1;
				
				for (int k = 0; k < groups.size(); ++k)
				{
					Group &othG = groups[k];
					
					if (&othG != g && othG.unitType == g->unitType)
					{
						double dist2 = othG.center.dist2(g->center);
						
						if (dist2 < minDist2)
						{
							minDist2 = dist2;
							targetK = k;
						}
					}
				}
				
				if (targetK >= 0 && minDist2 < sqr(300))
				{
					g->attractedToGroup = targetK;
					double curPts = sqrt(minDist2)*5 + g->health;
					if (curPts < pts)
					{
						pts = curPts;
						bestG = g;
					}
				}
			}
		}
		
		for (Group *g : grps[i])
		{
			if (g != bestG)
				g->attractedToGroup = -1;
		}
	}
}

void Strat::spreadDistributionMatrix()
{
	double horTicks[5];
	double diagTicks[5];
	int xx[8] = {-1, 1,  0,  0,  -1, -1, 1,  1};
	int yy[8] = { 0, 0, -1, -1,  -1,  1, 1, -1};
	for (int i = 0; i < 5; ++i)
	{
		double vel = unitVel((UnitType) i);
		horTicks[i] = DISTR_MAT_CELL_SIZE / vel;
		diagTicks[i] = horTicks[i] * 1.4;
	}
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DistributionMatrix::Cell &dcell = distributionMatrix.getCell(x, y);
			
			for (int i = 0; i < 5; ++i)
			{
				for (int j = 0; j < 8; ++j)
				{
					double dt = j < 4 ? horTicks[i] : diagTicks[i];
					if (dcell.updateTick + dt < tick)
					{
						int x2 = x + xx[j];
						int y2 = y + yy[j];
						if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
						{
							DistributionMatrix::Cell &othCell = distributionMatrix.getCell(x2, y2);
							double visFactor = visibilityFactors[y2 * DISTR_MAT_CELLS_X + x2];
							if (visFactor < 0.7)
							{
								othCell.health[i] = std::max(othCell.health[i], dcell.health[i]);
								othCell.count[i] = std::max(othCell.count[i], dcell.count[i]);
							}
							othCell.updateTick = std::max(othCell.updateTick, dcell.updateTick + dt);
							othCell.realUpdateTick = std::max(othCell.realUpdateTick, dcell.realUpdateTick);
						}
					}
				}
			}
		}
	}
}

bool Strat::anyEnemiesNearbyByDangerDistr(const Group &group)
{
	int x = group.center.x / DISTR_MAT_CELL_SIZE;
	int y = group.center.y / DISTR_MAT_CELL_SIZE;
	for (int yy = -7; yy <= 7; ++yy)
	{
		for (int xx = -7; xx <= 7; ++xx)
		{
			if (xx * xx + yy * yy <= 50)
			{
				int x2 = x + xx;
				int y2 = y + yy;
				if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
				{
					DangerDistCell &dCell = dangerDistCells[y2 * DISTR_MAT_CELLS_X + x2];
					if (dCell.totalEnemyDamage > 0.0 || dCell.totalMyDamage > 0.0)
					{
						return true;
					}
				}
			}
		}
	}
	
	return false;
}

void Strat::calcDangerDistCells(const Group &group)
{
	dngGr = &group;
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
			double totalMyDamage = 0;
			
			for (int myType = 0; myType < 5; ++myType)
			{
				double typeDamage = 0;
				if (group.sizeByTypes[myType])
				{
					for (int enemyType = 0; enemyType < 5; ++enemyType)
					{
						if (cell.count[enemyType])
						{
							double dmg = getDamage((UnitType) myType, (UnitType) enemyType);
							typeDamage = std::max(typeDamage, std::min(dmg * group.sizeByTypes[myType], 120.0 * cell.count[enemyType]));
						}
					}
				}
				totalMyDamage += typeDamage;
			}
			
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			double f2hDmg = 0.0;
			dCell.totalEnemyDamage = 0.0;
			dCell.totalEnemyHealth = 0.0;
			for (int enemyType = 0; enemyType < 5; ++enemyType)
			{
				dCell.enemyDamage[enemyType] = 0.0;
				dCell.enemyHealth[enemyType] = 0.0;
				
				if (cell.count[enemyType])
				{
					dCell.enemyHealth[enemyType] += cell.health[enemyType];
					for (int myType = 0; myType < 5; ++myType)
					{
						if (group.sizeByTypes[myType])
						{
							double dmg = getDamage((UnitType) enemyType, (UnitType) myType);
							dCell.enemyDamage[enemyType] = std::min(dmg * cell.count[enemyType], 120.0 * group.sizeByTypes[myType]);
							
							if (enemyType == (int) UnitType::FIGHTER && myType == (int) UnitType::HELICOPTER)
								f2hDmg = dCell.enemyDamage[enemyType];
						}
					}
				}
				
				dCell.enemyDamage[enemyType] *= 1.5;
				dCell.totalEnemyDamage += dCell.enemyDamage[enemyType];
				dCell.totalEnemyHealth += dCell.enemyHealth[enemyType];
			}
			
			dCell.totalMyDamage = totalMyDamage;
			dCell.f2hDmg = f2hDmg;
		}
	}
}

void Strat::calcVisibilityFactors()
{
	updateVisionRangeAndStealthFactor();
	
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		visibilityFactors[i] = 0.0;
	}
	
	for (MyUnit &u : units)
	{
		if (u.side == 0)
		{
			double range = u.visionRange;
			int minX = std::max(0.0, (u.pos.x - range)) / DISTR_MAT_CELL_SIZE;
			int maxX = std::min(WIDTH - 1.0, (u.pos.x + range)) / DISTR_MAT_CELL_SIZE;
			int minY = std::max(0.0, (u.pos.y - range)) / DISTR_MAT_CELL_SIZE;
			int maxY = std::min(HEIGHT - 1.0, (u.pos.y + range)) / DISTR_MAT_CELL_SIZE;
			double visionRange2 = sqr(u.visionRange);
			
			for (int y = minY; y <= maxY; ++y)
			{
				for (int x = minX; x <= maxX; ++x)
				{
					double &visFactor = visibilityFactors[y * DISTR_MAT_CELLS_X + x];
					if (visFactor == 1.0)
						continue;
					
					P center = P(x + 0.5, y + 0.5) * DISTR_MAT_CELL_SIZE;
					P nearPoint = center;
					P farPoint = center;
					if (center.x < u.pos.x)
					{
						nearPoint.x += DISTR_MAT_CELL_SIZE * 0.5;
						farPoint.x  -= DISTR_MAT_CELL_SIZE * 0.5;
					}
					else
					{
						nearPoint.x -= DISTR_MAT_CELL_SIZE * 0.5;
						farPoint.x  += DISTR_MAT_CELL_SIZE * 0.5;
					}
					
					if (center.y < u.pos.y)
					{
						nearPoint.y += DISTR_MAT_CELL_SIZE * 0.5;
						farPoint.y  -= DISTR_MAT_CELL_SIZE * 0.5;
					}
					else
					{
						nearPoint.y -= DISTR_MAT_CELL_SIZE * 0.5;
						farPoint.y  += DISTR_MAT_CELL_SIZE * 0.5;
					}
					
					if (nearPoint.dist2(u.pos) < visionRange2)
					{
						const Cell &cell = this->cell(x * DISTR_MAT_CELL_SIZE / CELL_SIZE, y * DISTR_MAT_CELL_SIZE / CELL_SIZE);
						double minStealth = 1.0;
						if (cell.groundType == GroundType::FOREST)
							minStealth = 0.6;
						else if (cell.weatherType == MyWeatherType::RAIN)
							minStealth = 0.6;
						else if (cell.weatherType == MyWeatherType::CLOUDY)
							minStealth = 0.8;
						
						double visibilityFactor = (range * minStealth) / farPoint.dist(u.pos);
						if (visibilityFactor > 1.0)
							visibilityFactor = 1.0;
						
						visFactor = std::max(visFactor, visibilityFactor);
					}
				}
			}
		}
	}
}

struct DangerDist {
	double dists[25];
	
	DangerDist() {
		dist(UnitType::ARV, UnitType::ARV) = 150.0;
		dist(UnitType::ARV, UnitType::FIGHTER) = 150.0;
		dist(UnitType::ARV, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::ARV, UnitType::IFV) = 150.0;
		dist(UnitType::ARV, UnitType::TANK) = 150.0;
		
		dist(UnitType::FIGHTER, UnitType::ARV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::IFV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::HELICOPTER, UnitType::ARV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::IFV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::IFV, UnitType::ARV) = 150.0;
		dist(UnitType::IFV, UnitType::FIGHTER) = 100.0;
		dist(UnitType::IFV, UnitType::HELICOPTER) = 100.0;
		dist(UnitType::IFV, UnitType::IFV) = 150.0;
		dist(UnitType::IFV, UnitType::TANK) = 150.0;
		
		dist(UnitType::TANK, UnitType::ARV) = 150.0;
		dist(UnitType::TANK, UnitType::FIGHTER) = 1.0;
		dist(UnitType::TANK, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::TANK, UnitType::IFV) = 150.0;
		dist(UnitType::TANK, UnitType::TANK) = 150.0;
		
		for (int i = 0; i < 25; ++i)
			dists[i] = sqr(dists[i]);
	}
	
	double &dist(UnitType from, UnitType to) {
		return dists[(int) from * 5 + (int) to];
	}
} DANGER_DISTS;


bool isClosedSpaceDanger(const P &myP, const P &enP, double myVel, double enemyVel, double ticks)
{
	double myR = myVel * ticks + 0.1;
	double dangerRad = 70.0;
	double enR = enemyVel * ticks + dangerRad;
	double d = myP.dist(enP);
	
	if (myR + d < enR)
		return true;
	
	const double borderDist = 40.0;
	if (myP.x > (borderDist + myR) && myP.x < (WIDTH - borderDist - myR) && myP.y > (borderDist + myR) && myP.y < (HEIGHT - borderDist - myR))
		return false;
	
	P myPn = myP;
	P enPn = enP;
	if (myPn.x > WIDTH / 2.0)
	{
		myPn.x = WIDTH - myPn.x;
		enPn.x = WIDTH - enPn.x;
	}
	
	if (myPn.y > HEIGHT / 2.0)
	{
		myPn.y = HEIGHT - myPn.y;
		enPn.y = HEIGHT - enPn.y;
	}
	
	if (myPn.x < myPn.y)
	{
		std::swap(myPn.x, myPn.y);
		std::swap(enPn.x, enPn.y);
	}
	
	double borderDistX = std::min(borderDist, myPn.x);
	double borderDistY = std::min(borderDist, myPn.y);
	
	double b = sqrt(sqr(myR) - sqr(myPn.y - borderDistY));
	double X = myPn.x + b;
	
	if (P(X, borderDistY).dist2(enPn) > sqr(enR))
		return false;
	
	if (myR > (myPn.x - borderDistX))
	{
		double Y = myPn.y + sqrt(sqr(myR) - sqr(myPn.x - borderDistX));
		
		if (P(borderDistX, Y).dist2(enPn) > sqr(enR))
			return false;
	}
	else
	{
		X = myPn.x - b;
	
		if (P(X, borderDistY).dist2(enPn) > sqr(enR))
			return false;
	}
	
	return true;
}

double captureTick(const P &myP, const P &enP, double myVel, double enemyVel)
{
	double dT = 125.0;
	double oldT = 0.0;
	for (double t = 0.0; t <= 625;)
	{
		if (!isClosedSpaceDanger(myP, enP, myVel, enemyVel, t))
		{
			oldT = t;
			t += dT;
		}
		else
		{
			if (dT <= 1.0)
				return t;
			
			t = oldT;
			dT /= 5.0;
			t += dT;
		}
	}
	
	return 625;
}

double Strat::attractionPoint(const P from, const Group &group, double ticks, bool angryMode)
{
	double res = 0.0;
	double f2hDmgRes = 0.0;
	bool spy = group.unitType == UnitType::FIGHTER;
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DangerDistCell &dCell = dangerDistCells[y * DISTR_MAT_CELLS_X + x];
			
			bool anyDamage = dCell.totalMyDamage > 0.0 || dCell.totalEnemyDamage > 0.0;
			
			if (anyDamage || (spy && dCell.totalEnemyHealth > 0))
			{
				double alpha = 0.3;
				double alphaM1 = 0.7;
				
				P p = P((x + 0.5) * DISTR_MAT_CELL_SIZE, (y + 0.5) * DISTR_MAT_CELL_SIZE);
				
				double dist2 = p.dist2(from);
				
				double pts = 0.0;
				
				if (anyDamage)
				{
					pts = (group.health * alphaM1 + dCell.totalEnemyHealth * alpha) / (dCell.totalEnemyHealth*0.01 + dCell.totalEnemyDamage) 
						- (dCell.totalEnemyHealth * alphaM1 + group.health * alpha) / (group.health * 0.01 + dCell.totalMyDamage);
				}
				else
				{
					if (dist2 > sqr(50))
						pts = 0.1;
				}
						
				
				/*if (enableFOW)
				{
					const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
					double dt = tick - cell.realUpdateTick;
		
					if (dt > 10)
					{
						pts /= sqr(dt / 10.0);
					}
				}*/
				
				pts *= (1.0 + dCell.totalEnemyHealth*0.0003);
				
				if (pts != 0.0)
				{
					double enemyVel = 0.0;
					
					for (int i = 0; i < 5; ++i) 
					{ 
						if (dCell.enemyHealth[i]) 
							enemyVel += unitVel((UnitType) i) * (dCell.enemyHealth[i] / dCell.totalEnemyHealth); 
					}
					
					if (pts < 0.0)
					{
						double t = captureTick(from, p, unitVel(group.unitType), enemyVel);
						res += pts * (625 - t) / 625.0;
					}
					else
					{
						/*double t = captureTick(p, from, enemyVel, unitVel(group.unitType));
						res += 0.1* pts * (625 - t) / 625.0;*/
					}
				}
				
				if (pts > 0.0)
				{
					double pp = 1.0/(1.0 + dist2);
					res += pts * pp;
				}
				else
				{
					/*for (int mt = 0; mt < 5; ++mt)
					{
						if (group.healthByTypes[mt])
						{
							for (int et = 0; et < 5; ++et)
							{
								if (dCell.enemyHealth[et])
								{
									double rad2 = DANGER_DISTS.dist((UnitType) et, (UnitType) mt);
									//double rad2 = 150*150;
									if (rad2 > dist2)
									{
										double fraction = group.healthByTypes[mt] / group.health * dCell.enemyHealth[et] / dCell.totalEnemyHealth;
										double pn = (1.0 - std::min(1.0, dist2/rad2));
										res += pts * pn * fraction;
									}
								}
							}
						}
					}*/
					
					double pn = (1.0 - std::min(1.0, dist2/sqr(150)));
					res += pts * pn;
				}
				
				//res += pts / p.dist2(from);
			}
			
			f2hDmgRes += dCell.f2hDmg;
		}
	}
	
	if (group.unitType == UnitType::FIGHTER)
	{
		double L = 1.5 * WIDTH;
		if (group.size * 80 > group.health)
		{
			Group *arvG = getGroup(UnitType::ARV);
			if (arvG && arvG->size > 20)
			{
				L = arvG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::ARV] * (80 - group.health/group.size)*0.2;
	}
	
	/*if (spy)
	{
		for (Group &g : groups)
		{
			if (&g != &group)
			{
				double dist2 = from.dist2(g.center);
				res -= 0.05 / (1.0 + dist2);
			}
		}
		
		if (enableFOW)
		{
			for (Building &b : buildings)
			{
				if (b.side != 0)
				{
					double dist2 = from.dist2(b.pos);
					double pp = 1.0/(1.0 + dist2);
					res += pp * 0.1;
				}
			}
		}
	}*/
	
	/*if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		double L = 1.5 * WIDTH;
		if (f2hDmgRes > 0.0 && res < 0.0)
		{
			if (fivG && fivG->size > 10)
			{
				L = fivG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::IFV];
		
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 300.0)
			{
				res -= (l - 300.0);
			}
		}
	}*/
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::HELICOPTER);
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 400.0)
			{
				res -= (l - 400.0);
			}
		}
	}*/
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::TANK);
		if (fivG) {
			double l = fivG->center.dist2(from);
			res += 0.0001 / (1.0 + l);
		}
	}
	
	if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		if (fivG) {
			double l = fivG->center.dist2(from);
			res += 0.0001 / (1.0 + l);
		}
	}*/
	
	if (isGroundUnit(group.unitType))
	{
		for (const Building &b : buildings)
		{
			if (b.side != 0)
			{
				double d = from.dist(b.pos);
				double coef = (b.assignedGroup == 0 || b.assignedGroup == group.internalId) ? 2.0 : 1.0;
				res += coef*group.health/(20 + d)*0.1;
			}
		}
	}
	
	if (group.attractedToGroup >= 0)
	{
		Group &othG = groups[group.attractedToGroup];
		double d = from.dist(othG.center);
		res += group.health/(20 + d)*0.3;
	}
	
	return res;
}


ShrinkResult Strat::findShrink(Group &group)
{
	ShrinkResult result;
	
	Simulator sim;
	sim.tick = tick;
	std::copy(cells, cells + CELLS_X * CELLS_Y, sim.cells);
	
	BBox gbox = group.bbox;
	gbox.expand(2.0);
	for (const MyUnit &u : units)
	{
		if (gbox.inside(u.pos))
		{
			sim.units.push_back(u);
			MyUnit &tu = *sim.units.rbegin();
			tu.selected = group.check(tu);
			tu.vel = P(0, 0);
		}
	}
	sim.groups.push_back(group);
	
	double bestArea = WIDTH * HEIGHT;
	for (double x = -1; x <= 1; ++x)
	{
		for (double y = -1; y <= 1; ++y)
		{
			Simulator sim2 = sim;
			MyMove myMove;
			myMove.action = MyActionType::SCALE;
			myMove.p = group.center + P(x, y) * 10.0;
			myMove.factor = 0.2;
			sim2.registerMove(myMove, 0);
			
			int i = 0;
			for (; i < 20; ++i)
			{
				sim2.tick++;
				sim2.resetAxisSorts();
				sim2.applyMoves();
				int cnt = sim2.moveUnits();
				if (!cnt)
					break;
			}
			sim2.updateStats();
			
			double area = sim2.groups.rbegin()->bbox.area();
			if (area < bestArea)
			{
				bestArea = area;
				result.shrinkPoint = myMove.p;
				result.ticks = i;
				result.endBBox = sim2.groups.rbegin()->bbox;
			}
		}
	}
	
	return result;
}
}

namespace StratV23 {
	double groupPriority(const Group &g) {
	double res = 0.0;
	
	if (g.shrinkAfterNuke)
		res += 1100.0;
	else if (g.actionStarted)
		res += 1000.0;
	
	res -= g.lastUpdateTick;
	
	return res;
}

void DistributionMatrix::clear()
{
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		Cell &cell = cells[i];
		for (int j = 0; j < 5; ++j)
			cell.count[j] = 0;
		for (int j = 0; j < 5; ++j)
			cell.health[j] = 0;
	}
}

void DistributionMatrix::initialize(const Simulator &sim, bool firstTick)
{
	clear();
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : sim.units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / DISTR_MAT_CELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / DISTR_MAT_CELL_SIZE;
				
				Cell &cell = getCell(x, y);
				int type = (int) u.type;
				cell.count[type] += 1.0 / K;
				cell.health[type] += u.durability / K;
			}
		}
		
		/*if (sim.enableFOW && firstTick && u.side == 0)
		{
			P pos = P(WIDTH, HEIGHT) - u.pos;
			int x = pos.x / DISTR_MAT_CELL_SIZE;
			int y = pos.y / DISTR_MAT_CELL_SIZE;
			Cell &cell = getCell(x, y);
			int type = (int) u.type;
			cell.count[type] += 1.0;
			cell.health[type] += u.durability;
		}*/
	}
}

void DistributionMatrix::blur(DistributionMatrix &oth) const
{
	oth.clear();
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			Cell &resCell = oth.getCell(x, y);
			int cnt = 0;
			for (int yy = -1; yy <= 1; ++yy)
			{
				for (int xx = -1; xx <= 1; ++xx)
				{
					int x2 = x + xx;
					int y2 = y + yy;
					if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
					{
						++cnt;
						const Cell &cell = getCell(x2, y2);
						for (int i = 0; i < 5; ++i)
							resCell.count[i] += cell.count[i];
						for (int i = 0; i < 5; ++i)
							resCell.health[i] += cell.health[i];
					}
				}
			}
			
			const Cell &cell = getCell(x, y);
			for (int i = 0; i < 5; ++i)
			{
				if (!cell.count[i])
				{
					resCell.count[i] = 0;
					resCell.health[i] = 0;
				}
			}
			
			/*for (int i = 0; i < 5; ++i)
				resCell.count[i] += (resCell.count[i] + cnt - 1) / cnt;
			for (int i = 0; i < 5; ++i)
				resCell.health[i] /= cnt;*/
		}
	}
}

Strat::Strat::Strat()
{
	Group g;
	g.actionStarted = false;
	g.lastUpdateTick = 0;
	g.lastShrinkTick = 0;
	
	for (int i = 0; i < 1; ++i)
	{
		g.miniGroupInd = -1;
		g.unitType = UnitType::HELICOPTER;
		//g.miniGroupInd = i;
		g.internalId = internalGroupSeq++;
		groups.push_back(g);
	}
	
	g.miniGroupInd = -1;
	g.unitType = UnitType::IFV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	/*g.miniGroupInd = 1;
	g.unitType = UnitType::IFV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);*/
	
	g.miniGroupInd = -1;
	g.unitType = UnitType::FIGHTER;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.miniGroupInd = -1;
	g.unitType = UnitType::TANK;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
	
	g.miniGroupInd = -1;
	g.unitType = UnitType::ARV;
	g.internalId = internalGroupSeq++;
	groups.push_back(g);
}


void Strat::calcMicroShift(Group &group, P &shift)
{
	if (group.unitType != UnitType::TANK && group.unitType != UnitType::FIGHTER)
		return;
	
	BBox bbox = group.bbox;
	bbox.p1 += shift - P(35, 35);
	bbox.p2 += shift + P(35, 35);
	
	int finalScore = -100000;
	int currentScore = -100000;
	double ticks = 20.0;
	
	//if (group.unitType == UnitType::TANK)
	{
		std::vector<P> enemies[(int) UnitType::COUNT];
		std::vector<P> myUnits;
		
		for (const MyUnit &u : units)
		{
			if (u.side > 0 && (u.type == UnitType::TANK || u.type == UnitType::HELICOPTER || u.type == UnitType::IFV) && bbox.inside(u.pos))
			{
				enemies[(int) u.type].push_back(u.pos + u.vel * ticks);
			}
			else if (group.check(u))
			{
				myUnits.push_back(u.pos);
			}
		}
		
		double L = 8.0;
		P newShift = shift;
		for (int k = 0; k < 4; ++k)
		{
			P foundShift = newShift;
			int foundScore = -100000;
			for (int i = 0; i < 9; ++i)
			{
				P testShift = newShift + P(i / 3 - 1, i % 3 - 1) * L;
				int score = 0;
				bool isCurShift = shift.dist2(testShift) < 0.1;
				
				if (!isCurShift || currentScore == -100000)
				{
					const std::vector<MicroShiftValues> &pos = microShiftMatrix.pos[(int) group.unitType];
					if (!pos.empty())
					{
						for (const P &myP : myUnits)
						{
							P myPos = myP + testShift;
							bool found = false;
							for (const MicroShiftValues &vals : pos)
							{
								if (!found)
								{
									for (const P &enP : enemies[(int) vals.unitType])
									{
										if (myPos.dist2(enP) < vals.dist2)
										{
											score += vals.val;
											found = true;
											break;
										}
									}
								}
							}
						}
					}
					
					// !! decrease SCORE
					int posScore = score;
					score /= 2;
					
					const std::vector<MicroShiftValues> &neg = microShiftMatrix.neg[(int) group.unitType];
					if (!neg.empty())
					{
						for (const MicroShiftValues &vals : neg)
						{
							for (const P &enP : enemies[(int) vals.unitType])
							{
								for (const P &myP : myUnits)
								{
									P myPos = myP + testShift;
									if (myPos.dist2(enP) < vals.dist2)
									{
										score -= vals.val;
										break;
									}
								}
							}
						}
					}
					
					int negScore = posScore / 2 - score;
					
					if (foundScore < score)
					{
						foundShift = testShift;
						foundScore = score;
					}
					
					if (isCurShift)
						currentScore = score;
				}
			}
			
			L /= 2.0;
			newShift = foundShift;
			
			finalScore = foundScore;
		}
		
		if (currentScore >= finalScore)
			return;
		
		shift = newShift;
	}
}

void Strat::calcNuclearEfficiency()
{
	for (int i = 0; i < MICROCELLS_X * MICROCELLS_Y; i++)
		nuclearEfficiency[i] = 0;
	
	constexpr double K = 10;
	constexpr double STEP = 5;
	
	for (const MyUnit &u : units)
	{
		if (u.side == 1)
		{
			for (int i = 0; i < K; ++i)
			{
				P pos = u.pos + u.vel * i * STEP;
				
				int x = clamp(pos.x, 1, WIDTH - 1) / MICROCELL_SIZE;
				int y = clamp(pos.y, 1, HEIGHT - 1) / MICROCELL_SIZE;
				
				double efficiency = 100.0 / (10.0 + u.durability) / K;
				
				if (u.type == UnitType::ARV)
					efficiency /= 10.0;
				
				if (u.type == UnitType::FIGHTER || u.type == UnitType::HELICOPTER)
					efficiency /= 1.5;
				
				nuclearEfficiency[y * MICROCELLS_X + x] += efficiency;
			}
		}
	}
}

UnitType Strat::calcNextUnitTypeForConstruction(bool ground)
{
	/*double totalCount = 0;
	double assetCount[5] = {};
	for (int i = 0; i < 5; ++i)
	{
		totalCount += myCount[(UnitType) i];
		assetCount[i] = myCount[(UnitType) i];
	}
	
	double totalAssets = totalCount;
	for (const Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE)
		{
			int cnt = std::max(0, 40 - b.unitCount);
			totalAssets += cnt;
			assetCount[(int) b.unitType] += cnt;
		}
	}
	
	if (assetCount[(int) UnitType::TANK] > 300)
	{
		if (assetCount[(int) UnitType::FIGHTER] < totalAssets * 0.20)
		{
			LOG("CREATE FIGHTER");
			return UnitType::FIGHTER;
		}
		
		if (assetCount[(int) UnitType::HELICOPTER] < totalAssets * 0.20)
		{
			LOG("CREATE HELICOPTER");
			return UnitType::HELICOPTER;
		}
	}
	LOG("CREATE TANK");
	return UnitType::TANK;*/
	
	////////////
	
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType == UnitType::HELICOPTER)
		{
			return UnitType::TANK;
		}
	}
	
	if (myCount[UnitType::TANK] > 200 && enemyCount[UnitType::HELICOPTER] * 0.9 > myCount[UnitType::HELICOPTER])
	{
		return UnitType::HELICOPTER;
	}
	
	return UnitType::TANK;
	
	/*double score[5] = {};
	
	int enCnt = enemyCount[UnitType::HELICOPTER]*0.7 + enemyCount[UnitType::FIGHTER]*0.3;
	int myCnt = myCount[UnitType::FIGHTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::FIGHTER] += enCnt - myCnt;
	}
	score[(int) UnitType::FIGHTER] *= 0.6;
	
	enCnt = enemyCount[UnitType::TANK];
	myCnt = myCount[UnitType::HELICOPTER];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::HELICOPTER] += enCnt - myCnt;
	}
	
	score[(int) UnitType::HELICOPTER] *= 0.8;
	
	enCnt = enemyCount[UnitType::IFV];
	myCnt = myCount[UnitType::TANK];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::TANK] += enCnt - myCnt;
	}
	
	enCnt = enemyCount[UnitType::FIGHTER]*0.7 + enemyCount[UnitType::HELICOPTER]*0.3;
	myCnt = myCount[UnitType::IFV];
	if (enCnt > myCnt)
	{
		score[(int) UnitType::IFV] += enCnt - myCnt;
	}
	
	
	for (Building &b : buildings)
	{
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE)
		{
			score[(int) b.unitType] -= 20;
		}
	}
	
	double grCount = 0;
	double airCount = 0;
	for (int i = 0; i < 5; ++i)
	{
		if (isGroundUnit((UnitType) i) && i != (int) UnitType::ARV)
		{
			grCount += myCount[(UnitType) i];
		}
		else
		{
			airCount += myCount[(UnitType) i];
		}
	}
	
	double totalCount = grCount + airCount;
	if (totalCount > 0)
	{
		for (int i = 0; i < 5; ++i)
		{
			if (isGroundUnit((UnitType) i))
			{
				score[i] *= airCount;
			}
			else
			{
				score[i] *= grCount;
			}
		}
	}
	
	int res = 0;
	int resType = -1;
	for (int i = 0; i < 5; ++i)
	{
		if (score[i] > res)
		{
			res = score[i];
			resType = i;
		}
	}
	
	if (resType >= 0)
		return (UnitType) resType;
	
	return UnitType::TANK;*/
}

MyMove Strat::calcNextMove()
{
	MyMove result;
	result.action = MyActionType::NONE;
	
	int moves = getAvailableActions(12);
	if (moves < 1)
		return result;
	
	if (players[1].remainingNuclearStrikeCooldownTicks < 30)
	{
		if (moves < 3)
			return result;
	}
	
	updateStats();
	
	if (players[1].nextNuclearStrikeTick >= 0)
	{
		bool select = false;
		bool expand = false;
		BBox bbox;
		int evaidingGroups = 0;
		for (Group &g : groups)
		{
			if (g.nukeEvadeStep == 0)
			{
				BBox gbox = g.bbox;
				gbox.expand(40.0);
				if (gbox.inside(players[1].nuclearStrike))
				{
					bbox.add(g.bbox);
					g.nukeEvadeStep = 1;
					select = true;
					++evaidingGroups;
				}
			}
			else if (g.nukeEvadeStep == 1)
			{
				expand = true;
				g.nukeEvadeStep = 2;
				++evaidingGroups;
			}
			else
			{
				++evaidingGroups;
			}
		}
		
		if (select)
		{
			result.action = MyActionType::CLEAR_AND_SELECT;
			result.p1 = bbox.p1;
			result.p2 = bbox.p2;
			
			return result;
		}
		else if (expand)
		{
			result.action = MyActionType::SCALE;
			result.p = players[1].nuclearStrike;
			result.factor = 9.0;
			return result;
		}
		
		if (evaidingGroups && moves < 3)
			return result;
	}
	else
	{
		for (Group &g : groups)
		{
			if (tick - g.lastShrinkTick > 30)
				g.shrinkAfterNuke = false;
			if (g.nukeEvadeStep != 0)
			{
				g.shrinkAfterNuke = true;
				g.nukeEvadeStep = -1;
			}
		}
	}
	
	/*if (tick > 200) {
		int moves = getAvailableActions(4, 10);
		if (moves < 1) // throttle
			return result;
	}*/
	
	if (!initialGroupsGerationDone && !buildings.empty())
	{
		for (Group &g : groups)
		{
			/*if (g.unitType == UnitType::HELICOPTER && !g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					P allCenter = P(0, 0);
					int count = 0;
					for (const MyUnit &u : units)
					{
						if (u.side == 0 && u.type == g.unitType)
						{
							allCenter += u.pos;
							++count;
						}
					}
					
					allCenter /= count;
					
					if (g.miniGroupInd == 0)
					{
						result.p1 = P(0, 0);
						result.p2 = allCenter;
					}
					else if (g.miniGroupInd == 1)
					{
						result.p1 = P(allCenter.x, 0);
						result.p2 = P(WIDTH, allCenter.y);
					}
					else if (g.miniGroupInd == 2)
					{
						result.p1 = P(0, allCenter.y);
						result.p2 = P(allCenter.x, HEIGHT);
					}
					else if (g.miniGroupInd == 3)
					{
						result.p1 = allCenter;
						result.p2 = P(WIDTH, HEIGHT);
					}
					
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					g.enumGroupBuildStep++;
					return result;
				}
				else
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}*/
			
			if (!g.group)
			{
				if (g.enumGroupBuildStep == 0)
				{
					result.unitType = g.unitType;
					result.action = MyActionType::CLEAR_AND_SELECT;
					
					if (g.miniGroupInd > -1)
					{
						P allCenter = P(0, 0);
						int count = 0;
						for (const MyUnit &u : units)
						{
							if (u.side == 0 && u.type == g.unitType)
							{
								allCenter += u.pos;
								++count;
							}
						}

						allCenter /= count;

						if (g.miniGroupInd == 0)
						{
							result.p2 = P(allCenter.x, HEIGHT);
						}
						else
						{
							result.p1 = P(allCenter.x, 0);
						}
					}
					//result.p1 = g.center - P(30, 30); // TODO
					//result.p2 = g.center + P(30, 30);
					
					g.enumGroupBuildStep++;
					return result;
				}
				else if (g.enumGroupBuildStep == 1)
				{
					result.action = MyActionType::ASSIGN;
					g.enumGroupBuildStep++;
					result.group = groupSeq++;
					g.group = result.group;
					
					return result;
				}
			}
		}
		
		initialGroupsGerationDone = true;
		LOG("DONE");
	}
	
	for (Building &b : buildings)
	{
		/*if (b.lastChangeUnitCount > b.unitCount)
			b.lastChangeUnitCount = b.unitCount;*/
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && (b.unitType == UnitType::NONE/* || (b.unitCount - b.lastChangeUnitCount) > 11*/))
		{
			//LOG("SVP " << buildingCaptured);
			result.action = MyActionType::SETUP_VEHICLE_PRODUCTION;
			result.facilityId = b.id;
			
			/*if (buildingCaptured % 2 == 0)
				result.unitType = UnitType::FIGHTER;
			else
				result.unitType = UnitType::IFV;*/
			
			//result.unitType = UnitType::TANK;
			result.unitType = calcNextUnitTypeForConstruction(false);
			//result.unitType = (UnitType) (buildingCaptured % 5);
			//b.lastChangeUnitCount = b.unitCount;
			
			++buildingCaptured;
			return result;
		}
		
		if (b.side == 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE && b.unitCount > 50 && b.productionProgress < 30
			|| b.createGroupStep > 0
			//|| b.side != 0 && b.unitCount > 0 && b.type == BuildingType::VEHICLE_FACTORY && b.unitType != UnitType::NONE && !isGroundUnit(b.unitType)
		)
		{
			UnitType unitType = UnitType::NONE;
			int cnt = 0;
			for (int i = 0; i < 5; ++i)
			{
				if (cnt < b.unitCountByType[i])
				{
					cnt = b.unitCountByType[i];
					unitType = (UnitType) i;
				}
			}
			
			UnitType newUnitType = calcNextUnitTypeForConstruction(false);
			if (b.unitType != newUnitType && unitType != newUnitType)
			{
				result.action = MyActionType::SETUP_VEHICLE_PRODUCTION;
				result.facilityId = b.id;
				result.unitType = newUnitType;
				return result;
			}
			
			//LOG("MAKE GROUP");
			if (b.createGroupStep == 0 || b.createGroupStep == 1)
			{
				BBox bbox;
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && !p.groups.any() && b.checkPoint(p.pos))
					{
						bbox.add(p.pos);
					}
				}
				
				result.p1 = bbox.p1 - P(1, 1);
				result.p2 = bbox.p2 + P(1, 1);
				
				result.action = MyActionType::CLEAR_AND_SELECT;
				/*result.p1 = b.pos - P(32, 32); 
				result.p2 = b.pos + P(32, 32); */
				
				result.unitType = unitType;
				b.createGroupStep = 2;
				return result;
			}
			
			if (b.createGroupStep == 2)
			{
				// check if selected
				bool anySelected = false;
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.groups.count() == 0 && p.type == unitType)
					{
						if (p.pos.x > b.pos.x - 32 && p.pos.x < b.pos.x + 32 && p.pos.y > b.pos.y - 32 && p.pos.y < b.pos.y + 32)
						{
							if (p.selected)
							{
								anySelected = true;
								break;
							}
						}
					}
				}
				
				if (anySelected)
				{
					result.action = MyActionType::ASSIGN;
					result.group = groupSeq++;
					b.createGroupStep = 3;
					
					Group newGroup;
					newGroup.unitType = unitType;
					newGroup.group = result.group;
					newGroup.internalId = internalGroupSeq++;
					groups.push_back(newGroup);
					
					b.createGroupStep = 0;
					return result;
				}
				else
				{
					b.createGroupStep = 1;
				}
			}
		}
	}
	
		
	
	
	bool angryMode = angryModeTill > tick;

	
	DistributionMatrix matr;
	matr.initialize(*this, !distributionMatrixInitialized);
	
	//if (!enableFOW || !distributionMatrixInitialized)
	{
		matr.blur(distributionMatrix);
		distributionMatrixInitialized = true;
	}
	/*else
	{
		DistributionMatrix othMatr;
		matr.blur(othMatr);
		
		spreadDistributionMatrix();
		
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &dcell = distributionMatrix.getCell(x, y);
				DistributionMatrix::Cell &simDcell = othMatr.getCell(x, y);
				double visFactor = visibilityFactors[y * DISTR_MAT_CELLS_X + x];
				
				if (visFactor > 0.8)
				{
					dcell = simDcell;
					dcell.updateTick = tick;
					dcell.realUpdateTick = tick;
				}
				else
				{
					for (int i = 0; i < 5; ++i)
					{
						dcell.health[i] = std::max(dcell.health[i], simDcell.health[i]);
						dcell.count[i] = std::max(dcell.count[i], simDcell.count[i]);
					}
					
					if (visFactor > 0.5)
					{
						dcell.updateTick = tick;
						dcell.realUpdateTick = tick;
					}
				}
			}
		}
	}*/
	
	/*if (tick > 532)
	{
		std::cout.width(2);
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				DistributionMatrix::Cell &resCell = distributionMatrix.getCell(x, y);
				std::cout << " " << resCell.count[1];
			}
			std::cout << std::endl;
		}
		std::cout << std::endl;
	}*/
	
	if (players[0].remainingNuclearStrikeCooldownTicks == 0)
	{
		resetCells();
		calcNuclearEfficiency();
		
		auto getEnemySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0.0;
			
			return nuclearEfficiency[y * MICROCELLS_X + x];
		};
		
		auto getMySize = [this](int x, int y) {
			if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
				return 0;
			
			return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
				+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
				+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
				+ (int) getMicrocell(x, y, 0, UnitType::ARV).size() / 2;
		};
		
		P bestTarget;
		int bestScore = 0;
		
		for (int y = 0; y < MICROCELLS_Y; ++y) {
			for (int x = 0; x < MICROCELLS_X; ++x) {
				double enemyN = getEnemySize(x, y);
				if (enemyN > 10)
				{
					int totalMyCount = 0;
					int myCenterCount = 0;
					for (int xx = -1; xx <= 1; ++xx)
					{
						for (int yy = -1; yy <= 1; ++yy)
						{
							totalMyCount += getMySize(x + xx, y + yy);
							if (xx == 0 && yy == 0)
								myCenterCount = totalMyCount;
						}
					}
					
					if (totalMyCount < 20 && myCenterCount == 0)
					{
						int totalMyExtCount = totalMyCount;
						if (totalMyExtCount == 0)
						{
							for (int xx = -2; xx <= 2; ++xx)
							{
								for (int yy = -2; yy <= 2; ++yy)
								{
									if (xx < -1 || xx > 1 || yy < -1 || yy > 1)
										totalMyExtCount += getMySize(x + xx, y + yy);
								}
							}
						}
						
						if (totalMyExtCount > 0)
						{
							double totalEnemyCount =
								enemyN +
								getEnemySize(x - 1, y)/2 +
								getEnemySize(x + 1, y)/2 + 
								getEnemySize(x, y - 1)/2 + 
								getEnemySize(x, y + 1)/2;
							
							if (bestScore < totalEnemyCount)
							{
								bestScore = totalEnemyCount;
								bestTarget = P(x + 0.5, y + 0.5) * MICROCELL_SIZE;
							}
						}
					}
				}
			}
		}
		
		if (bestScore > 0)
		{
			// Correct point
			P correctedBestTarget = bestTarget;
			
			std::vector<P> myVehicles;
			std::vector<P> enemyVehicles;
			int myInnerN = 0;
			int enemyInnerN = 0;
			for (const MyUnit &p : units)
			{
				double d2 = p.pos.dist2(bestTarget);
				if (d2 < (75.0*75.0))
				{
					if (d2 < (38.0*38.0))
					{
						if (p.side == 0)
							myInnerN++;
						else
							enemyInnerN++;
					}
					else
					{
						if (p.side == 0)
							myVehicles.push_back(p.pos);
						else
							enemyVehicles.push_back(p.pos);
					}
				}
			}
			
			int foundScore = -100000;
			for (int xx = -2; xx <= 2; ++xx)
			{
				for (int yy = -2; yy <= 2; ++yy)
				{
					P newTarget = bestTarget + P(xx, yy) * 6.0;
					
					int myNum = myInnerN*3;
					int myExtNum = 0;
					
					for (const P &p : myVehicles)
					{
						double d2 = p.dist2(newTarget);
						if (d2 < (50.0*50.0))
						{
							myNum++;
						}
						else if (d2 > (55.0*55.0) && d2 < (70.0*70.0))
						{
							myExtNum++;
						}
					}
					
					if (myExtNum > 0)
					{
						int enNum = enemyInnerN*3;
						for (const P &p : enemyVehicles)
						{
							double d2 = p.dist2(newTarget);
							if (d2 < (50.0*50.0))
							{
								enNum++;
							}
						}
						
						int score = enNum - myNum * 4;
						if (score > foundScore)
						{
							foundScore = score;
							correctedBestTarget = newTarget;
						}
					}
				}
			}
			
			if (foundScore > -100000)
			{
				auto getMySize = [this](int x, int y) {
					if (x < 0 || y < 0 || x > (MICROCELLS_X - 1) || y > (MICROCELLS_Y - 1))
						return 0;
					
					return (int) getMicrocell(x, y, 0, UnitType::FIGHTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::HELICOPTER).size()
						+ (int) getMicrocell(x, y, 0, UnitType::IFV).size()
						+ (int) getMicrocell(x, y, 0, UnitType::TANK).size()
						+ (int) getMicrocell(x, y, 0, UnitType::ARV).size();
				};
				
				int pts = -1000000;
				long bestId = -1;
				P vehPos;
				
				for (const MyUnit &p : units)
				{
					if (p.side == 0 && p.durability > 70)
					{
						double dist2 = p.pos.dist2(correctedBestTarget);
						if (dist2 > 55*55 && dist2 < 70*70)
						{
							bool outOfRange = false;
							
							double vel2 = p.vel.len2();
							
							MyUnit u = p;
							for (int i = 0; i < 30; ++i)
							{
								double visRange = getVisionRange(u) - unitVel(p.type) * 10;
								if (u.pos.dist2(correctedBestTarget) > sqr(visRange))
								{
									outOfRange = true;
									break;
								}
								
								if (vel2 < 0.01)
									break;
								u.pos += u.vel;
							}
							
							if (outOfRange)
								continue;
							
							int xx = p.pos.x / MICROCELL_SIZE;
							int yy = p.pos.y / MICROCELL_SIZE;
							
							//int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							int newPts = p.durability * 100 + getMySize(xx, yy) - std::abs(sqrt(dist2) - 67.0);
							if (pts < newPts)
							{
								pts = newPts;
								bestId = p.id;
								vehPos = p.pos;
							}
						}
					}
				}
				
				if (bestId >= 0)
				{
					result.action = MyActionType::TACTICAL_NUCLEAR_STRIKE;
					result.vehicleId = bestId;
					result.p = correctedBestTarget;
					return result;
				}
			}
		}
	}
	
	groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group &g){return g.size == 0;}), groups.end());
	
	if (false && enableFOW)
	{
		bool anyActiveAction = false;
		bool extractUnit = true;
		
		for (Group &g : groups)
		{
			if (g.actionStarted)
				anyActiveAction = true;
			
			if (g.unitInd >= 0)
			{
				extractUnit = false;
				break;
			}
		}
		
		if (extractUnit && !anyActiveAction && getGroup(UnitType::FIGHTER))
		{
			extractFighterStep = 0;
			extractFighterTarget = P(WIDTH / 2.0, HEIGHT / 2.0);
		}
	}
	
	if (extractFighterStep >= 0)
	{
		if (extractFighterStep == 0)
		{
			double dist2 = 1e8;
			const MyUnit *found = nullptr;
			for (const MyUnit &u : units)
			{
				if (u.side == 0 && u.type == UnitType::FIGHTER)
				{
					double d2 = u.pos.dist2(extractFighterTarget);
					if (d2 < dist2)
					{
						dist2 = d2;
						found = &u;
					}
				}
			}
			
			if (found)
			{
				result.action = MyActionType::CLEAR_AND_SELECT;
				result.p1 = found->pos - P(2, 2);
				result.p2 = found->pos + P(2, 2);
				result.unitType = UnitType::FIGHTER;
				extractFighterStep = 2;
				
				Group newGroup;
				newGroup.unitType = UnitType::FIGHTER;
				newGroup.unitId = found->id;
				newGroup.internalId = internalGroupSeq++;
				groups.push_back(newGroup);
				
				LOG("EXTRACT FIGHTER");
					
				return result;
			}
			else
			{
				extractFighterStep = -1;
			}
		}
		/*else if (extractFighterStep == 1)
		{
			if (selectCount != 1)
			{
				extractFighterStep = 0;
			}
			else
			{
				const MyUnit *found = nullptr;
				for (const MyUnit &u : units)
				{
					if (u.side == 0 && u.type == UnitType::FIGHTER && u.selected)
					{
						found = &u;
						break;
					}
				}
				
				if (!found)
				{
					extractFighterStep = 0;
				}
				else
				{
					result.action = MyActionType::MOVE;
					result.p = extractFighterTarget;
					extractFighterStep = 2;
					
					
					
					return result;
				}
			}
		}*/
		else if (extractFighterStep == 2)
		{
			for (Group &g : groups)
			{
				if (g.unitInd >= 0)
				{
					MyUnit &u = units[g.unitInd];
					if (u.groups.any())
					{
						if (u.selected)
						{
							for (int i = 1; i < 100; ++i)
							{
								if (u.hasGroup(i))
								{
									LOG("DISMISS FIGHTER FROM GROUP " << i);
									result.action = MyActionType::DISMISS;
									result.group = i;
									return result;
								}
							}
						}
						else
						{
							result.action = MyActionType::CLEAR_AND_SELECT;
							result.p1 = u.pos - P(2, 2);
							result.p2 = u.pos + P(2, 2);
							result.unitType = UnitType::FIGHTER;
							return result;
						}
					}
				}
			}
			
			extractFighterStep = -1;
		}
	}
	
	if (result.action == MyActionType::NONE && !groups.empty())
	{
		debugAttractionPoints.clear();
		
		std::sort(groups.begin(), groups.end(), [](const Group &g1, const Group &g2){
			double p1 = groupPriority(g1);
			double p2 = groupPriority(g2);
			return p2 < p1;
		});
		
		assignBuildings();
		updateGroupAttraction();
		
		if (enableFOW)
			calcVisibilityFactors();
		
		for (auto groupIt = groups.begin(); groupIt != groups.end(); ++groupIt)
		{
			Group &group = *groupIt;
			
			if (tick - group.lastComputeTick < 10 || group.nukeEvadeStep > 0)
				continue;
			
			if (group.shrinkActive)
			{
				if (tick - group.lastShrinkTick > 40 || !anyMoved(group))
				{
					group.shrinkActive = false;
				}
				else
				{
					continue;
				}
			}
			
			if (group.attractedToGroup >= 0)
			{
				Group &othG = groups[group.attractedToGroup];
				double dist2 = othG.center.dist2(group.center);
				if (dist2 < sqr(40))
				{
					if (!isSelected(group))
					{
						result = select(group);
						group.actionStarted = true;
						return result;
					}
					
					result.action = MyActionType::ASSIGN;
					result.group = othG.group;
					group.group = 0;
					group.unitType = UnitType::NONE;
					
					groups.erase(groupIt);
					//LOG("JOIN GROUPS ASSIGN " << (int) othG.group);
					return result;
				}
			}
			
			bool limitSpeed = false;
			if (nukeVehicleInd >= 0)
			{
				const MyUnit &u = units[nukeVehicleInd];
				if (group.check(u))
					limitSpeed = true;
			}
			
			dngGr = &group;
			calcDangerDistCells();
			
			P center = group.center;
			const BBox &bbox = group.bbox;
			int groupSize = group.size;
			double area = bbox.area();
			bool shrinkRequired = (area > groupSize * 40.0 || group.shrinkAfterNuke) && (((tick - group.lastUpdateTick) > 60 || group.shrinkAfterNuke) && (tick - group.lastShrinkTick) > 300);
			P shrinkPoint;
			if (shrinkRequired)
			{
				ShrinkResult shRes = findShrink(group);
				if (shRes.ticks > 7)
				{
					shrinkPoint = shRes.shrinkPoint;
				}
				else
				{
					shrinkRequired = false;
				}
				//LOG("SHRINK RES " << shRes.ticks << " " << shRes.endBBox << " P " << shRes.shrinkPoint);
			}
			//bool shrinkRequired = false;
			
			const double border = 20.0;
			P c = center;
			double R = 20 + unitVel(group.unitType) * 40;
			/*if (group.unitType == UnitType::ARV)
				R = 200.0;*/
			double ticks = R / unitVel(group.unitType);
			double curVal = attractionPoint(center, group, ticks, angryMode);
			P tp = center;
			bool found = false;
			
			bool canMoveFlag = false;
			
			P clampP1 = group.center - bbox.p1 + P(3.0, 3.0);
			P clampP2 = P(WIDTH - 3.0, HEIGHT - 3.0) + (group.center - bbox.p2);
			for (int k = 0; k < 3.0; ++k)
			{
				ticks = R / unitVel(group.unitType);
				int di = -1;
				
				for (int i = 0; i < 20; ++i)
				{
					P p = c + P(PI * 2.0 / 20.0 * i) * R;
					p = clampP(p, clampP1, clampP2);
					
					P shift = p - center;
					if (shift.len2() > 0.01)
					{
						if (canMove(p - center, group))
						{
							canMoveFlag = true;
							double val = attractionPoint(p, group, ticks, angryMode);
							
							DebugAttractionPointsInfo debugInfo;
							debugInfo.point = c;
							debugInfo.dir = p - c;
							debugInfo.val = val;
							debugAttractionPoints.push_back(debugInfo);
							
							if (val > curVal)
							{
								curVal = val;
								di = i;
								tp = p;
								found = true;
							}
						}
					}
				}
				
				if (di >= 0)
				{
					c = tp;
				}
				
				R /= 1.5;
			}
			
			if (!canMoveFlag)
			{
				double R = 20 + unitVel(group.unitType) * 40;
				
				std::vector<const MyUnit *> groupUnits;
				std::vector<const MyUnit *> otherUnits;
				
				BBox bbox = group.bbox;
				bbox.p1 -= P(2.0*R, 2.0*R);
				bbox.p2 += P(2.0*R, 2.0*R);
				
				for (const MyUnit &u : units)
				{
					if (group.check(u))
					{
						groupUnits.push_back(&u);
					}
					else if (group.canIntersectWith(u) && bbox.inside(u.pos))
					{
						otherUnits.push_back(&u);
					}
				}
				
				for (int k = 0; k < 3.0; ++k)
				{
					ticks = R / unitVel(group.unitType);
					int di = -1;
					
					for (int i = 0; i < 19; ++i)
					{
						P p = c + P(PI * 2.0 / 19.0 * i) * R;
						p = clampP(p, clampP1, clampP2);
						
						P shift = p - center;
						if (shift.len2() > 0.01)
						{
							if (canMoveDetailed(p - center, group, groupUnits, otherUnits))
							{
								canMoveFlag = true;
								double val = attractionPoint(p, group, ticks, angryMode);
								
								DebugAttractionPointsInfo debugInfo;
								debugInfo.point = c;
								debugInfo.dir = p - c;
								debugInfo.val = val;
								debugAttractionPoints.push_back(debugInfo);
								
								if (val > curVal)
								{
									curVal = val;
									di = i;
									tp = p;
									found = true;
								}
							}
						}
					}
					
					if (di >= 0)
					{
						c = tp;
					}
					
					R /= 1.5;
				}
				
				/*if (!canMoveFlag)
				{
					UnitType typeToSelect = UnitType::NONE;
					bool selected = false;
					GroupId otherGr = 0;
					for (const MyUnit *o : otherUnits)
					{
						if (group.bbox.inside(o->pos) && o->side == 0)
						{
							typeToSelect = o->type;
							if (o->selected)
								selected = true;
							
							if (o->groups.any())
							{
								for (Group &oth : groups)
								{
									if (oth.group != group.group && o->hasGroup(oth.group))
									{
										otherGr = oth.group;
										break;
									}
								}
								if (otherGr > 0)
									break;
							}
						}
					}
					
					if (typeToSelect != UnitType::NONE)
					{
						if (!selected)
						{
							result.p1 = group.bbox.p1;
							result.p2 = group.bbox.p2;
							result.unitType = typeToSelect;
							result.action = MyActionType::CLEAR_AND_SELECT;
							
							group.actionStarted = true;
							
							LOG("SELECT TO " << (int) group.group << " " << group.center);
							return result;
						}
						
						if (otherGr > 0)
						{
							result.action = MyActionType::DISMISS;
							result.group = otherGr;
							LOG("DISMISS TO " << (int) otherGr << " " << group.center);
							return result;
						}
						else
						{
							result.action = MyActionType::ASSIGN;
							result.group = group.group;
							LOG("ASSIGN TO " << (int) group.group << " " << group.center);
							return result;
						}
					}
				}*/
			}
			
			bool moveAway = false;
			P dirAway;
			P newShift = P(0, 0);
			if (!canMoveFlag)
			{
				//LOG("CANT MOVE " << group.center);
				group.canMove = false;
				
				dirAway = P(0, 0);
				for (Group &othGroup : groups)
				{
					if (&group != &othGroup && isGroundUnit(group.unitType) == isGroundUnit(othGroup.unitType))
					{
						P dp = group.center - othGroup.center;
						double clen = dp.len();
						
						if (clen < 150.0)
							dirAway += dp / clen;
					}
				}
				
				dirAway *= 10;
				dirAway += P(tick % 7 - 3, tick % 9 - 5);
				moveAway = true;
			}
			else
			{
				group.canMove = true;
				
				if (found)
					newShift = tp - center;
				
				if (newShift.len() < 15.0)
				{
					calcMicroShift(group, newShift);
				}
			}
			
			found = newShift.len2() > 0.1;
			
			if (found || shrinkRequired || moveAway)
			{
				if (!isSelected(group))
				{
					result = select(group);
					group.actionStarted = true;
				}
				else
				{
					if (shrinkRequired)
					{
						result.action = MyActionType::SCALE;
						result.factor = 0.2;
						result.p = shrinkPoint;
						group.lastShrinkTick = tick;
						group.shrinkActive = true;
						group.nukeEvadeStep = 0;
					}
					else if (found)
					{
						result.action = MyActionType::MOVE;
						result.p = newShift;
						group.shift = newShift.norm();
						
						if (limitSpeed)
							result.maxSpeed = unitVel(group.unitType) * 0.6;
						
						/*if (!limitSpeed)
						{
							limitSpeed = !anyEnemiesNearbyByDangerDistr(group);
							if (limitSpeed)
								result.maxSpeed = unitVel(group.unitType) * 0.8;
						}*/
						
						/*if (group.unitType == UnitType::FIGHTER)
							result.maxSpeed = 1.0;*/
					}
					else
					{
						result.action = MyActionType::MOVE;
						result.p = dirAway;
						group.shift = dirAway.norm();
						//std::cout << "AWAY " << dirAway.x << " " << dirAway.y << std::endl;
					}
					
					group.lastUpdateTick = tick;
					group.lastComputeTick = tick;
					group.actionStarted = false;
				}
				
				break;
			}
			else if (!found)
			{
				group.lastComputeTick = tick;
				group.lastUpdateTick = tick; // TODO ???
			}
		}
	}
	
	return result;
}

void Strat::assignBuildings()
{
	std::vector<Building *> pbuildings;
	
	for (Building &b : buildings)
	{
		if (b.side != 0)
		{
			pbuildings.push_back(&b);
		}
		b.assignedGroup = 0;
	}
	
	std::vector<Group *> pgroups;
	for (Group &g : groups)
	{
		if (isGroundUnit(g.unitType))
		{
			pgroups.push_back(&g);
		}
		g.hasAssignedBuilding = false;
	}
	
	std::set<std::pair<size_t, size_t>> pairs;
	for (size_t i = 0; i < pbuildings.size(); ++i)
	{
		for (size_t j = 0; j < pgroups.size(); ++j)
		{
			pairs.insert(std::make_pair(i, j));
		}
	}
	
	std::map<size_t, size_t> gcount;
	std::map<size_t, size_t> bcount;
	std::set<size_t> groupsInd;
	std::set<size_t> buildingsInd;
	
	for (size_t i = 0; i < pbuildings.size(); ++i)
	{
		bcount[i] = pgroups.size();
		buildingsInd.insert(i);
	}
	
	for (size_t i = 0; i < pgroups.size(); ++i)
	{
		gcount[i] = pbuildings.size();
		groupsInd.insert(i);
	}
	
	auto getLen = [&pbuildings, &pgroups](const std::pair<size_t, size_t> &p)
	{
		Building *b = pbuildings[p.first];
		Group *g = pgroups[p.second];
		
		double len = b->pos.dist(g->center);
		if (g->unitType == UnitType::ARV)
			len *= 1.5;
		return len;
	};
	
	while(1)
	{
		auto found = pairs.end();
		double len = 0.0;
		for (auto it = pairs.begin(); it != pairs.end(); ++it)
		{
			if (buildingsInd.size() <= groupsInd.size())
			{
				if (bcount[it->first] <= 1)
					continue;
			}
			
			if (groupsInd.size() <= buildingsInd.size())
			{
				if (gcount[it->second] <= 1)
					continue;
			}
			
			double l = getLen(*it);
			if (l > len)
			{
				len = l;
				found = it;
			}
		}
		
		if (found != pairs.end())
		{
			size_t bc = --bcount[found->first];
			size_t gc = --gcount[found->second];
			if (bc == 0)
				buildingsInd.erase(found->first);
			if (gc == 0)
				buildingsInd.erase(found->second);
			
			pairs.erase(found);
		}
		else
		{
			break;
		}
	}
	
	for (auto &p : pairs)
	{
		Building *b = pbuildings[p.first];
		Group *g = pgroups[p.second];
		b->assignedGroup = g->internalId;
		g->hasAssignedBuilding = true;
	}
	
	/*size_t count = std::min(pbuildings.size(), pgroups.size());
	for (int i = 0; i < count; ++i)
	{
		double dist2 = sqr(100000.0);
		std::set<Building *>::iterator b = pbuildings.end();
		std::set<Group *>::iterator g = pgroups.end();
		for (std::set<Building *>::iterator bit = pbuildings.begin(); bit != pbuildings.end(); ++bit)
		{
			for (std::set<Group *>::iterator git = pgroups.begin(); git != pgroups.end(); ++git)
			{
				double d2 = (*bit)->pos.dist2((*git)->center);
				if (d2 < dist2)
				{
					dist2 = d2;
					b = bit;
					g = git;
				}
			}
		}
		
		if (b != pbuildings.end() && g != pgroups.end())
		{
			(*b)->assignedGroup = (*g)->internalId;
			pbuildings.erase(b);
			pgroups.erase(g);
		}
	}*/
}

void Strat::updateGroupAttraction()
{
	if (buildings.empty())
		return;
	
	std::vector<Group *> grps[2];
	
	int grCount[5] = {};
	for (Group &g : groups)
	{
		g.attractedToGroup = -1;
		
		if (g.unitType != UnitType::NONE)
		{
			if (isGroundUnit(g.unitType))
				grps[0].push_back(&g);
			else
				grps[1].push_back(&g);
			
			grCount[(int) g.unitType]++;
		}
	}
	
	constexpr int MAX_GROUPS = 5;
	for (int i = 0; i < 2; ++i)
	{
		if (grps[i].size() <= MAX_GROUPS)
			continue;
		
		/*std::sort(grps[i].begin(), grps[i].end(), [](const Group *g1, const Group *g2) {
			return g1->health < g2->health;
		});*/
	
		double pts = 1e10;
		Group *bestG = nullptr;
		for (Group *g : grps[i])
		{
			if (grCount[(int) g->unitType] > 1)
			{
				double minDist2 = 1e8;
				int targetK = -1;
				
				for (int k = 0; k < groups.size(); ++k)
				{
					Group &othG = groups[k];
					
					if (&othG != g && othG.unitType == g->unitType)
					{
						double dist2 = othG.center.dist2(g->center);
						
						if (dist2 < minDist2)
						{
							minDist2 = dist2;
							targetK = k;
						}
					}
				}
				
				if (targetK >= 0 && minDist2 < sqr(300))
				{
					g->attractedToGroup = targetK;
					double curPts = sqrt(minDist2)*5 + g->health;
					if (curPts < pts)
					{
						pts = curPts;
						bestG = g;
					}
				}
			}
		}
		
		for (Group *g : grps[i])
		{
			if (g != bestG)
				g->attractedToGroup = -1;
		}
	}
}

void Strat::spreadDistributionMatrix()
{
	double horTicks[5];
	double diagTicks[5];
	int xx[8] = {-1, 1,  0,  0,  -1, -1, 1,  1};
	int yy[8] = { 0, 0, -1, -1,  -1,  1, 1, -1};
	for (int i = 0; i < 5; ++i)
	{
		double vel = unitVel((UnitType) i);
		horTicks[i] = DISTR_MAT_CELL_SIZE / vel;
		diagTicks[i] = horTicks[i] * 1.4;
	}
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DistributionMatrix::Cell &dcell = distributionMatrix.getCell(x, y);
			
			for (int i = 0; i < 5; ++i)
			{
				for (int j = 0; j < 8; ++j)
				{
					double dt = j < 4 ? horTicks[i] : diagTicks[i];
					if (dcell.updateTick + dt < tick)
					{
						int x2 = x + xx[j];
						int y2 = y + yy[j];
						if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
						{
							DistributionMatrix::Cell &othCell = distributionMatrix.getCell(x2, y2);
							double visFactor = visibilityFactors[y2 * DISTR_MAT_CELLS_X + x2];
							if (visFactor < 0.7)
							{
								othCell.health[i] = std::max(othCell.health[i], dcell.health[i]);
								othCell.count[i] = std::max(othCell.count[i], dcell.count[i]);
							}
							othCell.updateTick = std::max(othCell.updateTick, dcell.updateTick + dt);
							othCell.realUpdateTick = std::max(othCell.realUpdateTick, dcell.realUpdateTick);
						}
					}
				}
			}
		}
	}
}

bool Strat::anyEnemiesNearbyByDangerDistr(const Group &group)
{
	/*int x = group.center.x / DISTR_MAT_CELL_SIZE;
	int y = group.center.y / DISTR_MAT_CELL_SIZE;
	for (int yy = -7; yy <= 7; ++yy)
	{
		for (int xx = -7; xx <= 7; ++xx)
		{
			if (xx * xx + yy * yy <= 50)
			{
				int x2 = x + xx;
				int y2 = y + yy;
				if (x2 >= 0 && x2 < DISTR_MAT_CELLS_X && y2 >= 0 && y2 < DISTR_MAT_CELLS_Y)
				{
					DangerDistCell &dCell = dangerDistCells[y2 * DISTR_MAT_CELLS_X + x2];
					if (dCell.totalEnemyDamage > 0.0 || dCell.totalMyDamage > 0.0)
					{
						return true;
					}
				}
			}
		}
	}*/
	
	return false;
}

void Strat::calcDangerDistCells()
{
	for (Group &group : groups)
	{
		DangerDistCells &dangerCells = dangerDistCells[group.internalId];
		for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
		{
			for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
			{
				const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
				double totalMyDamage = 0;
				
				for (int myType = 0; myType < 5; ++myType)
				{
					double typeDamage = 0;
					if (group.sizeByTypes[myType])
					{
						for (int enemyType = 0; enemyType < 5; ++enemyType)
						{
							if (cell.count[enemyType])
							{
								double dmg = getDamage((UnitType) myType, (UnitType) enemyType);
								typeDamage = std::max(typeDamage, std::min(dmg * group.sizeByTypes[myType], 120.0 * cell.count[enemyType]));
							}
						}
					}
					totalMyDamage += typeDamage;
				}
				
				DangerDistCell &dCell = dangerCells.cells[y * DISTR_MAT_CELLS_X + x];
				
				double f2hDmg = 0.0;
				dCell.totalEnemyDamage = 0.0;
				dCell.totalEnemyHealth = 0.0;
				for (int enemyType = 0; enemyType < 5; ++enemyType)
				{
					dCell.enemyDamage[enemyType] = 0.0;
					dCell.enemyHealth[enemyType] = 0.0;
					
					if (cell.count[enemyType])
					{
						dCell.enemyHealth[enemyType] += cell.health[enemyType];
						for (int myType = 0; myType < 5; ++myType)
						{
							if (group.sizeByTypes[myType])
							{
								double dmg = getDamage((UnitType) enemyType, (UnitType) myType);
								dCell.enemyDamage[enemyType] = std::min(dmg * cell.count[enemyType], 120.0 * group.sizeByTypes[myType]);
								
								if (enemyType == (int) UnitType::FIGHTER && myType == (int) UnitType::HELICOPTER)
									f2hDmg = dCell.enemyDamage[enemyType];
							}
						}
					}
					
					dCell.enemyDamage[enemyType] *= 1.5;
					dCell.totalEnemyDamage += dCell.enemyDamage[enemyType];
					dCell.totalEnemyHealth += dCell.enemyHealth[enemyType];
				}
				
				dCell.totalMyDamage = totalMyDamage;
				dCell.f2hDmg = f2hDmg;
			}
		}
	}
}

void Strat::calcVisibilityFactors()
{
	updateVisionRangeAndStealthFactor();
	
	for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
	{
		visibilityFactors[i] = 0.0;
	}
	
	for (MyUnit &u : units)
	{
		if (u.side == 0)
		{
			double range = u.visionRange;
			int minX = std::max(0.0, (u.pos.x - range)) / DISTR_MAT_CELL_SIZE;
			int maxX = std::min(WIDTH - 1.0, (u.pos.x + range)) / DISTR_MAT_CELL_SIZE;
			int minY = std::max(0.0, (u.pos.y - range)) / DISTR_MAT_CELL_SIZE;
			int maxY = std::min(HEIGHT - 1.0, (u.pos.y + range)) / DISTR_MAT_CELL_SIZE;
			double visionRange2 = sqr(u.visionRange);
			
			for (int y = minY; y <= maxY; ++y)
			{
				for (int x = minX; x <= maxX; ++x)
				{
					double &visFactor = visibilityFactors[y * DISTR_MAT_CELLS_X + x];
					if (visFactor == 1.0)
						continue;
					
					P center = P(x + 0.5, y + 0.5) * DISTR_MAT_CELL_SIZE;
					P nearPoint = center;
					P farPoint = center;
					if (center.x < u.pos.x)
					{
						nearPoint.x += DISTR_MAT_CELL_SIZE * 0.5;
						farPoint.x  -= DISTR_MAT_CELL_SIZE * 0.5;
					}
					else
					{
						nearPoint.x -= DISTR_MAT_CELL_SIZE * 0.5;
						farPoint.x  += DISTR_MAT_CELL_SIZE * 0.5;
					}
					
					if (center.y < u.pos.y)
					{
						nearPoint.y += DISTR_MAT_CELL_SIZE * 0.5;
						farPoint.y  -= DISTR_MAT_CELL_SIZE * 0.5;
					}
					else
					{
						nearPoint.y -= DISTR_MAT_CELL_SIZE * 0.5;
						farPoint.y  += DISTR_MAT_CELL_SIZE * 0.5;
					}
					
					if (nearPoint.dist2(u.pos) < visionRange2)
					{
						const Cell &cell = this->cell(x * DISTR_MAT_CELL_SIZE / CELL_SIZE, y * DISTR_MAT_CELL_SIZE / CELL_SIZE);
						double minStealth = 1.0;
						if (cell.groundType == GroundType::FOREST)
							minStealth = 0.6;
						else if (cell.weatherType == MyWeatherType::RAIN)
							minStealth = 0.6;
						else if (cell.weatherType == MyWeatherType::CLOUDY)
							minStealth = 0.8;
						
						double visibilityFactor = (range * minStealth) / farPoint.dist(u.pos);
						if (visibilityFactor > 1.0)
							visibilityFactor = 1.0;
						
						visFactor = std::max(visFactor, visibilityFactor);
					}
				}
			}
		}
	}
}

struct DangerDist {
	double dists[25];
	
	DangerDist() {
		dist(UnitType::ARV, UnitType::ARV) = 150.0;
		dist(UnitType::ARV, UnitType::FIGHTER) = 150.0;
		dist(UnitType::ARV, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::ARV, UnitType::IFV) = 150.0;
		dist(UnitType::ARV, UnitType::TANK) = 150.0;
		
		dist(UnitType::FIGHTER, UnitType::ARV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::FIGHTER, UnitType::IFV) = 150.0;
		dist(UnitType::FIGHTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::HELICOPTER, UnitType::ARV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::FIGHTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::IFV) = 150.0;
		dist(UnitType::HELICOPTER, UnitType::TANK) = 150.0;
		
		dist(UnitType::IFV, UnitType::ARV) = 150.0;
		dist(UnitType::IFV, UnitType::FIGHTER) = 100.0;
		dist(UnitType::IFV, UnitType::HELICOPTER) = 100.0;
		dist(UnitType::IFV, UnitType::IFV) = 150.0;
		dist(UnitType::IFV, UnitType::TANK) = 150.0;
		
		dist(UnitType::TANK, UnitType::ARV) = 150.0;
		dist(UnitType::TANK, UnitType::FIGHTER) = 1.0;
		dist(UnitType::TANK, UnitType::HELICOPTER) = 150.0;
		dist(UnitType::TANK, UnitType::IFV) = 150.0;
		dist(UnitType::TANK, UnitType::TANK) = 150.0;
		
		for (int i = 0; i < 25; ++i)
			dists[i] = sqr(dists[i]);
	}
	
	double &dist(UnitType from, UnitType to) {
		return dists[(int) from * 5 + (int) to];
	}
} DANGER_DISTS;


bool isClosedSpaceDanger(const P &myP, const P &enP, double myVel, double enemyVel, double ticks)
{
	double myR = myVel * ticks + 0.1;
	double dangerRad = 70.0;
	double enR = enemyVel * ticks + dangerRad;
	double d = myP.dist(enP);
	
	if (myR + d < enR)
		return true;
	
	const double borderDist = 40.0;
	if (myP.x > (borderDist + myR) && myP.x < (WIDTH - borderDist - myR) && myP.y > (borderDist + myR) && myP.y < (HEIGHT - borderDist - myR))
		return false;
	
	P myPn = myP;
	P enPn = enP;
	if (myPn.x > WIDTH / 2.0)
	{
		myPn.x = WIDTH - myPn.x;
		enPn.x = WIDTH - enPn.x;
	}
	
	if (myPn.y > HEIGHT / 2.0)
	{
		myPn.y = HEIGHT - myPn.y;
		enPn.y = HEIGHT - enPn.y;
	}
	
	if (myPn.x < myPn.y)
	{
		std::swap(myPn.x, myPn.y);
		std::swap(enPn.x, enPn.y);
	}
	
	double borderDistX = std::min(borderDist, myPn.x);
	double borderDistY = std::min(borderDist, myPn.y);
	
	double b = sqrt(sqr(myR) - sqr(myPn.y - borderDistY));
	double X = myPn.x + b;
	
	if (P(X, borderDistY).dist2(enPn) > sqr(enR))
		return false;
	
	if (myR > (myPn.x - borderDistX))
	{
		double Y = myPn.y + sqrt(sqr(myR) - sqr(myPn.x - borderDistX));
		
		if (P(borderDistX, Y).dist2(enPn) > sqr(enR))
			return false;
	}
	else
	{
		X = myPn.x - b;
	
		if (P(X, borderDistY).dist2(enPn) > sqr(enR))
			return false;
	}
	
	return true;
}

double captureTick(const P &myP, const P &enP, double myVel, double enemyVel)
{
	double dT = 125.0;
	double oldT = 0.0;
	for (double t = 0.0; t <= 625;)
	{
		if (!isClosedSpaceDanger(myP, enP, myVel, enemyVel, t))
		{
			oldT = t;
			t += dT;
		}
		else
		{
			if (dT <= 1.0)
				return t;
			
			t = oldT;
			dT /= 5.0;
			t += dT;
		}
	}
	
	return 625;
}

double Strat::attractionPoint(const P from, const Group &group, double ticks, bool angryMode)
{
	double res = 0.0;
	double f2hDmgRes = 0.0;
	bool spy = group.unitType == UnitType::FIGHTER;
	bool singleSpy = group.unitInd >= 0;
	
	DangerDistCells &dangerCells = dangerDistCells[group.internalId];
	
	for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
	{
		for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
		{
			DangerDistCell &dCell = dangerCells.cells[y * DISTR_MAT_CELLS_X + x];
			
			bool anyDamage = dCell.totalMyDamage > 0.0 || dCell.totalEnemyDamage > 0.0;
			
			if (anyDamage || (spy && dCell.totalEnemyHealth > 0))
			{
				double alpha = 0.3;
				double alphaM1 = 0.7;
				
				P p = P((x + 0.5) * DISTR_MAT_CELL_SIZE, (y + 0.5) * DISTR_MAT_CELL_SIZE);
				
				double dist2 = p.dist2(from);
				
				double pts = 0.0;
				
				if (anyDamage)
				{
					double myDamage = dCell.totalMyDamage*0.9;
					
					if (myDamage > 0.0)
					{
						double dist = std::sqrt(dist2);
						
						for (Group &othG : groups)
						{
							if (&othG != &group && (othG.center + othG.shift * (ticks * unitVel(othG.unitType))).dist(p) < (dist + 20.0))
							{
								DangerDistCells &othDangerCells = dangerDistCells[othG.internalId];
								DangerDistCell &othDCell = othDangerCells.cells[y * DISTR_MAT_CELLS_X + x];
								myDamage += othDCell.totalMyDamage * 0.5;
							}
						}
					}
					
					pts = (group.health * alphaM1 + dCell.totalEnemyHealth * alpha) / (dCell.totalEnemyHealth*0.01 + dCell.totalEnemyDamage) 
						- (dCell.totalEnemyHealth * alphaM1 + group.health * alpha) / (group.health * 0.01 + myDamage);
				}
				else
				{
					if (dist2 > sqr(50))
						pts = 0.1;
				}
						
				
				/*if (enableFOW)
				{
					const DistributionMatrix::Cell &cell = distributionMatrix.getCell(x, y);
					double dt = tick - cell.realUpdateTick;
		
					if (dt > 10)
					{
						pts /= sqr(dt / 10.0);
					}
				}*/
				
				pts *= (1.0 + dCell.totalEnemyHealth*0.0003);
				
				if (pts != 0.0)
				{
					double enemyVel = 0.0;
					
					for (int i = 0; i < 5; ++i) 
					{ 
						if (dCell.enemyHealth[i]) 
							enemyVel += unitVel((UnitType) i) * (dCell.enemyHealth[i] / dCell.totalEnemyHealth); 
					}
					
					if (pts < 0.0)
					{
						double t = captureTick(from, p, unitVel(group.unitType), enemyVel);
						res += pts * (625 - t) / 625.0;
					}
					else
					{
						double t = captureTick(p, from, enemyVel, unitVel(group.unitType));
						double pp = 1.0/(1.0 + dist2);
						res += pts * (625 - t) / 625.0 * pp;
					}
					
					if (pts > 0.0)
					{
						double pp = 1.0/(1.0 + dist2);
						res += pts * pp * 0.01;
					}
					else
					{
						double pn = (1.0 - std::min(1.0, dist2/sqr(150)));
						res += pts * pn;
					}
				}
				
				
				
				//res += pts / p.dist2(from);
			}
			
			f2hDmgRes += dCell.f2hDmg;
		}
	}
	
	if (group.unitType == UnitType::FIGHTER)
	{
		double L2 = sqr(1.5 * WIDTH);
		if (group.size * 90 > group.health)
		{
			Group *arvG = getGroup(UnitType::ARV);
			if (arvG && arvG->size * 2 > group.size)
			{
				L2 = arvG->center.dist2(from);
			}
		}
		
		double h = group.health/group.size;
		double pp = myCount[UnitType::ARV] * (100 - h)*6.0 / (1.0 + L2);
		if (h < 60)
			pp *= 2;
		res += pp;
	}
	
	if (singleSpy)
	{
		if (res == 0.0)
		{
			P target = P(800.0, 800.0);
			double dist2 = from.dist2(target);
			res += 1.0 / (1.0 + dist2);
		}
		
		if (enableFOW)
		{
			for (Group &g : groups)
			{
				if (g.invisible)
				{
					double dist2 = from.dist2(g.center);
					res -= 0.1 * (1.0 - std::min(1.0, dist2/sqr(300)));
				}
			}
		}
	}
	
	/*if (spy)
	{
		for (Group &g : groups)
		{
			if (&g != &group)
			{
				double dist2 = from.dist2(g.center);
				res -= 0.05 / (1.0 + dist2);
			}
		}
		
		if (enableFOW)
		{
			for (Building &b : buildings)
			{
				if (b.side != 0)
				{
					double dist2 = from.dist2(b.pos);
					double pp = 1.0/(1.0 + dist2);
					res += pp * 0.1;
				}
			}
		}
	}*/
	
	/*if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		double L = 1.5 * WIDTH;
		if (f2hDmgRes > 0.0 && res < 0.0)
		{
			if (fivG && fivG->size > 10)
			{
				L = fivG->center.dist(from);
			}
		}
		res -= L / WIDTH * myCount[UnitType::IFV];
		
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 300.0)
			{
				res -= (l - 300.0);
			}
		}
	}*/
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::HELICOPTER);
		if (fivG) {
			double l = fivG->center.dist(from);
			if (l > 400.0)
			{
				res -= (l - 400.0);
			}
		}
	}*/
	
	/*if (group.unitType == UnitType::FIGHTER)
	{
		Group *fivG = getGroup(UnitType::TANK);
		if (fivG) {
			double l = fivG->center.dist2(from);
			res += 0.0001 / (1.0 + l);
		}
	}
	
	if (group.unitType == UnitType::HELICOPTER)
	{
		Group *fivG = getGroup(UnitType::IFV);
		if (fivG) {
			double l = fivG->center.dist2(from);
			res += 0.0001 / (1.0 + l);
		}
	}*/
	
	if (isGroundUnit(group.unitType))
	{
		for (const Building &b : buildings)
		{
			if (b.side != 0)
			{
				double d = from.dist(b.pos);
				double coef = 0.5;
				
				if (group.hasAssignedBuilding && b.assignedGroup != 0)
				{
					coef = (b.assignedGroup == group.internalId) ? 2.0 : 0.5;
				}
				res += coef*group.health/(20 + d)*0.1;
			}
		}
	}
	
	if (group.attractedToGroup >= 0)
	{
		Group &othG = groups[group.attractedToGroup];
		double d = from.dist(othG.center);
		res += group.health/(20 + d)*0.3;
	}
	
	return res;
}


ShrinkResult Strat::findShrink(Group &group)
{
	ShrinkResult result;
	
	Simulator sim;
	sim.tick = tick;
	std::copy(cells, cells + CELLS_X * CELLS_Y, sim.cells);
	
	BBox gbox = group.bbox;
	gbox.expand(2.0);
	for (const MyUnit &u : units)
	{
		if (gbox.inside(u.pos))
		{
			sim.units.push_back(u);
			MyUnit &tu = *sim.units.rbegin();
			tu.selected = group.check(tu);
			tu.vel = P(0, 0);
		}
	}
	sim.groups.push_back(group);
	
	double bestArea = WIDTH * HEIGHT;
	for (double x = -1; x <= 1; ++x)
	{
		for (double y = -1; y <= 1; ++y)
		{
			Simulator sim2 = sim;
			MyMove myMove;
			myMove.action = MyActionType::SCALE;
			myMove.p = group.center + P(x, y) * 10.0;
			myMove.factor = 0.2;
			sim2.registerMove(myMove, 0);
			
			int i = 0;
			for (; i < 20; ++i)
			{
				sim2.tick++;
				sim2.resetAxisSorts();
				sim2.applyMoves();
				int cnt = sim2.moveUnits();
				if (!cnt)
					break;
			}
			sim2.updateStats();
			
			double area = sim2.groups.rbegin()->bbox.area();
			if (area < bestArea)
			{
				bestArea = area;
				result.shrinkPoint = myMove.p;
				result.ticks = i;
				result.endBBox = sim2.groups.rbegin()->bbox;
			}
		}
	}
	
	return result;
}

}
