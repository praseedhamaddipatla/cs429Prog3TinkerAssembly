Folder Layout
====================
There are three tests present. Tests that are intended to compile (basic and full_valid) have the associated .tkir (intermediate) and .tko (output) files produced by my program. Depending on macro expansions yours may differ slightly, but should be largely the same.

Basic
====================
Small test suite, designed to manually check various components. Ensure macros are expanded correctly, negative values are properly represented in the bytecode, and labels expand to the correct location.

Full Valid
====================
~20 MB file with nearly every possible valid input instruction. Some have too many permutations to include all of them, but it should be rather comprehensive.

Full Invalid
====================
A set of invalid instructions for various reasons. Follow the comments for instructions, but every line should fail; remove them one by one and ensure that your program fails at each line. Includes invalid syntax, out of bounds literals, invalid registers, etc.
