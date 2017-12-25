#ifndef STRAT_HPP
#define STRAT_HPP

#include "Simulator.hpp"

constexpr int DISTR_MAT_CELL_SIZE = 16;
constexpr int DISTR_MAT_CELLS_X = (WIDTH + DISTR_MAT_CELL_SIZE - 1) / DISTR_MAT_CELL_SIZE;
constexpr int DISTR_MAT_CELLS_Y = (HEIGHT + DISTR_MAT_CELL_SIZE - 1) / DISTR_MAT_CELL_SIZE;

struct DistributionMatrix {
	struct Cell {
		double count[5] = {};
		double health[5] = {};
		double totalHealth = 0.0;
		double updateTick = 0;
		double realUpdateTick = 0;
	};
	
	Cell cells[DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y];
	
	Cell &getCell(int x, int y) {
		assert(x >= 0 && x < DISTR_MAT_CELLS_X);
		assert(y >= 0 && y < DISTR_MAT_CELLS_Y);
		return cells[y * DISTR_MAT_CELLS_X + x];
	}
	
	const Cell &getCell(int x, int y) const {
		assert(x >= 0 && x < DISTR_MAT_CELLS_X);
		assert(y >= 0 && y < DISTR_MAT_CELLS_Y);
		return cells[y * DISTR_MAT_CELLS_X + x];
	}
	
	DistributionMatrix() {}
	void initialize(const Simulator &sim, bool firstTick);
	void clear();
	void blur(DistributionMatrix &oth) const;
};

struct DebugAttractionPointsInfo {
	P point;
	P dir;
	double val;
};

struct DangerDistCell {
	double enemyDamage[5];
	double enemyHealth[5];
	double totalMyDamage, totalEnemyDamage, totalEnemyHealth;
	double f2hDmg;
};

struct ShrinkResult {
	BBox endBBox;
	P shrinkPoint;
	int ticks = 0;
};

struct DangerDistCells {
	DangerDistCell cells[DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y];
};

struct Strat : Simulator {
	Strat();
	
	MyMove calcNextMove();
	MyMove nextMove() {
		MyMove result = calcNextMove();
		if (result.action != MyActionType::NONE)
			registerAction();
		return result;
	}
	void calcMicroShift(Group &group, P &shift);
	double attractionPoint(const P from, const Group &group, double ticks, bool angryMode);
	void calcNuclearEfficiency();
	void calcDangerDistCells();
	void calcVisibilityFactors();
	bool anyEnemiesNearbyByDangerDistr(const Group &group);
	const Group *dngGr = nullptr;
	void assignBuildings();
	void updateGroupAttraction();
	void spreadDistributionMatrix(DistributionMatrix &distrMatrix);

	int extractFighterStep = -1;
	P extractFighterTarget;
	//bool highPriorityCells[DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y];
	
	
	int groupSeq = 1;
	int internalGroupSeq = 1;
	bool initialGroupsGerationDone = false;
	DistributionMatrix distributionMatrix;
	DistributionMatrix distributionMatrixFOWBlured;
	bool distributionMatrixInitialized = false;
	double nuclearEfficiency[MICROCELLS_X * MICROCELLS_Y];
	std::map<int, DangerDistCells> dangerDistCells;
	double visibilityFactors[DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y];
	
	ShrinkResult findShrink(Group &group);
	
	int buildingCaptured = 0;
	
	UnitType calcNextUnitTypeForConstruction(bool ground);
	
	///
	std::vector<DebugAttractionPointsInfo> debugAttractionPoints;
	
	bool canMove(const P &shift, const Group &group) const;
	bool canMoveDetailed(const P &shift, const Group &group, const std::vector<const MyUnit *> &groupUnits, const std::vector<const MyUnit *> &otherUnits) const;
	///
};

#endif
