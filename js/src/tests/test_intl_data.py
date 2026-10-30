"""Regression checks for the CLDR locale-mapping generator (no downloads)."""
import importlib.util
import io
from pathlib import Path
import unittest
from zipfile import ZipFile

GENERATOR = Path(__file__).resolve().parents[1] / 'builtin/intl/make_intl_data.py'
SPEC = importlib.util.spec_from_file_location('make_intl_data', str(GENERATOR))
intl_data = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(intl_data)


class VariantAliases(unittest.TestCase):
    def read_alias(self, replacement):
        with io.BytesIO() as data:
            with ZipFile(data, 'w') as archive:
                archive.writestr('common/supplemental/supplementalMetadata.xml',
                                 '<supplementalData><metadata><alias>'
                                 '<variantAlias type="polytoni" replacement="' +
                                 replacement + '" reason="deprecated"/>'
                                 '</alias></metadata></supplementalData>')
                archive.writestr('common/supplemental/likelySubtags.xml',
                                 '<supplementalData><likelySubtags/>'
                                 '</supplementalData>')
            data.seek(0)
            with ZipFile(data) as archive:
                return intl_data.readSupplementalData(archive)['variantMappings']['polytoni']

    def test_alphabetic_variant_is_not_a_language(self):
        self.assertEqual(self.read_alias('polyton'), ('variant', 'polyton'))
        self.assertEqual(self.read_alias('POLYTON'), ('variant', 'polyton'))

    def test_other_variant_forms(self):
        self.assertEqual(self.read_alias('alalc97'), ('variant', 'alalc97'))
        self.assertEqual(self.read_alias('1901'), ('variant', '1901'))

    def test_legacy_language_and_region_replacements(self):
        for replacement in ('hy', 'hyw'):
            self.assertEqual(self.read_alias(replacement), ('language', replacement))
        for replacement in ('AX', '419'):
            self.assertEqual(self.read_alias(replacement), ('region', replacement))

    def test_invalid_replacement_is_rejected(self):
        for replacement in ('a', 'latn', 'bad_value'):
            with self.assertRaises(AssertionError):
                self.read_alias(replacement)


if __name__ == '__main__':
    unittest.main()
