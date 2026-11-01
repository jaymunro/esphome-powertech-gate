import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('read_settings', ROOT / 'tools/read_settings.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

# Synthetic option fixture; no account, controller identity or captured payload.
FRAME = 'ACK READ FUNCTION,1:1,2:01,3:01,4:1,5:1,6:1,7:0,8:0,9:0,A:1,B:0,C:0,D:0,E:0,F:0,G:0,H:0,I:1,J:0'


class Settings(unittest.TestCase):
    def test_all_fields(self):
        result = module.decode_snapshot(FRAME)
        self.assertEqual(len(result['settings']), 19)
        self.assertEqual(result['settings'][1]['value'], '2 A')

    def test_lossless_unknown_suffix_and_order(self):
        frame = FRAME + ',Z:00aF;src=fixture;future=keep'
        result = module.decode_snapshot(frame)
        self.assertEqual(result['unknown_fields'], {'Z': '00aF'})
        self.assertEqual(module.reassemble_snapshot(result), frame)

    def test_reverse_duration_and_autoclose(self):
        result = module.decode_snapshot(FRAME.replace('J:0', 'J:1').replace('9:0', '9:2'))
        by_field = {p['field']: p['value'] for p in result['settings']}
        self.assertEqual(by_field['J'], '0.1 sec')
        self.assertEqual(by_field['9'], '10 sec')

    def test_reject_missing_duplicate_and_truncated(self):
        for frame in [FRAME.rsplit(',', 1)[0], FRAME + ',J:0', FRAME[:-1], FRAME + ',']:
            with self.subTest(frame=frame), self.assertRaises(ValueError):
                module.decode_snapshot(frame)

    def test_reject_bad_values_and_formats(self):
        for before, after in [('2:01', '2:1'), ('2:01', '2:0A'), ('J:0', 'J:7'), ('1:1', '1:0')]:
            with self.subTest(after=after), self.assertRaises(ValueError):
                module.decode_snapshot(FRAME.replace(before, after))

    def test_hall_dependency(self):
        frame = FRAME.replace('1:1', '1:3', 1).replace('2:01','2:05').replace('3:01','3:40')
        result=module.decode_snapshot(frame)
        self.assertEqual(result['settings'][1]['value'],'0.5 A')
        self.assertEqual(result['settings'][2]['value'],'4 A')
        with self.assertRaises(ValueError):
            module.decode_snapshot(FRAME.replace('1:1','1:3',1))

    def test_reject_wrong_frame_and_control_bytes(self):
        for frame in [FRAME.replace('FUNCTION,', 'FUNCTIONBAD,'), FRAME + '\x00', FRAME * 30]:
            with self.subTest(frame=frame[:25]), self.assertRaises(ValueError):
                module.decode_snapshot(frame)
