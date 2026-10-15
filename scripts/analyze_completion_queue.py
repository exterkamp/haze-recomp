"""Describe completion queue residency and producer intervals in QPC windows.

Publication is the locked coherent-notification boundary. Publication-to-pop
includes the producer's remaining lock hold, queue backlog and consumer delay;
it is neither pure wake latency nor a claim about removable frame time. Jobs
may overlap, so this report deliberately has no summed/per-frame job duration.
"""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import statistics


FLAG_BITS = {"no_publication": 1, "no_generation": 2,
             "ambiguous_generation": 4, "record_mismatch": 8, "bad_clock": 16}
ROW_FIELDS = {"qpc_frequency", "sequence", "queue_epoch", "publication_ordinal",
              "submit_generation", "queue_ea", "slot", "index", "flags",
              "submit_qpc", "publication_qpc", "pop_qpc", "producer_tid", "consumer_tid"}
META_INTS = {"schema", "qpc_frequency", "begin_qpc", "end_qpc", "recorded", "retained",
             "epochs", "publications", "submits", "invalidations", "unsupported_tail",
             "pending_overwritten", "pending_at_stop", "ambiguous_submits",
             "max_overwritten_pop_qpc"}
PENDING_INTS = {"min_pending_publication_qpc", "min_pending_submit_qpc",
                "pending_unknown_publication_qpc", "pending_unknown_submit_qpc"}


def _uint(value, field):
    if type(value) is not int or value < 0:
        raise ValueError(f"{field} must be a nonnegative integer")
    return value


def _csv_uint(value, field):
    if not isinstance(value, str) or not value.isascii() or not value.isdecimal():
        raise ValueError(f"invalid unsigned decimal CSV field {field}: {value!r}")
    return int(value)


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _load(csv_path, metadata_path):
    meta = json.loads(Path(metadata_path).read_text(encoding="utf-8-sig"), object_pairs_hook=_unique_object)
    if not isinstance(meta, dict):
        raise ValueError("metadata must be an object")
    missing = META_INTS - meta.keys()
    if missing:
        raise ValueError(f"missing metadata fields: {sorted(missing)}")
    for field in META_INTS:
        _uint(meta[field], f"metadata.{field}")
    if meta["schema"] not in (1, 2):
        raise ValueError("unsupported metadata schema")
    if meta["schema"] == 2:
        missing = PENDING_INTS - meta.keys()
        if missing:
            raise ValueError(f"missing schema 2 pending bounds: {sorted(missing)}")
        for field in PENDING_INTS:
            _uint(meta[field], f"metadata.{field}")
    if meta.get("publication_is_locked_notification") is not True:
        raise ValueError("metadata lacks the locked-publication boundary declaration")
    if meta.get("service_and_callback_phases") != "missing":
        raise ValueError("unexpected service/callback phase declaration")
    if (meta.get("flag_bits") != FLAG_BITS or
            any(type(value) is not int for value in meta["flag_bits"].values())):
        raise ValueError("unknown or missing metadata flag definitions")
    with Path(csv_path).open(newline="", encoding="utf-8-sig") as file:
        reader = csv.DictReader(file)
        if (not reader.fieldnames or len(reader.fieldnames) != len(set(reader.fieldnames)) or
                not ROW_FIELDS.issubset(reader.fieldnames)):
            raise ValueError("missing or duplicate CSV columns")
        rows = []
        for line, raw in enumerate(reader, 2):
            if None in raw or any(value is None for value in raw.values()):
                raise ValueError(f"nonrectangular CSV row at line {line}")
            rows.append({key: _csv_uint(raw[key], key) for key in ROW_FIELDS})
    _validate(meta, rows)
    return meta, rows


