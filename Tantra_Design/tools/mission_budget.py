"""Tantra mission budget by legs (2026-10-16). Anamezon only for acceleration and braking, jet 0.98 c.
Masses in kt. Each leg starts with full traps (Triton outbound, Parus containers for the return)."""
import math
VE = 0.98
DRY, TRAP_S, IRON, ARG_SCREEN, CHARGES = 7.88, 0.817, 4.0, 0.86, 30.3
SUPPORTS = 280.0

def need_L(v_cruise, reserve_c, m_end, trap_s=TRAP_S):
    z = 2 * math.atanh(v_cruise) + math.atanh(reserve_c)
    MR = math.exp(z / VE)
    return (MR - 1) * m_end / 4, MR

def staged_total(v, r, m_end, drop):
    """the iron returns to the rack below 0.5 c on braking and is dropped: the last part of braking runs without it"""
    z_low = math.atanh(0.5); z_hi = 2 * math.atanh(v) + math.atanh(r) - z_low
    m_mid = (m_end - drop) * math.exp(z_low / VE) + drop          # at 0.5 c, just before the drop
    m0 = m_mid * math.exp(z_hi / VE)
    return (m0 - m_end) / 4                                       # anamezon only (the dropped iron is not fuel)

ret_end = DRY + 4 * TRAP_S + IRON + ARG_SCREEN
print(f"Return leg end mass (dry {DRY} + traps {4*TRAP_S:.2f} + iron {IRON} + screen argon {ARG_SCREEN}) = {ret_end:.2f} kt")
print("\nA. Return leg dark planet -> Triton: anamezon per trap")
for v, r in ((0.834, 0.0), (0.834, 0.2), (0.834, 0.3), (0.834, 0.34), (0.80, 0.3), (0.75, 0.3)):
    L, MR = need_L(v, r, ret_end)
    ascent = ret_end + 4 * L + CHARGES * (ret_end + 4 * L) / 246.0 - ARG_SCREEN   # charges scaled to the ascent mass (30.3 kt for ~246 kt)
    print(f"  {v} c + reserve {r} c: {L:5.1f} kt/trap, MR {MR:5.2f}, ascent mass ~{ascent:4.0f} kt "
          f"{'> supports 280' if ascent > SUPPORTS else '<= supports 280'}")
print("\nB. What 47.5 kt/trap gives on the return leg")
z = VE * math.log((ret_end + 190) / ret_end)
print(f"  5/6 c with braking leaves reserve {math.tanh(z - 2*math.atanh(0.834)):.3f} c; trap loss after accel:", end=" ")
M0 = ret_end + 190; M1 = M0 / math.exp(math.atanh(0.834) / VE); per = (M1 - ret_end) / 4
M2 = M1 - per - TRAP_S; zb = VE * math.log(M2 / (ret_end - TRAP_S))
print("brakes" if zb >= math.atanh(0.834) else f"arrives at {math.tanh(math.atanh(0.834)-zb):.3f} c")
print("\nC. Iron used/dropped below 0.5 c on braking (screen not needed there)")
for r in (0.3, 0.34):
    print(f"  5/6 c + {r} c: {staged_total(0.834, r, ret_end, IRON):.1f} kt/trap")
print("\nD. Outbound leg Triton -> Zirda (end mass with the charges aboard for planetary work)")
for ch in (0.86, 10.0, 30.3):
    m_end = DRY + 4 * TRAP_S + IRON + ch
    L, _ = need_L(0.834, 0.3, m_end)
    print(f"  charges {ch:5.2f} kt aboard at arrival: {L:5.1f} kt/trap")
