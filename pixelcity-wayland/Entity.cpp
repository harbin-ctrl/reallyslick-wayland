/*-----------------------------------------------------------------------------

  Entity.cpp

  Copyright (c) 2005 Shamus Young
  All Rights Reserved

-------------------------------------------------------------------------------

  An entity is any renderable stationary object in the world.  This is an 
  abstract class.  This module gathers up the Entities, sorts them by 
  texture use and location, and then stores them in OpenGL render lists
  for faster rendering.    

-----------------------------------------------------------------------------*/

#ifdef WINDOWS
#include <windows.h>
#endif

#include <math.h>
#include <GL/gl.h>

#include <vector>
#include <algorithm>

#include "Camera.h"
#include "Entity.h"
#include "Macro.h"
#include "Math.h"
#include "Render.h"
#include "Texture.h"
#include "World.h"
#include "Visible.h"
#include "Win.h"
#include "Sky.h"
#include "time_util.h"

class entity
{
  private:

  class RefCounter {
    public:
      RefCounter(CEntity* o) : _obj(o), _refs(1) {};
      ~RefCounter() {delete _obj;};

      void AddRef() {_refs++;};
      void Release() {if (--_refs == 0) delete this;};

      CEntity& operator*() const {return *_obj;};
      CEntity* operator->() const {return _obj;};
      operator bool() const {return (!!_obj);};

    private:
      CEntity* _obj;
      int      _refs;

      // not callable
      RefCounter() : _obj(NULL) {};
      RefCounter(const RefCounter& rhs) : _obj(NULL) {};
      RefCounter& operator=(const RefCounter& rhs) {return *this;};
  };

  public:
  entity() {rc = NULL;};
  entity(CEntity* o) : rc(new RefCounter(o)) {};
  entity(const entity& rhs) : rc(rhs.rc) {if(rc) rc->AddRef();};
  ~entity() {rc->Release();}

  entity& operator=(const entity& rhs) {
    if(rc)
      rc->Release();

    rc = rhs.rc;

    if(rc)
      rc->AddRef();

    return *this;
  };

  CEntity& operator*() { return **rc; };
  RefCounter& operator->() { return *rc; };

  bool operator<(const entity& rhs) const {
    if(!rhs.rc)
      return true;
    if(!rc)
      return false;

    if ((*rc)->Alpha () && !(*rhs.rc)->Alpha ())
      return true;
    if (!(*rc)->Alpha () && (*rhs.rc)->Alpha ())
      return false;

    return (*rc)->Texture () < (*rhs.rc)->Texture ();
  };

  private:
  RefCounter *rc;
};


struct cell
{
    GLuint                    list_textured;
    GLuint                    list_textured_lod;
    GLuint                    list_flat;
  unsigned        list_flat_wireframe;
  unsigned        list_alpha;
  unsigned        list_logo;
  GLvector        pos;
};

typedef std::vector<entity> ent_list_t;

static cell       cell_list[GRID_SIZE][GRID_SIZE];
// Per-cell index into entity_list, built once (see build_buckets). Lets
// do_compile touch only the entities in the cell it is compiling instead of
// rescanning the entire (sorted) entity_list for every cell and every pass.
static std::vector<int> cell_bucket[GRID_SIZE][GRID_SIZE];
static ent_list_t entity_list;
static bool       sorted;
static bool       compiled;
static int        polycount;
static int        compile_x;
static int        compile_y;
static int        compile_count;
static int        compile_end;

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

