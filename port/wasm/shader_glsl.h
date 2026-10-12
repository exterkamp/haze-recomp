#pragma once
#include <string>
// Translate the portable RSX engine's HLSL to WebGL2 GLSL ES, retaining
// semantic names for inter-stage linkage and vertex attribute binding.
bool haze_hlsl_to_glsl(const char* hlsl, bool vertex, std::string& source, std::string& error);
