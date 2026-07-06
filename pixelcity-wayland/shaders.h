#ifndef SHADERS_H
#define SHADERS_H

// ── A/B toggle ──────────────────────────────────────────────────────────────
// Set to 1 for GLSL 140 (texelFetch, no filtering), 0 for GLSL 120 baseline.
#define SHADER_VERSION_140  1

#if SHADER_VERSION_140

// ── GLSL 140 path ───────────────────────────────────────────────────────────
// Pi 400 V3D 4.2 supports GL 3.1 / GLSL 1.40.
// texelFetch     : bypasses the texture filtering unit (we sample at exact
//                  integer texel centres anyway, so filtering was wasted work).
// texelFetchOffset: offset is a compile-time constant so the TMU can
//                  pre-compute the address -- one less ALU per tap.

const char* bloomVertexShader =
    "#version 140\n"
    "in vec4 gl_Vertex;\n"
    "in vec4 gl_MultiTexCoord0;\n"
    "uniform mat4 gl_ModelViewProjectionMatrix;\n"
    "out vec2 vTexCoord;\n"
    "void main() {\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
    "    vTexCoord = gl_MultiTexCoord0.xy;\n"
    "}\n";

const char* bloomFragmentShader =
    "#version 140\n"
    "in vec2 vTexCoord;\n"
    "uniform sampler2D tex;\n"
    "uniform vec2 resolution;\n"
    "\n"
    "const int  SPREAD = 6;\n"
    "const vec3 TINT   = vec3(0.40, 0.23, 0.13);\n"
    "\n"
    "out vec4 fragColor;\n"
    "\n"
    "void main() {\n"
    "    ivec2 tc = ivec2(vTexCoord * resolution);\n"
    "    vec3 base = texelFetch(tex, tc, 0).rgb;\n"
    "\n"
    "    vec3 bloom = vec3(0.0);\n"
    "    vec3 s;\n"
    "    s = texelFetchOffset(tex, tc, 0, ivec2(-SPREAD, 0)).rgb; bloom += s*s;\n"
    "    s = texelFetchOffset(tex, tc, 0, ivec2( SPREAD, 0)).rgb; bloom += s*s;\n"
    "    s = texelFetchOffset(tex, tc, 0, ivec2(0, -SPREAD)).rgb; bloom += s*s;\n"
    "    s = texelFetchOffset(tex, tc, 0, ivec2(0,  SPREAD)).rgb; bloom += s*s;\n"
    "\n"
    "    fragColor = vec4(base + bloom * TINT, 1.0);\n"
    "}\n";

#else

// ── GLSL 120 baseline (original) ────────────────────────────────────────────
const char* bloomVertexShader = 
    "#version 120\n"
    "varying vec2 vTexCoord;\n"
    "void main() {\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
    "    vTexCoord = gl_MultiTexCoord0.xy;\n"
    "}\n";

const char* bloomFragmentShader =
    "#version 120\n"
    "varying vec2 vTexCoord;\n"
    "uniform sampler2D tex;\n"
    "uniform vec2 resolution;\n"
    "\n"
    "const float SPREAD = 6.0;\n"
    "const vec3  TINT   = vec3(0.40, 0.23, 0.13);\n"
    "\n"
    "void main() {\n"
    "    vec4 baseColor = texture2D(tex, vTexCoord);\n"
    "    \n"
    "    vec3 bloom = vec3(0.0);\n"
    "    vec2 offset = (1.0 / resolution) * SPREAD;\n"
    "    \n"
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

#endif