/*static int do_compare (const void *arg1, const void *arg2 )
{

  struct entity*  e1 = (struct entity*)arg1;
  struct entity*  e2 = (struct entity*)arg2;

  if (e1->object->Alpha () && !e2->object->Alpha ())
    return 1;
  if (!e1->object->Alpha () && e2->object->Alpha ())
    return -1;
  if (e1->object->Texture () > e2->object->Texture ())
    return 1;
  else if (e1->object->Texture () < e2->object->Texture ())
    return -1;
  return 0;

}*/

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void add (CEntity* b)
{

  entity_list.push_back(entity(b));
  polycount = 0;

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

// Sort each entity into the grid cell its center falls in. Called once, right
// after entity_list is sorted (which groups by texture for batching). Because
// we walk the already-sorted list in order, every bucket inherits that texture
// grouping for free. This replaces the old do_compile scheme where every one of
// the six display-list passes rescanned the ENTIRE entity_list for every cell
// (GRID_SIZE^2 * 6 * N Center() calls) -- an O(cells * entities) blowup.
static void build_buckets ()
{
  for (int gx = 0; gx < GRID_SIZE; gx++)
    for (int gy = 0; gy < GRID_SIZE; gy++)
      cell_bucket[gx][gy].clear ();

  for (int idx = 0; idx < (int)entity_list.size (); idx++) {
    GLvector pos = entity_list[idx]->Center ();
    int gx = WORLD_TO_GRID(pos.x);
    int gy = WORLD_TO_GRID(pos.z);
    if (gx >= 0 && gx < GRID_SIZE && gy >= 0 && gy < GRID_SIZE)
      cell_bucket[gx][gy].push_back (idx);
  }
}

static void do_compile ()
{

  int                x, y;

  if (compiled)
    return;
  x = compile_x;
  y = compile_y;
  //Changing textures is pretty expensive, and thus sorting the entites so that
  //they are grouped by texture used can really improve framerate. The sort
  //happens once in EntityUpdate; build_buckets() then groups the sorted entities
  //by grid cell so each pass below only touches this cell's own entities.
  const std::vector<int>& bucket = cell_bucket[x][y];
  cell_list[x][y].pos = glVector (GRID_TO_WORLD(x), 0.0f, (float)y * GRID_RESOLUTION);

  //make a list for the textured objects in this region
  if (!cell_list[x][y].list_textured)
    cell_list[x][y].list_textured = glGenLists(1);
  glNewList (cell_list[x][y].list_textured, GL_COMPILE);
  for (int idx : bucket) {
    entity& e = entity_list[idx];
    if (!e->Alpha ()) {
      glBindTexture(GL_TEXTURE_2D, e->Texture ());
      e->Render ();
    }
  }
  glEndList();

  // LOD list for distant rendering
  if (!cell_list[x][y].list_textured_lod)
    cell_list[x][y].list_textured_lod = glGenLists(1);
  glNewList (cell_list[x][y].list_textured_lod, GL_COMPILE);
  for (int idx : bucket) {
    entity& e = entity_list[idx];
    if (!e->Alpha ()) {
      glBindTexture(GL_TEXTURE_2D, e->Texture ());
      e->RenderLOD ();
    }
  }
  glEndList();

  //Make a list of flat-color stuff (A/C units, ledges, roofs, etc.)
  if (!cell_list[x][y].list_flat)
    cell_list[x][y].list_flat = glGenLists(1);
  glNewList (cell_list[x][y].list_flat, GL_COMPILE);
  glEnable (GL_CULL_FACE);
  for (int idx : bucket) {
    entity& e = entity_list[idx];
    if (!e->Alpha ())
      e->RenderFlat (false);
  }
  glEndList();
  //Now a list of flat-colored stuff that will be wireframe friendly
  if (!cell_list[x][y].list_flat_wireframe)
    cell_list[x][y].list_flat_wireframe = glGenLists(1);
  glNewList (cell_list[x][y].list_flat_wireframe, GL_COMPILE);
  glEnable (GL_CULL_FACE);
  for (int idx : bucket) {
    entity& e = entity_list[idx];
    if (!e->Alpha ())
      e->RenderFlat (true);
  }
  glEndList();
  //Now a list of stuff to be alpha-blended, and thus rendered last. Building
  //signs (TEXTURE_LOGOS) are pulled into their own list so they can be drawn at
  //full distance -- there are only a handful and they are the landmark the user
  //looks for, whereas the rest of the alpha pass (radio towers, roof light
  //strips) is numerous/overdraw-heavy and stays distance-culled for fillrate.
  unsigned logo_tex = TextureId (TEXTURE_LOGOS);
  if (!cell_list[x][y].list_alpha)
    cell_list[x][y].list_alpha = glGenLists(1);
  glNewList (cell_list[x][y].list_alpha, GL_COMPILE);
  glDepthMask (false);
  glEnable (GL_BLEND);
  glDisable (GL_CULL_FACE);
  for (int idx : bucket) {
    entity& e = entity_list[idx];
    if (e->Alpha () && e->Texture () != logo_tex) {
      glBindTexture(GL_TEXTURE_2D, e->Texture ());
      e->Render ();
    }
  }
  glDepthMask (true);
  glEndList();

  //Building signs, drawn at full distance (see note above).
  if (!cell_list[x][y].list_logo)
    cell_list[x][y].list_logo = glGenLists(1);
  glNewList (cell_list[x][y].list_logo, GL_COMPILE);
  glDepthMask (false);
  glEnable (GL_BLEND);
  glDisable (GL_CULL_FACE);
  for (int idx : bucket) {
    entity& e = entity_list[idx];
    if (e->Alpha () && e->Texture () == logo_tex) {
      glBindTexture(GL_TEXTURE_2D, e->Texture ());
      e->Render ();
    }
  }
  glDepthMask (true);
  glEndList();

  //now walk the grid
  compile_x++;
  if (compile_x == GRID_SIZE) {
    compile_x = 0;
    compile_y++;
    if (compile_y == GRID_SIZE)
      compiled = true;
    compile_end = GetTimeInMillis ();
  } 
  compile_count++;


}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

bool EntityReady ()
{

  return compiled;

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

float EntityProgress ()
{

  return (float)compile_count / (GRID_SIZE * GRID_SIZE);

}


/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void EntityUpdate ()
{

  int stop_time;

  if (!TextureReady ()) {
    sorted = false;
    return;
  } 
  if (!sorted) {
    std::sort (entity_list.begin(), entity_list.end());
    build_buckets ();
    sorted = true;
  }
  //We want to do several cells at once. Enough to get things done, but
  //not so many that the program is unresponsive.
  if (LOADING_SCREEN) {  //If we're using a loading screen, we want to build as fast as possible
    stop_time = GetTimeInMillis () + 100;
    while (!compiled && GetTimeInMillis () < stop_time)
      do_compile ();
  } else //Take it slow
    do_compile ();

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void EntityRender ()
{

  int       polymode[2];
  bool      wireframe;
  int       x, y;
  int       elapsed;

  //Draw all textured objects
  glGetIntegerv (GL_POLYGON_MODE, &polymode[0]);
  wireframe = polymode[0] != GL_FILL;
  if (RenderFlat ())
    glDisable (GL_TEXTURE_2D);
  //If we're not using a loading screen, make the wireframe fade out via fog
  if (!LOADING_SCREEN && wireframe) {
    elapsed = 6000 - WorldSceneElapsed ();
    if (elapsed >= 0 && elapsed <= 6000)
      RenderFogFX ((float)elapsed / 6000.0f);
    else
      return;
  }
  GLvector cam_pos = CameraPosition();
  
  struct cell_dist {
      int x, y;
      float dist_sq;
  };
  static cell_dist sorted_cells[GRID_SIZE * GRID_SIZE];
  int visible_count = 0;
  
  for (x = 0; x < GRID_SIZE; x++) {
    for (y = 0; y < GRID_SIZE; y++) {
      if (Visible(x, y)) {
        sorted_cells[visible_count].x = x;
        sorted_cells[visible_count].y = y;
        float dx = (x * GRID_RESOLUTION) - cam_pos.x;
        float dz = (y * GRID_RESOLUTION) - cam_pos.z;
        sorted_cells[visible_count].dist_sq = dx*dx + dz*dz;
        visible_count++;
      }
    }
  }
  
  // Sort front-to-back
  qsort(sorted_cells, visible_count, sizeof(cell_dist), [](const void* a, const void* b) -> int {
      float da = ((const cell_dist*)a)->dist_sq;
      float db = ((const cell_dist*)b)->dist_sq;
      if (da > db) return 1;
      if (da < db) return -1;
      return 0;
  });

  // Always draw the full mesh. The LOD proxy box covers the same screen pixels
  // as the real building, so it saves no fill-rate (the actual bottleneck) --
  // benchmarks showed it was marginally *slower*. Its only visible effect was a
  // window-pattern "pop" as buildings crossed the threshold, because a box proxy
  // cannot reproduce the perimeter/tiered window UVs of tower & modern shapes.
  // The LOD mesh is still built (see CBuilding::CreateLOD) but is no longer used.
  //
  // Push wall faces slightly away from the camera so coplanar flat detail
  // (foundations, tier lids, ledges) wins the depth test -- this kills the
  // mid-distance z-fight on those bands. We bias the walls (not the flat
  // details) so the overhanging roof caps stay at their true depth and don't
  // clip the roof-edge lights/trim drawn on top of them later.
  if (!wireframe) {
      glEnable (GL_POLYGON_OFFSET_FILL);
      glPolygonOffset (1.0f, 1.0f);
  }
  for (int i = 0; i < visible_count; i++) {
      glCallList(cell_list[sorted_cells[i].x][sorted_cells[i].y].list_textured);
  }
  if (!wireframe) {
      glPolygonOffset (0.0f, 0.0f);
      glDisable (GL_POLYGON_OFFSET_FILL);
  }

  //draw all flat colored objects (drawn at true depth; the wall pass above is
  //biased back so these coplanar details win without moving toward the camera)
  glBindTexture(GL_TEXTURE_2D, 0);
  glColor3f (0, 0, 0);
  for (int i = 0; i < visible_count; i++) {
      if (wireframe)
          glCallList (cell_list[sorted_cells[i].x][sorted_cells[i].y].list_flat_wireframe);
      else
          glCallList (cell_list[sorted_cells[i].x][sorted_cells[i].y].list_flat);
  }

  //draw all alpha-blended objects (back-to-front for proper blending)
  glBindTexture(GL_TEXTURE_2D, 0);
  glColor3f (0, 0, 0);
  glEnable (GL_BLEND);
  float max_alpha_dist = 80.0f; // Alpha windows vanish into fog quickly
  float max_alpha_dist_sq = max_alpha_dist * max_alpha_dist;

  for (int i = visible_count - 1; i >= 0; i--) {
      if (sorted_cells[i].dist_sq < max_alpha_dist_sq) {
          glCallList (cell_list[sorted_cells[i].x][sorted_cells[i].y].list_alpha);
      }
  }

  //Building signs render at full distance -- they are few and are the landmark
  //the user looks for. Back-to-front (same sorted order) for correct blending.
  glDepthMask (false);
  for (int i = visible_count - 1; i >= 0; i--) {
      glCallList (cell_list[sorted_cells[i].x][sorted_cells[i].y].list_logo);
  }
  glDepthMask (true);
}




/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void EntityClear ()
{

  entity_list.clear();

  compile_x = 0;
  compile_y = 0;
  compile_count = 0;
  compiled = false;
  sorted = false;

  int  x, y;

  for (x = 0; x < GRID_SIZE; x++) {
    for (y = 0; y < GRID_SIZE; y++) {
      glNewList (cell_list[x][y].list_textured, GL_COMPILE);
      glEndList();	
      glNewList (cell_list[x][y].list_alpha, GL_COMPILE);
      glEndList();	
      glNewList (cell_list[x][y].list_flat_wireframe, GL_COMPILE);
      glEndList();	
      glNewList (cell_list[x][y].list_flat, GL_COMPILE);
      glEndList();
      if (cell_list[x][y].list_logo) {
        glNewList (cell_list[x][y].list_logo, GL_COMPILE);
        glEndList();
      }
    }
  }


}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

int EntityCount ()
{

  return entity_list.size();

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void EntityInit (void)
{

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

int EntityPolyCount (void)
{

  if (!sorted)
    return 0;
  if (polycount)
    return polycount;
  for (ent_list_t::iterator i = entity_list.begin(); i < entity_list.end(); ++i) 
    polycount += (*i)->PolyCount ();
  return polycount;

}


/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

CEntity::CEntity (void)
{

  add (this);

}

void CEntity::Render (void)
{

}

void CEntity::RenderFlat (bool wireframe)
{

}

void CEntity::Update (void)
{


}


