import json
import shlex
from pathlib import Path


def expand_tokens(tokens: list[str], base_dir: Path) -> list[str]:
    expanded = []
    for token in tokens:
        if token.startswith("@"):
            sub_tokens = shlex.split((base_dir / token[1:]).read_text())
            expanded.extend(expand_tokens(sub_tokens, base_dir))
        else:
            expanded.append(token)
    return expanded


def main() -> None:
    db_path = Path("compile_commands.json")
    db = json.loads(db_path.read_text())

    for entry in db:
        tokens = expand_tokens(shlex.split(entry["command"]), Path(entry["directory"]))
        if entry["file"].endswith(".cpp"):
            tokens[0] = tokens[0].replace("-gcc", "-g++")

        prefix = ""
        clean_tokens = []
        it = iter(tokens)
        for token in it:
            if token == "-iprefix":
                prefix = next(it, "")
            elif token == "-iwithprefixbefore":
                clean_tokens.append(f"-I{prefix}{next(it, '')}")
            else:
                clean_tokens.append(token)

        entry["command"] = " ".join(clean_tokens)

    db_path.write_text(json.dumps(db, indent=2))


if __name__ == "__main__":
    main()
