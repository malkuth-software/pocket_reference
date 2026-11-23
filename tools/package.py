#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""Packages a built Pocket Reference for download, in dist/.

  python3 tools/package.py --build <build dir> --toolkit <GUI.Forms SDK prefix>
                           [--runtime <LLVM runtime>] [--version <x.y.z>]

macOS: PocketReference.app with the LLVM runtime's dylibs in
Contents/Frameworks, signed ad hoc, zipped (needs --runtime, the
GUI_FORMS_LLVM_RUNTIME the program was built against). Windows: the program
(linked statically, so it imports only system DLLs) and its pack, zipped.
Linux: the program (its LLVM runtime linked statically) and its pack, as a
.tar.gz. Each carries LICENSE and the GUI.Forms SDK's notices
(share/licenses/GUIForms) in licenses/, and on macOS the LLVM runtime's.
"""
import argparse
import platform
import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(command: list[str]) -> str:
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


def project_version() -> str:
    for line in (ROOT / "CMakeLists.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith("project(PocketReference VERSION "):
            return line.split()[2]
    raise RuntimeError("CMakeLists.txt names no project version")


def copy_notices(toolkit: Path, destination: Path) -> None:
    notices = toolkit / "share/licenses/GUIForms"
    if not (notices / "LICENSE").is_file():
        raise RuntimeError(str(toolkit) + " is not an installed GUI.Forms SDK (no share/licenses/GUIForms/LICENSE)")
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "LICENSE", destination.parent / "LICENSE")
    shutil.copytree(notices, destination / "GUIForms")


def macos_dependencies(image: Path) -> list[str]:
    lines = run(["otool", "-L", str(image)]).splitlines()[1:]
    return [line.strip().split(" (")[0] for line in lines]


def package_macos(build: Path, toolkit: Path, runtime: Path, output: Path, stem: str) -> Path:
    app = output / "PocketReference.app"
    shutil.copytree(build / "PocketReference.app", app, symlinks=True)
    resources = app / "Contents/Resources"
    if not (resources / "reference.pack").is_file():
        raise RuntimeError("the bundle has no reference.pack: configure with POCKET_REFERENCE_DOCS")
    copy_notices(toolkit, resources / "licenses")
    shutil.copytree(runtime / "share/licenses/llvm-runtimes", resources / "licenses/LLVM")
    frameworks = app / "Contents/Frameworks"
    frameworks.mkdir(exist_ok=True)
    # The program's @rpath libraries (libc++, libc++abi, libunwind) and theirs,
    # from the runtime's lib; the bundle's rpath already points at Frameworks.
    pending = [app / "Contents/MacOS/PocketReference"]
    seen: set[str] = set()
    while pending:
        image = pending.pop(0)
        for dependency in macos_dependencies(image):
            if dependency.startswith(("/System/", "/usr/lib/")):
                continue
            if not dependency.startswith("@rpath/"):
                raise RuntimeError("non-portable dependency " + dependency + " in " + str(image))
            name = dependency[len("@rpath/"):]
            if name in seen or image.name == name:
                continue
            source = runtime / "lib" / name
            if not source.is_file():
                raise RuntimeError("the runtime has no " + name)
            target = frameworks / name
            shutil.copy2(source.resolve(), target)
            target.chmod(0o755)
            run(["install_name_tool", "-id", "@rpath/" + name, str(target)])
            seen.add(name)
            pending.append(target)
    for library in frameworks.iterdir():
        run(["codesign", "--force", "--sign", "-", str(library)])
    run(["codesign", "--force", "--deep", "--sign", "-", str(app)])
    run(["codesign", "--verify", "--deep", "--strict", str(app)])
    archive = output / (stem + ".zip")
    run(["ditto", "-c", "-k", "--keepParent", str(app), str(archive)])
    shutil.rmtree(app)
    return archive


def stage_portable(build: Path, toolkit: Path, folder: Path, program: str) -> None:
    folder.mkdir(parents=True)
    for name in (program, "reference.pack"):
        if not (build / name).is_file():
            raise RuntimeError("the build has no " + name)
        shutil.copy2(build / name, folder / name)
    copy_notices(toolkit, folder / "licenses")


def package_windows(build: Path, toolkit: Path, output: Path, stem: str) -> Path:
    folder = output / stem
    stage_portable(build, toolkit, folder, "PocketReference.exe")
    archive = output / (stem + ".zip")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zipped:
        for path in sorted(folder.rglob("*")):
            zipped.write(path, path.relative_to(output).as_posix())
    shutil.rmtree(folder)
    return archive


def package_linux(build: Path, toolkit: Path, output: Path, stem: str) -> Path:
    folder = output / stem
    stage_portable(build, toolkit, folder, "PocketReference")
    archive = output / (stem + ".tar.gz")
    with tarfile.open(archive, "w:gz") as tarred:
        tarred.add(folder, arcname=stem)
    shutil.rmtree(folder)
    return archive


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--toolkit", type=Path, required=True)
    parser.add_argument("--runtime", type=Path)
    parser.add_argument("--version", default=None)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    version = args.version or project_version()
    machine = platform.machine().lower()
    architecture = {"amd64": "x64", "x86_64": "x64", "arm64": "arm64", "aarch64": "arm64"}.get(machine, machine)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    build = args.build.resolve()
    toolkit = args.toolkit.resolve()
    if sys.platform == "darwin":
        if args.runtime is None:
            parser.error("macOS needs --runtime (GUI_FORMS_LLVM_RUNTIME)")
        archive = package_macos(build, toolkit, args.runtime.resolve(), output,
                                "PocketReference-" + version + "-macos-" + architecture)
    elif sys.platform in ("win32", "cygwin", "msys"):
        archive = package_windows(build, toolkit, output, "PocketReference-" + version + "-windows-" + architecture)
    else:
        archive = package_linux(build, toolkit, output, "PocketReference-" + version + "-linux-" + architecture)
    print(archive)
    return 0


if __name__ == "__main__":
    sys.exit(main())
