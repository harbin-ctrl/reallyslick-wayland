#ifndef SHADERS_H
#define SHADERS_H

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
    "void main() {\n"
    "    vec4 baseColor = texture2D(tex, vTexCoord);\n"
    "    \n"
    "    // Thresholded bloom: square the color to isolate bright spots\n"
    "    vec3 bloom = vec3(0.0);\n"
    "    vec2 offset = (1.0 / resolution) * 3.0;\n" // Spread the blur out
    "    \n"
    "    // 4-tap cross blur to save fillrate\n"
    "    vec3 s;\n"
    "    s = texture2D(tex, vTexCoord + vec2(-offset.x,  0.0     )).rgb; bloom += s*s;\n"
    "    s = texture2D(tex, vTexCoord + vec2( offset.x,  0.0     )).rgb; bloom += s*s;\n"
    "    s = texture2D(tex, vTexCoord + vec2( 0.0,      -offset.y)).rgb; bloom += s*s;\n"
    "    s = texture2D(tex, vTexCoord + vec2( 0.0,       offset.y)).rgb; bloom += s*s;\n"
    "    \n"
    "    // Precalculated: bloom * 0.25 * vec3(1.3, 0.9, 0.6) * 1.5\n"
    "    vec3 finalColor = baseColor.rgb + bloom * vec3(0.4875, 0.3375, 0.225);\n"
    "    \n"
    "    gl_FragColor = vec4(finalColor, 1.0);\n"
    "}\n";

#endif
