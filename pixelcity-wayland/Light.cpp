/*-----------------------------------------------------------------------------

  Light.cpp

  2006 Shamus Young

-------------------------------------------------------------------------------

  This tracks and renders the light sources. (Note that they do not really 
  CAST light in the OpenGL sense of the world, these are just simple panels.) 
  These are NOT subclassed to entities because these are dynamic.  Some lights 
  blink, and thus they can't go into the fixed render lists managed by 
  Entity.cpp.  

-----------------------------------------------------------------------------*/

#define MAX_SIZE            5

#ifdef WINDOWS
#include <windows.h>
#endif

#include <math.h>
#include <GL/gl.h>
#include <GL/glu.h>

#if defined(WINDOWS) && _MSC_VER <= 1200
#include <GL/glaux.h>
#endif

#include "glTypes.h"

#include "Camera.h"
#include "Entity.h"
#include "Light.h"
#include "Macro.h"
#include "Math.h"
#include "Random.h"
#include "Render.h"
#include "Texture.h"
#include "World.h"
#include "Visible.h"
#include "Win.h"
#include "time_util.h"

static GLvector2      angles[5][360];
static CLight*        light_grid[GRID_SIZE][GRID_SIZE];
static bool           angles_done;
static int            count;

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void LightClear ()
{

  CLight*   l;

  for (int x = 0; x < GRID_SIZE; x++) {
    for (int y = 0; y < GRID_SIZE; y++) {
      while (light_grid[x][y]) {
        l = light_grid[x][y];
        light_grid[x][y] = l->_next;
        delete l;
      }
    }
  }
  count = 0;

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

int LightCount ()
{

  return count;

}


/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void LightRender ()
{

  if (!EntityReady ())
    return;
  if (!angles_done) {
    for (int size = 0; size < MAX_SIZE; size++) {
      for (int i = 0 ;i < 360; i++) {
        angles[size][i].x = cosf ((float)i * DEGREES_TO_RADIANS) * ((float)size + 0.5f);
        angles[size][i].y = sinf ((float)i * DEGREES_TO_RADIANS) * ((float)size + 0.5f);
      }
    }
    angles_done = true;   // table is constant; only build it once
  }
  glDepthMask (false);
  glEnable (GL_BLEND);
  glDisable (GL_CULL_FACE);
  glBlendFunc (GL_ONE, GL_ONE);
  glBindTexture(GL_TEXTURE_2D, TextureId (TEXTURE_LIGHT));
  glDisable (GL_CULL_FACE);
  glBegin (GL_QUADS);
  for (int x = 0; x < GRID_SIZE; x++) {
    for (int y = 0; y < GRID_SIZE; y++) {
      if (Visible(x, y)) {
        for (CLight* l = light_grid[x][y]; l; l = l->_next) {
          l->Render();
        }
      }
    }
  }
  glEnd ();
  glDepthMask (true);

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

CLight::CLight (GLvector pos, GLrgba color, int size)
{

  _position = pos;
  _color = color;
  _size = CLAMP (size, 0, (MAX_SIZE - 1));
  _vert_size = (float)_size + 0.5f;
  _flat_size = _vert_size + 0.5f;
  _blink = false;
  _cell_x = CLAMP(WORLD_TO_GRID(pos.x), 0, GRID_SIZE - 1);
  _cell_z = CLAMP(WORLD_TO_GRID(pos.z), 0, GRID_SIZE - 1);
  _next = light_grid[_cell_x][_cell_z];
  light_grid[_cell_x][_cell_z] = this;
  count++;


}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void CLight::Blink ()
{

  _blink = true;
  //we don't want blinkers to be in sync, so have them blink at 
  //slightly different rates. (Milliseconds)
  _blink_interval = 1500 + RandomVal (500);

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void CLight::Render ()
{

  int       angle;
  GLvector  pos;
  GLvector  camera;
  GLvector  camera_position;
  GLvector2 offset;

  // Visible check now handled by LightRender
  camera = CameraAngle ();
  camera_position = CameraPosition ();
  if (fabs (camera_position.x - _position.x) > RenderFogDistance ())
    return;
  if (fabs (camera_position.z - _position.z) > RenderFogDistance ())
    return;
  if (_blink && (GetTimeInMillis () % _blink_interval) > 200)
    return;
  // MathAngle() can return exactly 360.0 for negative multiples of 360, which
  // would index one past angles[][360]. Clamp to keep the read in bounds.
  angle = (int)MathAngle (camera.y);
  if (angle < 0 || angle >= 360)
    angle = ((angle % 360) + 360) % 360;
  offset = angles[_size][angle];
  pos = _position;
  glColor4fv (&_color.red);
  glTexCoord2f (0, 0);   
  glVertex3f (pos.x + offset.x, pos.y - _vert_size, pos.z + offset.y);
  glTexCoord2f (0, 1);   
  glVertex3f (pos.x - offset.x, pos.y - _vert_size, pos.z - offset.y);
  glTexCoord2f (1, 1);   
  glVertex3f (pos.x - offset.x, pos.y + _vert_size, pos.z - offset.y);
  glTexCoord2f (1, 0);   
  glVertex3f (pos.x + offset.x, pos.y + _vert_size, pos.z + offset.y);

}