def _validate(meta, rows):
    if len(rows) != meta["retained"] or meta["retained"] > meta["recorded"]:
        raise ValueError("retained row count disagrees with metadata")
    if meta["recorded"] and not rows:
        raise ValueError("nonempty fixed ring has no retained rows")
    first = meta["recorded"] - meta["retained"]
    if [r["sequence"] for r in rows] != list(range(first, meta["recorded"])):
        raise ValueError("retained sequence is missing, duplicated, or out of order")
    if not first and meta["max_overwritten_pop_qpc"]:
        raise ValueError("overwrite boundary exists without overwritten rows")
    if meta["epochs"] == 0:
        if any(meta[k] for k in ("begin_qpc", "qpc_frequency", "recorded", "publications", "submits")):
            raise ValueError("unregistered capture has active metadata")
    elif (not meta["qpc_frequency"] or not meta["begin_qpc"] or
          meta["end_qpc"] < meta["begin_qpc"]):
        raise ValueError("invalid active capture clock or bounds")
    if meta["max_overwritten_pop_qpc"] > meta["end_qpc"]:
        raise ValueError("overwrite boundary is after capture end")
    if meta["schema"] == 2:
        for stage in ("publication", "submit"):
            unknown = meta[f"pending_unknown_{stage}_qpc"]
            minimum = meta[f"min_pending_{stage}_qpc"]
            if unknown > meta["pending_at_stop"]:
                raise ValueError("unknown pending clocks exceed pending records")
            if bool(minimum) != (meta["pending_at_stop"] > unknown):
                raise ValueError("pending minimum contradicts known-clock count")
            if minimum and not meta["begin_qpc"] <= minimum <= meta["end_qpc"]:
                raise ValueError("pending clock minimum outside capture")
        if (meta["pending_at_stop"] and not meta["pending_unknown_publication_qpc"] and
                not meta["pending_unknown_submit_qpc"] and
                meta["min_pending_submit_qpc"] > meta["min_pending_publication_qpc"]):
            raise ValueError("known pending submit minimum is after publication minimum")
    if meta["publications"] > meta["recorded"] + meta["pending_overwritten"] + meta["pending_at_stop"]:
        raise ValueError("publication count has unaccounted pending records")
    previous_epoch, previous_pop, previous_ordinal = 0, 0, 0
    ordinals, generations = set(), set()
    epoch_queues = {}
    for row in rows:
        flags = row["flags"]
        if flags & ~31:
            raise ValueError("unknown row flag bits")
        if row["qpc_frequency"] != meta["qpc_frequency"]:
            raise ValueError("CSV/metadata QPC frequency mismatch")
        epoch = row["queue_epoch"]
        if not 0 < epoch <= meta["epochs"] or epoch < previous_epoch:
            raise ValueError("invalid or reversed queue epoch")
        previous_epoch = epoch
        if not row["queue_ea"] or row["queue_ea"] & 127 or row["index"] >= 2048:
            raise ValueError("invalid queue identity/index")
        if epoch_queues.setdefault(epoch, row["queue_ea"]) != row["queue_ea"]:
            raise ValueError("queue address changed without a new epoch")
        if not row["consumer_tid"]:
            raise ValueError("missing consumer identity")
        ordinal, generation = row["publication_ordinal"], row["submit_generation"]
        if flags & 1:
            if not flags & 2 or any(row[k] for k in (
                    "publication_ordinal", "submit_generation", "submit_qpc", "publication_qpc", "producer_tid")):
                raise ValueError("unknown-publication row claims a publication/generation")
        else:
            if (not 0 < ordinal <= meta["publications"] or ordinal in ordinals or
                    ordinal <= previous_ordinal or not row["producer_tid"]):
                raise ValueError("invalid or duplicate publication identity")
            ordinals.add(ordinal)
            previous_ordinal = ordinal
        if bool(generation) == bool(flags & 2):
            raise ValueError("generation validity contradicts flags")
        if generation:
            key = (epoch, row["slot"], generation)
            if row["slot"] >= 512 or key in generations:
                raise ValueError("invalid or duplicate submit generation identity")
            generations.add(key)
        elif row["submit_qpc"]:
            raise ValueError("unknown generation claims a submit clock")
        if not flags & 16:
            if not meta["begin_qpc"] <= row["pop_qpc"] <= meta["end_qpc"] or not row["pop_qpc"]:
                raise ValueError("unflagged pop clock outside capture")
            if row["pop_qpc"] < previous_pop:
                raise ValueError("unflagged pop clocks are reversed")
            if row["pop_qpc"] < meta["max_overwritten_pop_qpc"]:
                raise ValueError("retained pop precedes overwritten boundary")
            previous_pop = row["pop_qpc"]
            if ordinal and not meta["begin_qpc"] <= row["publication_qpc"] <= row["pop_qpc"]:
                raise ValueError("unflagged publication clock outside capture/after pop")
            if generation and not meta["begin_qpc"] <= row["submit_qpc"] <= row["publication_qpc"]:
                raise ValueError("unflagged submit clock outside capture/after publication")


