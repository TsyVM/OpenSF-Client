// The game's shaders (Client/Source/Shaders/*.hlsl), carried in the executable (cmake/Shaders.cmake).
#pragma once

#include "Engine/Render/Device.hpp"

namespace lsf::shaders {

extern const eng::ShaderSource world;   // levels, props, the sky box, skinned models, lines
extern const eng::ShaderSource post;    // the picture out to the window, and Fidelity's finish (Render/FramePipe)

}  // namespace lsf::shaders
