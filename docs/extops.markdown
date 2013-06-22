# Strings in MoarVM
MoarVM implements strings using NFG (Grapheme Normal Form). This is a 32-bit
fixed width representation, with negative numbers used to represent graphemes
that don't have a Unicode representation, but instead are derived from the
combination of multiple code points.
