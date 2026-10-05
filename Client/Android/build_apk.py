"""Builds Soldier Front Legacy for Android: bin/android/legacysf.apk.

    py Client/Android/build_apk.py [--abis arm64-v8a,armeabi-v7a,x86_64] [--data <Soldier Front data>] [--install]

Needs the Android SDK in %LOCALAPPDATA%\\Android\\Sdk (build-tools 36.1.0, platform android-36,
NDK 29) and a JDK (Android Studio's, or C:\\Program Files\\Android\\openjdk). No Gradle:

  1. liblegacysf.so per ABI: Client/Android/CMakeLists.txt with the NDK's toolchain (VanGUI's
     libraries for the ABI come from vendor/VanGUI/lib/android-<abi>, built from VanGUI's source
     when missing); stripped for the APK, the unstripped copy kept in symbols/android/<version>/<abi>;
  2. the Java side (Client/Android/java) compiled against android.jar and turned into classes.dex;
  3. the manifest and resources linked by aapt2 into the base APK;
  4. classes.dex, lib/<abi>/liblegacysf.so and, with --data, the Soldier Front game data
     (assets/data, deflated, and assets/data.list), zipaligned, signed by apksigner with the release
     key in Client/Android/keystore (made on the first build; keep it: an update only installs
     over the old app when signed with the same key).

Without --data the APK is small and carries no game data: copy the data onto the phone instead
(--install does both: adb install, then adb push of the data folder into the app's own folder).
No server of any kind is in it (PF-1).
"""
import argparse
import datetime
import os
import secrets
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent            # Client/Android
ROOT = HERE.parents[1]                            # SF-Rewritten
SDK = Path(os.environ["LOCALAPPDATA"]) / "Android" / "Sdk"
NDK = SDK / "ndk" / "29.0.14206865"
BUILD_TOOLS = SDK / "build-tools" / "36.1.0"
ANDROID_JAR = SDK / "platforms" / "android-36" / "android.jar"
ADB = SDK / "platform-tools" / "adb.exe"
NINJA = Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe")
VANGUI_SRC = ROOT.parents[1] / "Projects" / "VanGUI"
MIN_SDK, TARGET_SDK = 26, 35
PACKAGE = "org.teamvanilla.legacysf"
LIB = "liblegacysf.so"
DATA_FOLDERS = ["area", "clan", "effect", "force", "lobby", "menu", "scr", "sound", "weapon"]


def find_jdk():
    for base in [os.environ.get("JAVA_HOME", ""), r"C:\Program Files\Android\openjdk", r"C:\Program Files\Android\Android Studio\jbr"]:
        if not base:
            continue
        b = Path(base)
        for cand in [b] + sorted(b.glob("jdk-*"), reverse=True):
            if (cand / "bin" / "javac.exe").exists():
                return cand
    raise SystemExit("No JDK found (set JAVA_HOME)")


