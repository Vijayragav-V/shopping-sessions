def read_rows(path, width):
    """Yield correctly framed rows, preserving all characters except CSV syntax."""
    with path.open(encoding="utf-8", newline="\n") as stream:
        next(stream, None)
        for line in stream:
            fields = line.removesuffix("\n").split(",")
            if len(fields) != width:
                continue
            for index, value in enumerate(fields):
                if '"' not in value:
                    continue
                if (len(value) < 2 or value[0] != '"' or value[-1] != '"'
                        or '"' in value[1:-1]):
                    break
                fields[index] = value[1:-1]
            else:
                yield fields