def _stats(ticks, frequency):
    values = sorted(tick * 1000.0 / frequency for tick in ticks)
    if not values:
        return {"count": 0, "mean_ms": None, "median_ms": None,
                "p95_ms": None, "p99_ms": None, "max_ms": None}
    def percentile(p):
        position = (len(values)-1)*p
        low = int(position)
        high = min(low+1, len(values)-1)
        return values[low]+(values[high]-values[low])*(position-low)
    return {"count": len(values), "mean_ms": statistics.fmean(values),
            "median_ms": statistics.median(values), "p95_ms": percentile(.95),
            "p99_ms": percentile(.99), "max_ms": values[-1]}


def _flags(rows):
    return {name: sum(bool(r["flags"] & bit) for r in rows) for name, bit in FLAG_BITS.items()}


def _windows(path, frequency):
    if path is None:
        return None
    windows = json.loads(Path(path).read_text(encoding="utf-8-sig"), object_pairs_hook=_unique_object)
    if not isinstance(windows, dict) or not isinstance(windows.get("blocks"), list):
        raise ValueError("windows JSON lacks blocks")
    window_frequency = _uint(windows.get("qpc_frequency"), "windows.qpc_frequency")
    if not window_frequency or (frequency and window_frequency != frequency):
        raise ValueError("windows/capture QPC frequency mismatch")
    for index, block in enumerate(windows["blocks"]):
        if not isinstance(block, dict):
            raise ValueError(f"block {index} is not an object")
        start = _uint(block.get("start_qpc"), f"block {index}.start_qpc")
        end = _uint(block.get("end_qpc"), f"block {index}.end_qpc")
        if not start or end <= start:
            raise ValueError(f"invalid block {index} bounds")
    return windows


def _block(meta, rows, block, index, window_complete):
    start, end = block["start_qpc"], block["end_qpc"]
    # Half-open endpoint cohorts avoid double-counting a boundary completion.
    popped = [r for r in rows if start <= r["pop_qpc"] < end]
    publications = [r for r in rows if r["publication_ordinal"] and not r["flags"] & 16
                    and start <= r["publication_qpc"] < end]
    residency = [r for r in popped if not r["flags"] & (1|8|16)]
    producer = [r for r in publications if not r["flags"]]
    residency_inside = [r for r in residency if r["publication_qpc"] >= start]
    producer_inside = [r for r in producer if r["submit_qpc"] >= start]
    reasons = []
    if not window_complete:
        reasons.append("windows_document_not_complete")
    if not meta["epochs"]:
        reasons.append("queue_never_registered")
    if not meta["begin_qpc"] or start < meta["begin_qpc"]:
        reasons.append("window_starts_before_capture")
    if not meta["end_qpc"] or end > meta["end_qpc"]:
        reasons.append("window_ends_after_capture")
    if meta["recorded"] > meta["retained"]:
        boundary = meta["max_overwritten_pop_qpc"]
        if not boundary or start <= boundary:
            reasons.append("window_not_strictly_after_overwritten_pop_boundary")
    # Both schemas have counters but no timestamps for these losses/owner changes.
    # They cannot safely be assigned to warmup or a different requested block.
    for field in ("invalidations", "unsupported_tail", "pending_overwritten"):
        if meta[field]:
            reasons.append(f"untimestamped_{field}")
    if meta["epochs"] > 1:
        reasons.append("multiple_queue_epochs_without_transition_timestamps")
    if any(r["flags"] & 16 for r in rows):
        reasons.append("capture_has_unlocatable_bad_clock_rows")
    if any(r["flags"] & (1|8) for r in popped):
        reasons.append("window_has_unknown_publication_or_record_mismatch")
    residency_proven = not reasons
    generation_reasons = []
    # Pending records cannot conceal a pop before end: the pop-selected cohort
    # is recorded synchronously at every successful HLE pop. The publication-
    # selected producer cohort can lose rows that have not yet popped, however.
    pending_status = "none"
    if meta["pending_at_stop"]:
        if meta["schema"] == 1:
            pending_status = "legacy_bounds_unavailable"
            generation_reasons.append("schema1_pending_publication_bounds_unavailable")
        elif meta["pending_unknown_publication_qpc"]:
            pending_status = "unknown_publication_clock"
            generation_reasons.append("pending_publication_clock_unknown")
        elif meta["min_pending_publication_qpc"] < end:
            pending_status = "may_overlap_window"
            generation_reasons.append("pending_publications_may_overlap_window")
        else:
            pending_status = "at_or_after_end"
    if meta["ambiguous_submits"]:
        generation_reasons.append("untimestamped_ambiguous_submits")
    if any(r["flags"] & (2|4) for r in publications):
        generation_reasons.append("window_has_unknown_or_ambiguous_generations")
    producer_proven = residency_proven and not generation_reasons
    return {
        "block_index": index, "flags": block.get("flags"),
        "camera_token": block.get("camera_token"), "start_qpc": start, "end_qpc": end,
        "coverage": {"status": "complete" if producer_proven else "partial",
                     "publication_to_pop_proven": residency_proven,
                     "submit_to_publication_proven": producer_proven,
                     "publication_to_pop_reasons": reasons,
                     "submit_to_publication_reasons": reasons+generation_reasons,
                     "reasons": reasons+generation_reasons},
        "pending_at_stop_publication_bounds": pending_status,
        "sample_status": "records" if popped or publications else "no_records",
        "popped_records": len(popped), "published_records_with_retained_pop": len(publications),
        "flagged_popped_records": sum(bool(r["flags"]) for r in popped),
        "flags_by_popped_record": _flags(popped),
        "flagged_published_records_with_retained_pop": sum(bool(r["flags"]) for r in publications),
        "flags_by_published_record": _flags(publications),
        "epochs_in_popped_records": sorted({r["queue_epoch"] for r in popped}),
        "publication_to_pop_ms": _stats(
            [r["pop_qpc"]-r["publication_qpc"] for r in residency_inside], meta["qpc_frequency"]),
        "submit_to_publication_ms": _stats(
            [r["publication_qpc"]-r["submit_qpc"] for r in producer_inside], meta["qpc_frequency"]),
        "publication_to_pop_crossing_start": len(residency)-len(residency_inside),
        "submit_to_publication_crossing_start": len(producer)-len(producer_inside),
        "publications_popping_at_or_after_end": sum(r["pop_qpc"] >= end for r in publications),
    }


