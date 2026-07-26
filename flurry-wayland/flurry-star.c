/*

Copyright (c) 2002, Calum Robinson
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the author nor the names of its contributors may be used
  to endorse or promote products derived from this software without specific
  prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

/* Star.c: implementation of the Star class. */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "flurry.h"

/* Construction/Destruction */

void InitStar(Star *s)
{
    int i;
    for (i=0;i<3;i++) {
        s->position[i] = RandFlt(-10000.0, 10000.0);
    }
    s->rotSpeed = RandFlt(0.4, 0.9);
    s->mystery = RandFlt(0.0, 10.0);
}

#define BIGMYSTERY 1800.0
#define MAXANGLES 16384

void UpdateStar(global_info_t *global, flurry_info_t *flurry, Star *s)
{
    float rotationsPerSecond = (2.0f * (float)PI * 12.0f / (float)MAXANGLES) * s->rotSpeed;
    float thisPointInRadians;
    float thisAngle = (float)(flurry->fTime * (double)rotationsPerSecond);
    float cf;
    float tmpX1,tmpY1,tmpZ1;
    float tmpX2,tmpY2,tmpZ2;
    float tmpX3,tmpY3,tmpZ3;
    float tmpX4,tmpY4,tmpZ4;
    float rotation;
    float cr;
    float sr;

    s->ate = 0;
    
    cf = (cosf(7.0f * (float)(flurry->fTime * (double)rotationsPerSecond)) +
          cosf(3.0f * (float)(flurry->fTime * (double)rotationsPerSecond)) +
          cosf(13.0f * (float)(flurry->fTime * (double)rotationsPerSecond)));
    cf /= 6.0f;
    cf += 0.75f; 
    thisPointInRadians = 2.0f * (float)PI * (float)s->mystery / (float)BIGMYSTERY;
    
    s->position[0] = 250.0f * cf * cosf(11.0f * (thisPointInRadians + 3.0f * thisAngle));
    s->position[1] = 250.0f * cf * sinf(12.0f * (thisPointInRadians + 4.0f * thisAngle));
    s->position[2] = 250.0f * cosf(23.0f * (thisPointInRadians + 12.0f * thisAngle));
    
    rotation = thisAngle * 0.501f + 5.01f * (float)s->mystery / (float)BIGMYSTERY;
    cr = cosf(rotation);
    sr = sinf(rotation);
    tmpX1 = s->position[0] * cr - s->position[1] * sr;
    tmpY1 = s->position[1] * cr + s->position[0] * sr;
    tmpZ1 = s->position[2];
    
    tmpX2 = tmpX1 * cr - tmpZ1 * sr;
    tmpY2 = tmpY1;
    tmpZ2 = tmpZ1 * cr + tmpX1 * sr;
    
    tmpX3 = tmpX2;
    tmpY3 = tmpY2 * cr - tmpZ2 * sr;
    tmpZ3 = tmpZ2 * cr + tmpY2 * sr + seraphDistance;
    
    rotation = thisAngle * 2.501f + 85.01f * (float)s->mystery / (float)BIGMYSTERY;
    cr = cosf(rotation);
    sr = sinf(rotation);
    tmpX4 = tmpX3 * cr - tmpY3 * sr;
    tmpY4 = tmpY3 * cr + tmpX3 * sr;
    tmpZ4 = tmpZ3;
    
    s->position[0] = tmpX4;
    s->position[1] = tmpY4;
    s->position[2] = tmpZ4;
}
