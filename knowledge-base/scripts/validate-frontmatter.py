#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12"
# dependencies = [
#   "jsonschema>=4.21",
#   "referencing>=0.32",
#   "pyyaml>=6",
# ]
# ///
"""Validate YAML frontmatter of every content .md file under knowledge-base/.

Each file is matched to a JSON Schema by the first known-bucket segment of
its path under knowledge-base/. Topic subdirectories between knowledge-base/
and the bucket are tolerated, so this works for the multi-topic layout
(knowledge-base/<topic>/<bucket>/...) and for any future flat layout.
Validation errors are printed; the script exits non-zero on errors unless
--warn is passed.

Usage:
    uv run knowledge-base/scripts/validate-frontmatter.py knowledge-base/
    uv run knowledge-base/scripts/validate-frontmatter.py path/to/file.md
    uv run knowledge-base/scripts/validate-frontmatter.py --warn knowledge-base/
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import sys
from pathlib import Path
from typing import Any, Iterable

import yaml
from jsonschema import Draft202012Validator
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT202012

REPO_KB_ROOT_NAME = "knowledge-base"
SCHEMAS_DIRNAME = ".schemas"
BASE_SCHEMA = "base.schema.json"

# This repo has one base schema. Bucket-specialised schemas can be added
# here later (name -> "<bucket>.schema.json" under .schemas/); an unknown
# bucket falls back to the base schema in schema_for().
BUCKET_TO_SCHEMA: dict[str, str] = {}
KNOWN_BUCKETS = frozenset(BUCKET_TO_SCHEMA)

# Top-level docs that get the base schema regardless of which directory they
# sit in (root or topic root). CLAUDE.md / AGENTS.md are excluded — they are
# harness, not content.
TOP_LEVEL_DOCS = {"README.md", "MASTER-PLAN.md", "COMPENDIUM.md", "PROGRESS.md",
                  "FINAL-CONVERGENCE-REPORT.md", "CONTRIBUTING.md"}


def find_bucket(rel: Path) -> str | None:
    """Return the first known-bucket segment of rel, or None.

    Walks past leading non-bucket segments (e.g. a topic directory like
    ``clock-sync/``) and returns the first part that names a known bucket.
    """
    for part in rel.parts:
        if part in KNOWN_BUCKETS:
            return part
    return None


def find_kb_root(start: Path) -> Path:
    """Walk up from `start` to find the knowledge-base/ directory."""
    p = start.resolve()
    while p != p.parent:
        if p.name == REPO_KB_ROOT_NAME and (p / SCHEMAS_DIRNAME).is_dir():
            return p
        if (p / REPO_KB_ROOT_NAME / SCHEMAS_DIRNAME).is_dir():
            return p / REPO_KB_ROOT_NAME
        p = p.parent
    raise SystemExit(f"Could not find {REPO_KB_ROOT_NAME}/ with {SCHEMAS_DIRNAME}/ from {start}")


def load_schema(schemas_dir: Path, name: str) -> dict:
    return json.loads((schemas_dir / name).read_text(encoding="utf-8"))


def _stringify_dates(obj: Any) -> Any:
    """Recursively convert datetime.date / datetime.datetime values to ISO-8601 strings.

    YAML 1.1 parses bare ISO-8601 dates to native date objects, but JSON Schema
    `format: date` validates against strings. Normalizing here lets unquoted
    dates in frontmatter still satisfy the schema.
    """
    if isinstance(obj, dt.datetime):
        return obj.isoformat()
    if isinstance(obj, dt.date):
        return obj.isoformat()
    if isinstance(obj, dict):
        return {k: _stringify_dates(v) for k, v in obj.items()}
    if isinstance(obj, list):
        return [_stringify_dates(v) for v in obj]
    return obj


def extract_frontmatter(text: str) -> tuple[dict | None, str | None]:
    """Return (data, error). data is None if no frontmatter; error is None if ok."""
    if not text.startswith("---\n") and not text.startswith("---\r\n"):
        return None, None
    end = text.find("\n---", 4)
    if end == -1:
        return None, "frontmatter opened with --- but no closing --- found"
    fm_text = text[4:end]
    try:
        data = yaml.safe_load(fm_text)
    except yaml.YAMLError as e:
        return None, f"YAML parse error: {e}"
    if not isinstance(data, dict):
        return None, "frontmatter is not a YAML mapping"
    return _stringify_dates(data), None


META_FILE_NAMES = {"INDEX.md", "README.md"}


def schema_for(path: Path, kb_root: Path) -> str:
    """Return the schema filename for a markdown file.

    INDEX.md / README.md inside a bucket use the base schema rather than the
    bucket-specialized schema, because they are navigational meta-files and do
    not represent (e.g.) a single standard, algorithm, or hardware component.
    """
    rel = path.relative_to(kb_root)
    if rel.name in TOP_LEVEL_DOCS:
        # Treat top-level / topic-level navigational docs uniformly.
        return BASE_SCHEMA
    if rel.name in META_FILE_NAMES:
        return BASE_SCHEMA
    bucket = find_bucket(rel)
    if bucket is None:
        return BASE_SCHEMA
    return BUCKET_TO_SCHEMA[bucket]


HARNESS_FILES = {"CLAUDE.md", "AGENTS.md"}


def iter_md_files(target: Path, kb_root: Path) -> Iterable[Path]:
    """Yield markdown content files under target, restricted to kb_root.

    Skips:
      - .schemas/ directory
      - CLAUDE.md / AGENTS.md harness files at any depth (root, topic root,
        and sub-bucket). Harness files are agent instructions, not corpus
        content, and do not carry frontmatter.
    """
    if target.is_file():
        if target.suffix == ".md" and target.name not in HARNESS_FILES:
            yield target
        return
    for p in sorted(target.rglob("*.md")):
        rel = p.relative_to(kb_root)
        if rel.parts and rel.parts[0] == SCHEMAS_DIRNAME:
            continue
        if p.name in HARNESS_FILES:
            continue
        yield p


def make_validators(schemas_dir: Path) -> dict[str, Draft202012Validator]:
    """Compile one validator per schema, sharing a registry so $ref to base resolves.

    Schemas reference base.schema.json by relative name; we register each schema
    under both its filename and its $id so either form resolves.
    """
    schemas: dict[str, dict] = {}
    for sf in schemas_dir.glob("*.schema.json"):
        schemas[sf.name] = json.loads(sf.read_text(encoding="utf-8"))

    resources: list[tuple[str, Resource]] = []
    for name, schema in schemas.items():
        res = Resource(contents=schema, specification=DRAFT202012)
        resources.append((name, res))
        if "$id" in schema:
            resources.append((schema["$id"], res))
    registry: Registry = Registry().with_resources(resources)

    validators: dict[str, Draft202012Validator] = {}
    for name, schema in schemas.items():
        validators[name] = Draft202012Validator(schema, registry=registry)
    return validators


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("target", type=Path, nargs="?", default=Path("knowledge-base"),
                        help="Directory or file to validate. Default: knowledge-base/")
    parser.add_argument("--warn", action="store_true",
                        help="Report errors but exit 0 (advisory mode).")
    parser.add_argument("--quiet", action="store_true",
                        help="Only print file-level summaries.")
    args = parser.parse_args(argv)

    target = args.target.resolve()
    kb_root = find_kb_root(target)
    schemas_dir = kb_root / SCHEMAS_DIRNAME
    validators = make_validators(schemas_dir)

    total = 0
    missing_fm = 0
    invalid = 0
    file_errors: list[tuple[Path, list[str]]] = []

    for md in iter_md_files(target, kb_root):
        if md.name in HARNESS_FILES:
            continue
        total += 1
        text = md.read_text(encoding="utf-8")
        data, fm_err = extract_frontmatter(text)
        if fm_err is not None:
            invalid += 1
            file_errors.append((md, [fm_err]))
            continue
        if data is None:
            missing_fm += 1
            file_errors.append((md, ["no YAML frontmatter"]))
            continue

        schema_name = schema_for(md, kb_root)
        v = validators[schema_name]
        errs = sorted(v.iter_errors(data), key=lambda e: (list(e.absolute_path), e.message))
        if errs:
            invalid += 1
            file_errors.append((md, [f"{'.'.join(map(str, e.absolute_path)) or '<root>'}: {e.message}"
                                     for e in errs]))

    if not args.quiet:
        for f, errs in file_errors:
            print(f"\x1b[33m{f.relative_to(kb_root.parent)}\x1b[0m  [{schema_for(f, kb_root)}]")
            for e in errs:
                print(f"  - {e}")
        print()
    print(f"validated: {total}  ok: {total - missing_fm - invalid}  "
          f"missing-frontmatter: {missing_fm}  invalid: {invalid}")

    if missing_fm + invalid > 0 and not args.warn:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
