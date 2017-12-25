#ifndef SIMULATOR_HPP
#define SIMULATOR_HPP

#include <cassert>
#include <vector>
#include <unordered_map>
#include <set>
#include "MyUtils.hpp"
#include <map>
#include <bitset>

constexpr int CELL_SIZE = 32;
constexpr int WIDTH = 1024;
constexpr int HEIGHT = 1024;

constexpr int CELLS_X = WIDTH / CELL_SIZE;
constexpr int CELLS_Y = HEIGHT / CELL_SIZE;

constexpr int UNIT_RAD = 2.0;
constexpr int UNIT_RAD_DOUBLED = UNIT_RAD * 2.0;
constexpr int UNIT_RAD_DOUBLED2 = UNIT_RAD_DOUBLED * UNIT_RAD_DOUBLED;
constexpr int MAX_GROUPS = 100;

typedef unsigned char GroupId;

enum class GroundType : char {
	PLAIN, FOREST, SWAMP
};

enum class MyWeatherType : char {
	FINE, CLOUDY, RAIN
};

enum class UnitType : char {
	NONE = -1, ARV, FIGHTER, HELICOPTER, IFV, TANK, COUNT
};

enum class BuildingType {
	CONTROL_CENTER, VEHICLE_FACTORY
};

const char *getUnitTypeName(UnitType unitType);

struct Cell {
	GroundType groundType;
	MyWeatherType weatherType;
};

struct MyUnit {
	long id;
	P pos, vel = P(0.0, 0.0);
	P moveStartPos;
	double durability = 100.0;
	int side;
	int attackCooldown = 0;
	UnitType type;
	int lastMovedTick = 0;
	bool visible;
	double visionRange;
	double stealthFactor;
	
	bool selected = false;
	int activeMove = -1;
	std::bitset<MAX_GROUPS> groups;
	
	bool hasGroup(int group) const {
		return groups.test(group - 1);
	}
	
	void addGroup(int group) {
		groups.set(group - 1);
	}
	
	void removeGroup(int group) {
		groups.reset(group - 1);
	}
};

struct Building {
	long id;
	P pos;
	double capturePoints = 0.0;
	BuildingType type;
	int side = -1;
	int productionProgress = 0.0;
	UnitType unitType = UnitType::NONE;
	int unitCount = 0;
	int unitCountByType[5] = {};
	int lastChangeUnitCount = 0;
	int createGroupStep = 0;
	int assignedGroup = 0;
	
	bool checkPoint(const P &p) const {
		return p.x > pos.x - 32 && p.x < pos.x + 32 && p.y > pos.y - 32 && p.y < pos.y + 32;
	}
	
	bool checkPointWithRad(const P &p) const {
		return p.x > pos.x - 34 && p.x < pos.x + 34 && p.y > pos.y - 34 && p.y < pos.y + 34;
	}
};

struct UnitProps {
	double speed;
	double viewRange;
	double groundAttackRange;
	double airAttackRange;
	double groundDamage;
	double airDamage;
	double groundDefence;
	double airDefence;
	int buildTime;
};

double getDamage(UnitType from, UnitType to);

const UnitProps &getProps(UnitType unitType);
double unitVel(UnitType unitType);

enum class MyActionType {
	NONE = 0,
	CLEAR_AND_SELECT = 1,
	ADD_TO_SELECTION = 2,
	DESELECT = 3,
	ASSIGN = 4,
	DISMISS = 5,
	DISBAND = 6,
	MOVE = 7,
	ROTATE = 8,
	SCALE = 9,
	SETUP_VEHICLE_PRODUCTION = 10,
	TACTICAL_NUCLEAR_STRIKE = 11,
	COUNT = 12
};

const char *getActionTypeName(MyActionType actionType);

struct MyMove {
	MyActionType action = MyActionType::NONE;
	P p1 = P(0, 0), p2 = P(WIDTH, HEIGHT), p = P(0, 0);
	double angle = 0.0;
	double factor = 0.0;
	double maxSpeed = 0.0;
	double maxAngularSpeed = 0.0;
	long vehicleId = -1;
	long facilityId = -1;
	int group = 0;
	
	UnitType unitType = UnitType::NONE;
};

