#include "desert.h"
#include "noise.h"

#include <omp.h>

// File scope variables
#define OMP_NUM_THREAD 8
#define MAX_BOUNCE 3

static float abrasionEpsilon = 0.5;
static Vector2i next8[8] = { Vector2i(1, 0), Vector2i(1, 1), Vector2i(0, 1), Vector2i(-1, 1), Vector2i(-1, 0), Vector2i(-1, -1), Vector2i(0, -1), Vector2i(1, -1) };
static Vector2i Next(int i, int j, int k)
{
	return Vector2i(i, j) + next8[k];
}

/*!
\brief Perform a simulation step.
*/
void DuneSediment::SimulationStepMultiThreadAtomic()
{
#pragma omp parallel num_threads(OMP_NUM_THREAD)
	{
#pragma omp for
		for (int a = 0; a < nx; a++)
		{
			for (int b = 0; b < ny; b++)
				SimulationStepWorldSpace();
		}
	}
	EndSimulationStep();
}

/*!
\brief Some operations are performed every five iteration
to improve computation time.
*/
void DuneSediment::EndSimulationStep()
{
	static int simulationStepCount = 0;

#pragma omp atomic
	simulationStepCount++;

	if (simulationStepCount % 5 == 0)
	{
		// Bedrock stabilization is required if abrasion is turned on
		// To avoid unrealistic bedrock shapes. However, the repose angle of the material
		// Can be changed (we use 68 degrees, see desert.h static variables).
		if (abrasionOn)
			StabilizeBedrockAll();
	}
}

/*!
\brief Main simulation entry point. This function performs
a single simulation step at a random cell in the terrain.
*/
void DuneSediment::SimulationStepWorldSpace()
{
	Vector2 windDir;

	// (1) Select a random grid position (Lifting)
	int startI = Random::Integer() % nx;
	int startJ = Random::Integer() % ny;
	int start1D = ToIndex1D(startI, startJ);

	// Compute wind at start cell
	ComputeWindAtCell(startI, startJ, windDir);

	// No sediment to move
	if (sediments.Get(start1D) <= 0.0)
		return;
	// Wind shadowing probability
	if (Random::Uniform() < IsInShadow(startI, startJ, windDir))
	{
		StabilizeSedimentRelative(startI, startJ);
		return;
	}
	// Vegetation can retain sediments in the lifting process
	if (vegetationOn && Random::Uniform() < vegetation[start1D])
	{
		StabilizeSedimentRelative(startI, startJ);
		return;
	}

	// (2) Lift grain at start cell
#pragma omp atomic
	sediments[start1D] -= matterToMove;

	// (3) Jump downwind by saltation hop length (wind direction). Repeat until sand is deposited.
	int destI = startI;
	int destJ = startJ;
	Vector2 pos = bedrock.ArrayVertex(destI, destJ);
	int bounce = 0;
	while (bounce < MAX_BOUNCE)
	{
		// Compute wind at the current cell
		ComputeWindAtCell(destI, destJ, windDir);

		// Compute new world position and new grid position (after wind addition)
		pos = pos + windDir;
		SnapWorld(pos);
		bedrock.CellInteger(pos, destI, destJ);

		// Conversion to 1D index to speed up computation
		int destID = ToIndex1D(destI, destJ);

		// Abrasion of the bedrock occurs with low sand supply, weak bedrock and a low probability.
		if (abrasionOn && Random::Uniform() < 0.2 && sediments.Get(destID) < 0.5)
			PerformAbrasionOnCell(destI, destJ, windDir);

		// Probability of deposition
		float p = Random::Uniform();

		// Shadowed cell
		if (p < IsInShadow(destI, destJ, windDir))
		{
#pragma omp atomic
			sediments[destID] += matterToMove;
			break;
		}
		// Sandy cell - 60% chance of deposition (if vegetation == 0.0)
		else if (sediments.Get(destID) > 0.0 && p < 0.6 + (vegetationOn ? (vegetation.Get(destID) * 0.4) : 0.0))
		{
#pragma omp atomic
			sediments[destID] += matterToMove;
			break;
		}
		// Empty cell - 40% chance of deposition (if vegetation == 0.0)
		else if (sediments.Get(destID) <= 0.0 && p < 0.4 + (vegetationOn ? (vegetation.Get(destID) * 0.6) : 0.0))
		{
#pragma omp atomic
			sediments[destID] += matterToMove;
			break;
		}

		// Perform reptation at each bounce
		bounce++;
		if (Random::Uniform() < 1.0 - vegetation[start1D])
			PerformReptationOnCell(destI, destJ, bounce);
	}
	// End of the deposition loop - we have move matter from (startI, startJ) to (destI, destJ)

	// Perform reptation at the deposition simulationStepCount
	if (Random::Uniform() < 1.0 - vegetation[start1D])
		PerformReptationOnCell(destI, destJ, bounce);

	// (4) Check for the angle of repose on the original cell
	StabilizeSedimentRelative(startI, startJ);

	// (5) Check for the angle of repose on the destination cell if different
	StabilizeSedimentRelative(destI, destJ);
}