def run(cmd, env=None, cwd=None, quiet=False):
    r = subprocess.run([str(c) for c in cmd], env=env, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        print((r.stdout or "")[-12000:])
        print((r.stderr or "")[-6000:])
        raise SystemExit(f"FAILED: {' '.join(str(c) for c in cmd)[:300]}")
    if not quiet and r.stdout.strip():
        print(r.stdout.strip()[-2000:])
    return r.stdout


def cmake_ndk(src, build, abi, extra=()):
    if not (build / "build.ninja").exists():
        run(["cmake", "-S", src, "-B", build, "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={NINJA}",
             f"-DCMAKE_TOOLCHAIN_FILE={NDK / 'build' / 'cmake' / 'android.toolchain.cmake'}", f"-DANDROID_ABI={abi}",
             f"-DANDROID_PLATFORM={MIN_SDK}", "-DANDROID_STL=c++_static", "-DCMAKE_BUILD_TYPE=Release", *extra], quiet=True)


def build_vangui(abi):
    dst = ROOT / "vendor" / "VanGUI" / "lib" / f"android-{abi}"
    if (dst / "libvangui.a").exists() and (dst / "libvangui_suite.a").exists():
        return
    print(f"VanGUI for {abi} (from {VANGUI_SRC})")
    b = ROOT / "build" / f"vangui-android-{abi}"
    compat = (HERE / "cmake" / "vangui_compat.h").as_posix()
    cmake_ndk(VANGUI_SRC, b, abi, ["-DVANGUI_BUILD_SUITE=ON", f"-DCMAKE_CXX_FLAGS=-include {compat}"])
    run(["cmake", "--build", b, "--target", "vangui", "vangui_suite"], quiet=True)
    dst.mkdir(parents=True, exist_ok=True)
    for lib in ("libvangui.a", "libvangui_suite.a"):
        shutil.copy2(next(b.rglob(lib)), dst / lib)


def build_native(abi):
    build_vangui(abi)
    b = ROOT / "build" / f"android-{abi}"
    cmake_ndk(HERE, b, abi)
    print(f"{LIB} for {abi}")
    run(["cmake", "--build", b], quiet=True)
    return b / LIB


def build_java(out, jdk):
    classes = out / "classes"
    shutil.rmtree(classes, ignore_errors=True)
    classes.mkdir(parents=True)
    sources = sorted((HERE / "java").rglob("*.java"))
    run([jdk / "bin" / "javac.exe", "--release", "11", "-nowarn", "-classpath", ANDROID_JAR, "-d", classes, *sources], quiet=True)
    env = dict(os.environ, JAVA_HOME=str(jdk), PATH=str(jdk / "bin") + os.pathsep + os.environ["PATH"])
    dex = out / "dex"
    shutil.rmtree(dex, ignore_errors=True)
    dex.mkdir()
    run([BUILD_TOOLS / "d8.bat", "--release", "--min-api", str(MIN_SDK), "--lib", ANDROID_JAR, "--output", dex,
         *sorted(classes.rglob("*.class"))], env=env, quiet=True)
    return dex / "classes.dex"


def keystore(jdk):
    ks_dir = HERE / "keystore"
    ks = ks_dir / "legacysf-release.jks"
    pw_file = ks_dir / "password.txt"
    if not ks.exists():
        ks_dir.mkdir(parents=True, exist_ok=True)
        pw = secrets.token_urlsafe(24)
        pw_file.write_text(pw + "\n", encoding="utf-8")
        run([jdk / "bin" / "keytool.exe", "-genkeypair", "-keystore", ks, "-alias", "legacysf", "-keyalg", "RSA", "-keysize", "4096",
             "-validity", "36500", "-storepass", pw, "-keypass", pw, "-dname", "CN=TeamVanilla, O=TeamVanilla"], quiet=True)
        print(f"NEW RELEASE KEY: {ks} (its password in {pw_file.name}). Keep both: updates must be signed with it.")
    return ks, pw_file.read_text(encoding="utf-8").strip()


def data_files(data):
    """The game data's archives, as SetupActivity copies them: (path under data/, file)."""
    out = []
    for folder in DATA_FOLDERS:
        d = Path(data) / folder
        if not d.is_dir():
            continue
        for f in sorted(d.iterdir()):
            if f.is_file() and f.suffix.lower() in (".sff", ".mrg"):
                out.append((f"{folder}/{f.name}", f))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--abis", default="arm64-v8a")
    ap.add_argument("--data", default="", help="the Soldier Front data folder, carried in the APK")
    ap.add_argument("--push-data", default="", help="with --install: this data folder is copied onto the phone by adb")
    ap.add_argument("--install", action="store_true", help="adb install the APK on the phone plugged in")
    ap.add_argument("--out", default=str(ROOT / "bin" / "android" / "legacysf.apk"))
    args = ap.parse_args()
    abis = [a for a in args.abis.split(",") if a]
    jdk = find_jdk()
    now = datetime.datetime.now()
    # Minutes since 2025: rises with every build (Android refuses to install a lower one over a higher).
    version_code = int((now - datetime.datetime(2025, 1, 1)).total_seconds() // 60)
    version_name = f"1.0.{now.strftime('%y%m%d.%H%M')}"
    work = ROOT / "build" / "apk"
    work.mkdir(parents=True, exist_ok=True)

    libs = {abi: build_native(abi) for abi in abis}
    symbols = ROOT / "symbols" / "android" / version_name
    stripped = {}
    strip = NDK / "toolchains" / "llvm" / "prebuilt" / "windows-x86_64" / "bin" / "llvm-strip.exe"
    for abi, so in libs.items():
        (symbols / abi).mkdir(parents=True, exist_ok=True)
        shutil.copy2(so, symbols / abi / LIB)
        out = work / "lib" / abi / LIB
        out.parent.mkdir(parents=True, exist_ok=True)
        run([strip, "--strip-unneeded", "-o", out, so], quiet=True)
        stripped[abi] = out
        print(f"  {abi}: {so.stat().st_size / 1e6:.1f} MB -> {out.stat().st_size / 1e6:.1f} MB stripped")

    print("Java")
    dex = build_java(work, jdk)

    print("Resources and manifest")
    aapt2 = BUILD_TOOLS / "aapt2.exe"
    res_zip = work / "res.zip"
    run([aapt2, "compile", "--dir", HERE / "res", "-o", res_zip], quiet=True)
    base = work / "base.apk"
    run([aapt2, "link", "-o", base, "-I", ANDROID_JAR, "--manifest", HERE / "AndroidManifest.xml", "--min-sdk-version", str(MIN_SDK),
         "--target-sdk-version", str(TARGET_SDK), "--version-code", str(version_code), "--version-name", version_name, "--auto-add-overlay",
         res_zip], quiet=True)

    unaligned = work / "unaligned.apk"
    shutil.copy2(base, unaligned)
    with zipfile.ZipFile(unaligned, "a", zipfile.ZIP_DEFLATED) as z:
        z.write(dex, "classes.dex")
        for abi, so in stripped.items():
            z.write(so, f"lib/{abi}/{LIB}")
        if args.data:
            files = data_files(args.data)
            if not files:
                raise SystemExit(f"No Soldier Front data in {args.data}")
            z.writestr("assets/data.list", "".join(f"{rel}\t{f.stat().st_size}\n" for rel, f in files))
            for rel, f in files:
                # Deflated: SetupActivity streams each one out of the package once (AssetManager
                # inflates as it reads), so the download is about half the data's size.
                z.write(f, f"assets/data/{rel}", compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
            print(f"  game data: {len(files)} archives, {sum(f.stat().st_size for _, f in files) / 1e9:.2f} GB")
    aligned = work / "aligned.apk"
    run([BUILD_TOOLS / "zipalign.exe", "-f", "-P", "16", "4", unaligned, aligned], quiet=True)

    ks, pw = keystore(jdk)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, JAVA_HOME=str(jdk), PATH=str(jdk / "bin") + os.pathsep + os.environ["PATH"])
    run([BUILD_TOOLS / "apksigner.bat", "sign", "--ks", ks, "--ks-key-alias", "legacysf", "--ks-pass", f"pass:{pw}", "--key-pass", f"pass:{pw}",
         "--min-sdk-version", str(MIN_SDK), "--out", out, aligned], env=env, quiet=True)
    run([BUILD_TOOLS / "apksigner.bat", "verify", out], env=env, quiet=True)
    print(f"{out}  {out.stat().st_size / 1e6:.1f} MB  version {version_name} ({version_code}), {', '.join(abis)}")

    if args.install:
        run([ADB, "install", "-r", out])
        if args.push_data:
            dest = f"/sdcard/Android/data/{PACKAGE}/files/data"
            print(f"Copying the game data to {dest} (a while)...")
            run([ADB, "shell", "mkdir", "-p", dest], quiet=True)
            for folder in DATA_FOLDERS:
                src = Path(args.push_data) / folder
                if src.is_dir():
                    run([ADB, "push", src, f"{dest}/"], quiet=True)
                    print(f"  {folder}")


if __name__ == "__main__":
    main()
