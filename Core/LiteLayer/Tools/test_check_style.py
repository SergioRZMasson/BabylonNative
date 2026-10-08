import os
from pathlib import Path
import shutil
import unittest

from check_style import compiler_ast, inspect_ast


class StyleChecks(unittest.TestCase):
    def check_source(self, code):
        clang = os.environ.get("LITE_STYLE_CLANG") or shutil.which("clang")
        self.assertIsNotNone(clang, "Clang is required for semantic style tests")
        root = Path(os.environ.get("LITE_STYLE_FIXTURE_ROOT", Path.cwd() / "build" / "style-fixtures"))
        directory = root / self._testMethodName
        directory.mkdir(parents=True, exist_ok=True)
        source = directory.resolve() / "fixture.cpp"
        try:
            source.write_text(code, encoding="utf-8")
            entry = {
                "directory": str(directory.resolve()),
                "arguments": ["c++", "-std=c++20", str(source)],
            }
            tree = compiler_ast(entry, clang, source)
            errors = set()
            inspect_ast(tree, source, source.parent, errors)
            return errors
        finally:
            source.unlink(missing_ok=True)
            directory.rmdir()

    def test_separate_declarations_and_braced_else_if_are_valid(self):
        errors = self.check_source("""
struct bl_example { int first; int second; };
int first;
int second;
int select_value(int value) {
    int lower = 0;
    int upper = 1;
    if (value < lower) { return lower; }
    else if (value > upper) { return upper; }
    else { return value; }
}
""")
        self.assertEqual(errors, set())

    def test_member_declarations_are_checked(self):
        errors = self.check_source("struct GeometryOptions { int first, second; };")
        self.assertEqual(len(errors), 1)
        self.assertIn("first, second", next(iter(errors)))

    def test_required_interface_methods_are_checked(self):
        errors = self.check_source("""
class RequiredInterface {
public:
    virtual int update(int value) = 0;
};
class UiAdapter final : public RequiredInterface {
    int first, second;
public:
    int update(int value) override {
        int left = 0, right = 1;
        if (value) return left + right;
        return first + second;
    }
};
""")
        self.assertEqual(len(errors), 3)

    def test_global_declarations_are_checked(self):
        errors = self.check_source("int first = 0, second = 1;")
        self.assertEqual(len(errors), 1)
        self.assertIn("first, second", next(iter(errors)))

    def test_local_and_for_initializer_declarations_are_checked(self):
        errors = self.check_source("""
int accumulate() {
    int first = 0, second = 1;
    for (int index = 0, end = 2; index < end; ++index) { first += second; }
    return first;
}
""")
        self.assertEqual(len(errors), 2)
        self.assertTrue(any("index, end" in error for error in errors))
        self.assertTrue(any("first, second" in error for error in errors))

    def test_macro_control_flow_is_checked(self):
        errors = self.check_source("""
#define CHECK(condition) do { if (condition) return 1; } while (false)
int checked(bool ready) { CHECK(ready); return 0; }
""")
        self.assertEqual(len(errors), 1)
        self.assertIn("if/else bodies must use braces", next(iter(errors)))

    def test_switch_requires_a_braced_body(self):
        errors = self.check_source("int checked(int value) { switch (value) case 0: return 0; return 1; }")
        self.assertEqual(len(errors), 1)
        self.assertIn("SwitchStmt body must use braces", next(iter(errors)))

    def test_all_loop_bodies_require_braces(self):
        errors = self.check_source("""
int checked(int value) {
    while (value > 1) --value;
    for (int index = 0; index < 1; ++index) ++value;
    do --value; while (value > 3);
    return value;
}
""")
        self.assertEqual(len(errors), 3)
        self.assertTrue(any("WhileStmt" in error for error in errors))
        self.assertTrue(any("ForStmt" in error for error in errors))
        self.assertTrue(any("do/while" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
