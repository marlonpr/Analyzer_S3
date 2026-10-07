"""Replay V10 qualification/association with original microsecond timestamps.

Raw and QUALIFIED emission order is preserved. A first edge is backfilled only
when its channel's next edge proves an alternating 1 Hz interval. Incomplete
groups therefore get TRAIN_MAX + association-window grace, matching firmware.
"""
from __future__ import annotations
from dataclasses import dataclass, field
from pathlib import Path
import re

EDGE = re.compile(r"ANZ\|(EDGE|QUALIFIED)\|(\d+)\|(\d+)\|([123])\|([^|]+)\|(?:q=\d+\|)?(\d+)\|([01])(?:\s|$)")
GROUP = re.compile(r"ANZ\|SKEW\|(\d+)\|(\d+)\|(.+)")
COUNTS = re.compile(r"ANZ\|COUNTS\|(\d+)\|(\d+)\|.*\bdropped=(\d+)")

@dataclass(frozen=True)
class Edge:
    run: int
    trial: int
    channel: int
    name: str
    timestamp: int
    level: int

@dataclass
class Group:
    run: int
    trial: int
    train: int
    boundary: int
    timestamps: dict[int, int]
    names: dict[int, str]
    level: int | None = None

    @property
    def span_us(self):
        return max(self.timestamps.values()) - min(self.timestamps.values())

@dataclass
class Replay:
    groups: list[Group] = field(default_factory=list)
    missing: list[dict] = field(default_factory=list)
    qualified_counts: dict[tuple[int, int, int], int] = field(default_factory=dict)
    dropped: int = 0
    errors: list[str] = field(default_factory=list)
    source: str = ""

    def passes(self, limit_us=100, start_boundaries=3):
        trains = self.trains()
        return bool(trains) and not self.missing and not self.dropped and not self.errors and all(
            len(groups) >= start_boundaries and all(g.span_us <= limit_us for g in groups[:start_boundaries])
            for groups in trains.values())

    def trains(self):
        out = {}
        for g in self.groups:
            out.setdefault((g.run, g.trial, g.train), []).append(g)
        return out

def replay_edges(edges, qualified=False, train_min=995000, train_max=1005000,
                 associate_us=5000, new_train_gap_us=1500000):
    result = Replay(source="QUALIFIED" if qualified else "EDGE")
    sessions = {}

    def session(e):
        return sessions.setdefault((e.run, e.trial), dict(previous={}, emitted={}, names={},
            pending=[], train=0, boundary=0, last=None))

    def emit(e):
        s = session(e)
        key = (e.run, e.trial, e.channel)
        result.qualified_counts[key] = result.qualified_counts.get(key, 0) + 1
        for p in s['pending'][:]:
            if e.timestamp - max(p['ts'].values()) > train_max + associate_us:
                result.missing.append(p)
                s['pending'].remove(p)
        choices = [p for p in s['pending'] if p['level'] == e.level and e.channel not in p['ts']
            and abs(e.timestamp - p['anchor']) <= associate_us
            and max([e.timestamp] + list(p['ts'].values())) - min([e.timestamp] + list(p['ts'].values())) <= associate_us]
        if choices:
            p = min(choices, key=lambda p: abs(e.timestamp - p['anchor']))
        else:
            p = dict(run=e.run, trial=e.trial, anchor=e.timestamp, level=e.level, ts={})
            s['pending'].append(p)
        p['ts'][e.channel] = e.timestamp
        if len(p['ts']) == 3:
            t = min(p['ts'].values())
            if s['last'] is None or t - s['last'] > new_train_gap_us:
                s['train'] += 1
                s['boundary'] = 0
            elif t <= s['last']:
                result.errors.append('completed groups arrived out of timestamp order')
            result.groups.append(Group(e.run, e.trial, s['train'], s['boundary'],
                dict(p['ts']), dict(s['names']), e.level))
            s['boundary'] += 1
            s['last'] = t
            s['pending'].remove(p)

    for e in edges:
        s = session(e)
        if e.channel in s['names'] and s['names'][e.channel] != e.name:
            result.errors.append('channel label changes inside a capture')
        s['names'][e.channel] = e.name
        if qualified:
            emit(e)
            continue
        prev = s['previous'].get(e.channel)
        good = prev is not None and train_min <= e.timestamp - prev.timestamp <= train_max and e.level != prev.level
        if good:
            if not s['emitted'].get(e.channel, False):
                emit(prev)
            emit(e)
        s['previous'][e.channel] = e
        s['emitted'][e.channel] = good
    for s in sessions.values():
        result.missing.extend(s['pending'])
    return result

def load_capture(path: Path, prefer_qualified=False, **kwargs):
    raw, qual, skew = [], [], []
    dropped = {}
    for line in path.read_text(encoding='utf-8-sig', errors='replace').splitlines():
        m = EDGE.search(line)
        if m:
            e = Edge(int(m[2]), int(m[3]), int(m[4])-1, m[5], int(m[6]), int(m[7]))
            (raw if m[1] == 'EDGE' else qual).append(e)
        m = COUNTS.search(line)
        if m:
            key = (int(m[1]), int(m[2]))
            dropped[key] = max(dropped.get(key, 0), int(m[3]))
        m = GROUP.search(line)
        if m:
            f = dict(part.split('=', 1) for part in m[3].split('|') if '=' in part)
            ts = {k: int(v) for k, v in f.items() if re.fullmatch(r'ESP\d+', k)}
            if len(ts) == 3:
                # In a grouped-only capture names carry physical identity.
                names = {i: name+'_COMMIT' for i, name in enumerate(ts)}
                skew.append(Group(int(m[1]), int(m[2]), int(f['train']), int(f['boundary']),
                    {i: t for i, t in enumerate(ts.values())}, names))
    if prefer_qualified and qual:
        out = replay_edges(qual, qualified=True, **kwargs)
    elif raw:
        out = replay_edges(raw, **kwargs)
    elif qual:
        out = replay_edges(qual, qualified=True, **kwargs)
    else:
        out = Replay(groups=skew, source='SKEW')
        keys = [(g.run, g.trial, g.train, g.boundary) for g in skew]
        if len(keys) != len(set(keys)):
            out.errors.append('duplicate grouped boundary')
        for groups in out.trains().values():
            if [g.boundary for g in groups] != list(range(len(groups))):
                out.errors.append('missing/out-of-order grouped boundary')
    out.dropped = sum(dropped.values())
    return out
