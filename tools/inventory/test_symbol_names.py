import unittest
from symbol_names import canonical_guest_symbol

class GuestSymbols(unittest.TestCase):
    def test_known_entries(self):
        for name in ('sub_825718F8', '__imp__sub_825718F8', 'nier_native_sub_825718F8', '__imp__nier_native_sub_825718F8'):
            self.assertEqual(canonical_guest_symbol(name), 'sub_825718F8')
        self.assertEqual(canonical_guest_symbol('nier_native_sub_82571508.cold'), 'sub_82571508.cold')

    def test_helpers_are_not_redistributed(self):
        for name in ('nier::render::ReplayDraw()', 'nier_native_matrix_helper', 'shared_audio_mix'):
            self.assertEqual(canonical_guest_symbol(name), name)

if __name__ == '__main__':
    unittest.main()
