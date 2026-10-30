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


class MultiSubtagAliases(unittest.TestCase):
    def read_rules(self, aliases):
        with io.BytesIO() as data:
            with ZipFile(data, 'w') as archive:
                archive.writestr('common/supplemental/supplementalMetadata.xml',
                                 '<supplementalData><metadata><alias>' +
                                 ''.join('<languageAlias type="%s" replacement="%s"/>' % pair
                                         for pair in aliases) +
                                 '</alias></metadata></supplementalData>')
                archive.writestr('common/supplemental/likelySubtags.xml',
                                 '<supplementalData><likelySubtags/></supplementalData>')
            data.seek(0)
            with ZipFile(data) as archive:
                return intl_data.readSupplementalData(archive)['multiSubtagMappings']

    def test_wildcards_and_specific_languages_are_preserved(self):
        rules = self.read_rules([('und_arevmda', 'und'), ('hy_arevmda', 'hyw'),
                                 ('und_aaland', 'und_AX'), ('sgn_US', 'ase')])
        self.assertEqual(rules[('und', None, None, ('aaland',))], ('und', None, 'AX', ()))
        self.assertEqual(rules[('hy', None, None, ('arevmda',))], ('hyw', None, None, ()))
        self.assertEqual(rules[('sgn', None, 'US', ())], ('ase', None, None, ()))
        lines = []
        intl_data.writeMultiSubtagMappings(lines.append, rules, 'test', 'test', 'test')
        output = '\n'.join(lines)
        self.assertLess(output.index('language().equalTo("hy") && hasVariant("arevmda")'),
                        output.index('if (hasVariant("arevmda"))'))
        self.assertIn('if (region().missing())', output)
        self.assertIn('region_.set(mozilla::Span<const char>())', output)

    def test_multi_variant_rule_and_invalid_language_forms(self):
        rules = self.read_rules([('und_hepburn_heploc', 'und_alalc97'),
                                 ('i_klingon', 'tlh'), ('zh_cmn', 'zh')])
        self.assertEqual(rules, {('und', None, None, ('hepburn', 'heploc')):
                                ('und', None, None, ('alalc97',))})


if __name__ == '__main__':
    unittest.main()
