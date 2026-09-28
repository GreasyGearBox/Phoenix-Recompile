#!/usr/bin/env python3
"""
Synthetic tests for the Deeps Ashita 4.30 compatibility parser's variable-length
FFXI 0x028 action-packet cursor logic.

These are not game-runtime tests. They exercise the exact packet bit layout used
by the plugin for simple melee, multi-hit, additional-effect, spikes, and
multi-target actions. The first case is deliberately 35 bytes: the previous
failed build's blanket ~46-byte guard rejected it before parsing.
"""


def set_bits(buf, bit, length, value):
    for i in range(length):
        if (value >> i) & 1:
            pos = bit + i
            buf[pos // 8] |= 1 << (pos % 8)


def get_bits(buf, bit, length):
    v = 0
    for i in range(length):
        pos = bit + i
        if (buf[pos // 8] >> (pos % 8)) & 1:
            v |= 1 << i
    return v


def ensure_bits(buf, bit_count):
    need = (bit_count + 7) // 8
    if len(buf) < need:
        buf.extend(b'\0' * (need - len(buf)))


def emit_action(buf, reaction_bit, *, damage, message=1, animation=0,
                add_damage=None, add_message=163, spikes=False):
    """Write one action beginning at its reaction bit; return end cursor."""
    # Base action: reaction5, animation12, special7, knockback3, param17,
    # message10, flags31, added-effect flag1.
    ensure_bits(buf, reaction_bit + 86)
    set_bits(buf, reaction_bit + 0, 5, 0)
    set_bits(buf, reaction_bit + 5, 12, animation)
    set_bits(buf, reaction_bit + 17, 7, 0)
    set_bits(buf, reaction_bit + 24, 3, 0)
    set_bits(buf, reaction_bit + 27, 17, damage)
    set_bits(buf, reaction_bit + 44, 10, message)
    set_bits(buf, reaction_bit + 54, 31, 0)
    set_bits(buf, reaction_bit + 85, 1, 1 if add_damage is not None else 0)
    cursor = reaction_bit + 86

    if add_damage is not None:
        # Canonical optional added-effect body: animation/effect10, param17, msg10.
        ensure_bits(buf, cursor + 37)
        set_bits(buf, cursor + 0, 10, 0)
        set_bits(buf, cursor + 10, 17, add_damage)
        set_bits(buf, cursor + 27, 10, add_message)
        cursor += 37

    # Spike flag comes after optional added-effect body.
    ensure_bits(buf, cursor + 1)
    set_bits(buf, cursor, 1, 1 if spikes else 0)
    cursor += 1
    if spikes:
        # Canonical optional spike body: effect10, param14, msg10.
        ensure_bits(buf, cursor + 34)
        set_bits(buf, cursor + 0, 10, 0)
        set_bits(buf, cursor + 10, 14, 7)
        set_bits(buf, cursor + 24, 10, 44)
        cursor += 34
    return cursor


def build_packet(targets, *, actor=0x00123456, category=1, param=0):
    """targets = list[list[action kwargs]]"""
    buf = bytearray(24)
    set_bits(buf, 40, 32, actor)
    set_bits(buf, 72, 10, len(targets))
    set_bits(buf, 82, 4, category)
    set_bits(buf, 86, 16, param)

    target_base = 150
    for t_index, actions in enumerate(targets):
        ensure_bits(buf, target_base + 36)
        set_bits(buf, target_base + 0, 32, 0x00ABC000 + t_index)
        set_bits(buf, target_base + 32, 4, len(actions))
        reaction = target_base + 36
        for action in actions:
            reaction = emit_action(buf, reaction, **action)
        target_base = reaction
    return buf


def parse_like_plugin(buf):
    """Mirror the plugin's target/action cursor logic and return decoded actions."""
    size = len(buf)
    target_num = get_bits(buf, 72, 10)
    category = get_bits(buf, 82, 4)
    param = get_bits(buf, 86, 16)
    action_num = get_bits(buf, 182, 4)
    decoded = []

    start_bit = 150
    for i in range(target_num):
        for _ in range(action_num):
            assert start_bit + 122 <= size * 8, ('short base', start_bit, size)
            reaction_bit = start_bit + 36
            animation = get_bits(buf, start_bit + 41, 12)
            damage = get_bits(buf, start_bit + 63, 17)
            message = get_bits(buf, start_bit + 80, 10)
            has_add = get_bits(buf, start_bit + 121, 1)
            cursor = start_bit + 122
            add = None
            if has_add:
                assert cursor + 37 <= size * 8, ('short add', cursor, size)
                cursor += 10
                add_damage = get_bits(buf, cursor, 17)
                cursor += 17
                add_message = get_bits(buf, cursor, 10)
                cursor += 10
                add = (add_damage, add_message)
            assert cursor + 1 <= size * 8, ('short spike flag', cursor, size)
            spikes = bool(get_bits(buf, cursor, 1))
            cursor += 1
            if spikes:
                assert cursor + 34 <= size * 8, ('short spikes', cursor, size)
                cursor += 34
            decoded.append({
                'target': i,
                'reaction_bit': reaction_bit,
                'animation': animation,
                'damage': damage,
                'message': message,
                'add': add,
                'spikes': spikes,
                'end': cursor,
            })
            start_bit = cursor - 36
        start_bit += 36
        if i + 1 < target_num:
            assert start_bit + 36 <= size * 8
            action_num = get_bits(buf, start_bit + 32, 4)
    return category, param, decoded


def check(name, targets, expected, **header):
    packet = build_packet(targets, **header)
    category, param, got = parse_like_plugin(packet)
    assert got == expected, f'{name}:\nexpected {expected}\ngot      {got}'
    print(f'PASS: {name:<30} size={len(packet):>2} bytes actions={len(got)} category={category} param={param}')
    return packet


# 1. Normal one-hit melee. This intentionally has Param=0 and is only 35 bytes.
one = check(
    '35-byte one-hit melee',
    [[dict(damage=42)]],
    [dict(target=0, reaction_bit=186, animation=0, damage=42, message=1,
          add=None, spikes=False, end=273)],
)
old_guard_requires = ((150 + 210) >> 3) + 1
assert len(one) < old_guard_requires
print(f'      old failed build required ~{old_guard_requires} bytes and rejected this packet')

# 2. Two actions on the same target; verifies the second reaction starts at the
# exact end of action one rather than using a fixed packet size.
check(
    'two actions / one target',
    [[dict(damage=31), dict(damage=29, message=67)]],
    [
        dict(target=0, reaction_bit=186, animation=0, damage=31, message=1,
             add=None, spikes=False, end=273),
        dict(target=0, reaction_bit=273, animation=0, damage=29, message=67,
             add=None, spikes=False, end=360),
    ],
)

# 3. Additional-effect body shifts the following action by 37 bits.
check(
    'additional effect + next hit',
    [[dict(damage=50, add_damage=12, add_message=163), dict(damage=25)]],
    [
        dict(target=0, reaction_bit=186, animation=0, damage=50, message=1,
             add=(12, 163), spikes=False, end=310),
        dict(target=0, reaction_bit=310, animation=0, damage=25, message=1,
             add=None, spikes=False, end=397),
    ],
)

# 4. Spike block shifts the following action by 34 bits.
check(
    'spikes block + next hit',
    [[dict(damage=60, spikes=True), dict(damage=20)]],
    [
        dict(target=0, reaction_bit=186, animation=0, damage=60, message=1,
             add=None, spikes=True, end=307),
        dict(target=0, reaction_bit=307, animation=0, damage=20, message=1,
             add=None, spikes=False, end=394),
    ],
)

# 5. Two targets with different action counts; verifies per-target action-count refresh.
check(
    'two targets / variable counts',
    [
        [dict(damage=11)],
        [dict(damage=22), dict(damage=33, animation=4)],
    ],
    [
        dict(target=0, reaction_bit=186, animation=0, damage=11, message=1,
             add=None, spikes=False, end=273),
        dict(target=1, reaction_bit=309, animation=0, damage=22, message=1,
             add=None, spikes=False, end=396),
        dict(target=1, reaction_bit=396, animation=4, damage=33, message=1,
             add=None, spikes=False, end=483),
    ],
)

print('ALL SYNTHETIC ACTION-PACKET TESTS PASSED')
