#
# PlatformIO pre-build hook (display envs).
#
# The web UI is embedded into the firmware app image (GM-106). The real bundle
# is produced by scripts/build_webui.sh (npm build -> gzip -> embed_webui.py) and
# lands, git-ignored, in src/display/webassets/. That step is the source of truth
# and is run explicitly in CI and locally when the UI changes.
#
# This hook only guarantees the build can compile *without* a prior web build: if
# the generated manifest is missing it drops in an empty stub so a bare
# `pio run -e display` still links (serving an empty UI). It never overwrites a
# real bundle.
#
import os
import subprocess
import sys

Import("env")  # noqa: F821 -- provided by PlatformIO/SCons

project_dir = env["PROJECT_DIR"]  # noqa: F821
out_dir = os.path.join(project_dir, "src", "display", "webassets")
manifest = os.path.join(out_dir, "web_ui_manifest.h")
packer = os.path.join(project_dir, "scripts", "embed_webui.py")

if not os.path.isfile(manifest):
    print("embed_webui_pre: no web bundle found, writing stub (run build_webui.sh for the real UI)")
    subprocess.check_call([sys.executable, packer, "--out", out_dir, "--stub", "--rel-to", project_dir])

# Assemble the blob explicitly.
#
# web_ui_blob.S .incbin's web_ui.bin and defines gWebUiBlobStart, which
# WebUIPlugin serves straight out of memory-mapped flash. The display envs
# exclude src/display/webassets/ from build_src_filter because the dual
# arduino+espidf build does not collect .S files out of src/ -- the file is
# simply never assembled, and the link still succeeds while the manifest is the
# empty stub, because the only reference to the symbol is then dead code. That
# turns a missing web UI into a link error only after a real bundle is built.
# Building it here works the same way under either framework combination.
#
# No include path is needed: web_ui_blob.S names web_ui.bin relative to the
# project root (embed_webui.py --rel-to), which is where the assembler runs from.
# An -I in ASFLAGS would not have helped anyway, as gcc passes it to the
# preprocessor rather than to the assembler that resolves .incbin.
env.BuildSources(  # noqa: F821
    os.path.join("$BUILD_DIR", "webassets"),
    out_dir,
    src_filter="+<web_ui_blob.S>",
)
