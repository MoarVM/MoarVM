use warnings; use strict;
# Make C versions of the Unicode tables.

my $sections = {};
my $planes = [];
my $points_by_hex = {};
my $points_by_code = {};
my $enumerated_properties = {};
my $binary_properties = {};
my $first_point = undef;
my $last_point = undef;
my $aliases = {};
my $named_sequences = {};

sub progress($);
sub main {
    $sections->{'aaa_header'} = header();
    
    # Load all the things
    binary_props('extracted/DerivedNumericType');
    binary_props('extracted/DerivedBinaryProperties');
    UnicodeData(
        derived_property('BidiClass', 'Bidi_Class', {}, 0),
        derived_property('GeneralCategory', 'General_Category', {}, 0),
        derived_property('CombiningClass',
            'Canonical_Combining_Class', { Not_Reordered => 0 }, 1),
        derived_property('DecompositionType',
            'Decomposition_Type', { None => 0 }, 1)
    );
    enumerated_property('ArabicShaping', 'Joining_Type', {}, 0, 2);
    enumerated_property('ArabicShaping', 'Joining_Group', {}, 0, 3);
    enumerated_property('Blocks', 'Block', { No_Block => 0 }, 1, 1);
    BidiMirroring();
    CaseFolding();
    enumerated_property('DerivedAge',
        'Age', { Unassigned => 0 }, 1, 1);
    binary_props('DerivedCoreProperties');
    DerivedNormalizationProps();
    enumerated_property('extracted/DerivedNumericValues',
        'Numeric_Value', { NaN => 0 }, 1, 1);
    enumerated_property('HangulSyllableType',
        'Hangul_Syllable_Type', { Not_Applicable => 0 }, 1, 1);
    Jamo();
    LineBreak();
    NameAliases();
    NamedSequences();
    binary_props('PropList');
    # XXX SpecialCasing.txt # haven't decided how to do it
    # XXX StandardizedVariants.txt # no clue what this is
    break_property('Grapheme', 'Grapheme_Cluster_Break');
    break_property('Sentence', 'Sentence_Break');
    break_property('Word', 'Word_Break');
    
    # Allocate all the things
    progress "done.\nallocating bitfield...";
    my $allocated = allocate_bitfield();
    progress "done.\ncomputing bitfield...";
    compute_bitfield($allocated);
    
    #sleep 60;
    write_file($sections);
}
sub apply_to_range {
    my ($first, $last) = split '..', (shift);
    my $fn = shift;
    my $point = $points_by_hex->{$first};
    $last = $last ? hex $last : undef;
    while ($point && $point->{code} <= $last) {
        $fn->($point);
        $point = $point->{next_point};
        last unless $last;
    }
}
sub progress($) {
    my $txt = shift;
    local $| = 1;
    print $txt;
}
sub binary_props {
    my $fname = shift;
    each_line($fname, sub { $_ = shift;
        my ($range, $pname) = split /\s*[;#]\s*/;
        $binary_properties->{$pname} ||= 1;
        apply_to_range($range, sub {
            (shift)->{$pname} = 1;
        });
    });
}
sub break_property {
    my ($fname, $pname) = @_;
    enumerated_property("auxiliary/${fname}BreakProperty",
        $pname, { Other => 0 }, 1, 1);
}
sub derived_property {
    my ($fname, $pname, $base, $j) = @_;
    enumerated_property("extracted/Derived$fname",
        $pname, $base, $j, 1);
}
sub enumerated_property {
    my ($fname, $pname, $base, $j, $value_index) = @_;
    $base = { enum => $base };
    each_line($fname, sub { $_ = shift;
        my @vals = split /\s*[#;]\s*/;
        my $range = $vals[0];
        my $value = $vals[$value_index];
        my $index = $base->{enum}->{$value};
        ($base->{enum}->{$value} = $index
            = $j++) unless defined $index;
        apply_to_range($range, sub {
            my $point = shift;
            $point->{$pname} = $index;
        });
    });
    my @keys = ();
    for my $key (keys %{$base->{enum}}) {
        $keys[$base->{enum}->{$key}] = $key;
    }
    $base->{keys} = \@keys;
    $base->{name} = $pname;
    $base->{num_keys} = $j;
    $base->{bit_width} = int(log($j)/log(2) - 0.00001) + 1;
    $enumerated_properties->{$pname} = $base
}
sub each_line {
    my ($fname, $fn, $force) = @_;
    progress "done.\nprocessing $fname.txt...";
    map {
        chomp;
        $fn->($_) unless !$force && /^(?:#|\s*$)/;
    } @{read_file("UNIDATA/$fname.txt")};
}
sub allocate_bitfield {
    my @biggest = map { $enumerated_properties->{$_} }
        sort { $enumerated_properties->{$b}->{bit_width}
            cmp $enumerated_properties->{$a}->{bit_width} }
            keys %$enumerated_properties;
    for (sort keys %$binary_properties) {
        push @biggest, { name => $_, bit_width => 1 };
    }
    my $byte_offset = 0;
    my $bit_offset = 0;
    my $allocated = [];
    my $index = 0;
    while (scalar @biggest) {
        my $i = -1;
        for(;;) {
            my $prop = $biggest[++$i];
            die "failed to allocate" unless $prop;
            my $width = $prop->{bit_width};
            if ($bit_offset + $width <= 8) {
                $prop->{byte_offset} = $byte_offset;
                $prop->{bit_offset} = $bit_offset;
                $bit_offset += $width;
                if ($bit_offset == 8) {
                    $byte_offset++;
                    $bit_offset = 0;
                }
                push @$allocated, $prop;
                splice(@biggest, $i, 1);
                $prop->{prop_index} = $index++;
                last;
            }
        }
    }
    print "bitfield width is ".($byte_offset+1)." bytes\n";
    for (@$allocated) {
        print "$_->{name} : width:$_->{bit_width} byte:$_->{byte_offset} bit:$_->{bit_offset}\n"
    }
    $allocated
}
sub compute_bitfield {
    local $| = 1;
    my $fields = shift;
    for my $field (@$fields) {
        my $point = $first_point;
        print "\n..$field->{name}";
        my $i = 0;
        while ($point) {
            $point->{bytes}->[$field->{byte_offset}] |=
                ($point->{$field->{name}} << $field->{bit_offset})
                    if defined $point->{$field->{name}};
            $point = $point->{next_point};
        }
    }
}
sub header {
'/*   DO NOT MODIFY THIS FILE!  YOU WILL LOSE YOUR CHANGES!
This file is generated by ucd2c.pl from the Unicode database.

from http://unicode.org/copyright.html#Exhibit1 on 2012-07-20:

COPYRIGHT AND PERMISSION NOTICE

Copyright ?1991-2012 Unicode, Inc. All rights reserved. Distributed 
under the Terms of Use in http://www.unicode.org/copyright.html.

Permission is hereby granted, free of charge, to any person obtaining a
copy of the Unicode data files and any associated documentation (the
"Data Files") or Unicode software and any associated documentation (the
"Software") to deal in the Data Files or Software without restriction,
including without limitation the rights to use, copy, modify, merge,
publish, distribute, and/or sell copies of the Data Files or Software,
and to permit persons to whom the Data Files or Software are furnished
to do so, provided that (a) the above copyright notice(s) and this
permission notice appear with all copies of the Data Files or Software,
(b) both the above copyright notice(s) and this permission notice appear
in associated documentation, and (c) there is clear notice in each
modified Data File or in the Software as well as in the documentation
associated with the Data File(s) or Software that the data or software
has been modified.

THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF
ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR HOLDERS INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR
ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER
RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
CONNECTION WITH THE USE OR PERFORMANCE OF THE DATA FILES OR SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in these Data Files or Software without prior written
authorization of the copyright holder. */

#include "moarvm.h"
'}
sub read_file {
    my $fname = shift;
    open FILE, $fname or die "Couldn't open file '$fname': $!";
    my @lines = ();
    while( <FILE> ) {
        push @lines, $_;
    }
    close FILE;
    \@lines;
}
sub write_file {
    my $sections = shift;
    my $fname = 'src/strings/unicode_db.c';
    open FILE, ">$fname" or die "Couldn't open file '$fname': $!";;
    print FILE $sections->{$_} for (sort keys %{$sections});
    close FILE;
}
sub UnicodeData {
    my ($bidi_classes, $general_categories, $ccclasses, $decomp_types) = @_;
    my $plane = {
        number => 0,
        points => []
    };
    push @$planes, $plane;
    each_line('UnicodeData', sub {
        $_ = shift;
        my ($code_str, $name, $gencat, $ccclass, $bidiclass, $decmptype,
            $num1, $num2, $num3, $bidimirrored, $u1name, $isocomment,
            $suc, $slc, $stc) = split ';';
        
        my $code = hex $code_str;
        my $plane_num = $code >> 16;
        my $index = $code & 0xFFFF;
        my $point = {
            code_str => $code_str,
            name => $name,
            General_Category => $general_categories->{$gencat},
            Canonical_Combining_Class => $ccclasses->{$ccclass},
            Bidi_Class => $bidi_classes->{$bidiclass},
            Decomposition_Type => $decomp_types->{$decmptype},
            num1 => $num1,
            num2 => $num2,
            num3 => $num3,
            Bidi_Mirroring_Glyph => +$bidimirrored,
            u1name => $u1name,
            isocomment => $isocomment,
            suc => $suc,
            slc => $slc,
            stc => $stc,
            NFD_QC => 1, # these are defaults (inverted)
            NFC_QC => 1,
            NFKD_QC => 1,
            NFKC_QC => 1,
            code => $code,
            plane => $plane,
            'index' => $index
        };
        while ($plane->{number} < $plane_num) {
            push(@$planes, ($plane = {
                number => $plane->{number} + 1,
                points => []
            }));
        }
        push @{$plane->{points}}, $point;
        $points_by_hex->{$code_str} = $points_by_code->{$code} = $point;
        if ($last_point) {
            $last_point = $last_point->{next_point} = $point;
        }
        else {
            $last_point = $first_point = $point;
        }
    });
}
sub BidiMirroring {
    each_line('BidiMirroring', sub { $_ = shift;
        my ($left, $right) = /^([0-9A-F]+).*?([0-9A-F]+)/;
        $points_by_hex->{$left}->{Bidi_Mirroring_Glyph} = $right;
    });
}
sub CaseFolding {
    each_line('CaseFolding', sub { $_ = shift;
        my ($left, $type, $right) = split /\s*;\s*/;
        return if $type eq 'S' || $type eq 'T';
        my @parts = split ' ', $right;
        $points_by_hex->{$left}->{Case_Folding} = \@parts;
    });
}
sub DerivedNormalizationProps {
    my $binary = {
        Full_Composition_Exclusion => 1,
        Changes_When_NFKC_Casefolded => 1
    };
    my $inverted_binary = {
        NFD_QC => 1,
        NFC_QC => 1,
        NFKD_QC => 1,
        NFKC_QC => 1
    };
    $binary_properties->{$_} = 1 for ((keys %$binary),(keys %$inverted_binary));
    # XXX handle NFKC_CF
    each_line('DerivedNormalizationProps', sub { $_ = shift;
        my ($range, $property_name, $value) = split /\s*[;#]\s*/;
        if (exists $binary->{$property_name}) {
            $value = 1;
        }
        elsif (exists $inverted_binary->{$property_name}) {
            $value = undef;
        }
        elsif ($property_name eq 'NFKC_Casefold') {
            my @parts = split ' ', $value;
            $value = \@parts;
        }
        else {
            return; # deprecated # XXX verify
        }
        apply_to_range($range, sub {
            my $point = shift;
            $point->{$property_name} = $value;
        });
    });
}
sub Jamo {
    each_line('Jamo', sub { $_ = shift;
        my ($code_str, $name) = split /\s*[;#]\s*/;
        $points_by_hex->{$code_str}->{Jamo_Short_Name} = $name;
    });
}
sub LineBreak {
    my $enum = {};
    my $base = { enum => $enum };
    my $j = 0;
    $enum->{$_} = $j++ for ("BK", "CR", "LF", "CM", "SG", "GL",
        "CB", "SP", "ZW", "NL", "WJ", "JL", "JV", "JT", "H2", "H3");
    each_line('LineBreak', sub { $_ = shift;
        my ($range, $name) = split /\s*[;#]\s*/;
        return unless exists $enum->{$name}; # only normative
        apply_to_range($range, sub {
            my $point = shift;
            $point->{Line_Break} = $enum->{$name};
        });
    });
    my @keys = ();
    for my $key (keys %{$base->{enum}}) {
        $keys[$base->{enum}->{$key}] = $key;
    }
    $base->{keys} = \@keys;
    $base->{name} = 'Line_Break';
    $base->{num_keys} = $j;
    $base->{bit_width} = int(log($j)/log(2) - 0.00001) + 1;
    $enumerated_properties->{Line_Break} = $base
}
sub NameAliases {
    each_line('NameAliases', sub { $_ = shift;
        my ($code_str, $name) = split /\s*[;#]\s*/;
        $aliases->{$name} = hex $code_str;
    });
}
sub NamedSequences {
    each_line('NamedSequences', sub { $_ = shift;
        my ($name, $codes) = split /\s*[;#]\s*/;
        my @parts = split ' ', $codes;
        $named_sequences->{$name} = \@parts;
    });
}
main();