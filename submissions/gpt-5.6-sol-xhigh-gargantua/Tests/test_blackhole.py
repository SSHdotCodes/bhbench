import argparse
from pathlib import Path
import tempfile
import unittest

import blackhole


class ResolutionTests(unittest.TestCase):
    def test_parses_1440p(self):
        self.assertEqual(blackhole.parse_resolution("2560x1440"), (2560, 1440))

    def test_rejects_malformed_resolution(self):
        with self.assertRaises(argparse.ArgumentTypeError):
            blackhole.parse_resolution("1440p")

    def test_rejects_unreasonable_resolution(self):
        with self.assertRaises(argparse.ArgumentTypeError):
            blackhole.parse_resolution("100x100")


class LauncherTests(unittest.TestCase):
    def test_native_defaults_request_full_resolution_1440p120(self):
        args = blackhole.create_parser().parse_args([])
        native = blackhole.native_arguments(args)
        self.assertEqual(args.command, "run")
        self.assertIn("2560", native)
        self.assertIn("1440", native)
        self.assertIn("120", native)
        self.assertIn("1.0", native)
        self.assertIn("192", native)
        self.assertNotIn("--no-adaptive", native)

    def test_benchmark_forwards_duration(self):
        args = blackhole.create_parser().parse_args(["benchmark", "--seconds", "4.5"])
        native = blackhole.native_arguments(args)
        index = native.index("--benchmark-seconds")
        self.assertEqual(native[index + 1], "4.5")

    def test_package_is_a_supported_command(self):
        args = blackhole.create_parser().parse_args(["package"])
        self.assertEqual(args.command, "package")

    def test_missing_binary_requires_rebuild(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertTrue(blackhole.needs_rebuild(Path(directory) / "missing"))


if __name__ == "__main__":
    unittest.main()
