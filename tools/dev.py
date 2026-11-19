"""Build / run / test this app against the pinned wilibsp.

    tools\\dev build            configure (first time) + build build/lander.elf/.uf2
    tools\\dev run              build, then load it into DISPLAY SRAM and start it
    tools\\dev test             host unit tests (no hardware)
    tools\\dev install          copy build/lander.uf2 to the SD card's /apps/
    tools\\dev <anything else>  passed to wilibsp's fw CLI (screenshot, press,
                               hold, release, touch, type, rtt, run-app, ...)

Toolchain discovery, OpenOCD and the device verbs all come from
wilibsp/tools/fw.py, so this script stays in step with the pinned BSP.
"""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
WILIBSP = ROOT / "wilibsp"
sys.path.insert(0, str(WILIBSP / "tools"))
import fw  # noqa: E402  (wilibsp/tools/fw.py)

APP = "voidpanzer"
BUILD = ROOT / "build"
ELF = BUILD / f"{APP}.elf"
UF2 = BUILD / f"{APP}.uf2"


def run(cmd, cwd=ROOT):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], cwd=cwd, check=True)


def configure():
    if (BUILD / "build.ninja").exists():
        return
    cmd = ["cmake", "-S", ROOT, "-B", BUILD, "-G", "Ninja",
           "-DCMAKE_BUILD_TYPE=RelWithDebInfo"]
    sdk = fw._pico_sdk_dir("sdk", fw.PICO_SDK_VERSION)
    if sdk:
        cmd.append(f"-DPICO_SDK_PATH={sdk.as_posix()}")
    tc = fw._pico_sdk_dir("toolchain", fw.PICO_TOOLCHAIN_VERSION)
    if tc:
        cmd.append(f"-DPICO_TOOLCHAIN_PATH={tc.as_posix()}")
    pt = fw._pico_sdk_dir("picotool", fw.PICO_SDK_VERSION)
    if pt and (pt / "picotool" / "picotoolConfig.cmake").exists():
        cmd.append(f"-Dpicotool_DIR={(pt / 'picotool').as_posix()}")
    ninja = fw._ninja()
    if ninja:
        cmd.append(f"-DCMAKE_MAKE_PROGRAM={ninja.as_posix()}")
    run(cmd)


def build():
    configure()
    run(["cmake", "--build", BUILD, "--target", APP])


def ramrun():
    build()
    fw.check_flash_elf(ELF)   # refuse anything that would overwrite stock firmware
    run(fw._openocd_base() + ["-c", f"set ELF {ELF.as_posix()}",
                              "-f", str(WILIBSP / "tools" / "openocd" / "ramrun.tcl")])


def test():
    tests = ROOT / "tests"
    out = ROOT / "build-tests"
    run(["cmake", "-S", tests, "-B", out] + fw._host_toolchain_args())
    run(["cmake", "--build", out])
    run(["ctest", "--test-dir", out, "--output-on-failure"])


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    verb, rest = argv[0], argv[1:]
    if verb == "build":
        build()
    elif verb == "run":
        ramrun()
    elif verb == "test":
        test()
    elif verb == "install":
        build()
        run([sys.executable, WILIBSP / "tools" / "fw.py", "install-app", UF2] + rest)
    else:
        run([sys.executable, WILIBSP / "tools" / "fw.py", verb] + rest)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except subprocess.CalledProcessError as exc:
        sys.exit(exc.returncode or 1)
