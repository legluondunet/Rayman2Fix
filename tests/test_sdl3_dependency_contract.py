"""Static integration contract for the audited, dynamically loaded SDL3 DLL.
No network access, game assets, Windows environment or third-party module needed.
"""
import ast
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SDL3DependencyContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / "tools/sdl3/audit_release.py").read_text(encoding="utf-8")
        cls.audit = source
        cls.constants = {}
        for node in ast.parse(source).body:
            if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
                try:
                    cls.constants[node.targets[0].id] = ast.literal_eval(node.value)
                except (ValueError, TypeError):
                    pass
        cls.adapter = (ROOT / "src/runtime/sdl_gamepads.cpp").read_text(encoding="utf-8")
        cls.cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")

    def test_every_bound_runtime_symbol_is_audited(self):
        bound = set(re.findall(r'bind\s*\(\s*dll\s*,\s*api->\w+\s*,\s*"(SDL_\w+)"\s*\)', self.adapter))
        self.assertGreaterEqual(len(bound), 18)
        self.assertLessEqual(bound, self.constants["REQUIRED_EXPORTS"])

    def test_payload_pins_agree(self):
        dll, notice = (self.constants[k] for k in ("DLL_SHA256", "NOTICE_SHA256"))
        for digest in (dll, notice, self.constants["ARCHIVE_SHA256"]):
            self.assertRegex(digest, r"^[0-9a-f]{64}$")
        self.assertIn(dll, self.adapter)
        self.assertIn(dll, self.cmake)
        self.assertIn(notice, self.cmake)
        self.assertIn("digest(dll) != DLL_SHA256", self.audit)
        self.assertIn("digest(license_data) != NOTICE_SHA256", self.audit)

    def test_version_and_architecture_agree(self):
        version = self.constants["VERSION"]
        major, minor, patch = map(int, version.split("."))
        self.assertIn(f"kVersion = {major * 1000000 + minor * 1000 + patch};", self.adapter)
        self.assertIn(f"SDL3-{version}-win32-x86.zip", self.workflow)
        self.assertIn("pe.FILE_HEADER.Machine != 0x14c", self.audit)
        self.assertIn("pe.OPTIONAL_HEADER.Magic != 0x10b", self.audit)


if __name__ == "__main__":
    unittest.main()
