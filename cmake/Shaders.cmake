# The game's shaders into a target (Game/Render/Shaders.hpp): Client/Source/Shaders/<name>.hlsl
# as HLSL (Direct3D 11 compiles it at start), as DXIL (Direct3D 12) and as GLSL ES (OpenGL and
# Android), one stage per vs_*/ps_* entry, made by Tools/shaders_gen.py at build time.
get_filename_component(LSF_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(LSF_SHADER_NAMES world post)
find_program(LSF_PYTHON NAMES py python3 python REQUIRED)

function(lsf_embed_shaders target)
    set(files "")
    foreach(name IN LISTS LSF_SHADER_NAMES)
        list(APPEND files "${LSF_ROOT}/Client/Source/Shaders/${name}.hlsl")
    endforeach()
    list(JOIN LSF_SHADER_NAMES "," list)   # commas: a list would split the command
    set(out "${CMAKE_BINARY_DIR}/generated/Shaders.cpp")
    add_custom_command(OUTPUT "${out}"
        COMMAND "${LSF_PYTHON}" "${LSF_ROOT}/Tools/shaders_gen.py" "${out}"
                "${LSF_ROOT}/Client/Source/Shaders" "${list}" "${LSF_ROOT}/vendor/ShaderTools"
        DEPENDS ${files} "${LSF_ROOT}/Tools/shaders_gen.py"
        COMMENT "Embedding the shaders (HLSL, DXIL and GLSL ES)")
    target_sources(${target} PRIVATE "${out}")
endfunction()