#ifdef ENABLE_LOGGING
inline std::ostream &operator << ( std::ostream &str, const MyMove &move )
{
	str << getActionTypeName(move.action) << "(";
	switch(move.action) {
		case MyActionType::NONE:
			break;
		case MyActionType::CLEAR_AND_SELECT:
		case MyActionType::ADD_TO_SELECTION:
		case MyActionType::DESELECT:
			if (move.group)
			{
				str << "g(" << move.group << ") ";
			}
			else
			{
				str << "p1 " << move.p1 << " p2 " << move.p2;
				if (move.unitType != UnitType::NONE)
					str << "t " << getUnitTypeName(move.unitType);
			}
			break;
		case MyActionType::ASSIGN:
		case MyActionType::DISMISS:
		case MyActionType::DISBAND:
			str << "g(" << move.group << ") ";
			break;
		case MyActionType::MOVE:
			str << "p " << move.p;
			if (move.maxSpeed > 0.0)
				str << " maxS " << move.maxSpeed;
			break;
		case MyActionType::ROTATE:
			str << "p " << move.p << " a " << move.angle;
			if (move.maxSpeed > 0.0)
				str << " maxS " << move.maxSpeed;
			if (move.maxAngularSpeed > 0.0)
				str << " maxA " << move.maxAngularSpeed;
			break;
		case MyActionType::SCALE:
			str << "p " << move.p;
			str << " f " << move.factor;
			break;
		case MyActionType::SETUP_VEHICLE_PRODUCTION:
			str << "t " << getUnitTypeName(move.unitType) << " f " << move.facilityId;
			break;
		case MyActionType::TACTICAL_NUCLEAR_STRIKE:
			str << "p " << move.p << " vid " << move.vehicleId;
			break;
		case MyActionType::COUNT:
			break;
	}
	str << ")";
	return str;
}
#endif

inline bool isGroundUnit(UnitType unitType)
{
	return unitType == UnitType::ARV || unitType == UnitType::IFV || unitType == UnitType::TANK;
}

inline bool checkUnitWorldBounds(const P &p)
{
	return UNIT_RAD < p.x && UNIT_RAD < p.y && (WIDTH - UNIT_RAD) > p.x && (HEIGHT - UNIT_RAD) > p.y;
}

struct BBox {
	P p1 = P(WIDTH, HEIGHT), p2 = P(0, 0);
	
	bool inside(const P &p) const {
		return p1.x < p.x && p1.y < p.y && p2.x > p.x && p2.y > p.y;
	}
	
	void add(const BBox &oth) {
		p1.x = std::min(p1.x, oth.p1.x);
		p1.y = std::min(p1.y, oth.p1.y);
		p2.x = std::max(p2.x, oth.p2.x);
		p2.y = std::max(p2.y, oth.p2.y);
	}
	
	void add(const P &p) {
		p1.x = std::min(p1.x, p.x);
		p1.y = std::min(p1.y, p.y);
		p2.x = std::max(p2.x, p.x);
		p2.y = std::max(p2.y, p.y);
	}
	
	void expand(double d) {
		p1.x -= d;
		p1.y -= d;
		p2.x += d;
		p2.y += d;
	}
	
	double area() const {
		P diag = p1 - p2;
		return std::abs(diag.x * diag.y);
	}
	
	bool intersectsSegment(const P &start, const P &end) const
	{
		return inside(start) ||
			checkSegsIntersect(p1, P(p2.x, p1.y), start, end) ||
			checkSegsIntersect(p1, P(p1.x, p2.y), start, end) ||
			checkSegsIntersect(p2, P(p2.x, p1.y), start, end) ||
			checkSegsIntersect(p2, P(p1.x, p2.y), start, end);
	}
};

#ifdef ENABLE_LOGGING
inline std::ostream &operator << ( std::ostream &str, const BBox &p )
{
	
	str << "BBOX(" << p.p1 << "," << p.p2 << " A " << p.area() << ")";
	return str;
}
#endif

struct Group {
	UnitType unitType = UnitType::NONE;
	GroupId group = 0;
	int miniGroupInd = -1;
	int enumGroupBuildStep = 0;
	int lastUpdateTick = 0, lastShrinkTick = 0, lastComputeTick = 0;
	int nukeEvadeStep = 0;
	int internalId;
	bool shrinkAfterNuke = false;
	bool shrinkActive = false;
	bool actionStarted = false;
	bool canMove = true;
	bool hasAssignedBuilding = false;
	bool invisible = true;
	int attractedToGroup = -1;
	long unitId = -1;
	int unitInd = -1;
	P shift = P(0, 0);
	
	BBox bbox;
	P center;
	int size;
	int sizeByTypes[5];
	double health;
	double healthByTypes[5];
	
	bool check(const MyUnit &u) const;
	bool canIntersectWith(const MyUnit &u) const;
};

