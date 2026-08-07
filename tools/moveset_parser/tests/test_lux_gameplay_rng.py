from lux_gameplay_rng import Xorshift96GameplayState


def test_xorshift96_gameplay_known_vectors_use_four_transforms_per_word() -> None:
    state = Xorshift96GameplayState(0x12345678, 0x9ABCDEF0, 0x0FEDCBA9)

    assert state.draw_u32() == 0xCC5E09D5
    assert state.tuple == (0xE08FDBCE, 0xDEF7827D, 0xF2265066)

    assert state.draw_u32() == 0x9C84EE2D
    assert state.tuple == (0x3A676141, 0x827D294C, 0x249EA620)

    assert state.draw_u32() == 0xF20AB596
    assert state.tuple == (0xA9A4F354, 0x294C5C4C, 0x72E21A8E)


def test_xorshift96_gameplay_state_is_always_uint32() -> None:
    state = Xorshift96GameplayState(-1, 0x1_0000_0001, -0x12345678)
    assert state.tuple == (0xFFFFFFFF, 1, 0xEDCBA988)
    state.draw_u32()
    assert all(0 <= word <= 0xFFFFFFFF for word in state.tuple)
