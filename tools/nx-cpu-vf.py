# CPU V/F for this Switch (T210 A02, SKU 0x83 -> cpu speedo id 3, process 0),
# from Linux mainline TEGRA210_CPU_CVB_TABLE_ODN (DFLL) and CTCaer's L4T
# CPU_PLL_CVB_TABLE_ODN (PLL, +30% CVB_PLL_MARGIN, min 950, max 1257 mV).
# Rail: MAX77621, 606.25 mV + n x 6.25 mV.
def drc(a, b):  # DIV_ROUND_CLOSEST for ints (kernel semantics, sign-aware)
    q = (abs(a) + b // 2) // b
    return q if a >= 0 else -q
def cvb(s, c):
    c0, c1, c2 = c
    mv = drc(c2 * s, 100)
    return drc((mv + c1) * s, 100) + c0
def round_cvb(mv, vscale=1000, step=6250, off=606250):  # tegra_round_cvb_voltage, rounds up
    st, of = step * vscale, off * vscale
    uv = max(mv * 1000, of) - of
    uv = -(-uv // st) * step + off
    return uv // 1000
def round_v(mv, up, step=6250, off=606250):
    uv = max(mv * 1000, off) - off
    uv = (uv + (step - 1 if up else 0)) // step
    return (uv * step + off) // 1000
dfll = {204:721094,306:754040,408:786986,510:819932,612:852878,714:885824,816:918770,918:915716,
        1020:984662,1122:1017608,1224:1050554,1326:1083500,1428:1116446,1581:1130000,1683:1168000,1785:1227500}
pll = {1020:(-2875621,358099,-8585),1122:(-52225,104159,-2816),1224:(1076868,8356,-727),
       1326:(2208191,-84659,1240),1428:(2519460,-105063,1611),1581:(2889664,-122173,1834),
       1683:(5100873,-279186,4747),1785:(5100873,-279186,4747)}
for s in (1993, 1988, 2100):
    print(f"cpu speedo value {s}:")
    print("  MHz  DFLL mV (825..1227)  PLL raw mV  PLL +30% (950..1257)")
    for f in sorted(dfll):
        d = min(max(round_cvb(dfll[f]), round_v(825, 1)), round_v(1227, 0))
        if f in pll:
            raw = cvb(s, pll[f])
            m = round_cvb(130 * raw // 100)
            p = f"{raw/1000:8.1f}    {max(m, round_v(950,1))}" + ("  > max 1257: not allowed on PLL" if m > 1257 else "")
        else:
            p = "       0    950 (pll_min)"
        print(f"  {f:4d}  {d:6d}              {p}")
