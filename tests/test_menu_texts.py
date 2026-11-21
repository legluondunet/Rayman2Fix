"""Synthetic-only Rayman 2 menu text table tests."""
import struct
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from inspect_rayman2_sna import decode_sna
from inspect_rayman2_menu_texts import inspect,InvalidMenuData

SEED=bytes.fromhex("79ccb56a")
BASE=0x00100000
LANGS=["/C:LANGUAGE","/C:LANGUE","/C:IDIOMA","/C:LINGUA","/C:SPRACHE"]
STRIDE=666*4+8

def fixture():
    data=bytearray(5*STRIDE)
    empty=len(data)
    data+=b"\0"
    for k,tag in enumerate(LANGS):
        for j in range(666):
            struct.pack_into("<I",data,k*STRIDE+j*4,BASE+empty)
        for j,label in ((370,tag),(379,"RESOLUTION"),(380,"SAVE")):
            pos=len(data)
            data+=label.encode("cp1252")+b"\0"
            struct.pack_into("<I",data,k*STRIDE+j*4,BASE+pos)
    sna=SEED+struct.pack("<BBBiIIII",14,0,1,BASE,0,0,0,len(data))+data
    rtb=bytearray(b"\x01"+b"\0"*4+struct.pack("<BBI",14,0,5*666))
    for i in range(5*666):
        rtb+=struct.pack("<IBBBB",BASE+(i//666)*STRIDE+(i%666)*4,
                         14,0,205,205)
    dsb=bytearray(48)
    dsb[:4]=SEED
    struct.pack_into("<I",dsb,44,0x46)
    return decode_sna(bytes(sna)),bytes(rtb),decode_sna(bytes(dsb))

class Tests(unittest.TestCase):
    def test_menu_ids_all_languages(self):
        sna,rtb,dsb=fixture()
        report=inspect(sna,rtb,dsb)
        self.assertEqual(set(report["languages"]),set(("en","fr","es","it","de")))
        self.assertEqual(report["languages"]["fr"]["handles"]["379"],"RESOLUTION")
        self.assertEqual(report["rtb_relocations"],3330)
        self.assertFalse(report["resolution_submenu_identified"])

    def test_invalid_relations(self):
        sna,rtb,dsb=fixture()
        with self.assertRaises((InvalidMenuData,ValueError)):
            inspect(sna,rtb[:-1],dsb)
        corrupt=bytearray(rtb)
        struct.pack_into("<I",corrupt,11,0xF0000000)
        with self.assertRaises(InvalidMenuData):
            inspect(sna,bytes(corrupt),dsb)
        with self.assertRaises((InvalidMenuData,ValueError)):
            inspect(sna,rtb,dsb[:8])

if __name__=="__main__":
    unittest.main()
