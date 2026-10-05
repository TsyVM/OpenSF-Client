"""Carries the game's shaders into the build (Game/Render/Shaders.hpp).

    py Tools/shaders_gen.py <out.cpp> <shader dir> <name,name,...> <tools dir>

Each Client/Source/Shaders/<name>.hlsl goes in as its HLSL, as Shader Model 6 bytecode (DXIL, signed by
DXC) for Direct3D 12 and as GLSL ES for OpenGL ES, one program stage per entry: every function
named vs_* (vertex) or ps_* (pixel). The GLSL is made here, at build time, by DXC (HLSL to SPIR-V, the legacy HLSL rules FXC
uses) and SPIRV-Cross (SPIR-V to GLSL ES 3.00, or 3.10 where an entry needs it: multisampled
textures), then brought to the device's conventions (Engine/Render/Device.hpp):

  - clip-space depth 0..1 made OpenGL's -1..1, and the picture turned upside down when drawing
    into a texture (dev_Flip.x = -1), so a texture drawn into has its top row first, as in
    Direct3D, and reads back the same way the textures loaded from files do;
  - gl_FragCoord.y counted from the top in the window as well (dev_Flip.y + dev_Flip.z * y);
  - the stages' in-between values named by their HLSL semantics on both sides (v_TEXCOORD0),
    so the two stages link by name as OpenGL ES 3.00 wants.

A shader's registers (t#, s#, b#) are written beside it: the device finds each combined sampler's
texture and sampler slots (SPIRV_Cross_Combined<texture><sampler>) and each uniform block's slot
(type_<cbuffer>) by these names.
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ENTRY = re.compile(r"^[A-Za-z_][\w<>, ]*?\s+((?:vs|ps)_\w+)\s*\(", re.M)
RESOURCE = re.compile(r"\b(Texture\w*(?:<[^>]*>)?|SamplerState|SamplerComparisonState|cbuffer)\s+(\w+)\s*:\s*register\(\s*([tsb])(\d+)\s*\)")


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode, (r.stdout or "") + (r.stderr or "")


def glsl_for(tools, hlsl_path, entry, tmp):
    vertex = entry.startswith("vs_")
    spv = tmp / f"{hlsl_path.stem}_{entry}.spv"
    code, out = run([str(tools / "dxc.exe"), "-spirv", "-HV", "2018", "-fspv-target-env=vulkan1.0", "-fvk-use-gl-layout",
                     "-O3", "-T", ("vs_6_0" if vertex else "ps_6_0"), "-E", entry, str(hlsl_path), "-Fo", str(spv)])
    if code != 0:
        raise SystemExit(f"{hlsl_path.name} {entry}: DXC failed\n{out}")
    for version in (300, 310):
        cmd = [str(tools / "spirv-cross.exe"), str(spv), "--es", "--version", str(version)]
        if vertex:
            cmd += ["--fixup-clipspace", "--flip-vert-y"]
        code, out = run(cmd)
        # Multisampled textures are OpenGL ES 3.10's (SPIRV-Cross writes them into 3.00 regardless).
        if code == 0 and not (version == 300 and "sampler2DMS" in out):
            return version, out
    raise SystemExit(f"{hlsl_path.name} {entry}: SPIRV-Cross failed\n{out}")


def dxil_for(tools, hlsl_path, entry, tmp):
    # Shader Model 6.0 (every Direct3D 12 card and driver), the legacy HLSL rules the shaders were
    # written to; DXC signs the bytecode with the dxil.dll beside it.
    out = tmp / f"{hlsl_path.stem}_{entry}.dxil"
    code, msg = run([str(tools / "dxc.exe"), "-HV", "2018", "-O3", "-Qstrip_debug", "-Qstrip_reflect",
                     "-T", ("vs_6_0" if entry.startswith("vs_") else "ps_6_0"), "-E", entry, str(hlsl_path), "-Fo", str(out)])
    if code != 0:
        raise SystemExit(f"{hlsl_path.name} {entry}: DXC (DXIL) failed\n{msg}")
    data = out.read_bytes()
    if data[:4] != b"DXBC" or not any(data[4:20]):
        raise SystemExit(f"{hlsl_path.name} {entry}: the DXIL is not signed (dxil.dll missing beside dxc.exe?)")
    return data


def c_blob(data):
    rows = []
    for i in range(0, len(data), 40):
        rows.append(",".join(str(b) for b in data[i:i + 40]))
    return ",\n".join(rows)


def adapt(glsl, vertex):
    if vertex:
        flip = "gl_Position.y = -gl_Position.y;"
        if flip not in glsl:
            raise SystemExit("vertex entry without SPIRV-Cross's flip")
        glsl = glsl.replace(flip, "gl_Position.y *= dev_Flip.x;")
        glsl = re.sub(r"\bout_var_", "v_", glsl)
    else:
        glsl = re.sub(r"\bin_var_", "v_", glsl)
        glsl = re.sub(r"\bgl_FragCoord\b", "dev_FragCoord", glsl)
    lines = glsl.split("\n")
    # After the version, extensions and default precisions: the device's flip.
    at = 1
    while at < len(lines) and (lines[at].startswith(("#extension", "precision ")) or not lines[at].strip()):
        at += 1
    extra = ["uniform highp vec4 dev_Flip;"]
    if not vertex:
        extra.append("#define dev_FragCoord vec4(gl_FragCoord.x, dev_Flip.y + dev_Flip.z * gl_FragCoord.y, gl_FragCoord.zw)")
    lines[at:at] = extra
    return "\n".join(lines)


def c_bytes(text):
    data = text.encode("utf-8") + b"\0"
    rows = []
    for i in range(0, len(data), 40):
        rows.append(",".join(str(b) for b in data[i:i + 40]))
    return ",\n".join(rows)


def main():
    out, shader_dir, names, tools = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3].split(","), Path(sys.argv[4])
    body = ["// Made by Tools/shaders_gen.py from Client/Source/Shaders: do not edit.",
            '#include "Engine/Render/Device.hpp"', "", "namespace lsf::shaders {", ""]
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        for name in names:
            path = shader_dir / f"{name}.hlsl"
            hlsl = path.read_text(encoding="utf-8")
            regs = {"t": [], "s": [], "b": []}
            for kind, rname, reg, num in RESOURCE.findall(hlsl):
                regs[reg].append(f"{('type_' + rname) if reg == 'b' else rname}={num}")
            bindings = ";".join(f"{k}:{','.join(v)}" for k, v in regs.items())
            entries = sorted(set(ENTRY.findall(hlsl)))
            body.append(f"static const unsigned char k_{name}[] = {{{c_bytes(hlsl)}}};")
            rows, dxil_rows = [], []
            for e in entries:
                version, glsl = glsl_for(tools, path, e, tmp)
                glsl = adapt(glsl, e.startswith("vs_"))
                body.append(f"static const unsigned char k_{name}_{e}[] = {{{c_bytes(glsl)}}};")
                rows.append(f'    {{"{e}", reinterpret_cast<const char*>(k_{name}_{e}), {version}}},')
                blob = dxil_for(tools, path, e, tmp)
                body.append(f"static const unsigned char k_{name}_{e}_dxil[] = {{{c_blob(blob)}}};")
                dxil_rows.append(f'    {{"{e}", k_{name}_{e}_dxil, sizeof(k_{name}_{e}_dxil)}},')
            body.append(f"static const eng::GlslEntry k_{name}_glsl[] = {{")
            body += rows
            body.append("};")
            body.append(f"static const eng::DxilEntry k_{name}_dxil[] = {{")
            body += dxil_rows
            body.append("};")
            body.append(f'extern const eng::ShaderSource {name}{{"{name}", reinterpret_cast<const char*>(k_{name}), k_{name}_glsl, '
                        f'{len(rows)}, "{bindings}", k_{name}_dxil, {len(dxil_rows)}}};')
            body.append("")
    body.append("}  // namespace lsf::shaders")
    text = "\n".join(body) + "\n"
    if not out.exists() or out.read_text(encoding="utf-8") != text:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text, encoding="utf-8")
    print(f"shaders_gen: {len(names)} shaders -> {out}")


if __name__ == "__main__":
    main()