def analyze(csv_path, metadata_path=None, windows_path=None):
    metadata_path = metadata_path or Path(str(csv_path)+".meta.json")
    try:
        meta, rows = _load(csv_path, metadata_path)
        windows = _windows(windows_path, meta["qpc_frequency"])
    except (OSError, ValueError, TypeError) as exc:
        return {"status": "invalid", "errors": [str(exc)], "blocks": []}
    blocks = [_block(meta, rows, block, i, windows.get("complete") is True)
              for i, block in enumerate(windows["blocks"])] if windows else []
    reasons = []
    if windows is None:
        reasons.append("no_windows_document")
    elif not blocks:
        reasons.append("no_requested_blocks")
    status = "complete" if blocks and all(b["coverage"]["status"] == "complete" for b in blocks) else "partial"
    return {
        "status": status, "errors": [], "coverage_reasons": reasons,
        "interpretation": "Descriptive job intervals; publication is locked notification. "
            "Residency includes remaining producer lock hold, backlog and consumer delay. "
            "It is not pure wake latency or removable frame time. No overlapping-job sums are reported.",
        "selection": {"publication_to_pop": "pop in [start,end), publication >= start; "
                      "requires known publication and matching payload, but not known submit generation",
                      "submit_to_publication": "publication in [start,end), submit >= start; "
                      "requires unflagged generation; only records popped before dump are exported",
                      "percentiles": "linear interpolation at (count-1)*p"},
        "capture": {**meta, "flagged_retained_records": sum(bool(r["flags"]) for r in rows),
                    "flags_by_retained_record": _flags(rows),
                    "epochs_in_retained_records": sorted({r["queue_epoch"] for r in rows}),
                    "service_and_callback_phases": "missing"},
        "blocks": blocks,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--metadata", type=Path, help="default: CSV path plus .meta.json")
    parser.add_argument("--windows", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = analyze(args.csv, args.metadata, args.windows)
    encoded = json.dumps(report, indent=2, allow_nan=False)+"\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    else:
        print(encoded, end="")
    return 2 if report["status"] == "invalid" else 0


if __name__ == "__main__":
    raise SystemExit(main())