constexpr int MICROCELL_SIZE = 32;
constexpr int MICROCELLS_X = WIDTH / MICROCELL_SIZE;
constexpr int MICROCELLS_Y = HEIGHT / MICROCELL_SIZE;
constexpr int MICROCELLS_NUMBER = MICROCELLS_X * MICROCELLS_Y*2*5;

struct Random
{
	uint32_t m_w = 12345;    /* must not be zero, nor 0x464fffff */
	uint32_t m_z = 456345;    /* must not be zero, nor 0x9068ffff */

	uint32_t get_random()
	{
		m_z = 36969 * (m_z & 65535) + (m_z >> 16);
		m_w = 18000 * (m_w & 65535) + (m_w >> 16);
		return (m_z << 16) + m_w;  /* 32-bit result */
	}
	
	double getDouble()
	{
		uint32_t r = get_random();
		return (double) r / ((double) (1L << 32));
	}
};

struct MyPLayer {
	int remainingNuclearStrikeCooldownTicks = 0;
	int nextNuclearStrikeTick = -1;
	P nuclearStrike;
	long nextNuclearStrikeVehicleId = -1;
	int score = 0;
};

struct MicroShiftValues {
	double dist2;
	int val;
	UnitType unitType;
};

struct MicroShiftMatrix {
	MicroShiftMatrix();
	std::vector<MicroShiftValues> pos[(int) UnitType::COUNT];
	std::vector<MicroShiftValues> neg[(int) UnitType::COUNT];
};

extern MicroShiftMatrix microShiftMatrix;

struct UnitStat {
	int died = 0;
	int produced = 0;
	double damageMade = 0.0;
	double damageByNuke = 0.0;
};

struct UnitStats {
	UnitStat unitStats[5];
	double healed = 0.0;
};

struct Simulator {
	Simulator();
	
	int tick = -1;
	int angryModeTill = 0;
	long idSeq = 10000;
	MyPLayer players[2];
	std::vector<MyMove> moves[2];
	UnitStats unitStats[2];
	
	std::vector<Group> groups;
	std::set<int> actionTicks;
	
	Random random;
	
	Cell cells[CELLS_X * CELLS_Y];
	
	typedef std::vector<MyUnit> Units;
	Units units;
	std::vector<Building> buildings;
	std::map<UnitType, int> myCount;
	std::map<UnitType, int> enemyCount;
	int selectCount = 0;
	
	std::vector<short> microcells[MICROCELLS_NUMBER];
	std::vector<short> unitsSortedByX, unitsSortedByY;
	
	int nukeVehicleInd = -1;
	bool enableFOW = false;
	
	void synchonizeWith(const Simulator &oth, int mySide);
	void registerMove(const MyMove &move, int side);
	void applyMoves();
	void step();
	void resetCells();
	void resetAxisSorts();
	void updateBuildings();
	double getMaxSpeed(const MyUnit &unit) const;
	double getVisionRange(const MyUnit &unit) const;
	void updateVisionRangeAndStealthFactor();
	void updateFOW(int ignoreViewSide);
	
	std::vector<short> &getMicrocell(int x, int y, int side, UnitType unitType) {
		int ind = ((int) unitType + 5*side) * MICROCELLS_X * MICROCELLS_Y + y * MICROCELLS_X + x;
		return microcells[ind];
	}
	
	const Cell &cell(int x, int y) const {
		assert(x >= 0 && x < CELLS_X);
		assert(y >= 0 && y < CELLS_Y);
		return cells[y * CELLS_X + x];
	}
	
	Cell &cell(int x, int y) {
		assert(x >= 0 && x < CELLS_X);
		assert(y >= 0 && y < CELLS_Y);
		return cells[y * CELLS_X + x];
	}
	
	int moveUnits();
	void attackAndHealUnits();
	void updateStats();
	void registerAction();
	int getAvailableActions(int actionsPerMinute) const;
	int getAvailableActions(int actionsPerMinute, int timeRange) const;

	bool isSelected(const Group &group) const;
	MyMove select(const Group &group);
	bool canMoveNuke(const P &shift, const Group &group) const;
	bool canMove(const P &shift, const Group &group) const;
	bool canMoveDetailed(const P &shift, const Group &group, const std::vector<const MyUnit *> &groupUnits, const std::vector<const MyUnit *> &otherUnits) const;
	
	bool anyMoved(UnitType unitType, int side) const;
	bool anyMoved(const Group &g) const;
	Group *getGroup(UnitType unitType);
};

#endif
