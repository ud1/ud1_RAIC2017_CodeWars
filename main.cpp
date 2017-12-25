#include <iostream>

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <boost/timer/timer.hpp>
#include <vector>
#include <set>
#include <sstream>
#include <algorithm>
#include <chrono>

#define ENABLE_LOGGING 1
#include "MyUtils.hpp"


#define NANOVG_GL2_IMPLEMENTATION	// Use GL2 implementation.
#include "nanovg.h"
#include "nanovg_gl.h"

#include "Strat.hpp"
#include "OldStrat.hpp"

struct Renderer {
    void init();
    void startRendering();
    void finishRendering(Simulator &sim, Strat *g_myStrat);
    void renderPoint(const P &p);
};

SDL_Window *window;
SDL_GLContext glContext;
NVGcontext* vg;
GLuint clouds_textureID, rain_textureID, forest_textureID;

double SCALE = 1.0;
double zoom = 1.0;
P zoomCenter = P(0.5, 0.5);

std::vector<P> points;

class PerlinNoise {
    // The permutation vector
    std::vector<int> p;
public:
    // Generate a new permutation vector based on the value of seed
    PerlinNoise(unsigned int seed);
    // Get a noise value, for 2D images z can have any value
    double noise(double x, double y, double z);
private:
    double fade(double t);
    double lerp(double t, double a, double b);
    double grad(int hash, double x, double y, double z);
};

// Generate a new permutation vector based on the value of seed
PerlinNoise::PerlinNoise(unsigned int seed) {
    p.resize(256);

    // Fill p with values from 0 to 255
    std::iota(p.begin(), p.end(), 0);

    // Initialize a random engine with seed
    std::default_random_engine engine(seed);

    // Suffle  using the above random engine
    std::shuffle(p.begin(), p.end(), engine);

    // Duplicate the permutation vector
    p.insert(p.end(), p.begin(), p.end());
}

double PerlinNoise::noise(double x, double y, double z) {
    // Find the unit cube that contains the point
    int X = (int) floor(x) & 255;
    int Y = (int) floor(y) & 255;
    int Z = (int) floor(z) & 255;

    // Find relative x, y,z of point in cube
    x -= floor(x);
    y -= floor(y);
    z -= floor(z);

    // Compute fade curves for each of x, y, z
    double u = fade(x);
    double v = fade(y);
    double w = fade(z);

    // Hash coordinates of the 8 cube corners
    int A = p[X] + Y;
    int AA = p[A] + Z;
    int AB = p[A + 1] + Z;
    int B = p[X + 1] + Y;
    int BA = p[B] + Z;
    int BB = p[B + 1] + Z;

    // Add blended results from 8 corners of cube
    double res = lerp(w, lerp(v, lerp(u, grad(p[AA], x, y, z), grad(p[BA], x-1, y, z)), lerp(u, grad(p[AB], x, y-1, z), grad(p[BB], x-1, y-1, z))),	lerp(v, lerp(u, grad(p[AA+1], x, y, z-1), grad(p[BA+1], x-1, y, z-1)), lerp(u, grad(p[AB+1], x, y-1, z-1),	grad(p[BB+1], x-1, y-1, z-1))));
    return (res + 1.0)/2.0;
}

double PerlinNoise::fade(double t) {
    return t * t * t * (t * (t * 6 - 15) + 10);
}

double PerlinNoise::lerp(double t, double a, double b) {
    return a + t * (b - a);
}

