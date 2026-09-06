from pathlib import Path
import re
import sys


PROJECT_ROOT = Path(__file__).resolve().parent
SOURCE_ROOT = PROJECT_ROOT / "alchemist"
PART_COUNT = 5
OUTPUT_FILES = tuple(
	PROJECT_ROOT / f"code-part{part_number}.md"
	for part_number in range(1, PART_COUNT + 1)
)

SOURCE_LANGUAGES = {
	".c": "c",
	".cc": "cpp",
	".cpp": "cpp",
	".cxx": "cpp",
	".h": "cpp",
	".hh": "cpp",
	".hpp": "cpp",
	".hxx": "cpp",
	".inl": "cpp",
	".rc": "rc",
	".cmake": "cmake",
}
SOURCE_FILENAMES = {
	"CMakeLists.txt": "cmake",
}


def relative_path(path: Path) -> str:
	return path.relative_to(PROJECT_ROOT).as_posix()


def discover_source_files() -> list[Path]:
	files = []
	for path in SOURCE_ROOT.rglob("*"):
		if not path.is_file():
			continue
		if path.name in SOURCE_FILENAMES or path.suffix.lower() in SOURCE_LANGUAGES:
			files.append(path)

	return sorted(files, key=lambda path: (relative_path(path).casefold(), relative_path(path)))


def language_for(path: Path) -> str:
	language = SOURCE_FILENAMES.get(path.name)
	if language is not None:
		return language
	return SOURCE_LANGUAGES[path.suffix.lower()]


def fence_for(content: str) -> str:
	longest_run = max(
		(len(match.group(0)) for match in re.finditer(r"`+", content)),
		default=0,
	)
	return "`" * max(3, longest_run + 1)


def read_source(path: Path) -> str | None:
	try:
		with path.open("r", encoding="utf-8", newline="") as source_file:
			return source_file.read()
	except UnicodeDecodeError as error:
		print(f"Skipping {relative_path(path)}: invalid UTF-8 ({error})", file=sys.stderr)
		return None


def render_section(path: Path) -> str | None:
	content = read_source(path)
	if content is None:
		return None

	fence = fence_for(content)
	if content and not content.endswith(("\n", "\r")):
		content += "\n"

	return "".join(
		[
			f"## `{relative_path(path)}`\n",
			"\n",
			f"{fence}{language_for(path)}\n",
			content,
			f"{fence}\n",
			"\n",
		]
	)


def partition_sections(sections: list[str], part_count: int) -> list[list[str]]:
	if len(sections) < part_count:
		raise ValueError(f"Need at least {part_count} source sections, found {len(sections)}")

	line_counts = [len(section.splitlines(keepends=True)) for section in sections]
	part_indices: list[list[int]] = [[] for _ in range(part_count)]
	part_line_counts = [0] * part_count

	for section_index in sorted(
		range(len(sections)),
		key=lambda index: (-line_counts[index], index),
	):
		part_index = min(
			range(part_count),
			key=lambda index: (part_line_counts[index], index),
		)
		part_indices[part_index].append(section_index)
		part_line_counts[part_index] += line_counts[section_index]

	return [
		[sections[section_index] for section_index in sorted(indices)]
		for indices in part_indices
	]


def render_markdown(sections: list[str], output_name: str) -> str:
	return "".join(
		[
			"# Prosperous Alchemist Source Code\n",
			"\n",
			*sections,
			f"**end of `{output_name}`**\n",
		]
	)


def main() -> int:
	if not SOURCE_ROOT.is_dir():
		print(f"Source directory not found: {SOURCE_ROOT}", file=sys.stderr)
		return 1

	print(f"Scanning source directory: {SOURCE_ROOT}", flush=True)
	files = discover_source_files()
	print(f"Discovered {len(files)} source files", flush=True)

	sections = []
	for path in files:
		section = render_section(path)
		if section is not None:
			sections.append(section)

	try:
		parts = partition_sections(sections, len(OUTPUT_FILES))
	except ValueError as error:
		print(str(error), file=sys.stderr)
		return 1

	for output_file, part_sections in zip(OUTPUT_FILES, parts):
		markdown = render_markdown(part_sections, output_file.name)
		with output_file.open("w", encoding="utf-8", newline="\n") as output:
			output.write(markdown)
		print(f"Wrote {output_file} with {len(part_sections)} source files", flush=True)

	included_count = len(sections)
	skipped_count = len(files) - included_count
	if skipped_count:
		print(f"Skipped {skipped_count} source files", file=sys.stderr)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
