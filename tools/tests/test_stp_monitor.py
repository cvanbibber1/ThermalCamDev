"""Golden V3 command and LRT vectors shared with the firmware layout."""

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import stp_monitor as stp


class ProtocolToolTests(unittest.TestCase):
    def setUp(self):
        self.codec = stp.Codec(True, 0xFFFF)

    def test_v3_command_with_arguments(self):
        packet = stp.build_command(self.codec, 0x6F, 0xC7, args=b"\x01",
                                   sequence=0xFFFF, coarse_time=0)
        self.assertEqual(len(packet), 120)
        self.assertEqual(packet[12:17], b"\x6f\xff\xff\x01\x00")
        self.assertEqual(packet[19], 1)
        self.assertEqual(self.codec.u16(packet, 17),
                         stp.crc16_ccitt(packet[12:17] + packet[19:20], 0xFFFF))
        self.assertEqual(self.codec.u16(packet, 118), self.codec.crc(packet[4:118]))
        with self.assertRaises(ValueError):
            stp.build_command(self.codec, 0x74, 0xC7, args=bytes(99))

    def test_lrt_v2_sequence_result_crc_and_negative_temperature(self):
        payload = bytearray(1248)
        struct.pack_into(">I", payload, 0, 2)
        payload[192:196] = bytes((1, 1, 1, 3))
        struct.pack_into(">h", payload, 214, -1250)
        struct.pack_into(">h", payload, 216, 2350)
        payload[222] = 0x14
        payload[232:240] = bytes((2, 1, 1, 1, 0, 2, 1, 1))
        struct.pack_into(">IHHBBH", payload, 240, 0x12345678, 64, 0xFFFF,
                         0x74, 6, 9)
        struct.pack_into(">H", payload, 254,
                         stp.crc16_ccitt(payload[232:254], 0xFFFF))
        struct.pack_into(">I", payload, 256, 3)
        result = stp.decode_lrt(self.codec, payload)
        self.assertTrue(result["command_block_valid"])
        self.assertEqual(result["command_seq"], 0xFFFF)
        self.assertEqual(result["last_result"], "unsupported")
        self.assertEqual(result["thermal_scene_min_c"], -12.5)
        self.assertEqual(result["thermal_spot_valid"], True)
        payload[249] ^= 1
        self.assertFalse(stp.decode_lrt(self.codec, payload)["command_block_valid"])

    def test_lrt_identity_availability_requires_crc(self):
        payload = bytearray(1248)
        struct.pack_into(">I", payload, 0, 2)
        payload[232] = 2
        payload[268:271] = bytes((1, 0x0F, 0))
        payload[272:292] = bytes(range(20))
        payload[292:324] = bytes(range(32))
        struct.pack_into(">H", payload, 324,
                         stp.crc16_ccitt(payload[268:324], 0xFFFF))
        result = stp.decode_lrt(self.codec, payload)
        self.assertTrue(result["identity_block_valid"])
        self.assertEqual(result["build_commit"], bytes(range(20)).hex())
        self.assertEqual(result["build_source_sha256"], bytes(range(32)).hex())
        self.assertFalse(result["storage_health_valid"])
        self.assertFalse(result["cpu_health_valid"])
        self.assertFalse(result["safe_mode_health_valid"])
        payload[300] ^= 1
        self.assertFalse(stp.decode_lrt(self.codec, payload)["identity_block_valid"])
        self.assertIsNone(stp.decode_lrt(self.codec, payload)["build_commit"])


if __name__ == "__main__":
    unittest.main()
