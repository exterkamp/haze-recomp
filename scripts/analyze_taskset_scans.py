"""Validate and summarize bounded SPURS taskset scan profiling CSVs.

QPC durations are wall time. ``*_thread_cycles`` remain raw QueryThreadCycleTime
counts and are never converted to milliseconds. Summed scan wall time can overlap
across dispatcher threads, so the report includes both sum and interval-union
occupancy. Per-present normalization is descriptive occupancy, not a speedup claim.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import json
from pathlib import Path


ROW_FIELDS = {
    "ticket", "taskset_ea", "dispatch_cause", "host_tid", "begin_qpc", "end_qpc",
    "qpc_valid", "total_ticks", "prep_ticks", "scan_ticks", "total_thread_cycles",
    "prep_thread_cycles", "scan_thread_cycles", "total_cycles_valid",
    "prep_cycles_valid", "watch_ticks", "slots", "eligible", "claimed",
    "dispatch_attempts", "dispatch_ok", "dispatch_failed",
}
META_REQUIRED = {"qpc_frequency", "capacity", "next_ticket", "overwritten",
                 "dropped_busy", "dropped_stale", "max_discarded_end_qpc",
                 "capture_start_qpc", "capture_end_qpc", "complete", "inflight",
                 "retained_rows", "failure_reason", "discarded_qpc_invalid"}
FLAGS = ("qpc_valid", "total_cycles_valid", "prep_cycles_valid")


def _uint(value: str, field: str) -> int:
    try:
        parsed = int(value, 16) if value.lower().startswith("0x") else int(value, 10)
        if parsed < 0:
            raise ValueError
        return parsed
    except (AttributeError, ValueError) as exc:
        raise ValueError(f"invalid unsigned integer in {field}: {value!r}") from exc


def _metadata(line: str) -> dict[str, int]:
    if not line.startswith("#"):
        raise ValueError("missing taskset-scan metadata row")
    values = next(csv.reader([line[1:].strip()]))
    if len(values) % 2 or len(values[::2]) != len(set(values[::2])):
        raise ValueError("malformed or duplicate taskset-scan metadata keys")
    meta = {}
    for key, value in zip(values[::2], values[1::2]):
        if key == "failure_reason":
            meta[key] = value
            continue
        meta[key] = _uint(value, key)
    missing = META_REQUIRED - meta.keys()
    if missing:
        raise ValueError(f"missing taskset-scan metadata fields: {sorted(missing)}")
    if meta["qpc_frequency"] <= 0 or meta["capacity"] <= 0:
        raise ValueError("invalid taskset-scan frequency or capacity")
    if any(value < 0 for key, value in meta.items()
           if key != "qpc_frequency" and isinstance(value, int)):
        raise ValueError("negative taskset-scan metadata")
    return meta


def _read_capture(path: Path) -> tuple[dict[str, int], list[dict]]:
    with Path(path).open(newline="", encoding="utf-8-sig") as stream:
        meta = _metadata(stream.readline())
        reader = csv.DictReader(stream)
        if not reader.fieldnames or len(reader.fieldnames) != len(set(reader.fieldnames)):
            raise ValueError("missing or duplicate taskset-scan CSV header")
        missing = ROW_FIELDS - set(reader.fieldnames)
        if missing:
            raise ValueError(f"missing taskset-scan columns: {sorted(missing)}")
        rows = []
        for line, raw in enumerate(reader, start=3):
            if None in raw or any(value is None for value in raw.values()):
                raise ValueError(f"nonrectangular taskset-scan row at line {line}")
            row = {key: (int(value, 16) if key == "taskset_ea" else _uint(value, key))
                   for key, value in raw.items()}
            if row["taskset_ea"] < 0:
                raise ValueError(f"invalid taskset EA at line {line}")
            rows.append(row)
    return meta, rows


def _validate(meta: dict[str, int], rows: list[dict]) -> dict:
    if meta["complete"] != 1 or meta["inflight"] != 0:
        raise ValueError("taskset-scan capture is incomplete or has active writers at dump")
    if meta["failure_reason"] not in ("", "none"):
        raise ValueError("complete taskset-scan capture has a failure reason")
    if meta["capture_start_qpc"] <= 0 or meta["capture_end_qpc"] <= meta["capture_start_qpc"]:
        raise ValueError("invalid taskset-scan capture bounds")
    capacity, next_ticket = meta["capacity"], meta["next_ticket"]
    tickets = [row["ticket"] for row in rows]
    if tickets != sorted(set(tickets)) or any(ticket >= next_ticket for ticket in tickets):
        raise ValueError("retained tickets are duplicated, out of order, or outside ticket bounds")
    if len(rows) != meta["retained_rows"] or len(rows) > capacity:
        raise ValueError("retained row count disagrees with metadata or ring capacity")
    if meta["retained_rows"] + meta["overwritten"] + meta["dropped_busy"] + meta["dropped_stale"] != next_ticket:
        raise ValueError("ticket allocations do not balance retained, overwritten, and dropped rows")

    frequency = meta["qpc_frequency"]
    invalid_qpc = invalid_cycles = 0
    for row in rows:
        if any(row[name] not in (0, 1) for name in FLAGS):
            raise ValueError(f"invalid validity flag for ticket {row['ticket']}")
        if row["taskset_ea"] == 0 or row["host_tid"] == 0:
            raise ValueError(f"invalid scan identity for ticket {row['ticket']}")
        if row["slots"] != 128 or any(row[key] > row["slots"] for key in ("eligible", "claimed")):
            raise ValueError(f"invalid task/slot counts for ticket {row['ticket']}")
        if row["dispatch_ok"] + row["dispatch_failed"] != row["dispatch_attempts"]:
            raise ValueError(f"dispatch outcome counts do not balance for ticket {row['ticket']}")
        if row["dispatch_attempts"] > row["claimed"]:
            raise ValueError(f"more dispatch attempts than claims for ticket {row['ticket']}")
        if row["claimed"] > row["eligible"]:
            raise ValueError(f"more claims than eligible tasks for ticket {row['ticket']}")
        if row["dispatch_attempts"] != row["claimed"]:
            raise ValueError(f"dispatch attempts do not match claimed tasks for ticket {row['ticket']}")
        if row["qpc_valid"]:
            if row["begin_qpc"] <= 0 or row["end_qpc"] < row["begin_qpc"]:
                raise ValueError(f"invalid QPC interval for ticket {row['ticket']}")
            if row["total_ticks"] != row["end_qpc"] - row["begin_qpc"] or row["prep_ticks"] > row["total_ticks"]:
                raise ValueError(f"total ticks disagree with QPC for ticket {row['ticket']}")
        else:
            invalid_qpc += 1
        if not (row["total_cycles_valid"] and row["prep_cycles_valid"]):
            invalid_cycles += 1
        if row["prep_cycles_valid"] and not row["total_cycles_valid"]:
            raise ValueError(f"valid prep cycles without valid total cycles for ticket {row['ticket']}")
        if row["prep_cycles_valid"] and row["prep_thread_cycles"] > row["total_thread_cycles"]:
            raise ValueError(f"prep cycles exceed total cycles for ticket {row['ticket']}")
        if row["total_cycles_valid"] and row["prep_cycles_valid"]:
            if row["scan_thread_cycles"] != row["total_thread_cycles"] - row["prep_thread_cycles"]:
                raise ValueError(f"scan cycles do not reconcile for ticket {row['ticket']}")
        if row["qpc_valid"] and row["scan_ticks"] != row["total_ticks"] - row["prep_ticks"]:
            raise ValueError(f"scan ticks do not reconcile for ticket {row['ticket']}")
        if row["dispatch_cause"] < 0:
            raise ValueError("negative dispatch cause")
    return {"retained": len(rows), "invalid_qpc_rows": invalid_qpc,
            "invalid_cycle_rows": invalid_cycles, "scale_ms_per_qpc": 1000.0 / frequency}


def _union_ticks(intervals: list[tuple[int, int]]) -> int:
    ordered = sorted((a, b) for a, b in intervals if b > a)
    if not ordered:
        return 0
    total = 0
    start, end = ordered[0]
    for lo, hi in ordered[1:]:
        if lo <= end:
            end = max(end, hi)
        else:
            total += end - start
            start, end = lo, hi
    return total + end - start


def _percentile(values: list[float], p: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = (len(ordered) - 1) * p
    low = int(index)
    high = min(low + 1, len(ordered) - 1)
    return ordered[low] + (ordered[high] - ordered[low]) * (index - low)


def _read_windows(path: Path, frequency: int) -> list[dict]:
    data = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    if (data.get("qpc_frequency") != frequency or data.get("complete") is not True or
            not isinstance(data.get("blocks"), list)):
        raise ValueError("windows QPC frequency or blocks are missing/mismatched")
    result = []
    for i, block in enumerate(data["blocks"]):
        start, end = block.get("start_qpc"), block.get("end_qpc")
        if not isinstance(start, int) or not isinstance(end, int) or start <= 0 or end <= start:
            raise ValueError(f"invalid QPC window block {i}")
        if result and start < result[-1]["end_qpc"]:
            raise ValueError("measurement windows overlap or are out of order")
        result.append({"index": i, "start_qpc": start, "end_qpc": end,
                       "label": block.get("camera_token", block.get("flags", i))})
    if not result:
        raise ValueError("no measurement windows")
    return result


def _read_presents(path: Path, frequency: int) -> list[dict]:
    with Path(path).open(newline="", encoding="utf-8-sig") as stream:
        header = stream.readline().strip()
        prefix = "# qpc_frequency="
        if not header.startswith(prefix):
            raise ValueError("presents CSV lacks QPC frequency metadata")
        try:
            present_frequency = int(header[len(prefix):])
        except ValueError as exc:
            raise ValueError("invalid presents QPC frequency") from exc
        if present_frequency != frequency:
            raise ValueError("presents and taskset-scan QPC frequencies differ")
        reader = csv.DictReader(stream)
        required = {"present_id", "guest_frame", "qpc"}
        if not reader.fieldnames or not required.issubset(reader.fieldnames):
            raise ValueError("presents CSV lacks present_id/guest_frame/qpc")
        rows = []
        for line, raw in enumerate(reader, start=3):
            if None in raw or any(value is None for value in raw.values()):
                raise ValueError(f"nonrectangular presents row at line {line}")
            rows.append({key: _uint(raw[key], key) for key in required})
    for prev, cur in zip(rows, rows[1:]):
        if cur["present_id"] != prev["present_id"] + 1 or cur["qpc"] <= prev["qpc"]:
            raise ValueError("present IDs or QPC timestamps are not strictly increasing")
    return rows


def analyze(scan_path: Path, windows_path: Path, presents_path: Path) -> dict:
    meta, rows = _read_capture(Path(scan_path))
    validation = _validate(meta, rows)
    if validation["invalid_qpc_rows"]:
        raise ValueError("one or more retained scans have invalid QPC timing")
    frequency = meta["qpc_frequency"]
    scale = validation["scale_ms_per_qpc"]
    windows = _read_windows(Path(windows_path), frequency)
    if any(window["start_qpc"] < meta["capture_start_qpc"] or
           window["end_qpc"] > meta["capture_end_qpc"] for window in windows):
        raise ValueError("measurement window extends outside taskset-scan capture bounds")
    for window in windows:
        if meta["discarded_qpc_invalid"]:
            raise ValueError("a discarded scan has invalid QPC timing; window coverage cannot be established")
        if ((meta["overwritten"] or meta["dropped_busy"] or meta["dropped_stale"]) and
                not meta["max_discarded_end_qpc"]):
            raise ValueError("scan records were lost but no discarded-end QPC is available")
        if meta["max_discarded_end_qpc"] > window["start_qpc"]:
            raise ValueError("a discarded scan may overlap a requested measurement window")
    presents = _read_presents(Path(presents_path), frequency)
    present_intervals = [{"begin_qpc": a["qpc"], "end_qpc": b["qpc"], "frame": b["guest_frame"]}
                         for a, b in zip(presents, presents[1:])]
    present_starts = [item["begin_qpc"] for item in present_intervals]
    present_ends = [item["end_qpc"] for item in present_intervals]

    reports = []
    for window in windows:
        start, end = window["start_qpc"], window["end_qpc"]
        selected, boundary = [], 0
        for row in rows:
            if not row["qpc_valid"]:
                continue
            overlaps = row["begin_qpc"] < end and row["end_qpc"] > start
            if not overlaps:
                continue
            if start <= row["begin_qpc"] and row["end_qpc"] <= end:
                selected.append(row)
            else:
                boundary += 1

        by_dispatch_cause = {}
        by_thread = {}
        for row in selected:
            cause = str(row["dispatch_cause"])
            cause_group = by_dispatch_cause.setdefault(cause, {
                "scans": 0, "empty": 0, "nonempty": 0, "eligible": 0,
                "claimed": 0, "dispatch_attempts": 0, "dispatch_ok": 0,
                "dispatch_failed": 0,
            })
            cause_group["scans"] += 1
            cause_group["empty"] += int(row["eligible"] == 0)
            cause_group["nonempty"] += int(row["eligible"] > 0)
            for field in ("eligible", "claimed", "dispatch_attempts", "dispatch_ok", "dispatch_failed"):
                cause_group[field] += row[field]
            tid = str(row["host_tid"])
            thread_group = by_thread.setdefault(tid, {"scans": 0, "scan_wall_ticks": 0,
                                                       "prep_wall_ticks": 0, "scan_thread_cycles_raw": 0,
                                                       "cycle_valid_scans": 0})
            thread_group["scans"] += 1
            thread_group["scan_wall_ticks"] += row["scan_ticks"]
            thread_group["prep_wall_ticks"] += row["prep_ticks"]
            if row["total_cycles_valid"] and row["prep_cycles_valid"]:
                thread_group["scan_thread_cycles_raw"] += row["scan_thread_cycles"]
                thread_group["cycle_valid_scans"] += 1

        call_intervals = [(row["begin_qpc"], row["end_qpc"]) for row in selected]
        empty_rows = [row for row in selected if row["eligible"] == 0]
        empty_ms = [row["scan_ticks"] * scale for row in empty_rows]
        selected_present_intervals = []
        i = bisect.bisect_right(present_ends, start)
        while i < len(present_intervals) and present_starts[i] < end:
            item = present_intervals[i]
            if item["begin_qpc"] >= start and item["end_qpc"] <= end:
                selected_present_intervals.append(item)
            i += 1
        frame_count = len(selected_present_intervals)
        if not frame_count:
            raise ValueError(f"no whole present interval is contained in measurement window {window['index']}")
        sum_scan_ticks = sum(row["scan_ticks"] for row in selected)
        sum_prep_ticks = sum(row["prep_ticks"] for row in selected)
        union_call_ticks = _union_ticks(call_intervals)
        empty_call_ticks = _union_ticks([(row["begin_qpc"], row["end_qpc"]) for row in empty_rows])
        by_thread_out = {}
        for tid, group in by_thread.items():
            by_thread_out[tid] = {
                "scans": group["scans"],
                "scan_wall_ms_sum": group["scan_wall_ticks"] * scale,
                "prep_wall_ms_sum": group["prep_wall_ticks"] * scale,
                "scan_thread_cycles_raw_sum": group["scan_thread_cycles_raw"],
                "cycle_valid_scans": group["cycle_valid_scans"],
            }
        reports.append({
            "index": window["index"], "label": window["label"], "start_qpc": start,
            "end_qpc": end, "window_ms": (end - start) * scale,
            "coverage": {
                "ticket_accounting_complete": (len(rows) + meta["overwritten"] +
                                                meta["dropped_busy"] + meta["dropped_stale"] ==
                                                meta["next_ticket"]),
                "dropped_busy_total": meta["dropped_busy"],
                "dropped_stale_total": meta["dropped_stale"],
                "discarded_qpc_invalid": meta["discarded_qpc_invalid"],
                "overwritten_total": meta["overwritten"],
                "max_discarded_end_qpc": meta["max_discarded_end_qpc"],
                "discard_may_overlap": meta["max_discarded_end_qpc"] > start,
                "invalid_qpc_rows": validation["invalid_qpc_rows"],
                "boundary_scans_excluded": boundary,
                "capture_start_qpc": meta["capture_start_qpc"],
                "capture_end_qpc": meta["capture_end_qpc"],
                "inflight_at_dump": meta["inflight"],
                "recording_lifetime_proven": True,
            },
            "counts": {"whole_scans": len(selected), "empty": len(empty_rows),
                       "nonempty": len(selected) - len(empty_rows),
                       "dispatch_causes": by_dispatch_cause,
                       "host_threads": by_thread_out, "present_intervals": frame_count},
            "wall": {
                "scan_sum_ms": sum_scan_ticks * scale, "prep_sum_ms": sum_prep_ticks * scale,
                "whole_call_union_occupancy_ms": union_call_ticks * scale,
                "empty_call_union_occupancy_ms": empty_call_ticks * scale,
                "scan_sum_ms_per_present": (sum_scan_ticks * scale / frame_count) if frame_count else None,
                "prep_sum_ms_per_present": (sum_prep_ticks * scale / frame_count) if frame_count else None,
                "whole_call_union_ms_per_present": (union_call_ticks * scale / frame_count) if frame_count else None,
            },
            "empty_scan_ms": {"mean": (sum(empty_ms) / len(empty_ms)) if empty_ms else None,
                              "p50": _percentile(empty_ms, .50), "p99": _percentile(empty_ms, .99)},
            "thread_cycles": {"scan_raw_sum": sum(g["scan_thread_cycles_raw"] for g in by_thread.values()),
                              "valid_scans": sum(g["cycle_valid_scans"] for g in by_thread.values()),
                              "invalid_scans": len(selected) - sum(g["cycle_valid_scans"] for g in by_thread.values()),
                              "unit": "raw QueryThreadCycleTime counts; not milliseconds or aggregate CPU time"},
        })
    return {"source": str(scan_path), "qpc_frequency": frequency,
            "metadata": meta, "validation": validation, "windows": reports,
            "interpretation": "Per-present values normalize observed scan durations by successful present intervals. Scan calls may overlap across threads; summed wall time is not elapsed frame time or CPU time. Only whole-call intervals are unioned; phase ticks have no subinterval timestamps, so prep/scan phase occupancy is not inferred. Boundary scans are excluded and counted separately."}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scan_csv", type=Path)
    parser.add_argument("--windows", type=Path, required=True)
    parser.add_argument("--presents", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = json.dumps(analyze(args.scan_csv, args.windows, args.presents), indent=2)
    if args.output:
        args.output.write_text(report + "\n", encoding="utf-8")
    print(report)


if __name__ == "__main__":
    main()