double PerlinNoise::grad(int hash, double x, double y, double z) {
    int h = hash & 15;
    // Convert lower 4 bits of hash into 12 gradient directions
    double u = h < 8 ? x : y,
           v = h < 4 ? y : h == 12 || h == 14 ? x : z;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

void Renderer::init()
{
    int width = (int) (WIDTH * SCALE);
    int height = (int) (HEIGHT * SCALE);

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        return;

    window = SDL_CreateWindow("My Game Window",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              width, height,
                              SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);

    glContext = SDL_GL_CreateContext(window);
    if (glContext == NULL)
    {
        printf("There was an error creating the OpenGL context!\n");
        return;
    }

    const unsigned char *version = glGetString(GL_VERSION);
    if (version == NULL)
    {
        printf("There was an error creating the OpenGL context!\n");
        return;
    }

    SDL_GL_MakeCurrent(window, glContext);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetSwapInterval(1);

    //MUST make a context AND make it current BEFORE glewInit()!
    glewExperimental = GL_TRUE;
    GLenum glew_status = glewInit();
    if (glew_status != 0)
    {
        fprintf(stderr, "Error: %s\n", glewGetErrorString(glew_status));
        return;
    }

    glViewport( 0, 0, width, height);
}

void updateZoom()
{
    glMatrixMode(GL_PROJECTION);

    P center = zoomCenter * P(WIDTH, HEIGHT);
    P c1 = center - P(WIDTH, HEIGHT) * 0.5 / zoom;
    P c2 = center + P(WIDTH, HEIGHT) * 0.5 / zoom;

    /*if (c2.x > WIDTH)
    {
        double d = c2.x - WIDTH;
        c1.x -= d;
        c2.x -= d;
    }

    if (c2.y > HEIGHT)
    {
        double d = c2.y - HEIGHT;
        c1.y -= d;
        c2.y -= d;
    }

    if (c1.x < 0)
    {
        double d = -c1.x;
        c1.x += d;
        c2.x += d;
    }

    if (c1.y < 0)
    {
        double d = -c1.y;
        c1.y += d;
        c2.y += d;
    }*/

    zoomCenter = (c1 + c2) / 2.0 / P(WIDTH, HEIGHT);

    //LOG("ZOOM " << zoomCenter);
    glLoadIdentity();
    glOrtho(c1.x, c2.x, c2.y, c1.y, 0.0, 1.0);
}

void Renderer::startRendering()
{
    points.clear();
}

void renderCircumference(const P &p, double rad)
{
    glBegin ( GL_TRIANGLE_STRIP );

    double N = 30;

    for ( double i = 0; i <= N; ++i ) {
        double alpha = i * PI * 2.0 / N;
        P p2 = P (rad  * std::cos ( alpha ), rad * std::sin ( alpha ) ) + p;
        glVertex2d ( p2.x, p2.y );

        p2 = P (rad  * std::cos ( alpha ), rad * std::sin ( alpha ) )*0.8 + p;
        glVertex2d ( p2.x, p2.y );
    }

    glEnd();
}

void renderPie(const P &p, double rad, double startAlpha, double dAlpha)
{
    startAlpha = startAlpha - std::floor(startAlpha);
    glBegin ( GL_TRIANGLE_FAN );
    glVertex2d ( p.x, p.y );

    double N = 2;

    for ( double i = 0; i <= N; ++i ) {
        double alpha = startAlpha * PI * 2.0 + dAlpha * i * PI * 2.0 / N;
        P p2 = P (rad  * std::cos ( alpha ), rad * std::sin ( alpha ) ) + p;
        glVertex2d ( p2.x, p2.y );
    }

    glEnd();
}

void renderCircle(const P &p, double rad)
{
    glBegin ( GL_TRIANGLE_FAN );

    glVertex2d ( p.x, p.y );

    double N = 30;

    for ( double i = 0; i <= N; ++i ) {
        double alpha = i * PI * 2.0 / N;
        P p2 = P (rad  * std::cos ( alpha ), rad * std::sin ( alpha ) ) + p;
        glVertex2d ( p2.x, p2.y );
    }

    glEnd();
}

float floorCoord(float y)
{
    return 950.0f - y*100.0f;
}

using namespace std::chrono;
steady_clock::time_point programStart = steady_clock::now();

void vertexWithTex(float x, float y)
{
    glTexCoord2f(x / WIDTH * 10.0, y / WIDTH * 10.0);
    glVertex2f(x, y);
}

void vertexWithTexCloud(float x, float y, float time_span)
{
    glTexCoord2f(x / WIDTH * 5.0 + time_span / 50.0f, y / WIDTH * 5.0 - time_span / 50.0f);
    glVertex2f(x, y);
}

void vertexWithTexRain(float x, float y, float time_span)
{
    glTexCoord2f(x / WIDTH * 10.0 - time_span / 50.0f, y / WIDTH * 10.0 - time_span / 20.0f);
    glVertex2f(x, y);
}

static P mousePos = P(0.0, 0.0);

enum class RenderMode {
    NORMAL,
    SIMPLE,
    DANGER
};

RenderMode renderMode = RenderMode::NORMAL;

struct color_triplet{
    unsigned char r;
    unsigned char g;
    unsigned char b;
};

struct Palete {
	color_triplet table[256] = {{0,0,0}};
	
	Palete() {
		int i = 0;
		int red, green, blue;

		for (red = 0; red <= 255; red+= 51) {/* the six values of red */
			for (green = 0; green <= 255; green += 51) {
				for (blue = 0; blue <= 255; blue+= 51) {
					table[i].r = red;
					table[i].g = green;
					table[i].b = blue;
					++i;
				}
			}
		}
	}
} palete;

bool renderSim = true;

void renderSimulator(Simulator &sim, Strat *g_myStrat)
{
    std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
    float time_span = duration_cast<duration<float>>(t2 - programStart).count();

    glEnable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);

    if (renderMode == RenderMode::NORMAL)
    {
        for (int y = 0; y < CELLS_Y; ++y)
        {
            for (int x = 0; x < CELLS_X; ++x)
            {
                const Cell &cell = sim.cell(x, y);
                if (cell.groundType == GroundType::FOREST)
                {
                    glColor3ub(15, 153, 31);
                    glBegin(GL_QUADS);
                    glVertex2f(x * CELL_SIZE, y * CELL_SIZE);
                    glVertex2f((x + 1) * CELL_SIZE, y * CELL_SIZE);
                    glVertex2f((x + 1) * CELL_SIZE, (y + 1) * CELL_SIZE);
                    glVertex2f(x * CELL_SIZE, (y + 1) * CELL_SIZE);
                    glEnd();
                }
                else if (cell.groundType == GroundType::SWAMP)
                {
                    glColor3ub(179, 198, 147);
                    glBegin(GL_QUADS);
                    glVertex2f(x * CELL_SIZE, y * CELL_SIZE);
                    glVertex2f((x + 1) * CELL_SIZE, y * CELL_SIZE);
                    glVertex2f((x + 1) * CELL_SIZE, (y + 1) * CELL_SIZE);
                    glVertex2f(x * CELL_SIZE, (y + 1) * CELL_SIZE);
                    glEnd();
                }
                else if (cell.groundType == GroundType::PLAIN)
                {
                    glColor3ub(239, 215, 107);
                    glBegin(GL_QUADS);
                    glVertex2f(x * CELL_SIZE, y * CELL_SIZE);
                    glVertex2f((x + 1) * CELL_SIZE, y * CELL_SIZE);
                    glVertex2f((x + 1) * CELL_SIZE, (y + 1) * CELL_SIZE);
                    glVertex2f(x * CELL_SIZE, (y + 1) * CELL_SIZE);
                    glEnd();
                }
            }
        }
    }

    glEnable(GL_BLEND);

    if (renderMode == RenderMode::NORMAL)
    {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, clouds_textureID);

        for (int y = 0; y < CELLS_Y; ++y)
        {
            for (int x = 0; x < CELLS_X; ++x)
            {
                Cell &cell = sim.cell(x, y);
                if (cell.weatherType == MyWeatherType::CLOUDY)
                {
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                    glBegin(GL_QUADS);
                    glColor4ub(255,255,255, 50);
                    vertexWithTexCloud(x * CELL_SIZE, y * CELL_SIZE, time_span);
                    vertexWithTexCloud((x + 1) * CELL_SIZE, y * CELL_SIZE, time_span);
                    vertexWithTexCloud((x + 1) * CELL_SIZE, (y + 1) * CELL_SIZE, time_span);
                    vertexWithTexCloud(x * CELL_SIZE, (y + 1) * CELL_SIZE, time_span);
                    glEnd();
                }
            }
        }

        glBindTexture(GL_TEXTURE_2D, rain_textureID);
        for (int y = 0; y < CELLS_Y; ++y)
        {
            for (int x = 0; x < CELLS_X; ++x)
            {
                Cell &cell = sim.cell(x, y);
                if (cell.weatherType == MyWeatherType::RAIN)
                {
                    glBlendFunc(GL_CONSTANT_COLOR, GL_ONE_MINUS_SRC_COLOR);
                    glBlendEquation(GL_FUNC_ADD);
                    glBlendColor(0.180, 0.247, 1.000, 1.000);

                    glBegin(GL_QUADS);
                    glColor4ub(180,180,220, 120);
                    vertexWithTexRain(x * CELL_SIZE, y * CELL_SIZE, time_span);
                    vertexWithTexRain((x + 1) * CELL_SIZE, y * CELL_SIZE, time_span);
                    vertexWithTexRain((x + 1) * CELL_SIZE, (y + 1) * CELL_SIZE, time_span);
                    vertexWithTexRain(x * CELL_SIZE, (y + 1) * CELL_SIZE, time_span);
                    glEnd();
                }
            }
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }

    if (renderMode == RenderMode::SIMPLE || renderMode == RenderMode::DANGER)
    {
		if (!renderSim && g_myStrat)
		{
			for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
			{
				for (int x = 0; x < DISTR_MAT_CELLS_X; ++x)
				{
					double visFactor = g_myStrat->visibilityFactors[y * DISTR_MAT_CELLS_X + x];
					glColor3f(visFactor, visFactor, visFactor);
					glBegin(GL_QUADS);
					glVertex2f((x + 0) * DISTR_MAT_CELL_SIZE, (y + 0) * DISTR_MAT_CELL_SIZE);
					glVertex2f((x + 1) * DISTR_MAT_CELL_SIZE, (y + 0) * DISTR_MAT_CELL_SIZE);
					glVertex2f((x + 1) * DISTR_MAT_CELL_SIZE, (y + 1) * DISTR_MAT_CELL_SIZE);
					glVertex2f((x + 0) * DISTR_MAT_CELL_SIZE, (y + 1) * DISTR_MAT_CELL_SIZE);
					glEnd();
				}
			}
		}
		
        glColor4ub(200,200,200, 255);
        glBegin(GL_LINES);
        for (int i = 0; i <= WIDTH; i += CELL_SIZE)
        {
            glVertex2f(i, 0);
            glVertex2f(i, HEIGHT);
        }
        for (int i = 0; i <= HEIGHT; i += CELL_SIZE)
        {
            glVertex2f(0, i);
            glVertex2f(WIDTH, i);
        }
        glEnd();
		
		if (g_myStrat)
		{
			for (Group &g : g_myStrat->groups)
			{
				if (g.canMove)
					glColor4ub(150,150,150, 255);
				else
					glColor4ub(255,100, 100, 255);
				
				glBegin(GL_LINE_STRIP);
				glVertex2f(g.bbox.p1.x, g.bbox.p1.y);
				glVertex2f(g.bbox.p2.x, g.bbox.p1.y);
				glVertex2f(g.bbox.p2.x, g.bbox.p2.y);
				glVertex2f(g.bbox.p1.x, g.bbox.p2.y);
				glVertex2f(g.bbox.p1.x, g.bbox.p1.y);
				glEnd();
				
				glBegin(GL_LINES);
				glVertex2f(g.bbox.p1.x, g.bbox.p1.y);
				glVertex2f(g.center.x, g.center.y);
				
				glVertex2f(g.bbox.p2.x, g.bbox.p1.y);
				glVertex2f(g.center.x, g.center.y);
				
				glVertex2f(g.bbox.p2.x, g.bbox.p2.y);
				glVertex2f(g.center.x, g.center.y);
				
				glVertex2f(g.bbox.p1.x, g.bbox.p2.y);
				glVertex2f(g.center.x, g.center.y);
				glEnd();
				
				if (g.attractedToGroup >= 0)
				{
					glColor4ub(0,0, 0, 255);
					glBegin(GL_LINES);
					glVertex2f(g.center.x, g.center.y);
					
					Group &othG = g_myStrat->groups[g.attractedToGroup];
					glVertex2f(othG.center.x, othG.center.y);
					glEnd();
				}
				
				if (g.hasAssignedBuilding)
				{
					for (Building &b : g_myStrat->buildings)
					{
						if (b.assignedGroup == g.internalId)
						{
							glColor4ub(0,255, 0, 255);
							glBegin(GL_LINES);
							glVertex2f(g.center.x, g.center.y);
							glVertex2f(b.pos.x, b.pos.y);
							glEnd();
						}
					}
				}
				
				if (g.unitInd > 0)
				{
					MyUnit &u = g_myStrat->units[g.unitInd];
					glColor4ub(0,0, 255, 255);
					renderCircumference(u.pos, 3);
				}
			}
		}
    }

    /*if (renderMode == RenderMode::DANGER && g_myStrat)
    {
        double maxTotalMyDamage = 100.0, maxTotalEnemyDamage = 100.0, maxTotalEnemyHealth = 500.0;

        for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
        {
            DangerDistCell &cell = g_myStrat-> dangerDistCells[i];
            maxTotalMyDamage    = std::max(maxTotalMyDamage,    cell.totalMyDamage);
            maxTotalEnemyDamage = std::max(maxTotalEnemyDamage, cell.totalEnemyDamage);
            maxTotalEnemyHealth = std::max(maxTotalEnemyHealth, cell.totalEnemyHealth);
        }

        glBegin(GL_QUADS);
        for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
        {
            for (int x = 0; x < DISTR_MAT_CELLS_Y; ++x)
            {
                DangerDistCell &cell = g_myStrat-> dangerDistCells[y * DISTR_MAT_CELLS_X + x];

                glColor4ub(100,200,100, 255);

                P pos = P(x, y + 1) * DISTR_MAT_CELL_SIZE;
                double h = cell.totalMyDamage * DISTR_MAT_CELL_SIZE / maxTotalMyDamage;
                glVertex2f(pos.x, pos.y);
                glVertex2f(pos.x + 2, pos.y);
                glVertex2f(pos.x + 2, pos.y - h);
                glVertex2f(pos.x, pos.y - h);

                glColor4ub(200,100,100, 255);

                pos = P(x, y + 1) * DISTR_MAT_CELL_SIZE + P(3, 0);
                h = cell.totalEnemyDamage * DISTR_MAT_CELL_SIZE / maxTotalEnemyDamage;
                glVertex2f(pos.x, pos.y);
                glVertex2f(pos.x + 2, pos.y);
                glVertex2f(pos.x + 2, pos.y - h);
                glVertex2f(pos.x, pos.y - h);

                glColor4ub(100,100,150, 255);

                pos = P(x, y + 1) * DISTR_MAT_CELL_SIZE + P(6, 0);
                h = cell.totalEnemyHealth * DISTR_MAT_CELL_SIZE / maxTotalEnemyHealth;
                glVertex2f(pos.x, pos.y);
                glVertex2f(pos.x + 2, pos.y);
                glVertex2f(pos.x + 2, pos.y - h);
                glVertex2f(pos.x, pos.y - h);
            }
        }
        glEnd();
    }*/
	
	if (renderMode == RenderMode::DANGER && g_myStrat)
    {
		double maxHealth = 1.0;
		
		for (int i = 0; i < DISTR_MAT_CELLS_X * DISTR_MAT_CELLS_Y; ++i)
        {
            DistributionMatrix::Cell &cell = g_myStrat->distributionMatrix.cells[i];
			for (int i = 0; i < 5; ++i)
				maxHealth = std::max(maxHealth, cell.health[i]);
		}
		
		glBegin(GL_QUADS);
        for (int y = 0; y < DISTR_MAT_CELLS_Y; ++y)
        {
            for (int x = 0; x < DISTR_MAT_CELLS_Y; ++x)
            {
				DistributionMatrix::Cell &cell = g_myStrat->distributionMatrix.getCell(x, y);
				
				glColor4ub(100,200,100, 255);
				for (int i = 0; i < 5; ++i)
				{
					P pos = P(x, y + 1) * DISTR_MAT_CELL_SIZE + P(i*2.0, 0.0);
					double h = cell.health[i] * DISTR_MAT_CELL_SIZE / maxHealth;
					glVertex2f(pos.x, pos.y);
					glVertex2f(pos.x + 1.5, pos.y);
					glVertex2f(pos.x + 1.5, pos.y - h);
					glVertex2f(pos.x, pos.y - h);
				}
			}
		}
		glEnd();
	}
	
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (const Building &b : sim.buildings)
    {
        if (b.side == -1)
            glColor4ub(127,127,127, 200);
        else if (b.side == 0)
            glColor4ub(255,127,127, 200);
        else if (b.side == 1)
            glColor4ub(127,127,255, 200);

        if (b.type == BuildingType::VEHICLE_FACTORY)
        {
            glBegin(GL_QUADS);
            glVertex2f(b.pos.x - 32, b.pos.y - 32);
            glVertex2f(b.pos.x + 32, b.pos.y - 32);
            glVertex2f(b.pos.x + 32, b.pos.y + 32);
            glVertex2f(b.pos.x - 32, b.pos.y + 32);
            glEnd();
        }
        else
        {
            glBegin(GL_QUADS);
            glVertex2f(b.pos.x - 32, b.pos.y - 32);
            glVertex2f(b.pos.x + 27, b.pos.y - 27);
            glVertex2f(b.pos.x + 32, b.pos.y + 32);
            glVertex2f(b.pos.x - 27, b.pos.y + 27);
            glEnd();
        }

        glColor4ub(0,255,0, 200);
        glBegin(GL_QUADS);
        glVertex2f(b.pos.x, b.pos.y - 3);
        glVertex2f(b.pos.x + b.capturePoints * 32.0 / 100.0, b.pos.y - 3);
        glVertex2f(b.pos.x + b.capturePoints * 32.0 / 100.0, b.pos.y + 3);
        glVertex2f(b.pos.x, b.pos.y + 3);
        glEnd();

        if (b.unitType != UnitType::NONE)
        {
            double buildTime = getProps(b.unitType).buildTime;
            glColor4ub(0,255,255, 200);
            glBegin(GL_QUADS);
            glVertex2f(b.pos.x - 32, b.pos.y - 9);
            glVertex2f(b.pos.x - 32 + 64*b.productionProgress / buildTime, b.pos.y - 9);
            glVertex2f(b.pos.x - 32 + 64*b.productionProgress / buildTime, b.pos.y - 3);
            glVertex2f(b.pos.x - 32, b.pos.y - 3);
            glEnd();
        }
    }

    for (int i = 0; i < 2; ++i)
    {
        MyPLayer &p = sim.players[i];
        if (p.nextNuclearStrikeTick >= 0)
        {


            if (i == 0)
                glColor4ub(255,127,127, 100);
            else
                glColor4ub(127,127,255, 100);

            renderCircle(p.nuclearStrike, 50.0);
        }
    }

    if (g_myStrat)
    {
        double minVal = 100000.0;
        double maxVal = -100000.0;
        for (DebugAttractionPointsInfo &info : g_myStrat->debugAttractionPoints)
        {
            minVal = std::min(minVal, info.val);
            maxVal = std::max(maxVal, info.val);
        }

        double range = maxVal - minVal;


        glBegin(GL_TRIANGLES);
        for (DebugAttractionPointsInfo &info : g_myStrat->debugAttractionPoints)
        {
            if (info.val < 0.0)
                glColor4ub(255,0,0, 100);
            else
                glColor4ub(0,255,0, 100);

            glVertex2f(info.point.x, info.point.y);

            P p = info.point + info.dir.rotate(info.val / range / 30.0);
            glVertex2f(p.x, p.y);

            p = info.point + info.dir.rotate(-info.val / range / 30.0);
            glVertex2f(p.x, p.y);
        }
        glEnd();
    }

    

    for (const MyUnit &unit : sim.units)
    {
        if (renderMode != RenderMode::NORMAL && unit.groups.any())
		{
			for (int i = 1; i < 100; ++i)
			{
				if (unit.hasGroup(i))
				{
					color_triplet &p = palete.table[(i*2 + 1 + unit.side) % 100];
					int alpha = renderSim && sim.enableFOW && !unit.visible ? 50 : 255;
					
					glColor4ub(p.r, p.g, p.b, alpha);
					break;
				}
			}
		}
		else
		{
			float alpha = renderSim && sim.enableFOW && !unit.visible ? 50.0/255.0 : 255.0/255.0;
			
			if (unit.side == 0)
				glColor4f(1, 0, 0, alpha);
			else
				glColor4f(0, 0, 1, alpha);

			if (unit.selected)
			{
				if (unit.side == 0)
					glColor4f(1, 0.5, 0, alpha);
				else
					glColor4f(0, 0.5, 1, alpha);
			}
		}

        float rad = UNIT_RAD;
        renderCircumference(unit.pos, rad);

        if (unit.type == UnitType::HELICOPTER)
        {
            float dt = time_span;
            renderPie(unit.pos, rad, 0 + dt, 1.0/16.0);
            renderPie(unit.pos, rad, 0.25 + dt, 1.0/16.0);
            renderPie(unit.pos, rad, 0.5 + dt, 1.0/16.0);
            renderPie(unit.pos, rad, 0.75 + dt, 1.0/16.0);
        }
        else if (unit.type == UnitType::TANK)
        {
            glBegin(GL_QUADS);
            glVertex2f(unit.pos.x - rad * 0.6, unit.pos.y - rad * 0.4);
            glVertex2f(unit.pos.x + rad * 0.6, unit.pos.y - rad * 0.4);
            glVertex2f(unit.pos.x + rad * 0.6, unit.pos.y + rad * 0.4);
            glVertex2f(unit.pos.x - rad * 0.6, unit.pos.y + rad * 0.4);

            glVertex2f(unit.pos.x + rad * 0.6, unit.pos.y - rad * 0.05);
            glVertex2f(unit.pos.x + rad * 0.8, unit.pos.y - rad * 0.05);
            glVertex2f(unit.pos.x + rad * 0.8, unit.pos.y + rad * 0.05);
            glVertex2f(unit.pos.x + rad * 0.6, unit.pos.y + rad * 0.05);

            glEnd();
        }
        else if (unit.type == UnitType::IFV)
        {
            glBegin(GL_QUADS);
            glVertex2f(unit.pos.x - rad * 0.6, unit.pos.y - rad * 0.4);
            glVertex2f(unit.pos.x + rad * 0.2, unit.pos.y - rad * 0.4);
            glVertex2f(unit.pos.x + rad * 0.2, unit.pos.y + rad * 0.4);
            glVertex2f(unit.pos.x - rad * 0.6, unit.pos.y + rad * 0.4);

            glVertex2f(unit.pos.x + rad * 0.2, unit.pos.y - rad * 0.2);
            glVertex2f(unit.pos.x + rad * 0.6, unit.pos.y - rad * 0.2);
            glVertex2f(unit.pos.x + rad * 0.6, unit.pos.y - rad * 0.1);
            glVertex2f(unit.pos.x + rad * 0.2, unit.pos.y - rad * 0.1);

            glVertex2f(unit.pos.x + rad * 0.2, unit.pos.y + rad * 0.2);
            glVertex2f(unit.pos.x + rad * 0.6, unit.pos.y + rad * 0.2);
            glVertex2f(unit.pos.x + rad * 0.6, unit.pos.y + rad * 0.1);
            glVertex2f(unit.pos.x + rad * 0.2, unit.pos.y + rad * 0.1);

            glEnd();
        }
        else if (unit.type == UnitType::FIGHTER)
        {
            glBegin(GL_TRIANGLES);
            glVertex2f(unit.pos.x - rad * 0.6, unit.pos.y - rad * 0.6);
            glVertex2f(unit.pos.x + rad * 0.1, unit.pos.y);
            glVertex2f(unit.pos.x - rad * 0.6, unit.pos.y + rad * 0.6);

            glVertex2f(unit.pos.x, unit.pos.y - rad * 0.2);
            glVertex2f(unit.pos.x + rad * 0.9, unit.pos.y);
            glVertex2f(unit.pos.x, unit.pos.y + rad * 0.2);

            glEnd();
        }
        else if (unit.type == UnitType::ARV)
        {
            renderCircumference(unit.pos - P(rad/2.0, 0), rad/2.0);
            renderCircumference(unit.pos + P(rad/2.0, 0), rad/2.0);

            glBegin(GL_QUADS);
            glVertex2f(unit.pos.x - rad * 0.9, unit.pos.y - rad * 0.1);
            glVertex2f(unit.pos.x + rad * 0.9, unit.pos.y - rad * 0.1);
            glVertex2f(unit.pos.x + rad * 0.9, unit.pos.y + rad * 0.1);
            glVertex2f(unit.pos.x - rad * 0.9, unit.pos.y + rad * 0.1);

            glEnd();
        }

        glColor3f(0, 1.0, 0);
        glBegin(GL_QUADS);
        glVertex2f(unit.pos.x - rad * 0.1, unit.pos.y - rad + 2.0*rad*(100.0 - unit.durability)*0.01);
        glVertex2f(unit.pos.x + rad * 0.1, unit.pos.y - rad + 2.0*rad*(100.0 - unit.durability)*0.01);
        glVertex2f(unit.pos.x + rad * 0.1, unit.pos.y + rad);
        glVertex2f(unit.pos.x - rad * 0.1, unit.pos.y + rad);
        glEnd();
		
		glColor3f(0.0, 0.0, 1.0);
        glBegin(GL_QUADS);
        glVertex2f(unit.pos.x +     rad * 0.1, unit.pos.y - rad + 2.0*rad*(unit.attackCooldown)/60.0);
        glVertex2f(unit.pos.x + 3.0*rad * 0.1, unit.pos.y - rad + 2.0*rad*(unit.attackCooldown)/60.0);
        glVertex2f(unit.pos.x + 3.0*rad * 0.1, unit.pos.y + rad);
        glVertex2f(unit.pos.x +     rad * 0.1, unit.pos.y + rad);
        glEnd();
    }

    glDisable(GL_BLEND);
	
    glColor3f(0.5, 0.5, 0);
    glBegin(GL_LINES);
    for (const MyUnit &unit : sim.units)
    {
        glVertex2f(unit.pos.x, unit.pos.y);
        P t = unit.pos + unit.vel * 300;
        glVertex2f(t.x, t.y);
    }
    glEnd();

    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    nvgBeginFrame(vg, w, h, 1);

    nvgFontFace(vg, "sans");
    nvgFontSize(vg, 15.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
    std::stringstream oss;
    oss << "Tick " << sim.tick << " Score " << sim.players[0].score << " / " << sim.players[1].score;
    double textY = 20.0;
    nvgText(vg, 10, textY, oss.str().c_str(), nullptr);
    textY += 15.0;

    oss.str("");
    oss << "Nuke " << sim.players[0].remainingNuclearStrikeCooldownTicks << " / " << sim.players[1].remainingNuclearStrikeCooldownTicks;
    nvgText(vg, 10, textY, oss.str().c_str(), nullptr);
    textY += 15.0;

    if (g_myStrat->dngGr)
    {
        P titlePos = (zoomCenter + (mousePos - P(0.5, 0.5))/zoom)*WIDTH;
        titlePos = clampP(titlePos, P(0, 0), P(WIDTH - 1, HEIGHT - 1));
        int x = titlePos.x / DISTR_MAT_CELL_SIZE;
        int y = titlePos.y / DISTR_MAT_CELL_SIZE;

        /*if (x >= 0 && x < DISTR_MAT_CELLS_X && y >= 0 && y < DISTR_MAT_CELLS_Y)
        {
            oss.str("");

            const DangerDistCell &cell = g_myStrat->dangerDistCells[y * DISTR_MAT_CELLS_X + x];
            oss << "ED " << cell.totalEnemyDamage << " EH " << cell.totalEnemyHealth << " MD " << cell.totalMyDamage << " F2H " << cell.f2hDmg << " MH " << g_myStrat->dngGr->health;

            if (cell.totalMyDamage > 0.0 || cell.totalEnemyDamage > 0.0)
            {
            	double alpha = 0.3;
            	double alphaM1 = 0.7;
            	double pts = (g_myStrat->dngGr->health * alphaM1 + cell.totalEnemyHealth * alpha) / (cell.totalEnemyHealth*0.01 + cell.totalEnemyDamage)
            	- (cell.totalEnemyHealth * alphaM1 + g_myStrat->dngGr->health * alpha) / (g_myStrat->dngGr->health * 0.01 + cell.totalMyDamage);

				double dt = 0;
				double realUpdateTick = 0;
				if (g_myStrat->enableFOW)
				{
					const DistributionMatrix::Cell &cell = g_myStrat->distributionMatrix.getCell(x, y);
					dt = g_myStrat->tick - cell.realUpdateTick;
					realUpdateTick = cell.realUpdateTick;				
					if (dt > 10)
					{
						pts /= sqr(dt / 10.0);
					}
				}
				
				oss << " PTS " << pts << " DT " << dt << " RUT " << realUpdateTick;
            }

            nvgText(vg, 10, textY, oss.str().c_str(), nullptr);
            textY += 15.0;
        }*/
    }

    int unitsCount[10] = {};

    for (const MyUnit &u: sim.units)
    {
        int ind = u.side * 5 + (int) u.type;
        unitsCount[ind]++;
    }

    for (int i = 0; i < 5; ++i)
    {
        int count1 = unitsCount[i];
        int count2 = unitsCount[5 + i];

        oss.str("");
        oss << getUnitTypeName((UnitType) i) << " " << count1 << " / " << count2;
        nvgText(vg, 10, textY, oss.str().c_str(), nullptr);
        textY += 15.0;
    }
    
    textY += 10;

    for (int i = 0; i < 5; ++i)
	{
		oss.str("");
		oss << getUnitTypeName((UnitType) i) << " D " << sim.unitStats[0].unitStats[i].died << "/" << sim.unitStats[1].unitStats[i].died
			<< " P " << sim.unitStats[0].unitStats[i].produced << "/" << sim.unitStats[1].unitStats[i].produced
			<< " DM " << sim.unitStats[0].unitStats[i].damageMade << "/" << sim.unitStats[1].unitStats[i].damageMade
			<< " NK " << sim.unitStats[0].unitStats[i].damageByNuke << "/" << sim.unitStats[1].unitStats[i].damageByNuke;
		
		nvgText(vg, 10, textY, oss.str().c_str(), nullptr);
		textY += 15.0;
	}
	
	oss.str("");
	oss << "Heal " << sim.unitStats[0].healed << "/" << sim.unitStats[1].healed;
	nvgText(vg, 10, textY, oss.str().c_str(), nullptr);
	textY += 15.0;
    textY += 10;
	
    if (g_myStrat)
    {
        for (const Group &g : g_myStrat->groups)
        {
            oss.str("");
            oss << getUnitTypeName(g.unitType) << " NES " << g.nukeEvadeStep << " SAN " << g.shrinkAfterNuke << " SA " << g.shrinkActive << " LAT " << g.lastUpdateTick;
            nvgText(vg, 10, textY, oss.str().c_str(), nullptr);
            textY += 15.0;
        }
    }

    /* P mousePos = (P(100, 100) / WIDTH - zoomCenter) * zoom + P(0.5, 0.5);
     P p = mousePos * P(WIDTH * SCALE, HEIGHT*SCALE);
     nvgText(vg, p.x, p.y, oss.str().c_str(), nullptr);*/

    nvgEndFrame(vg);
}

int renderTickMod = 1;

std::map<int, Simulator> history;

bool simulationEntered = false;
void enterSimulation(Simulator &sim);

void Renderer::finishRendering(Simulator &realSim, Strat *g_myStrat)
{
    static std::set<int> keys, pressedKeys;
    static bool pause = true;
    static double delay = 1.0;
    static bool lButton = false;
    static bool updateTitle = true;
	
	static int currentFrame = -1;
	
	if (!simulationEntered)
	{
		history.insert(std::make_pair(realSim.tick, realSim));
		currentFrame = realSim.tick;
	}

    do
    {
        pressedKeys.clear();
        SDL_Event e;
        while ( SDL_PollEvent(&e) )
        {
            if (e.type == SDL_KEYDOWN)
            {
                keys.insert(e.key.keysym.sym);
                pressedKeys.insert(e.key.keysym.sym);
                //std::cout << "PRESS " << e.key.keysym.sym << std::endl;
            }
            if (e.type == SDL_KEYUP)
            {
                keys.erase(e.key.keysym.sym);
            }

            if (e.type == SDL_MOUSEMOTION)
            {
                mousePos = P(e.motion.x, e.motion.y) / P(WIDTH * SCALE, HEIGHT*SCALE);
                if (lButton)
                {
                    zoomCenter -= P(e.motion.xrel, e.motion.yrel) / P(WIDTH * SCALE, HEIGHT*SCALE) / zoom;
                }
            }

            if (e.type == SDL_MOUSEBUTTONDOWN)
            {
                if (e.button.button == SDL_BUTTON_LEFT)
                    lButton = true;
            }

            if (e.type == SDL_MOUSEBUTTONUP)
            {
                if (e.button.button == SDL_BUTTON_LEFT)
                    lButton = false;
            }

            if (e.type == SDL_MOUSEWHEEL)
            {
                P zoomPoint = zoomCenter + (mousePos - P(0.5, 0.5))/zoom;

                int w = e.wheel.y;
                if (w > 0)
                {
                    for (int i = 0; i < w; ++i)
                        zoom *= 1.2;
                } else
                {
                    for (int i = 0; i < -w; ++i)
                        zoom /= 1.2;
                }

                if (zoom < 0.9)
                    zoom = 0.9;

                zoomCenter = zoomPoint - (mousePos - P(0.5, 0.5))/zoom;
            }
        }

        if (pressedKeys.count(SDLK_SPACE))
        {
            pause = !pause;
            keys.erase(SDLK_SPACE);
            SDL_Delay(200);
        }

        if (pressedKeys.count(SDLK_F1))
        {
            renderMode = RenderMode::NORMAL;
        }
        if (pressedKeys.count(SDLK_F2))
        {
            renderMode = RenderMode::SIMPLE;
        }
        if (pressedKeys.count(SDLK_F3))
        {
            renderMode = RenderMode::DANGER;
        }
        if (pressedKeys.count(SDLK_F12))
        {
            renderSim = !renderSim;
        }

        if (pressedKeys.count(SDLK_KP_0))
        {

        }

        if (pressedKeys.count(SDLK_KP_1))
        {
            updateTitle = !updateTitle;
        }

        if (pressedKeys.count(SDLK_KP_MULTIPLY))
        {

        }

        if (pressedKeys.count(SDLK_KP_PLUS))
        {
            renderTickMod++;
        }
        else if (pressedKeys.count(SDLK_KP_MINUS))
        {
            if (renderTickMod > 1)
                renderTickMod--;
        }

        if (keys.count(SDLK_UP))
        {
            delay /= 1.5;
        }

        if (keys.count(SDLK_DOWN))
        {
            delay *= 1.5;
        }

        if (pressedKeys.count(SDLK_ESCAPE))
        {
            exit(0);
        }

        glClearColor(1.0, 1.0, 1.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT);
        glClear(GL_DEPTH_BUFFER_BIT);
        updateZoom();
		
		Simulator *sim;
		
		if (!simulationEntered && currentFrame < realSim.tick)
		{
			auto it = history.lower_bound(currentFrame);
			if (it != history.end())
			{
				renderSimulator(it->second, g_myStrat);
				sim = &it->second;
			}
			else
			{
				renderSimulator(realSim, g_myStrat);
				sim = & realSim;
			}
		}
		else
		{
			sim = (renderSim || !g_myStrat) ? &realSim : g_myStrat;
			renderSimulator(*sim, g_myStrat);
		}

        std::ostringstream oss;
        P titlePos = (zoomCenter + (mousePos - P(0.5, 0.5))/zoom)*WIDTH;
        titlePos = clampP(titlePos, P(0, 0), P(WIDTH - 1, HEIGHT - 1));

        //double ptn = sim.attractionPoint(titlePos, UnitType::HELICOPTER, 0.0, false);
        double ptn = 0;
        if (g_myStrat)
        {
            if (g_myStrat->getGroup(UnitType::FIGHTER))
                ptn = g_myStrat->attractionPoint(titlePos, *g_myStrat->getGroup(UnitType::FIGHTER), 0.0, false);

            int cnt = g_myStrat->distributionMatrix.getCell(titlePos.x / 16, titlePos.y/16).count[1];
            oss << "CNT " << cnt;
            oss << " AA " << g_myStrat->getAvailableActions(12);
        }

        oss << titlePos << " PTN " << ptn << " NUKE " << sim->players[0].remainingNuclearStrikeCooldownTicks;
        SDL_SetWindowTitle(window, oss.str().c_str());


        SDL_GL_SwapWindow(window);
		
		if (!simulationEntered && pressedKeys.count(SDLK_LEFT))
        {
			if (currentFrame > 0)
				--currentFrame;
			
            keys.erase(SDLK_LEFT);
        }

        if (pressedKeys.count(SDLK_RIGHT))
        {
            keys.erase(SDLK_RIGHT);
			
			if (!simulationEntered && currentFrame < realSim.tick)
				++currentFrame;
			else
				return;
        }
        
        if (!simulationEntered && pressedKeys.count(SDLK_e))
		{
			simulationEntered = true;
			keys.erase(SDLK_e);
			enterSimulation(*sim);
		}
		
		if (simulationEntered && pressedKeys.count(SDLK_q))
		{
			simulationEntered = false;
			keys.erase(SDLK_q);
			return;
		}

        if (delay > 1)
            SDL_Delay((int) delay);
    } while(pause);
}

GLint getTextureFormat(SDL_Surface *image) {
    int nOfColors = image->format->BytesPerPixel;
    if (nOfColors == 4) {
        if (image->format->Rmask == 0x000000ff)
            return GL_RGBA;
        else
            return GL_BGRA;
    } else if (nOfColors == 3) {
        if (image->format->Rmask == 0x000000ff)
            return GL_RGB;
        else
            return GL_BGR;
    }

    return GL_RGB;
}

//=============================
#include "Runner.h"

#include <memory>

#include "MyStrategy.h"

using namespace model;
using namespace std;


Renderer renderer;
void doRender(Simulator &sim, Strat *g_myStrat)
{
    if (sim.tick % renderTickMod == 0)
    {
        renderer.startRendering();
        renderer.finishRendering(sim, g_myStrat);
    }
}

int main2(int argc, char* argv[]) {
    if (argc == 4) {
        Runner runner(argv[1], argv[2], argv[3]);
        runner.run();
    } else {
        Runner runner("127.0.0.1", "31001", "0000000000000000");
        runner.run();
    }

    return 0;
}

Runner::Runner(const char* host, const char* port, const char* token)
    : remoteProcessClient(host, atoi(port)), token(token) {
}

extern Strat g_sim;

void Runner::run() {
    remoteProcessClient.writeTokenMessage(token);
    remoteProcessClient.writeProtocolVersionMessage();
    remoteProcessClient.readTeamSizeMessage();
    Game game = remoteProcessClient.readGameContextMessage();

    unique_ptr<Strategy> strategy(new MyStrategy);

    shared_ptr<PlayerContext> playerContext;

    while ((playerContext = remoteProcessClient.readPlayerContextMessage()) != nullptr) {
        Player player = playerContext->getPlayer();

        Move move;
        strategy->move(player, playerContext->getWorld(), game, move);
        doRender(g_sim, &g_sim);

        remoteProcessClient.writeMoveMessage(move);
    }
}

//=================================

void generateInitalState(Simulator &sim, int rndSeed, bool useBuildings)
{
    Random rnd;
    rnd.m_w = rndSeed;

    PerlinNoise perlinGround(rnd.get_random());
    PerlinNoise perlinWeather(rnd.get_random());

    double gv[CELLS_X * CELLS_Y];
    double wv[CELLS_X * CELLS_Y];

    double minG = 100.0, maxG = -100.0;
    double minW = 100.0, maxW = -100.0;

    for (int y = 0; y < CELLS_Y; ++y)
    {
        for (int x = 0; x < CELLS_X; ++x)
        {
            double S = CELLS_Y * 0.1;
            double g = (perlinGround.noise((x + 0.5) / S, (y + 0.5) / S, 0.5) + perlinGround.noise((CELLS_X - x - 0.5) / S, (CELLS_Y - y - 0.5) / S, 0.5))*0.5;
            double w = (perlinWeather.noise((x + 0.5) / S, (y + 0.5) / S, 0.5) + perlinWeather.noise((CELLS_X - x - 0.5) / S, (CELLS_Y - y - 0.5) / S, 0.5))*0.5;
            gv[y * CELLS_X + x] = g;
            wv[y * CELLS_X + x] = w;

            minG = std::min(minG, g);
            maxG = std::max(maxG, g);
            minW = std::min(minW, w);
            maxW = std::max(maxW, w);
        }
    }

    for (int y = 0; y < CELLS_Y; ++y)
    {
        for (int x = 0; x < CELLS_X; ++x)
        {
            gv[y * CELLS_X + x] = (gv[y * CELLS_X + x] - minG) / (maxG - minG);
            wv[y * CELLS_X + x] = (wv[y * CELLS_X + x] - minW) / (maxW - minW);
        }
    }


    double groundLevel1 = rnd.getDouble() * 0.6;
    double groundLevel2 = rnd.getDouble() * 0.6;
    if (groundLevel1 > groundLevel2)
        std::swap(groundLevel1, groundLevel1);

    groundLevel1 += 0.4;
    groundLevel2 += 0.4;

    double weatherLevel1 = rnd.getDouble() * 0.6;
    double weatherLevel2 = rnd.getDouble() * 0.6;
    if (weatherLevel1 > weatherLevel2)
        std::swap(weatherLevel1, weatherLevel2);

    weatherLevel1 += 0.4;
    weatherLevel2 += 0.4;

    for (int y = 0; y < CELLS_Y; ++y)
    {
        for (int x = 0; x < CELLS_X; ++x)
        {
            Cell &cell = sim.cell(x, y);
            cell.groundType = GroundType::PLAIN;
            cell.weatherType = MyWeatherType::FINE;

            double g = gv[y * CELLS_X + x];

            if (g > groundLevel2)
                cell.groundType = GroundType::SWAMP;
            else if (g > groundLevel1)
                cell.groundType = GroundType::FOREST;

            double w = wv[y * CELLS_X + x];

            if (w > weatherLevel2)
                cell.weatherType = MyWeatherType::RAIN;
            else if (w > weatherLevel1)
                cell.weatherType = MyWeatherType::CLOUDY;

        }
    }

    int pos[9];
    for (int i = 0; i < 9; ++i)
        pos[i] = i;

    std::random_shuffle(pos, pos + 9, [&rnd](int n) {
        return rnd.get_random() % n;
    });

    long idCounter = 1;

    for (int k = 0; k < 5; ++k)
    {
        /*if (!isGroundUnit((UnitType) k))
        	continue;*/

        int x = pos[k] % 3;
        int y = pos[k] / 3;

        for (int i = 0; i < 10; ++i)
        {
        	for (int j = 0; j < 10; ++j)
        	{
        		MyUnit unit1;
        		unit1.pos = P(18 + x*74, 18 + y*74) + P(i*6, j*6);
        		unit1.side = 0;
        		unit1.type = (UnitType) k;
        		unit1.vel = P(0, 0);
        		unit1.durability = 100.0;
        		unit1.id = idCounter++;

        		sim.units.push_back(unit1);

        		unit1.side = 1;
        		unit1.pos = P(WIDTH, HEIGHT) - unit1.pos;
        		unit1.id = idCounter++;

        		sim.units.push_back(unit1);
        	}
        }
    }
    
   /* {
		MyUnit unit1;
		unit1.pos = P(300, 300);
		unit1.side = 0;
		unit1.type = UnitType::FIGHTER;
		unit1.vel = P(0, 0);
		unit1.durability = 100.0;
		unit1.id = idCounter++;

		sim.units.push_back(unit1);
		
		unit1.pos = P(800, 900);
		unit1.side = 1;
		unit1.type = UnitType::TANK;
		unit1.vel = P(0, 0);
		unit1.durability = 100.0;
		unit1.id = idCounter++;

		sim.units.push_back(unit1);
	}*/


    if (useBuildings)
    {
        std::set<int> buildings;
        std::set<int> slots;
        //int count = rnd.get_random() % 8 + 1;
		int count = rnd.get_random() % 4 + 5;
        for (int i = 0; i < count;)
        {
            int pos = rnd.get_random() % (31*31);
            int x = pos / 31 + 1;
            int y = pos % 31 + 1;

            if (slots.count(y*32 + x) || slots.count((y - 1)*32 + x) || slots.count((y - 1)*32 + x - 1) || slots.count(y*32 + x - 1))
                continue;

            x = 32 - x;
            y = 32 - y;

            if (slots.count(y*32 + x) || slots.count((y - 1)*32 + x) || slots.count((y - 1)*32 + x - 1) || slots.count(y*32 + x - 1))
                continue;

            buildings.insert(pos);
            slots.insert(y*32 + x);
            slots.insert((y - 1)*32 + x);
            slots.insert((y - 1)*32 + x - 1);
            slots.insert(y*32 + x - 1);

            x = 32 - x;
            y = 32 - y;

            buildings.insert(pos);

            slots.insert(y*32 + x);
            slots.insert((y - 1)*32 + x);
            slots.insert((y - 1)*32 + x - 1);
            slots.insert(y*32 + x - 1);

            ++i;
        }

        for (int pos : buildings)
        {
            Building b;
            //b.type = (rnd.get_random() % 2) ? BuildingType::VEHICLE_FACTORY : BuildingType::CONTROL_CENTER;
			b.type = BuildingType::VEHICLE_FACTORY;
			
            int x = pos / 31 + 1;
            int y = pos % 31 + 1;
            b.side = -1;

            b.pos = P(x, y) * 32;
            b.id = idCounter++;

            sim.buildings.push_back(b);

            x = 32 - x;
            y = 32 - y;

            b.pos = P(x, y) * 32;
            b.id = idCounter++;

            sim.buildings.push_back(b);
        }
    }

    /*{
		for (int i = 0; i < 1; ++i)
		{
			MyUnit unit1;
			unit1.pos = P(300 + i * 10, 100);
			unit1.side = 0;
			unit1.type = UnitType::FIGHTER;
			unit1.vel = P(0, 0);
			unit1.durability = 100.0;
			unit1.id = idCounter++;
			unit1.addGroup(90 + i);

			sim.units.push_back(unit1);
		}
    }*/

    /*for (int y = 0; y < 10; ++y)
    {
    	for (int x = 0; x < 10; ++x)
    	{
    		MyUnit unit1;
    		unit1.pos = P(100 + x*4, 100 + y*4);
    		unit1.side = 0;
    		unit1.type = UnitType::ARV;

    		unit1.vel = P(0, 0);
    		unit1.durability = 100.0;
    		unit1.id = idCounter++;

    		sim.units.push_back(unit1);
    	}
    }
    
    for (int y = 0; y < 10; ++y)
    {
    	for (int x = 0; x < 10; ++x)
    	{
    		MyUnit unit1;
    		unit1.pos = P(200 + x*4, 250 + y*4);
    		unit1.side = 1;
    		unit1.type = UnitType::IFV;

    		unit1.vel = P(0, 0);
    		unit1.durability = 100.0;
    		unit1.id = idCounter++;

    		sim.units.push_back(unit1);
    	}
    }*/

    /*for (int y = 0; y < 10; ++y)
    {
    	for (int x = 0; x < 10; ++x)
    	{
    		MyUnit unit1;
    		unit1.pos = P(550 - x*4, 650 + y*4);
    		unit1.side = 0;
    		unit1.type = UnitType::IFV;

    		unit1.vel = P(0, 0);
    		unit1.durability = 100.0;
    		unit1.id = idCounter++;

    		sim.units.push_back(unit1);
    	}
    }*/

    /*for (int y = 0; y < 10; ++y)
    {
    	for (int x = 0; x < 30; ++x)
    	{
    		MyUnit unit1;
    		unit1.pos = P(600 + x*4, 600 + (y - 5)*4);
    		unit1.side = 1;
    		int type = x % 3;
    		if (type == 0)
    			unit1.type = UnitType::TANK;
    		if (type == 1)
    			unit1.type = UnitType::ARV;
    		if (type == 2)
    			unit1.type = UnitType::IFV;

    		unit1.vel = P(0, 0);
    		unit1.durability = 100.0;
    		unit1.id = idCounter++;

    		sim.units.push_back(unit1);
    	}

    	for (int x = 0; x < 20; ++x)
    	{
    		MyUnit unit1;
    		unit1.pos = P(600 + x*4, 600 + (y - 5)*4);
    		unit1.side = 1;
    		int type = x % 2;
    		if (type == 0)
    			unit1.type = UnitType::HELICOPTER;
    		if (type == 1)
    			unit1.type = UnitType::FIGHTER;

    		unit1.vel = P(0, 0);
    		unit1.durability = 100.0;
    		unit1.id = idCounter++;

    		sim.units.push_back(unit1);
    	}
    }*/
}

//=================================

void enterSimulation(Simulator &sim)
{
	Strat myStrat;
	Strat myStrat2;
	Simulator simulator = sim;
	
	LOG("SIMULATION ENTER");
	
	for (int i = sim.tick; i < 20000; ++i) {
		renderer.startRendering();
		renderer.finishRendering(simulator, &myStrat2);
		if (!simulationEntered)
		{
			LOG("SIMULATION EXIT");
			return;
		}

		g_tick = simulator.tick;

		if (simulator.enableFOW)
			simulator.updateFOW(-1);

		{
			myStrat.synchonizeWith(simulator, 0);
			MyMove myMove = myStrat.nextMove();
			if (myMove.action != MyActionType::NONE)
			{
				simulator.registerMove(myMove, 0);
			}
		}

		{
			myStrat2.synchonizeWith(simulator, 1);
			MyMove myMove2 = myStrat2.nextMove();
			if (myMove2.action != MyActionType::NONE)
			{
				simulator.registerMove(myMove2, 1);
			}
		}
		
		simulator.step();
	}
	
	LOG("SIMULATION EXIT");
}

extern long g_tick;

int main(int argc, char* argv[]) {
	/*{
		Strat strat;
		
		MyUnit unit1;
		unit1.pos = P(400, 400);
		unit1.side = 0;
		unit1.type = UnitType::IFV;

		unit1.vel = P(1, 1).norm() * 0.4;
		//unit1.vel = P(0, 0);
		unit1.durability = 100.0;
		unit1.id = 1;
		unit1.addGroup(1);

		strat.units.push_back(unit1);
		
		unit1.pos = P(400, 414.1);
		unit1.vel = P(0, 0);
		unit1.removeGroup(1);
		unit1.addGroup(2);
		
		strat.units.push_back(unit1);
		
		strat.groups.clear();
		
		Group g;
		g.actionStarted = false;
		g.lastUpdateTick = 0;
		g.lastShrinkTick = 0;
		g.unitType = UnitType::IFV;
		g.group = 1;
		g.internalId = 1;
		strat.groups.push_back(g);
		
		g.group = 2;
		g.internalId = 2;
		strat.groups.push_back(g);
		
		strat.updateStats();
		
		Group &group = strat.groups[1];
		double R = 20 + unitVel(group.unitType) * 40;
				
		std::vector<const MyUnit *> groupUnits;
		std::vector<const MyUnit *> otherUnits;
		
		BBox bbox = group.bbox;
		bbox.p1 -= P(2.0*R, 2.0*R);
		bbox.p2 += P(2.0*R, 2.0*R);
		
		for (const MyUnit &u : strat.units)
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
				
				
		bool res = strat.canMoveDetailed(P(30, -30), strat.groups[1], groupUnits, otherUnits);
		LOG(res);
	}
	
	return 0;*/
	/////////////
	
    renderer.init();

    SDL_Surface *clouds = nullptr;
    if (!(clouds = SDL_LoadBMP("/media/denis/tmp/bb_ai/clouds.bmp")))
        return -1;

    SDL_Surface *rain = nullptr;
    if (!(rain = SDL_LoadBMP("/media/denis/tmp/bb_ai/rain.bmp")))
        return -1;

    SDL_Surface *forest = nullptr;
    if (!(forest = SDL_LoadBMP("/media/denis/tmp/bb_ai/forest2.bmp")))
        return -1;

    glGenTextures(1, &clouds_textureID);
    glBindTexture(GL_TEXTURE_2D, clouds_textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, clouds->w, clouds->h, 0, getTextureFormat(clouds), GL_UNSIGNED_BYTE, clouds->pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &rain_textureID);
    glBindTexture(GL_TEXTURE_2D, rain_textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, rain->w, rain->h, 0, getTextureFormat(rain), GL_UNSIGNED_BYTE, rain->pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &forest_textureID);
    glBindTexture(GL_TEXTURE_2D, forest_textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, forest->w, forest->h, 0, getTextureFormat(forest), GL_UNSIGNED_BYTE, forest->pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    vg = nvgCreateGL2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    nvgCreateFont(vg, "sans", "/media/denis/tmp/bb_ai/nanovg/nanovg/example/Roboto-Regular.ttf");

    int totalLeft = 0;
    int totalRight = 0;
    int w = 0, l = 0;

    bool doTests = true;
    bool useBuildings = true;
    bool render = false;
	bool fogOfWar = true;

    for (int test = 0; test < (doTests ? 50 : 1); ++test)
    {
        if (doTests)
        {
            Simulator simulator;

            int seed = 6871337 + test * 654377;
            generateInitalState(simulator, seed, useBuildings);

            simulator.tick = 0;
			simulator.enableFOW = fogOfWar;

            Strat myStrat;
			
			/*myStrat.groups.clear();
			Group g;
			g.group = 1;
			g.unitType = UnitType::TANK;
			myStrat.groups.push_back(g);*/
			
            //StratV14::Strat myStrat;

            //StratV8::Strat strat2;
            //StratV9::Strat strat2;
            //StratV10::Strat strat2;
            //StratV11::Strat strat2;
            //StratV13::Strat strat2;
            //StratV14::Strat strat2;
            //StratV15::Strat strat2;
			//StratV17::Strat strat2;
			//StratV18::Strat strat2;
			//StratV19::Strat strat2;
			//StratV20::Strat strat2;
			//StratV21::Strat strat2;
			//TestV1::Strat strat2;
			StratV22::Strat strat2;
			//StratV23::Strat strat2;


            for (int i = 0; i < 20000; ++i) {
                if (render && i % renderTickMod == 0)
                {
                    renderer.startRendering();
                    renderer.finishRendering(simulator, &myStrat);
                }

                g_tick = simulator.tick;

                //simulator.players[0].remainingNuclearStrikeCooldownTicks = 100000;
                //simulator.players[1].remainingNuclearStrikeCooldownTicks = 100000;
				
				if (simulator.enableFOW)
					simulator.updateFOW(-1);

                {
                    myStrat.synchonizeWith(simulator, 0);
                    MyMove myMove = myStrat.nextMove();
                    if (myMove.action != MyActionType::NONE)
                    {
                        simulator.registerMove(myMove, 0);
                    }
                }

                {
                    strat2.synchonizeWith(simulator, 1);
                    MyMove myMove2 = strat2.nextMove();
                    if (myMove2.action != MyActionType::NONE)
                    {
                        simulator.registerMove(myMove2, 1);
                    }
                }

                simulator.step();

                int counts[2] = {};
                for (const MyUnit &u : simulator.units)
                {
                    counts[u.side]++;
                }

                if (counts[0] == 0 || counts[1] == 0)
                    break;
				
				int total0 = counts[0] + simulator.players[0].score;
				int total1 = counts[1] + simulator.players[1].score;
				
				if (simulator.tick > 10000 && (total0 * 1.5 < total1 || total1 * 1.5 < total0))
					break;
				
				if (simulator.tick % 5000 == 1 && simulator.tick > 1000)
					LOG(simulator.players[0].score << " / " << simulator.players[1].score);
            }

            totalLeft += simulator.players[0].score;
            totalRight += simulator.players[1].score;
            if (simulator.players[0].score > simulator.players[1].score)
                w++;
            else
                l++;
            LOG("S " << seed << " SCORE " << simulator.players[0].score << " / " << simulator.players[1].score << " \t " << totalLeft << "/" << totalRight <<
                " \t " << w << "/" << l
               );

            //return 0;

        }
        else
        {
            return main2(argc, argv);
        }
    }
}
