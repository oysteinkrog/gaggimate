#
# PlatformIO pre-build hook (env:display-sim).
#
# sim/web/web_ui_blob_sim.S .incbin's src/display/webassets/web_ui.bin, which is
# git-ignored and produced by scripts/build_webui.sh. Without it the assembler
# fails outright, so a clean checkout could not build the simulator at all --
# which is how it broke in CI the first time the job ran.
#
# The display envs solve this with embed_webui_pre.py; that hook cannot be
# reused here because it also assembles the generated web_ui_blob.S, and the
# simulator has its own Mach-O-aware copy of that file. So this hook does only
# the half that applies: drop in the empty placeholder when no bundle has been
# built. It never overwrites a real one.
#
import os
import subprocess
import sys

Import("env")  # noqa: F821 -- provided by PlatformIO/SCons

project_dir = env["PROJECT_DIR"]  # noqa: F821
out_dir = os.path.join(project_dir, "src", "display", "webassets")
manifest = os.path.join(out_dir, "web_ui_manifest.h")
blob = os.path.join(out_dir, "web_ui.bin")
packer = os.path.join(project_dir, "scripts", "embed_webui.py")

if not (os.path.isfile(manifest) and os.path.isfile(blob)):
    print("sim_webui_pre: no web bundle found, writing stub (run build_webui.sh for the real UI)")
    subprocess.check_call([sys.executable, packer, "--out", out_dir, "--stub", "--rel-to", project_dir])
