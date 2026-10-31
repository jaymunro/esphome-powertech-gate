import importlib.util,json,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('generate',ROOT/'tools/generate.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class Profiles(unittest.TestCase):
 def setUp(self): self.p=json.loads((ROOT/'models/ps19001.json').read_text())
 def test_profiles(self):
  for path in (ROOT/'models').glob('*.json'): module.validate(json.loads(path.read_text()))
 def test_rejects_unsupported_protocol(self):
  self.p['protocol']='uart_v2'
  with self.assertRaises(ValueError): module.validate(self.p)
 def test_rejects_bad_threshold(self):
  self.p['full_open_min_position']=101
  with self.assertRaises(ValueError): module.validate(self.p)
 def test_rejects_unvalidated_fields(self):
  self.p['arbitrary_uart_command']='FACTORY DEFAULT'
  with self.assertRaises(ValueError): module.validate(self.p)
 def test_rejects_boolean_threshold(self):
  self.p['closed_position']=False
  with self.assertRaises(ValueError): module.validate(self.p)
 def test_rejects_excessive_closed_tolerance(self):
  self.p['closed_position_tolerance']=6
  with self.assertRaises(ValueError): module.validate(self.p)
 def test_rejects_boolean_closed_tolerance(self):
  self.p['closed_position_tolerance']=True
  with self.assertRaises(ValueError): module.validate(self.p)
 def test_rejects_ambiguous_opening_signatures(self):
  self.p['car_opening_status']=self.p['pedestrian_opening_status']
  with self.assertRaises(ValueError): module.validate(self.p)
 def test_allows_unvalidated_empty_opening_signatures(self):
  self.p['car_opening_status']=''
  self.p['pedestrian_opening_status']=''
  module.validate(self.p)
 def test_position_diagnostic(self):
  source=(ROOT/'templates/powertech-gate.yaml.in').read_text()
  sensor=source.split('name: Gate position',1)[1].split('  - platform:',1)[0]
  self.assertIn('entity_category: diagnostic',sensor)
  self.assertNotIn('internal: true',sensor)
 def test_autonomous_package(self):
  source=(ROOT/'templates/powertech-gate.yaml.in').read_text()
  self.assertIn("id: monitor_enabled\n    type: bool\n    restore_value: false\n    initial_value: 'true'",source)
  self.assertIn('Starting continuous monitoring with status only',source)
  self.assertNotIn('id: monitor_switch',source)
  self.assertNotIn('id: read_settings_button',source)
if __name__=='__main__': unittest.main()