/*!
\brief Performs the reptation process as described in the paper.
Although some observations have been made in geomorphology about the impact
of reptation, we didn't find any particular change with or without reptation activated.
Still, implementation is provided if someone wants to try it.
*/
void DuneSediment::PerformReptationOnCell(int i, int j, int bounce)
{
	// Compute amount of sand to creep; function of number of bounce.
	int b = Math::Clamp(bounce, 0, 3);
	float t = float(b) / 3.0f;
	float se = Math::Lerp(matterToMove / 2.0f, matterToMove, t);
	float rReptationSquared = 2.0 * 2.0;
	Vector2 p = bedrock.ArrayVertex(i, j);

	// Distribute sand at the 2-steepest neighbours
	Vector2i nei[8];
	float nslope[8];
	int n = Math::Min(2, CheckSedimentFlowRelative(Vector2i(i, j), tanThresholdAngleSediment, nei, nslope));
	int nEffective = 0;
	for (int k = 0; k < n; k++)
	{
		Vector2i next = nei[k];
		float sei = se / n;

		// We don't perform reptation if the grid discretization is too low.
		// (If cells are too far away from each other in world space)
		Vector2 pk = bedrock.ArrayVertex(next.x, next.y);
		if (SquaredMagnitude(p - pk) > rReptationSquared)
			continue;

		// Distribute sediment to neighbour
#pragma omp atomic
		sediments[ToIndex1D(next)] += sei;

		// Count the amount of neighbour which received sand from the current cell (i, j)
		nEffective++;
	}

	// Remove sediment at the current cell
	if (n > 0 && nEffective > 0)
	{
#pragma omp atomic
		sediments[ToIndex1D(i, j)] -= se;
	}
}

/*!
\brief Compute the wind direction at a given cell.
\param i cell coordinate
\param j cell coordinate
\param windDir wind direction
*/
void DuneSediment::ComputeWindAtCell(int i, int j, Vector2& windDir) const
{
	// Get altitude of the sand at current cell
	const float sandHeight = sediments.Get(i, j);
	windDir = (1.0f + (0.005f * sandHeight)) * wind; 

	// If no wind
	if (windDir.x < 0.001f && windDir.y < 0.001f)
		return;

	// Modulate wind strength with sediment layer: increase velocity on slope in the direction of the wind
	Vector2 g = sediments.Gradient(i, j);
	Vector2 orthogonalVec = Vector2(-g.y, g.x);
	float slope = 0.0f;
	// If the gradient is not 0 and the wind direction is not 0
	if (g != Vector2(0.0f) && windDir != Vector2(0.0f))
	{
		slope = Math::Clamp(Magnitude(g));
		// Flip orthogonalVec when not in direction of V
		if (Dot(g, orthogonalVec)) {
			orthogonalVec = -orthogonalVec;
		}
	}

	// Implement change in wind direction mentioned in the paper
	windDir = Math::Lerp(windDir, 5.0f * orthogonalVec, slope);
}

/*!
\brief This functions performs the abrasion algorithm described in the paper,
which is responsible for the creation of yardang features.
\param i cell i
\param j cell j
\param windDir wind direction at this cell
*/
void DuneSediment::PerformAbrasionOnCell(int i, int j, const Vector2& windDir)
{
	int id = ToIndex1D(i, j);

	// Vegetation protects from abrasion
	float v = vegetationOn ? vegetation.Get(id) : 0.0f;

	// Bedrock resistance [0, 1] (1.0 equals to weak, 0.0 equals to hard)
	// Here with a simple sin() function, but anything could be used: texture, noise, construction trees...
	// In the paper, we used various noises octaves combined with each other.
	// Note: To get a more interesting look on the yardangs, turbulent wind is required. It is not provided
	// In this implementation.
	const Vector2 p = bedrock.ArrayVertex(i, j);
	const float freq = 0.08f;
	const float warp = 15.36f;
	float h = (sinf((p.y * freq) + (warp * PerlinNoise::GetValue(0.05f * p))) + 1.0f) / 2.0f;

	// Wind strength
	float w = Math::Clamp(Magnitude(windDir), 0.0f, 2.0f);

	// Abrasion strength, function of vegetation, hardness and wind speed.
	float si = abrasionEpsilon * (1.0f - v) * (1.0f - h) * w;
	if (si == 0.0)
		return;

	// Transform bedrock into dust
#pragma omp atomic
	bedrock[id] -= si;
}

/*!
\brief Check if a given grid vertex is in the wind shadow.
Use the threshold angle described in geomorphology papers, ie. ~[5, 15]�.
\param i x coordinate
\param j y coordinate
\param unitWindDir unit wind direction.
\returns true of the vertex is in shadow, false otherwise.
*/
float DuneSediment::IsInShadow(int i, int j, const Vector2& windDir) const
{
	const float windStepLength = 1.0;

	// Brennen: Add check to exit early no wind, thus no shadow
	if (Magnitude(windDir) < 0.001f)
		return 0.0f;

	const Vector2 windStep = 0.5f * Normalize(windDir);
	Vector2 p = bedrock.ArrayVertex(i, j);
	Vector2 pShadow = p;
	float rShadow = 10.0f;
	float hp = Height(p);
	float ret = 0.0;
	while (true)
	{
		pShadow = pShadow - windStep;
		if (pShadow == p)
			break;
		Vector2 pShadowSnapped = pShadow;
		SnapWorld(pShadowSnapped);

		float d = Magnitude(p - pShadow);
		if (d > rShadow)
			break;

		float step = Height(pShadowSnapped) - hp;
		float t = (step / d);
		// Brennen: Update function to match the paper
		float s = Math::Step(t, tanThresholdAngleWindShadowMin, tanThresholdAngleWindShadowMax);
		ret = Math::Max(ret, s);
	}
	return ret;
}

/*!
\brief Snaps the coordinates of a given point to stay within terrain boundaries.
*/
void DuneSediment::SnapWorld(Vector2& p) const
{
	if (p[0] < 0)
		p[0] = box.Size()[0] + p[0];
	else if (p[0] >= box.Size()[0])
		p[0] = p[0] - box.Size()[0];
	if (p[1] < 0)
		p[1] = box.Size()[1] + p[1];
	else if (p[1] >= box.Size()[1])
		p[1] = p[1] - box.Size()[1];
}
