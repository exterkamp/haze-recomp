"""Copy the default Kenney Input Prompts PNGs without altering their pixels."""
from pathlib import Path
import argparse
import hashlib
import json
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="kenney_input-prompts_1.5.zip")
    args = parser.parse_args()
    destination = Path(__file__).resolve().parents[1] / "assets/input-prompts"
    families = {"Keyboard & Mouse": "keyboard", "Xbox Series": "xbox",
                "PlayStation Series": "playstation", "Nintendo Switch": "switch"}
    files = {}
    with zipfile.ZipFile(args.archive) as archive:
        for info in archive.infolist():
            parts = info.filename.split("/")
            if (len(parts) != 3 or parts[0] not in families or parts[1] != "Default"
                    or not parts[2].endswith(".png") or "_outline" in parts[2]):
                continue
            relative = Path(families[parts[0]]) / parts[2]
            data = archive.read(info)
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            files[relative.as_posix()] = hashlib.sha256(data).hexdigest()
        (destination / "License.txt").write_bytes(archive.read("License.txt"))
    manifest = {"source": "https://kenney.nl/assets/input-prompts",
                "archive": args.archive.name,
                "archive_sha256": hashlib.sha256(args.archive.read_bytes()).hexdigest(),
                "files": dict(sorted(files.items()))}
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Imported {len(files)} unchanged PNGs into {destination}")


if __name__ == "__main__":
    main()
