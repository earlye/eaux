# static_format compile-fail tests collide on case-insensitive filesystems

## Context

These two compile-fail tests have names that differ only in letter case:

- `c++/text/static_format/tests/fail/pfmt_v_unformattable.cpp` checks `%v`, which needs a
  `std::formatter<T>` specialization or `operator<<`.
- `c++/text/static_format/tests/fail/pfmt_V_unformattable.cpp` checks `%V`, which needs a
  specialization of `eaux::text::static_format::formatter`.

macOS's default filesystem (APFS) is case-insensitive, so one file overwrites the other at
checkout. Early reported the conflict on macOS. Both files arrived in `2f4c315` (PR #7, "C++23
static_format, with ascii and unicode text packages").

`eaux_compile_fail_tests` makes the CMake target `${prefix}_fail_${stem}` and the test
`${prefix}.fail.${stem}` from each file's stem. So the two target names also differ only by
case. Their `CMakeFiles/<target>.dir` build directories would likely collide on macOS too,
even if the sources were renamed some other way. This hasn't been checked on a Mac.

No other names in `tests/fail/` collide when case is ignored. Nothing currently prevents a
new collision from being added.

## Relevant files

- `c++/text/static_format/tests/fail/pfmt_v_unformattable.cpp`, `pfmt_V_unformattable.cpp`: the
  colliding pair.
- `c++/cmake/eaux.cmake:42-64`: `eaux_compile_fail_tests`, which turns each file stem into a
  target and test name.
- `c++/text/static_format/CMakeLists.txt:25`: where the `tests/fail` directory is registered.

## Resolution (2026-10-27)

- Renamed the pair to `pfmt_v_no_std_formatter.cpp` (`%v`) and `pfmt_V_no_eaux_formatter.cpp`
  (`%V`). Contents are unchanged.
- `eaux_compile_fail_tests` now stops configure when two files in a directory share a name up to
  the first `.` when case is ignored, naming both files. Only a case-sensitive checkout (the Linux
  CI legs) has both files, so that is where a new collision gets caught.
- Checked on Linux with g++ 15.2 and clang++ 21.1: 62/62 tests pass, and configure fails with the
  new error when a case-colliding copy or an identical-stem copy (`x.old.cpp` next to `x.cpp`) is
  added. Still not checked on a Mac.
