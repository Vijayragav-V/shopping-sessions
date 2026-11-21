from pathlib import Path

if __package__:
    from .csv_rows import read_rows
else:
    from csv_rows import read_rows


class LinkedIDs(dict[str, str]):
    """Map linked IDs to canonical IDs. Unlinked IDs resolve to themselves."""

    def __missing__(self, user_id: str) -> str:
        return user_id


def load_linked_ids(dataset_dir: str | Path) -> LinkedIDs:
    parents = LinkedIDs()

    def root(user_id: str) -> str:
        while parents[user_id] != user_id:
            parents[user_id] = parents[parents[user_id]]
            user_id = parents[user_id]
        return user_id

    for path in (Path(dataset_dir) / "links").glob("*.csv"):
        for left, right in read_rows(path, 2):
            if not left or not right:
                continue
            parents.setdefault(left, left)
            parents.setdefault(right, right)
            a, b = root(left), root(right)
            # For valid UTF-8, code-point order equals byte-wise order.
            if a < b:
                parents[b] = a
            elif b < a:
                parents[a] = b

    for user_id in parents:
        parents[user_id] = root(user_id)
    return parents
