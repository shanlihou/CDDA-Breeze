# -*- coding: utf-8 -*-
"""计算中秋（农历八月十五）对应的公历日期，并生成 src/cata_utility.cpp 里的查表数据。

用法：
    python tools/lunar_mid_autumn.py            # 校验已知中秋日期 + 打印抽查年份
    python tools/lunar_mid_autumn.py --cpp      # 输出可直接粘进 cata_utility.cpp 的偏移表

偏移 = 当年中秋距该年 9 月 1 日的天数（9/7 -> 6，10/1 -> 30，10/8 -> 37）。

天文算法：Meeus《Astronomical Algorithms》
  - ch.49 月亮相位（朔）
  - ch.25 太阳视黄经（冬至 / 中气）
农历规则（与紫金山天文台/香港天文台一致）：
  - 含冬至的月为十一月
  - 两冬至月之间若有 13 个月，则第一个无中气的月为闰月
  - 朔所在日（北京时间 UTC+8 的 0 点划分）为初一
"""
import math
import datetime

SYNODIC = 29.530588861


def _norm360(x):
    x = math.fmod(x, 360.0)
    return x + 360.0 if x < 0 else x


def new_moon_jde(k):
    """第 k 个朔（k=0 对应 2000-01-06 附近）的力学时儒略日"""
    T = k / 1236.85
    jde = (2451550.09766 + SYNODIC * k + 0.00015437 * T ** 2
           - 0.000000150 * T ** 3 + 0.00000000073 * T ** 4)
    E = 1.0 - 0.002516 * T - 0.0000074 * T ** 2
    M = math.radians(2.5534 + 29.10535670 * k - 0.0000014 * T ** 2 - 0.00000011 * T ** 3)
    Mp = math.radians(201.5643 + 385.81693528 * k + 0.0107582 * T ** 2
                      + 0.00001238 * T ** 3 - 0.000000058 * T ** 4)
    F = math.radians(160.7108 + 390.67050284 * k - 0.0016118 * T ** 2
                     - 0.00000227 * T ** 3 + 0.000000011 * T ** 4)
    Om = math.radians(124.7746 - 1.56375588 * k + 0.0020672 * T ** 2 + 0.00000215 * T ** 3)

    s = (-0.40720 * math.sin(Mp)
         + 0.17241 * E * math.sin(M)
         + 0.01608 * math.sin(2 * Mp)
         + 0.01039 * math.sin(2 * F)
         + 0.00739 * E * math.sin(Mp - M)
         - 0.00514 * E * math.sin(Mp + M)
         + 0.00208 * E * E * math.sin(2 * M)
         - 0.00111 * math.sin(Mp - 2 * F)
         - 0.00057 * math.sin(Mp + 2 * F)
         + 0.00056 * E * math.sin(2 * Mp + M)
         - 0.00042 * math.sin(3 * Mp)
         + 0.00042 * E * math.sin(M + 2 * F)
         + 0.00038 * E * math.sin(M - 2 * F)
         - 0.00024 * E * math.sin(2 * Mp - M)
         - 0.00017 * math.sin(Om)
         - 0.00007 * math.sin(Mp + 2 * M)
         + 0.00004 * math.sin(2 * Mp - 2 * F)
         + 0.00004 * math.sin(3 * M)
         + 0.00003 * math.sin(Mp + M - 2 * F)
         + 0.00003 * math.sin(2 * Mp + 2 * F)
         - 0.00003 * math.sin(Mp + M + 2 * F)
         + 0.00003 * math.sin(Mp - M + 2 * F)
         - 0.00002 * math.sin(Mp - M - 2 * F)
         - 0.00002 * math.sin(3 * Mp + M)
         + 0.00002 * math.sin(4 * Mp))

    # 额外修正项（Meeus 49 的 14 个行星摄动项）
    A1 = math.radians(299.77 + 0.107408 * k - 0.009173 * T ** 2)
    A2 = math.radians(251.88 + 0.016321 * k)
    A3 = math.radians(251.83 + 26.651886 * k)
    A4 = math.radians(349.42 + 36.412478 * k)
    A5 = math.radians(84.66 + 18.206239 * k)
    A6 = math.radians(141.74 + 53.303771 * k)
    A7 = math.radians(207.14 + 2.453732 * k)
    A8 = math.radians(154.84 + 7.306860 * k)
    A9 = math.radians(34.52 + 27.261239 * k)
    A10 = math.radians(207.19 + 0.121824 * k)
    A11 = math.radians(291.34 + 1.844379 * k)
    A12 = math.radians(161.72 + 24.198154 * k)
    A13 = math.radians(239.56 + 25.513099 * k)
    A14 = math.radians(331.55 + 3.592518 * k)
    extra = (0.000325 * math.sin(A1) + 0.000165 * math.sin(A2)
             + 0.000164 * math.sin(A3) + 0.000126 * math.sin(A4)
             + 0.000110 * math.sin(A5) + 0.000062 * math.sin(A6)
             + 0.000060 * math.sin(A7) + 0.000056 * math.sin(A8)
             + 0.000047 * math.sin(A9) + 0.000042 * math.sin(A10)
             + 0.000040 * math.sin(A11) + 0.000037 * math.sin(A12)
             + 0.000035 * math.sin(A13) + 0.000023 * math.sin(A14))
    return jde + s + extra


