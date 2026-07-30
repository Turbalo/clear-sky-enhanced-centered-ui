import sys
import tempfile
import unittest
from pathlib import Path

from lxml import etree

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))

from build_menu_backplates import add_backplate, build, parse_fragment


class MenuBackplateTests(unittest.TestCase):
    def test_backplate_is_first_background_child(self):
        root = etree.fromstring(b"<root><w><background/></w></root>")
        add_backplate(root)

        backplate = root.find("w/background")[0]
        self.assertEqual(backplate.tag, "auto_static")
        self.assertEqual(backplate.get("x"), "-4096")
        self.assertEqual(backplate.get("width"), "9216")
        self.assertEqual(backplate.findtext("texture"), "ui_menu2_backgraund")

    def test_build_preserves_existing_named_controls(self):
        source_text = """
        <w>
          <background x="0" y="0" width="1024" height="768"/>
          <menu><btn name="btn_options"/></menu>
        </w>
        """
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "source.xml"
            output = Path(temporary) / "output.xml"
            source.write_text(source_text, encoding="utf-8")

            build(source, output)
            root, messages = parse_fragment(output)

        self.assertEqual(messages, [])
        self.assertIsNotNone(root.find("w/menu/btn[@name='btn_options']"))


if __name__ == "__main__":
    unittest.main()
