"""Power Drift (MAME set `pdrift`, World Rev A) ROM table.

This file contains only file names, sizes and hashes: facts needed to identify a
user's own dump. It contains no game data.

Sources: MAME src/mame/sega/segaybd.cpp ROM_START(pdrift); CRC-32 and SHA-1 as
published there. Layout follows the board's interleave, not the zip order.

This is the pdrift set (World, Rev A). MAME's pdrifta set (World) is a
different dump: its sub Y EPROMs are epr-12019.54/epr-12018.53 rather than
epr-12019a.54/epr-12018a.53, so it is not compatible with this recompilation.
"""

PDRIFT = {
    "set": "pdrift",
    "title": "Power Drift (World, Rev A)",
    "regions": {
        "main": {
            "kind": "w16",
            "size": 0x80000,
            "files": [
                ("epr-12017.25", 0x20000, 0x31190322, "18df4bd6078b1c76c7061c05a476a7cbf7d0b37b", 0x000000),
                ("epr-12016.24", 0x20000, 0x499f64a6, "506652bdb0f3d23a2cf7ff29732b8ec77c389b46", 0x000001),
                ("epr-11748.27", 0x20000, 0x82a76cab, "f8d3fe059e18896cd0e64711f1a3ee8b6372b4e0", 0x040000),
                ("epr-11747.26", 0x20000, 0x9796ece5, "f84f5689c2edc0853ff173ce20f93f89758b2f31", 0x040001),
            ],
        },
        "subx": {
            "kind": "w16",
            "size": 0x40000,
            "files": [
                ("epr-11905.81", 0x20000, 0x1cf68109, "198c1f9d4278586d1e0f50acaaf08e60ba464f8a", 0x000000),
                ("epr-11904.80", 0x20000, 0xbb993681, "c5107e8d52cba3a0fc813ffa9e3752e0f9c3c0df", 0x000001),
            ],
        },
        "suby": {
            "kind": "w16",
            "size": 0x40000,
            "files": [
                ("epr-12019a.54", 0x20000, 0x11188a30, "42dd0344d92529848b53a8cec4c145237ccd5b51", 0x000000),
                ("epr-12018a.53", 0x20000, 0x1c582e1f, "c32d2f921554bddd7dedcb81e231aa91f50fa27b", 0x000001),
            ],
        },
        "bsprite": {
            "kind": "w16",
            "size": 0x80000,
            "files": [
                ("epr-11789.16", 0x20000, 0xb86f8d2b, "a053f2021841fd0ef89fd3f28050a698b36c435e", 0x000000),
                ("epr-11791.14", 0x20000, 0x36b2910a, "9948b91837f944a7a606542fa685525e74bbe398", 0x000001),
                ("epr-11790.17", 0x20000, 0x2a564e66, "5f30fc15bfd017d75cfffe1e9e62ed0bcf32a98e", 0x040000),
                ("epr-11792.15", 0x20000, 0xc85caf6e, "2411ea99ec7f6e2b0b4f219e86ff2172539ad2c4", 0x040001),
            ],
        },
        "ysprite": {
            "kind": "x64",
            "size": 0x400000,
            "files": [
                ("epr-11757.67", 0x20000, 0xe46dc478, "baf79e230aef3d63fb50373b2b1626f7c56ee94f", 0x000000),
                ("epr-11758.75", 0x20000, 0x5b435c87, "6b42b08e73957c36cd8faa896ca14461d00afd29", 0x000001),
                ("epr-11773.63", 0x20000, 0x1b5d5758, "54f58a274740a0566e0553d145c0c284ffd1d36b", 0x000002),
                ("epr-11774.71", 0x20000, 0x2ca0c170, "7de74c045bf084659ba70da9458d720125ff25ae", 0x000003),
                ("epr-11759.86", 0x20000, 0xac8111f6, "6412716dc97ae697b438d9c9cd554d1087416bc2", 0x000004),
                ("epr-11760.114", 0x20000, 0x91282af9, "fddee7982949b7da724c7830e7bd139aeb84672d", 0x000005),
                ("epr-11775.82", 0x20000, 0x48225793, "ee003c2ea24c14e0968da94bac139735660932fe", 0x000006),
                ("epr-11776.110", 0x20000, 0x78c46198, "d299e631843da47cb7a46103d52a3dabfab71746", 0x000007),
                ("epr-11761.66", 0x20000, 0xbaa5d065, "56dc71814e3f0f327781b0c1587038351c60f7b7", 0x100000),
                ("epr-11762.74", 0x20000, 0x1d1af7a5, "86c02565b5aca201588c98678fb0c54faa8d4d6b", 0x100001),
                ("epr-11777.62", 0x20000, 0x9662dd32, "454ec914b6c936f692bf90d2232c8169acec470a", 0x100002),
                ("epr-11778.70", 0x20000, 0x2dfb7494, "4b9f1609e425c5e634e95dbc2d0ca820dd9212bc", 0x100003),
                ("epr-11763.85", 0x20000, 0x1ee23407, "776c868e0e4e601fd6d0a83561b064b4be0560e2", 0x100004),
                ("epr-11764.113", 0x20000, 0xe859305e, "aafcc3209a4fb6e0e8169ae6cce386b370b824f7", 0x100005),
                ("epr-11779.81", 0x20000, 0xa49cd793, "efe77949be39a2ff88b50bfb2b4664b9267d9a09", 0x100006),
                ("epr-11780.109", 0x20000, 0xd514ed81, "fbac3ad085363972a79e77aebb7fdae2200e7cda", 0x100007),
                ("epr-11765.65", 0x20000, 0x649e2dff, "a6c61b71d08b31a0ca175ab0404e2eaf1d09ccc2", 0x200000),
                ("epr-11766.73", 0x20000, 0xd92fb7fc, "2f5c2d88ae0766351b9efe8ffcbebc88fc3a6c59", 0x200001),
                ("epr-11781.61", 0x20000, 0x9692d4cd, "967351ba2c781ca865e3c1ee9eeef1aad2247c27", 0x200002),
                ("epr-11782.69", 0x20000, 0xc913bb43, "9bc15a3180cf4c3134bb55e99e6092f0faf95c56", 0x200003),
                ("epr-11767.84", 0x20000, 0x1f8ad054, "289f5795116ee29540f28e35c3b4f72adeca7891", 0x200004),
                ("epr-11768.112", 0x20000, 0xdb2c4053, "a5b6daa6deb7afb0019e289acb81c82d507ec93a", 0x200005),
                ("epr-11783.80", 0x20000, 0x6d189007, "dd871ea3166fdcb59d49707d35dde8b6c7fdc76b", 0x200006),
                ("epr-11784.108", 0x20000, 0x57f5fd64, "6aff54d3f3f76ce0f1a93485d1a35a3987d456d9", 0x200007),
                ("epr-11769.64", 0x20000, 0x28f0ab51, "d7cb7b83e5d85eb59d34cfd5c0d8e6c7ff81e24c", 0x300000),
                ("epr-11770.72", 0x20000, 0xd7557ea9, "62430505d399ee2cc0f94e03144860056345573c", 0x300001),
                ("epr-11785.60", 0x20000, 0xe6ef32c4, "869ba3816f5e3125f613f3b284fec74cd19db79e", 0x300002),
                ("epr-11786.68", 0x20000, 0x2066b49d, "905ce70c921043d07591422a87fedd6e897ff38e", 0x300003),
                ("epr-11771.83", 0x20000, 0x67635618, "f690ace026130ecb95532c92f2ad3741d0d167c1", 0x300004),
                ("epr-11772.111", 0x20000, 0x0f798d3a, "71565ce28b93ae50d64af8c965fba6408a07f031", 0x300005),
                ("epr-11787.79", 0x20000, 0xe631dc12, "3fd6db2eb297890b35dec566b6a90fc2d96bd085", 0x300006),
                ("epr-11788.107", 0x20000, 0x8464c66e, "af93cbcc50acbd929d0298fb9a75da0369e13ff7", 0x300007),
            ],
        },
        "z80": {
            "kind": "flat",
            "size": 0x10000,
            "files": [
                ("epr-11899.102", 0x10000, 0xed9fa889, "25d1a069254b34c31d8ee82d301ada895e8dc391", 0x000000),
            ],
        },
        "pcm": {
            "kind": "flat",
            "size": 0x200000,
            "fill": 0xFF,
            "files": [
                ("mpr-11754.107", 0x80000, 0xebeb8484, "269f33cb1a9be126bada858e25291385d48686a2", 0x000000),
                # ROM_RELOAD 4x, 128 KB each
                ("epr-11756.106", 0x20000, 0x12e43f8a, "0f9a11ba6b7c1a352daa1146a01ce147945e91e4", 0x080000, 4),
                ("epr-11755.105", 0x20000, 0xc2db1244, "c98fe17c9f04a639a862cc2a86fab17d1f5d025c", 0x100000, 4),
            ],
        },
    },
    # Regions we do not load (drive board is stubbed; user1 is a split copy of PCM)
    "ignored": ["drive_board", "user1"],
}

# The bank geometry used by the video/audio models.
YSPRITE_BANKS = 8           # 512 KB each
BSPRITE_BANKS = 4           # 128 KB each
PCM_BANK_MASK = 0xF8
IRQ2_LINE = 170             # MAME's tuned value