def delta_t_days(jd):
    """TD -> UT 的 ΔT（近似，秒级精度）"""
    y = 2000.0 + (jd - 2451545.0) / 365.25
    if y < 1900:
        t = y - 1860
        return (7.62 + 0.5737 * t - 0.251754 * t ** 2 + 0.01680668 * t ** 3
                - 0.0004473624 * t ** 4 + t ** 5 / 233174.0) / 86400.0
    if y < 1920:
        t = y - 1900
        return (-2.79 + 1.494119 * t - 0.0598939 * t ** 2
                + 0.0061966 * t ** 3 - 0.000197 * t ** 4) / 86400.0
    if y < 1941:
        t = y - 1920
        return (21.20 + 0.84493 * t - 0.076100 * t ** 2 + 0.0020936 * t ** 3) / 86400.0
    if y < 1961:
        t = y - 1950
        return (29.07 + 0.407 * t - t ** 2 / 233.0 + t ** 3 / 2547.0) / 86400.0
    if y < 1986:
        t = y - 1975
        return (45.45 + 1.067 * t - t ** 2 / 260.0 - t ** 3 / 718.0) / 86400.0
    if y < 2005:
        t = y - 2000
        return (63.86 + 0.3345 * t - 0.060374 * t ** 2 + 0.0017275 * t ** 3
                + 0.000651814 * t ** 4 + 0.00002373599 * t ** 5) / 86400.0
    if y < 2050:
        t = y - 2000
        return (62.92 + 0.32217 * t + 0.005589 * t ** 2) / 86400.0
    if y < 2150:
        u = (y - 1820) / 100
        return (-20 + 32 * u ** 2 - 0.5628 * (2150 - y)) / 86400.0
    u = (y - 1820) / 100
    return (-20 + 32 * u ** 2) / 86400.0


def jd_from_date(y, m, d):
    if m <= 2:
        y -= 1
        m += 12
    a = y // 100
    b = 2 - a + a // 4
    return math.floor(365.25 * (y + 4716)) + math.floor(30.6001 * (m + 1)) + d + b - 1524.5


def date_from_jd(jd):
    """UT 儒略日 -> 公历日期"""
    z = math.floor(jd + 0.5)
    f = jd + 0.5 - z
    if z < 2299161:
        a = z
    else:
        alpha = math.floor((z - 1867216.25) / 36524.25)
        a = z + 1 + alpha - alpha // 4
    b = a + 1524
    c = math.floor((b - 122.1) / 365.25)
    d = math.floor(365.25 * c)
    e = math.floor((b - d) / 30.6001)
    day = b - d - math.floor(30.6001 * e) + f
    month = e - 1 if e < 14 else e - 13
    year = c - 4716 if month > 2 else c - 4715
    return year, month, int(math.floor(day))


def bj_date_from_td(jd_td):
    """力学时 -> 北京时间的公历日期"""
    jd_ut = jd_td - delta_t_days(jd_td)
    return date_from_jd(jd_ut + 8.0 / 24.0)


def sun_apparent_longitude(jd):
    T = (jd - 2451545.0) / 36525.0
    L0 = _norm360(280.46646 + 36000.76983 * T + 0.0003032 * T * T)
    M = math.radians(_norm360(357.52911 + 35999.05029 * T - 0.0001537 * T * T))
    C = ((1.914602 - 0.004817 * T - 0.000014 * T * T) * math.sin(M)
         + (0.019993 - 0.000101 * T) * math.sin(2 * M)
         + 0.000289 * math.sin(3 * M))
    omega = math.radians(125.04 - 1934.136 * T)
    return _norm360(L0 + C - 0.00569 - 0.00478 * math.sin(omega))


def solar_term_jd(year, target_deg):
    """求某年内太阳视黄经到达 target_deg 的力学时儒略日（牛顿迭代）"""
    # 粗估：春分 3/20 → 0 度，每度约 1.0146 天
    guess = jd_from_date(year, 1, 1) + (target_deg + 280.0) % 360.0 * 365.2422 / 360.0
    jd = guess
    for _ in range(30):
        diff = (target_deg - sun_apparent_longitude(jd) + 180.0) % 360.0 - 180.0
        jd += diff * 365.2422 / 360.0
        if abs(diff) < 1e-7:
            break
    return jd


def winter_solstice_jd(year):
    """year 年 12 月的冬至（力学时）"""
    return solar_term_jd(year, 270.0)


def moon_phase_k_near(jd):
    return round((jd - 2451550.09766) / SYNODIC)


def new_moon_before(jd):
    """不晚于 jd 的朔（力学时）"""
    k = moon_phase_k_near(jd) + 1
    while True:
        t = new_moon_jde(k)
        if t <= jd:
            return t
        k -= 1


def next_new_moon(jd):
    k = moon_phase_k_near(jd)
    while new_moon_jde(k) <= jd:
        k += 1
    return new_moon_jde(k)


