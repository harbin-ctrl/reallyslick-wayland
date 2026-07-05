#ifndef SHADERS_H
#define SHADERS_H

const char* bloomVertexShader = 
    "#version 120\n"
    "varying vec2 vTexCoord;\n"
    "void main() {\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
    "    vTexCoord = gl_MultiTexCoord0.xy;\n"
    "}\n";

// The original PixelCity bloom was WIDE, SOFT and WARM (a ~15px cross of additive
// copies of the squared bright-pass, tinted with a warm reddish colour). This
// 4-tap version keeps that budget but reaches for the same look purely by tuning
// three constants -- no extra taps / passes:
//   SPREAD : how far the halo reaches from a bright source (wider = softer).
//   TINT   : warm colour * intensity applied to the accumulated bloom. Ratio
//            leans red (R > G > B); lower magnitude = softer / less intense.
// The taps are a cross, so a very large SPREAD would detach the halo into a ring;
// this is the widest that still reads as a connected glow on 4 taps.
const char* bloomFragmentShader =
    "#version 120\n"
    "varying vec2 vTexCoord;\n"
    "uniform sampler2D tex;\n"
    "uniform vec2 resolution;\n"
    "\n"
    "const float SPREAD = 6.0;\n"                        // was 3.0 -- wider, softer
    "const vec3  TINT   = vec3(0.40, 0.23, 0.13);\n"     // warmer (redder) + a bit dimmer
    "\n"
    "void main() {\n"
    "    vec4 baseColor = texture2D(tex, vTexCoord);\n"
    "    \n"
    "    // Thresholded bloom: square the color to isolate bright spots\n"
    "    vec3 bloom = vec3(0.0);\n"
    "    vec2 offset = (1.0 / resolution) * SPREAD;\n"
    "    \n"
    "    // 4-tap cross blur to save fillrate\n"
    "    vec3 s;\n"
    "    s = texture2D(tex, vTexCoord + vec2(-offset.x,  0.0     )).rgb; bloom += s*s;\n"
    "    s = texture2D(tex, vTexCoord + vec2( offset.x,  0.0     )).rgb; bloom += s*s;\n"
    "    s = texture2D(tex, vTexCoord + vec2( 0.0,      -offset.y)).rgb; bloom += s*s;\n"
    "    s = texture2D(tex, vTexCoord + vec2( 0.0,       offset.y)).rgb; bloom += s*s;\n"
    "    \n"
    "    vec3 finalColor = baseColor.rgb + bloom * TINT;\n"
    "    \n"
    "    gl_FragColor = vec4(finalColor, 1.0);\n"
    "}\n";

#endif
