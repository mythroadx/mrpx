import unittest

from tools import mrp_doc_index


class DocumentIndexTests(unittest.TestCase):
    def test_signature_uses_definition_heading_and_balances_callback_parameters(self):
        page = """
        <html><head><title>mrc_draw(fake)</title></head><body>
        mrc_draw is described here. API 定义： <b>int32</b>
        mrc_draw(uint16 x, void (*cb)(int)); 参数说明
        </body></html>
        """
        signature, confidence = mrp_doc_index.extract_signature(page, "mrc_draw", True)
        self.assertEqual(signature, "int32 mrc_draw(uint16 x, void (*cb)(int))")
        self.assertEqual(confidence, "definition-heading")
        self.assertEqual(mrp_doc_index.extract_signature(page, "mrc_missing", True),
                         (None, "missing"))

    def test_signature_handles_markup_whitespace_and_callback_declarations(self):
        spaced = "<body>API 定义： int32 mrc_GPRS_ <b>GetMode</b> (void); 参数说明</body>"
        callback = "<body>API 定义： void (*mrc_callback)(int32 result); 参数说明</body>"
        self.assertEqual(mrp_doc_index.extract_signature(spaced, "mrc_GPRS_GetMode", True),
                         ("int32 mrc_GPRS_GetMode(void)", "definition-heading"))
        self.assertEqual(mrp_doc_index.extract_signature(callback, "mrc_callback", True),
                         ("void (*mrc_callback)(int32 result)", "definition-heading"))

    def test_switch_ignores_comments_nested_cases_and_reports_unknown_macros(self):
        source = """
        int32 mr_plat(int code) {
            switch (code) {
                case 1: // case 3:
                case SECOND: { switch (other) { case 9: break; } break; }
                case MISSING: return 0;
                default: return 1;
            }
        }
        """
        cases, unresolved = mrp_doc_index.switch_cases(source, "mr_plat", "#define SECOND 2")
        self.assertEqual(set(cases), {1, 2})
        self.assertEqual(unresolved, ["MISSING"])

    def test_source_definitions_do_not_count_function_calls_in_conditions(self):
        source = """
        int32 mr_platEx(int code) { return code; }
        void caller(void) {
            if (mr_other(1) == MR_SUCCESS) { return; }
        }
        """
        definitions = mrp_doc_index.source_definitions(source, "source.c")
        self.assertIn("mr_platEx", definitions)
        self.assertNotIn("mr_other", definitions)

    def test_selector_mismatch_requires_comparable_numeric_values(self):
        self.assertTrue(mrp_doc_index.selector_mismatch("1211", "int mr_plat(int code = 1221)"))
        self.assertFalse(mrp_doc_index.selector_mismatch("1211", "int mr_plat(int code = 1211)"))
        self.assertFalse(mrp_doc_index.selector_mismatch("201x", "int mr_plat(int code = 2010)"))
        self.assertFalse(mrp_doc_index.selector_mismatch("1211", None))


if __name__ == "__main__":
    unittest.main()
