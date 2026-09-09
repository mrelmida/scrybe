"""Installer transactions using a local Git fixture and fake build artifacts.

No network, package installation, desktop configuration, or real executables.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class InstallerTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="scrybe installer ")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.repo = self.root / "remote source"
        self.repo.mkdir()
        self.dest = self.root / "source checkout"
        self.prefix = self.root / "install prefix"
        self.env = dict(os.environ, SCRYBE_REPO=str(self.repo), SCRYBE_SRC=str(self.dest),
                        SCRYBE_PREFIX=str(self.prefix))
        for key in ("SCRYBE_REF", "SCRYBE_BRANCH", "SCRYBE_COMMIT", "BUILD_FAIL"):
            self.env.pop(key, None)
        self.git("init", "-q", "--initial-branch=main")
        self.git("config", "user.email", "fixture@example.invalid")
        self.git("config", "user.name", "Installer fixture")
        (self.repo / "scripts").mkdir()
        shutil.copy(ROOT / "scripts/install-runtime.sh", self.repo / "scripts")
        (self.repo / "CMakeLists.txt").write_text("# scrybe-clipboard is required\n")
        (self.repo / ".gitignore").write_text("/build/\n")
        (self.repo / "build-and-setup.sh").write_text('''#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
[[ -z "${BUILD_FAIL:-}" ]] || exit 42
if [[ -f "$root/build/CMakeCache.txt" ]]; then
    [[ "$(cat "$root/build/CMakeCache.txt")" == "$root" ]] || exit 43
fi
mkdir -p "$root/build/bin"
printf '%s\\n' "$root" > "$root/build/CMakeCache.txt"
printf '#!/bin/sh\\ncat "$(dirname "$(readlink -f "$0")")/../REVISION"\\n' > "$root/build/bin/scrybe"
printf '#!/bin/sh\\nexit 0\\n' > "$root/build/bin/scrybe-clipboard"
chmod 755 "$root/build/bin/"*
bash "$root/scripts/install-runtime.sh" install "$root" "$root/build" "$SCRYBE_PREFIX"
''')
        self.first = self.revision("first")

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.repo), *args], text=True).strip()

    def revision(self, label):
        (self.repo / "scripts/faster_whisper_sidecar.py").write_text(label)
        self.git("add", ".")
        self.git("commit", "-qm", label)
        return self.git("rev-parse", "HEAD")

    def install(self, success=True):
        result = subprocess.run(["bash", str(ROOT / "install.sh")], env=self.env,
                                text=True, capture_output=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def active(self):
        return (self.prefix / "bin/scrybe").resolve().parents[1]

    @unittest.skipIf(os.geteuid() == 0, "Online installer intentionally refuses root")
    def test_upgrade_pins_bundle_and_rollback_restores_it(self):
        self.install()
        first_bundle = self.active()
        self.assertEqual((first_bundle / "REVISION").read_text().strip(), self.first)
        second = self.revision("second")
        self.install()
        self.assertNotEqual(self.active(), first_bundle)
        self.assertEqual((self.active() / "REVISION").read_text().strip(), second)
        self.assertEqual((self.active() / "scripts/faster_whisper_sidecar.py").read_text(), "second")
        self.assertTrue((self.active() / "bin/scrybe-clipboard").is_file())
        subprocess.run([str(self.prefix / "bin/scrybe-rollback")], check=True, capture_output=True)
        self.assertEqual(self.active(), first_bundle)
        self.assertEqual((self.active() / "scripts/faster_whisper_sidecar.py").read_text(), "first")
        self.assertTrue(list(self.root.glob("source checkout.previous.*")))

    @unittest.skipIf(os.geteuid() == 0, "Online installer intentionally refuses root")
    def test_failed_build_preserves_current_runtime_and_source(self):
        self.install()
        original = self.active()
        self.revision("second")
        self.env["BUILD_FAIL"] = "1"
        self.install(success=False)
        self.assertEqual(self.active(), original)
        head = subprocess.check_output(["git", "-C", str(self.dest), "rev-parse", "HEAD"], text=True).strip()
        self.assertEqual(head, self.first)
        self.assertFalse(list(self.root.glob(".scrybe-source.*")))

    @unittest.skipIf(os.geteuid() == 0, "Online installer intentionally refuses root")
    def test_published_source_reconfigures_after_relocation(self):
        self.install()
        self.assertFalse((self.dest / "build").exists())
        # The fixture rejects a CMake cache from a different source directory.
        # Rebuilding the published checkout must configure afresh.
        result = subprocess.run(["bash", str(self.dest / "build-and-setup.sh")],
                                env=self.env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.dest / "build/CMakeCache.txt").read_text().strip(), str(self.dest))

    @unittest.skipIf(os.geteuid() == 0, "Online installer intentionally refuses root")
    def test_failed_fetch_preserves_existing_runtime(self):
        self.install()
        original = self.active()
        self.env["SCRYBE_REF"] = "missing-tag"
        self.install(success=False)
        self.assertEqual(self.active(), original)

    @unittest.skipIf(os.geteuid() == 0, "Online installer intentionally refuses root")
    def test_activation_failure_preserves_runtime_and_rollback_pointer(self):
        self.install()
        self.revision("second")
        self.install()
        original = self.active()
        previous = os.readlink(self.prefix / "share/scrybe/runtime/previous")
        mockbin = self.root / "mockbin"
        mockbin.mkdir()
        script = mockbin / "mv"
        script.write_text('#!/usr/bin/env bash\n'
                          '[[ "${!#}" != */runtime/current ]] || exit 19\n'
                          'exec /usr/bin/mv "$@"\n')
        script.chmod(0o755)
        self.env["PATH"] = str(mockbin) + os.pathsep + self.env["PATH"]
        self.revision("third")
        self.install(success=False)
        self.assertEqual(self.active(), original)
        self.assertEqual(os.readlink(self.prefix / "share/scrybe/runtime/previous"), previous)

    @unittest.skipIf(os.geteuid() == 0, "Online installer intentionally refuses root")
    def test_unmanaged_or_dirty_source_is_never_replaced(self):
        self.dest.mkdir()
        note = self.dest / "precious.txt"
        note.write_text("keep me")
        self.install(success=False)
        self.assertEqual(note.read_text(), "keep me")
        note.unlink()
        self.dest.rmdir()
        self.install()
        (self.dest / "untracked notes").write_text("keep me too")
        original = self.active()
        self.install(success=False)
        self.assertEqual(self.active(), original)
        self.assertEqual((self.dest / "untracked notes").read_text(), "keep me too")

    @unittest.skipIf(os.geteuid() == 0, "Online installer intentionally refuses root")
    def test_default_prefers_version_tag_and_branch_override_is_explicit(self):
        self.git("tag", "v1.0.0")
        second = self.revision("second")
        self.install()
        self.assertEqual((self.active() / "REVISION").read_text().strip(), self.first)
        self.env["SCRYBE_BRANCH"] = "main"
        self.install()
        self.assertEqual((self.active() / "REVISION").read_text().strip(), second)

    def test_missing_helper_cannot_activate(self):
        # The real app defines its helper in a nested CMake file.
        (self.repo / "CMakeLists.txt").write_text("add_subdirectory(src/paste)\n")
        (self.repo / "src/paste").mkdir(parents=True)
        (self.repo / "src/paste/CMakeLists.txt").write_text("add_executable(scrybe-clipboard helper.c)\n")
        build = self.root / "build/bin"
        build.mkdir(parents=True)
        (build / "scrybe").write_text("#!/bin/sh\nexit 0\n")
        (build / "scrybe").chmod(0o755)
        result = subprocess.run(["bash", str(ROOT / "scripts/install-runtime.sh"), "install",
                                 str(self.repo), str(build.parent), str(self.prefix)],
                                text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("clipboard helper is missing", result.stderr)
        self.assertFalse((self.prefix / "bin/scrybe").exists())

    def test_legacy_binary_and_helper_are_preserved_for_rollback(self):
        build = self.root / "build/bin"
        build.mkdir(parents=True)
        old_bin = self.prefix / "bin"
        old_bin.mkdir(parents=True)
        for name in ("scrybe", "scrybe-clipboard"):
            (build / name).write_text("#!/bin/sh\n# new\nexit 0\n")
            (build / name).chmod(0o755)
            (old_bin / name).write_text("#!/bin/sh\n# old\nexit 0\n")
            (old_bin / name).chmod(0o755)
        result = subprocess.run(["bash", str(ROOT / "scripts/install-runtime.sh"), "install",
                                 str(self.repo), str(build.parent), str(self.prefix)],
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        subprocess.run([str(old_bin / "scrybe-rollback")], check=True, capture_output=True)
        for name in ("scrybe", "scrybe-clipboard"):
            self.assertIn("# old", (self.active() / "bin" / name).read_text())


if __name__ == "__main__":
    unittest.main()
