# Notes on UNIDATA file formats

This file contains short notes on file formats for all of the files in the
`UNIDATA/` tree (created by `tools/UCD-download.raku`), for use when updating
scripts that process these files, such as `tools/ucd2c.pl`.

Some files show multiple formats separated by `OR` because parsing the file
correctly will require handling each of the different line formats specified.
This will occasionally break parsing of an existing file when a new Unicode
version is released because a new line format was added.  This occurred for
example in the DerivedCoreProperties file in Unicode 16.0.

The format for test files is not specified (other than to note that's what the
file contains) since these appear to all be bespoke to the test in question.

A few files point to other files in the collection as being preferred for
machine interpretation; this is noted as well.


## By Filename

Here the files are individually listed with their notes, sorted by
directory and then file basename (almost all files in the tree have .txt or
.html extensions, and all of the latter seem to be test files).

For files collected by parsing method, see the By Parsing Method section below.


### Top Directory

ArabicShaping
* `codepoint; short schematic name; Joining_Type enum; Joining_Group enum`
* Recommends using DerivedJoiningType file instead for Joining_Type

BidiBrackets
* `codepoint; Bidi_Paired_Bracket (cp or <none>); Bidi_Paired_Bracket_Type enum`

BidiCharacterTest
* TEST FILE

BidiMirroring
* `codepoint; Bidi_Mirroring_Glyph (cp)`

BidiTest
* TEST FILE

Blocks
* `codepoint range; Block enum`

CaseFolding
* `code; status; mapping (1+ cp)`
* `status` indicates which case folding set the mapping belongs to

CJKRadicals
* Maps CJK radical numbers to characters

CompositionExclusions
* List of excluded codepoints
* Points to `Full_Composition_Exclusion` in DerivedNormalizationProps

DerivedAge
* `codepoint range; Age enum`

DerivedCoreProperties
* `codepoint range; binary property name` OR
* `codepoint range; enumerated property name; enum value`
* The second format is new, and was not expected by `ucd2c.pl`

DerivedNormalizationProps
* `codepoint range; binary property name` OR
* `codepoint range; inverted binary property name; explicit inversion` OR
* `codepoint range; inverted trinary property name; explicit trinary` OR
* `codepoint range; mapping property name; mapping`

DoNotEmit
* `codepoint sequence; replacement sequence; Do_Not_Emit type enum`

EastAsianWidth
* `codepoint range; East_Asian_Width enum`

EmojiSources
* `codepoint; DoCoMo Shift-JIS code; KDDI Shift-JIS code; SoftBank Shift-JIS code`

EquivalentUnifiedIdeograph
* `codepoint range; Equivalent_Unified_Ideograph codepoint`

HangulSyllableType
* `codepoint range; Hangul_Syllable_Type enum`

Index
* It's a character finding helper index!

IndicPositionalCategory
* `codepoint range; Indic_Positional_Category enum`

IndicSyllabicCategory
* `codepoint range; Indic_Syllabic_Category enum`

Jamo
* `codepoint; Jamo_Short_Name string value`

LineBreak
* `codepoint range; Line_Break enum`

NameAliases
* `codepoint;alias string;alias type enum`

NamedSequencesProv
* `sequence name;codepoint sequence`
* Provisional NamedSequences data file entries, currently empty in 16.0

NamedSequences
* `sequence name;codepoint sequence`

NamesList
* Marked as not for machine-readable use

NormalizationCorrections
* `codepoint;erroneous decomp;corrected decomp;UnicodeData version with fix`
* Historical ... unless it ever grows

NormalizationTest
* TEST FILE

NushuSources
* Special tab-separated format
* `codepoint in U+ form<tab>value type tag<tab>value in UTF-8`

PropertyAliases
* `property short name; property long name (; other property alias)*`

PropertyValueAliases
* `property short name; value short name; value long name (; other value alias)*` OR
* `property short name; value short name; value long name (; other value alias)* # union | comment` OR
* `ccc; ccc numeric value; value short name; value long name` OR
* `# property long name (property short name)` comments

PropList
* `codepoint range; binary property name`

ReadMe
* Basic stub readme file

ScriptExtensions
* `codepoint range; set of abbreviated script names`
* Characters used in multiple but *not* all scripts

Scripts
* `codepoint range; script name enum`

SpecialCasing
* `codepoint; lower; title; upper; (condition_list;)? # comment`
* If the condition_list field exists, it is a conditional casing
* Points to CLDR as preferred way to describe tailored casing

StandardizedVariants
* `variation sequence; appearance description; affected shaping environments`

TangutSources
* Same tab-separated format as NushuSources
* `codepoint in U+ form<tab>value type tag<tab>value in UTF-8`

UnicodeData
* Custom very wide ;-separated format

UniKemet
* Same tab-separated format as NushuSources/TangutSources, but way more tags

USourceData
* Wide ;-separated format for U-source ideograph info

VerticalOrientation
* `codepoint range; Vertical_Orientation enum`


### auxiliary/ subdirectory

auxiliary/GraphemeBreakProperty
* `codepoint range; Grapheme_Cluster_Break enum`

auxiliary/GraphemeBreakTest.{txt,html}
* TEST FILES

auxiliary/LineBreakTest.{txt,html}
* TEST FILES

auxiliary/SentenceBreakProperty
* `codepoint range; Sentence_Break enum`

auxiliary/SentenceBreakTest.{txt,html}
* TEST FILES

auxiliary/WordBreakProperty
* `codepoint range; Word_Break enum`

auxiliary/WordBreakTest.{txt,html}
* TEST FILES


### CODETABLES/ subdirectory