def months_between(dz0, dz1):
    """返回从「含冬至 dz0 的月」到「下一冬至月」之间的所有月的朔时刻列表（不含 dz1 所在月）"""
    starts = []
    m = new_moon_before(dz0)
    while True:
        starts.append(m)
        m = next_new_moon(m)
        if m >= new_moon_before(dz1):
            break
    return starts


def has_zhongqi(m_start, m_end):
    """该朔望月内是否含中气（太阳视黄经为 30 度整数倍的时刻）"""
    lo = sun_apparent_longitude(m_start)
    hi = sun_apparent_longitude(m_end)
    # 该月太阳黄经扫过的区间
    deg = math.floor(lo / 30.0) * 30.0
    for i in range(3):
        cand = (deg + 30.0 * i)
        # 归一化到 [lo, lo+span]
        rel = (cand - lo) % 360.0
        span = (hi - lo) % 360.0
        if 0.0 < rel < span:
            return True
    return False


def lunar_year_months(base_year):
    """生成 lunar_year 农历年（以 base_year 年冬至所在月为十一月）的月表
    返回 [(月序号, 是否闰月, 月首朔 JD_TD), ...]，月序号 11,12,1,2,...
    """
    dz0 = winter_solstice_jd(base_year)
    dz1 = winter_solstice_jd(base_year + 1)
    starts = months_between(dz0, dz1)  # 十二月间所有月，长度 12 或 13
    n = len(starts)
    result = []
    num = 11
    if n == 12:
        for i in range(n):
            end = new_moon_jde(moon_phase_k_near(starts[i]) + 1)
            result.append((num if num <= 12 else num - 12, False, starts[i]))
            num += 1
        return result
    # 13 个月：找第一个无中气的月（跳过十一月本身）
    leap_index = -1
    for i in range(1, n):
        end = new_moon_jde(moon_phase_k_near(starts[i]) + 1)
        if not has_zhongqi(starts[i], end):
            leap_index = i
            break
    for i in range(n):
        end = new_moon_jde(moon_phase_k_near(starts[i]) + 1)
        if i == leap_index:
            result.append((result[-1][0], True, starts[i]))
        else:
            cur = num if num <= 12 else num - 12
            result.append((cur, False, starts[i]))
            num += 1
    return result


def bj_day_start(jd_td):
    """把时刻换算成「其所属北京日的 0 点 UT 儒略日」"""
    yy, mm, dd = bj_date_from_td(jd_td)
    return jd_from_date(yy, mm, dd)


def solar_to_lunar(y, m, d):
    target = jd_from_date(y, m, d)
    base = y - 1 if m <= 2 else y
    for by in (base, base - 1, base + 1):
        for num, is_leap, start in lunar_year_months(by):
            s0 = bj_day_start(start)
            s1 = bj_day_start(new_moon_jde(moon_phase_k_near(start) + 1))
            if s0 <= target < s1:
                # by 年冬至为十一月，该序列的正月落在 by+1 公历年，故农历年为 by+1
                return by + 1, num, is_leap, int(target - s0) + 1
    raise RuntimeError("no lunar month found for %04d-%02d-%02d" % (y, m, d))


def mid_autumn_date(year):
    """返回 year 年中秋（农历八月十五）对应的公历 (y, m, d)"""
    # 以 by 年冬至月为十一月的序列，其八月落在公历 by+1 年，故先试 by = year-1
    for by in (year - 1, year):
        for num, is_leap, start in lunar_year_months(by):
            if num == 8 and not is_leap:
                return bj_date_from_td(start + 14)
    raise RuntimeError("not found")


def cpp_table(first_year=1900, last_year=2100, per_line=16):
    import datetime
    offs = []
    for y in range(first_year, last_year + 1):
        _, mm, dd = mid_autumn_date(y)
        offs.append((datetime.date(2000, mm, dd) - datetime.date(2000, 9, 1)).days)
    lines = []
    for i in range(0, len(offs), per_line):
        lines.append("    " + ", ".join("%2d" % v for v in offs[i:i + per_line]) + ",")
    return "\n".join(lines)


if __name__ == "__main__":
    import sys as _sys
    if "--cpp" in _sys.argv:
        print(cpp_table())
        _sys.exit(0)
    # 已知中秋日期校验
    known = {
        2011: (9, 12), 2012: (9, 30), 2013: (9, 19), 2014: (9, 8),
        2015: (9, 27), 2016: (9, 15), 2017: (10, 4), 2018: (9, 24),
        2019: (9, 13), 2020: (10, 1), 2021: (9, 21), 2022: (9, 10),
        2023: (9, 29), 2024: (9, 17), 2025: (10, 6), 2026: (9, 25),
    }
    ok = True
    for y, (mm, dd) in sorted(known.items()):
        got = mid_autumn_date(y)
        mark = "OK " if (got[1], got[2]) == (mm, dd) else "BAD"
        if mark == "BAD":
            ok = False
        print("%s %d -> %04d-%02d-%02d (expect %02d-%02d)" % (mark, y, got[0], got[1], got[2], mm, dd))
    print("ALL OK" if ok else "HAS MISMATCH")
