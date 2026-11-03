"""Lists the project files one quality check must look at when only changed files are checked.

--kind format: changed C++ files under src/ and tests/, except tests/fixtures/.
--kind lizard: changed C++ files under src/.
--kind tidy:   changed .cpp files under src/, plus every .cpp under src/ that directly includes a changed
               header (includes are written relative to src/, e.g. "model.hpp").
"Changed" means different from --base (default HEAD) in the working tree, or untracked.
Paths are written one per line, relative to the repository root, with forward slashes.
"""
import argparse
import pathlib
import subprocess

SOURCE_SUFFIXES = {'.cpp', '.cc', '.cxx'}
HEADER_SUFFIXES = {'.h', '.hpp', '.hh', '.hxx', '.inl'}


def git_lines(root, *args):
    """Runs git in root and returns its non-empty output lines."""
    result = subprocess.run(['git', '-C', str(root), *args], check=True, capture_output=True, text=True)
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def changed_files(root, base):
    """Changed or untracked C++ files under src/ and tests/ that still exist, relative to root; src/html/ (the HTML
    generators) is not C++ code of the digger."""
    names = git_lines(root, 'diff', '--name-only', base, '--') + git_lines(
        root, 'ls-files', '--others', '--exclude-standard')
    suffixes = SOURCE_SUFFIXES | HEADER_SUFFIXES
    return sorted({
        n for n in names
        if n.startswith(('src/', 'tests/')) and not n.startswith('src/html/') and
        pathlib.PurePosixPath(n).suffix in suffixes and (root / n).is_file()
    })


def includers(root, headers):
    """Translation units under src/ that directly include one of the given headers."""
    spellings = {f'"{h[len("src/"):]}"' for h in headers}
    units = []
    for path in sorted((root / 'src').rglob('*.c*')):
        if path.suffix in SOURCE_SUFFIXES and not path.relative_to(root).as_posix().startswith('src/html/'):
            text = path.read_text(encoding='utf-8', errors='replace')
            if any(spelling in text for spelling in spellings):
                units.append(path.relative_to(root).as_posix())
    return units


def main():
    """Parses arguments, selects the files and writes the list."""
    parser = argparse.ArgumentParser()
    parser.add_argument('--kind', choices=['format', 'lizard', 'tidy'], required=True)
    parser.add_argument('--root', required=True)
    parser.add_argument('--base', default='HEAD')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    root = pathlib.Path(args.root).resolve()
    changed = changed_files(root, args.base)
    if args.kind == 'format':
        selected = [n for n in changed if not n.startswith('tests/fixtures/')]
    else:
        in_src = [n for n in changed if n.startswith('src/')]
        cpp = [n for n in in_src if pathlib.PurePosixPath(n).suffix in SOURCE_SUFFIXES]
        headers = [n for n in in_src if pathlib.PurePosixPath(n).suffix in HEADER_SUFFIXES]
        selected = cpp + headers if args.kind == 'lizard' else sorted(set(cpp) | set(includers(root, headers)))
    pathlib.Path(args.output).write_text(''.join(f'{n}\n' for n in selected), encoding='utf-8')


if __name__ == '__main__':
    main()