CODETABLES/CP1251
* `cp1251 code<tab>Unicode codepoint<tab>#Unicode name`

CODETABLES/CP1252
* `cp1252 code<tab>Unicode codepoint<tab>#Unicode name`

CODETABLES/index-jis0208
* `right-justified-number<tab>Unicode_codepoint<tab>actual_char (Unicode_name)`
* Mixed whitespace layout


### emoji/ subdirectory

emoji/emoji-data
* `codepoint range; binary property name# emoji_release_number other comments`

emoji/emoji-variation-sequences
* `codepoint sequence; style description; # (Unicode_version) Unicode_name`

emoji/ReadMe
* Pointers to where to find emoji-related files (versioned or unversioned dirs)


### emoji-<ver>/ subdirectories

emoji-16.0/emoji-sequences
* `codepoint range; type_field; description # emoji_release [count] (examples)` OR
* `codepoint sequence; type_field; description # emoji_release [1] (example)`
* Handling of type_field seems specific to regex?

emoji-16.0/emoji-test
* TEST FILE

emoji-16.0/emoji-zwj-sequences
* `codepoint sequence; type_field; CLDR short name; # emoji_release [1] (example)`

emoji-16.0/ReadMe
* Pointers to where to find emoji-related files (versioned or unversioned dirs)


### extracted/ subdirectory

extracted/DerivedBidiClass
* `codepoint range; Bidi_Class enum`

extracted/DerivedBinaryProperties
* `codepoint range; binary property name`
* Currently only Bidi_Mirrored, but clearly intended for future extension

extracted/DerivedCombiningClass
* `codepoint range; Canonical_Combining_Class numeric value`

extracted/DerivedCompositionType
* `codepoint range; Decomposition_Type enum`

extracted/DerivedEastAsianWidth
* `codepoint range; East_Asian_Width enum`

extracted/DerivedGeneralCategory
* `codepoint range; General_Category enum`

extracted/DerivedJoiningGroup
* `codepoint range; Joining_Group enum`

extracted/DerivedJoiningType
* `codepoint range; Joining_Type enum`

extracted/DerivedLineBreak
* `codepoint range; Line_Break enum`

extracted/DerivedName
* `codepoint range; Name string or pattern`
* Patterns allow replacement of the metachar `*` with the hex codepoint

extracted/DerivedNumericType
* `codepoint range; Numeric_Type enum`

extracted/DerivedNumericValues
* `codepoint range; value as decimal; ; value as whole number or fraction`
* Third field is intentionally empty (held an enum value in prior versions)


### UCA/ subdirectory

UCA/allkeys
* `codepoint sequence; [./*collation_triplet]+` OR
* `@implicitweights codepoint_range; hex` OR
* `@version Unicode version`

UCA/CollationTest/*
* TEST FILES


## By Parsing Method

This section collects files requiring the same (or substantially similar)
parsing methods together, for convenience when checking that all files of
a given parsing type have been processed.


### Alias files

* NameAliases
* PropertyAliases
* PropertyValueAliases - will also require multiformat parsing


### Binary properties

* PropList

* emoji/emoji-data

* extracted/DerivedBinaryProperties


### Enumerated properties

* ArabicShaping - use for Joining_Group only
* Blocks
* DerivedAge
* EastAsianWidth - XXXX: what about derived version?
* HangulSyllableType
* IndicPositionalCategory
* IndicSyllabicCategory
* LineBreak
* Scripts
* VerticalOrientation

* auxiliary/GraphemeBreakProperty
* auxiliary/SentenceBreakProperty
* auxiliary/WordBreakProperty

* extracted/DerivedBidiClass
* extracted/DerivedCombiningClass
* extracted/DerivedCompositionType
* extracted/DerivedEastAsianWidth
* extracted/DerivedGeneralCategory
* extracted/DerivedJoiningGroup
* extracted/DerivedJoiningType
* extracted/DerivedLineBreak
* extracted/DerivedNumericType


### Ignored files

* CompositionExclusions    - replaced with property in DerivedNormalizationProps
* Index                    - intended for humans
* NamedSequencesProv       - for provisional sequences; currently empty
* NamesList                - intended for humans
* NormalizationCorrections - historical
* ReadMe                   - intended for humans
* StandardizedVariants     - only affects fonts and shaping algorithms

* emoji/ReadMe             - intended for humans
* emoji-ver/ReadMe         - intended for humans


### Mapping files

* BidiBrackets
* BidiMirroring
* CaseFolding
* CJKRadicals
* DoNotEmit
* EquivalentUnifiedIdeograph
* SpecialCasing

* CODETABLES/CP1251
* CODETABLES/CP1252
* CODETABLES/index-jis0208 - uses mixed whitespace for layout


### Sequences files

* NamedSequences

* emoji/emoji-variation-sequences
* emoji-ver/emoji-sequences
* emoji-ver/emoji-zwj-sequences


### Sources files

* EmojiSources
* NushuSources
* TangutSources
* UniKemet
* USourceData


### Test files

* BidiCharacterTest
* BidiTest
* NormalizationTest

* auxiliary/GraphemeBreakTest
* auxiliary/LineBreakTest
* auxiliary/SentenceBreakTest
* auxiliary/WordBreakTest

* emoji-ver/emoji-test

* UCA/CollationTest/*


### Multiformat parsing required

* DerivedCoreProperties
* DerivedNormalizationProps
* PropertyValueAliases - also one of the alias definition files


### Bespoke parsing required

* Jamo - feeds Hangul syllable naming algorithm
* ScriptExtensions
* UnicodeData

* extracted/DerivedName
* extracted/DerivedNumericValues

* UCA/allkeys
