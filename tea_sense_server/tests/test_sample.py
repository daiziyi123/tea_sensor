"""Simulator contract checks; no .env or production database."""
import unittest
from app.protocol import ReadingV2
from tools.send_sample import snapshot

class SampleTests(unittest.TestCase):
    def test_independent_query_identities(self):
        a,b,c=(snapshot(n) for n in (1,2,4))
        for message in (a,b,c):
            ReadingV2(**message)
        self.assertEqual(a['data']['temperature_c']['sample_seq'], b['data']['temperature_c']['sample_seq'])
        self.assertNotEqual(a['data']['temperature_c']['sample_seq'], c['data']['temperature_c']['sample_seq'])
        self.assertEqual(a['data']['pressure_kpa']['sample_seq'], c['data']['pressure_kpa']['sample_seq'])
        self.assertNotEqual(a['data']['soil_ph']['sample_seq'], b['data']['soil_ph']['sample_seq'])
        self.assertGreater(b['data']['temperature_c']['age_ms'], a['data']['temperature_c']['age_ms'])

    def test_modes_are_strict_physical_values(self):
        for mode in ('fresh','mixed','failure','zero','stale'):
            model=ReadingV2(**snapshot(4,mode=mode))
            self.assertEqual(len(model.data.model_dump()),11)
        mixed=snapshot(4,mode='mixed')['data']
        self.assertEqual(mixed['wind_speed_mps']['value'],0)
        self.assertIsNone(mixed['soil_ph']['value'])
        self.assertEqual(mixed['temperature_c']['value'],19.4)
        self.assertIsNone(mixed['air_humidity_rh_pct']['value'])
