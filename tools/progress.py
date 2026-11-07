#!/usr/bin/env python3
"""Generate honest, reproducible decompilation metrics and SVG charts."""

import argparse
import hashlib
import json
import math
import re
from datetime import date, datetime, timezone
from html import escape
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STAGES = ("discovered", "recompiled", "decompiled", "verified")
LABELS = {
    "recompiled": "Código traduzido ou reconstruído",
    "decompiled": "Fonte reconstruída",
    "verified": "Decompilação verificada",
}
COLORS = {"recompiled": "#60a5fa", "decompiled": "#c4b5fd", "verified": "#34d399"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def integer(value):
    return type(value) is int


def local_file(root, name):
    require(isinstance(name, str) and bool(name), "missing source/evidence path")
    path = (root / name).resolve()
    require(path.is_relative_to(root.resolve()), "source/evidence must stay inside repository")
    require(path.is_file(), f"source/evidence file missing: {name}")
    return path


def calculate(document, root):
    require(document.get("schema_version") == 1, "unsupported function schema")
    baseline, functions = document["baseline"], document["functions"]
    complete = baseline["inventory_complete"]
    require(type(complete) is bool, "inventory_complete must be boolean")
    digest = baseline["executable_sha256"]
    require(digest is None or isinstance(digest, str) and re.fullmatch(r"[a-f0-9]{64}", digest),
            "executable SHA-256 must be null or 64 lowercase hex digits")
    ranges = baseline["code_ranges"]
    require(isinstance(ranges, list), "code_ranges must be a list")
    intervals = []
    for item in ranges:
        start, size = item["address"], item["size"]
        require(integer(start) and integer(size) and start >= 0 and size > 0
                and start + size <= 2**32, "invalid code range")
        intervals.append((start, start + size))
    intervals.sort()
    require(all(a[1] <= b[0] for a, b in zip(intervals, intervals[1:])), "overlapping code ranges")
    require(isinstance(functions, list), "functions must be a list")
    ordered = sorted(functions, key=lambda item: item["address"])
    previous_end = -1
    counts = {stage: {"functions": 0, "bytes": 0} for stage in STAGES}
    for function in ordered:
        address, size = function["address"], function["size"]
        require(integer(address) and integer(size) and address >= 0 and size > 0
                and address + size <= 2**32, "invalid function range")
        require(address >= previous_end, "overlapping or duplicate functions")
        previous_end = address + size
        require(any(start <= address and address + size <= end for start, end in intervals),
                "function outside declared executable code ranges")
        stage = function["status"]
        require(stage in STAGES, "unknown function status")
        if stage in ("decompiled", "verified"):
            local_file(root, function.get("source"))
        if stage == "verified":
            evidence = function.get("evidence")
            require(isinstance(evidence, list) and bool(evidence), "verified function needs evidence")
            for item in evidence:
                local_file(root, item)
        for included in STAGES[:STAGES.index(stage) + 1]:
            counts[included]["functions"] += 1
            counts[included]["bytes"] += size
    if complete:
        require(bool(digest) and bool(functions) and bool(ranges), "complete inventory needs hash and functions")
        require(isinstance(baseline.get("version"), str) and bool(baseline["version"].strip()),
                "complete inventory needs an executable version")
        local_file(root, baseline.get("inventory_evidence"))
    signature = {"executable_sha256": digest, "code_ranges": ranges,
                 "functions": [[f["address"], f["size"]] for f in ordered]}
    baseline_id = hashlib.sha256(json.dumps(signature, sort_keys=True).encode()).hexdigest()
    total = counts["discovered"]["bytes"]
    for stage in counts:
        counts[stage]["percent"] = round(100 * counts[stage]["bytes"] / total, 4) if complete else None
    return {"schema_version": 1, "baseline_id": baseline_id, "executable_sha256": digest,
            "inventory_complete": complete, "inventory_functions": len(functions),
            "inventory_bytes": total, "coverage": counts}


def milestones(document, root):
    require(document.get("schema_version") == 1, "unsupported milestone schema")
    items = document["milestones"]
    ids = set()
    for item in items:
        require(isinstance(item["id"], str) and item["id"] not in ids, "duplicate milestone")
        ids.add(item["id"])
        require(isinstance(item["label"], str) and bool(item["label"]), "missing milestone label")
        require(item["status"] in ("pending", "in_progress", "done"), "invalid milestone status")
        if item["status"] == "done":
            local_file(root, item.get("evidence"))
    return items


def validate_history(history):
    require(isinstance(history, list), "history must be a list")
    previous = ""
    for item in history:
        stamp = item["date"]
        require(date.fromisoformat(stamp).isoformat() == stamp and stamp > previous,
                "history dates must be unique and chronological")
        previous = stamp
        require(isinstance(item["baseline_id"], str) and re.fullmatch(r"[a-f0-9]{64}", item["baseline_id"]),
                "invalid historical baseline")
        value = item["verified_percent"]
        require(value is None or type(value) in (int, float) and math.isfinite(value) and 0 <= value <= 100,
                "invalid historical percentage")


def snapshot(history, metrics, stamp):
    date.fromisoformat(stamp)
    row = {"date": stamp, "baseline_id": metrics["baseline_id"],
           "verified_percent": metrics["coverage"]["verified"]["percent"],
           "verified_bytes": metrics["coverage"]["verified"]["bytes"],
           "inventory_bytes": metrics["inventory_bytes"]}
    result = sorted([item for item in history if item["date"] != stamp] + [row], key=lambda item: item["date"])
    validate_history(result)
    return result


def percent(value):
    return "Não medida" if value is None else f"{value:.2f}%"


def svg(height, title, description):
    return [f'<svg xmlns="http://www.w3.org/2000/svg" width="960" height="{height}" viewBox="0 0 960 {height}" role="img" aria-labelledby="title desc">',
            f"<title id=\"title\">{escape(title)}</title><desc id=\"desc\">{escape(description)}</desc>",
            '<style>text{font-family:DejaVu Sans,Arial,sans-serif;fill:#e2e8f0}.muted{fill:#94a3b8}.small{font-size:14px}.label{font-size:18px}.heading{font-size:28px;font-weight:bold}</style>',
            f'<rect width="960" height="{height}" rx="18" fill="#0f172a"/>']


def text_at(x, y, value, css="label", extra=""):
    return f'<text x="{x}" y="{y}" class="{css}" {extra}>{escape(str(value))}</text>'


def progress_svg(metrics, items):
    parts = svg(850, "NieR: progresso de código e port", "Coberturas por bytes e marcos independentes do port.")
    parts += [text_at(36, 52, "NieR / Xbox 360 → ARM64", "heading"),
              text_at(36, 83, "Decompilação e port NextOS · GLES2 · Mali-450", "small muted")]
    for index, stage in enumerate(LABELS):
        y, coverage = 132 + index * 94, metrics["coverage"][stage]
        parts += [text_at(36, y, LABELS[stage]),
                  text_at(920, y, percent(coverage["percent"]), extra='text-anchor="end"'),
                  f'<rect x="36" y="{y + 15}" width="888" height="13" rx="6" fill="#334155"/>']
        if coverage["percent"] is not None:
            width = coverage["percent"] / 100 * 888
            parts.append(f'<rect x="36" y="{y + 15}" width="{width:.3f}" height="13" rx="6" fill="{COLORS[stage]}"/>')
        detail = f'{coverage["functions"]} funções · {coverage["bytes"]:,} bytes'
        parts.append(text_at(36, y + 51, detail, "small muted"))
    parts += [text_at(36, 421, f'Inventário: {metrics["inventory_functions"]} funções · {metrics["inventory_bytes"]:,} bytes', "small muted"),
              text_at(36, 446, "Base completa e identificada" if metrics["inventory_complete"] else "Aguardando executável e inventário completo; porcentagem indisponível.", "small muted"),
              text_at(36, 493, "Marcos do port", "heading")]
    for index, item in enumerate(items):
        y = 530 + index * 35
        color = {"done": "#34d399", "in_progress": "#fbbf24", "pending": "#475569"}[item["status"]]
        label = {"done": "Concluído", "in_progress": "Em andamento", "pending": "Pendente"}[item["status"]]
        parts += [f'<circle cx="44" cy="{y - 6}" r="6" fill="{color}"/>', text_at(64, y, item["label"], "small"),
                  text_at(920, y, label, "small muted", 'text-anchor="end"')]
    parts.append(text_at(36, 830, "Cobertura de código e marcos não equivalem a porcentagem da campanha.", "small muted"))
    return "\n".join(parts + ["</svg>\n"])


def history_svg(history):
    parts = svg(340, "Histórico da decompilação verificada", "Percentual por bytes; sem medição, não se desenha um ponto em zero.")
    parts += [text_at(36, 48, "Histórico da decompilação verificada", "heading")]
    rows = history[-12:]
    for value in (0, 25, 50, 75, 100):
        y = 265 - value * 1.65
        parts += [f'<path d="M80 {y} H920" stroke="#334155"/>', text_at(65, y + 5, f"{value}%", "small muted", 'text-anchor="end"')]
    previous = None
    for index, row in enumerate(rows):
        x = 80 + index * 840 / max(len(rows) - 1, 1) if len(rows) > 1 else 500
        parts.append(text_at(x, 294, row["date"][5:], "small muted", 'text-anchor="middle"'))
        if row["verified_percent"] is None:
            previous = None
            continue
        y = 265 - row["verified_percent"] * 1.65
        if previous and previous[2] == row["baseline_id"]:
            parts.append(f'<path d="M{previous[0]} {previous[1]} L{x} {y}" stroke="#34d399" stroke-width="3"/>')
        parts.append(f'<circle cx="{x}" cy="{y}" r="5" fill="#34d399"/>')
        previous = (x, y, row["baseline_id"])
    if not any(row["verified_percent"] is not None for row in rows):
        parts.append(text_at(500, 180, "Aguardando a primeira medição", extra='text-anchor="middle"'))
    parts.append(text_at(36, 326, "Até 12 snapshots diários · lacunas = sem medição · bases distintas não são unidas", "small muted"))
    return "\n".join(parts + ["</svg>\n"])


def outputs(metrics, items, history):
    value = percent(metrics["coverage"]["verified"]["percent"])
    badge = ('<svg xmlns="http://www.w3.org/2000/svg" width="340" height="28" role="img" '
             f'aria-label="Decompilação verificada: {escape(value)}"><rect width="340" height="28" rx="5" fill="#1e293b"/>'
             '<text x="12" y="19" fill="#e2e8f0" font-family="Arial,sans-serif" font-size="12">'
             f'Decompilação verificada: {escape(value)}</text></svg>\n')
    status = ["# Estado gerado", "", "Gerado por `python3 tools/progress.py`. Não editar manualmente.", "",
              f'Inventário completo: **{"sim" if metrics["inventory_complete"] else "não"}**.',
              f'Funções inventariadas: **{metrics["inventory_functions"]}**; bytes: **{metrics["inventory_bytes"]}**.', "",
              "| Cobertura cumulativa | Funções | Bytes | Porcentagem |", "| --- | ---: | ---: | ---: |"]
    for stage, label in LABELS.items():
        coverage = metrics["coverage"][stage]
        status.append(f'| {label} | {coverage["functions"]} | {coverage["bytes"]} | {percent(coverage["percent"])} |')
    status += ["", "## Marcos", "", "| Marco | Estado | Evidência |", "| --- | --- | --- |"]
    for item in items:
        proof = f'[registro](../../{item["evidence"]})' if item.get("evidence") else "—"
        status.append(f'| {item["label"]} | {item["status"]} | {proof} |')
    status += ["", "Decompilação verificada exige fonte reconstruída e evidência. Recompilação automática é uma cobertura distinta.",
               "Porcentagens ficam indisponíveis enquanto o denominador não estiver estabelecido.", ""]
    return {"progress/metrics.json": json.dumps(metrics, indent=2, ensure_ascii=False) + "\n",
            "docs/generated/badge.svg": badge, "docs/generated/progress.svg": progress_svg(metrics, items),
            "docs/generated/history.svg": history_svg(history), "docs/generated/STATUS.md": "\n".join(status)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="fail if generated output is stale")
    mode.add_argument("--snapshot", action="store_true", help="record/update today's metrics")
    parser.add_argument("--date", default=datetime.now(timezone.utc).date().isoformat(), help="snapshot date (YYYY-MM-DD)")
    args = parser.parse_args()
    try:
        read = lambda name: json.loads((ROOT / name).read_text(encoding="utf-8"))
        metrics = calculate(read("progress/functions.json"), ROOT)
        items = milestones(read("progress/milestones.json"), ROOT)
        history = read("progress/history.json")
        validate_history(history)
        if args.snapshot:
            history = snapshot(history, metrics, args.date)
        require(bool(history) and history[-1] == snapshot([], metrics, history[-1]["date"])[0],
                "latest history snapshot does not match metrics; run with --snapshot")
        files = outputs(metrics, items, history)
        if args.snapshot:
            files["progress/history.json"] = json.dumps(history, indent=2, ensure_ascii=False) + "\n"
        stale = []
        for name, content in files.items():
            path = ROOT / name
            if args.check:
                if not path.is_file() or path.read_text(encoding="utf-8") != content:
                    stale.append(name)
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")
        require(not stale, "stale generated files: " + ", ".join(stale))
        print(f'Progress OK: {metrics["inventory_functions"]} functions; verified {percent(metrics["coverage"]["verified"]["percent"])}')
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.exit(1, f"progress: {error}\n")


if __name__ == "__main__":
    main()
