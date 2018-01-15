#!/usr/bin/env perl
use 5.014;
use warnings; use strict;
use utf8;
use feature 'unicode_strings';
use Data::Dumper;
use Carp qw(cluck croak carp);
$Data::Dumper::Maxdepth = 1;
# Make C versions of the Unicode tables.

# Before running, downloading UNIDATA.zip from http://www.unicode.org/Public/zipped/
# and extract them into UNIDATA in the root of this repository.

# Download allkeys.txt from http://www.unicode.org/Public/UCA/latest/
# and place into a folder named UCA under the UNIDATA directory.

my $DEBUG = $ENV{UCD2CDEBUG} // 0;

my @NAME_LINES;
my $LOG_FH;
if ($DEBUG) {
    open($LOG_FH, ">", "extents") or croak "can't create extents: $!";
    binmode $LOG_FH, ':encoding(UTF-8)';
}
binmode STDOUT, ':encoding(UTF-8)';
binmode STDERR, ':encoding(UTF-8)';
my $LOG;
# All globals should use UPPERCASE
my $DB_SECTIONS = {};
my $SEQUENCES = {};
my $HOUT = "";
my $H_SECTIONS = {};
my @POINTS_SORTED;
my $POINTS_BY_CODE = {};
my $ENUMERATED_PROPERTIES = {};
my $BINARY_PROPERTIES = {};
my $FIRST_POINT = undef;
my $ALIASES = {};
my $ALIAS_TYPES = {};
my $PROP_NAMES = {};
my $NAMED_SEQUENCES = {};
my $BITFIELD_TABLE = [];
my $PROP_CODES = {};
my $ALL_PROPERTIES = {};
my $PROPERTY_INDEX = 0;
my $ESTIMATED_TOTAL_BYTES = 0;
my $TOTAL_BYTES_SAVED = 0;
my $WRAP_TO_COLUMNS = 120;
my $COMPRESS_CODEPOINTS = 1;
my $GAP_LENGTH_THRESHOLD = 1000;
my $SPAN_LENGTH_THRESHOLD = 100;
my $SKIP_MOST_MODE = 0;
my $BITFIELD_CELL_BITWIDTH = 32;
my $GC_ALIAS_CHECKERS = [];
my $GENERAL_CATEGORIES = {};

sub trim {
    my ($s) = @_;
    $s =~ s/   \s+ $ //xg;
    $s =~ s/ ^ \s+   //xg;
    if ($s =~ / ^ \s /x or $s =~ / \s$ /x) {
        croak "'$s'";
    }
    return $s;
}
sub add_emoji_sequences {
    my $directory = "UNIDATA";
    my @emoji_dirs;
    my $highest_emoji_version = "";
    # Find all the emoji dirs
    opendir (UNIDATA_DIR, $directory) or die $!;
    while (my $file = readdir(UNIDATA_DIR)) {
        if (-d "$directory/$file" and $file =~ /^emoji/) {
            push @emoji_dirs, $file;
            $file =~ s/^emoji-//;
            # Get the highest version. Make sure longer values take precidence so when we hit 10.0 or higher it will work fine.
            if (length($highest_emoji_version) < length($file) or (length($highest_emoji_version) < length($file) and $highest_emoji_version lt $file)) {
                $highest_emoji_version = $file;
            }
        }
    }
    say "Highest emoji version found is $highest_emoji_version";
    if (!@emoji_dirs) {
        die "Couldn't find any emoji folders. Please run UCD-download.p6 again";
    }
    for my $folder (@emoji_dirs) {
        add_unicode_sequence("$folder/emoji-sequences");
        add_unicode_sequence("$folder/emoji-zwj-sequences");
    }
    return $highest_emoji_version;
}
#sub progress($);
sub main {
    $DB_SECTIONS->{'AAA_header'} = header();
    my $highest_emoji_version = add_emoji_sequences();
    add_unicode_sequence('NamedSequences');
    gen_unicode_sequence_keypairs();
    NameAliases();
    gen_name_alias_keypairs();
    # Load all the things
    UnicodeData(
        derived_property('BidiClass', 'Bidi_Class', { L => 0 }, 0),
        derived_property('GeneralCategory', 'General_Category', { Cn => 0 }, 0),
        derived_property('CombiningClass',
            'Canonical_Combining_Class', { Not_Reordered => 0 }, 1)
    );
    enumerated_property('BidiMirroring', 'Bidi_Mirroring_Glyph', { 0 => 0 }, 1, 'int', 1);
    collation();
    Jamo();
    #BidiMirroring();
    goto skip_most if $SKIP_MOST_MODE;
    binary_props('extracted/DerivedBinaryProperties');
    binary_props("emoji-$highest_emoji_version/emoji-data");
    enumerated_property('ArabicShaping', 'Joining_Group', {}, 3);
    enumerated_property('Blocks', 'Block', { No_Block => 0 }, 1);
    # sub Jamo sets names properly. Though at the moment Jamo_Short_Name likely
    # will not need to be a property since it's only used for programatically
    # creating Jamo's Codepoint Names
    #enumerated_property('Jamo', 'Jamo_Short_Name', {  }, 1, 1);
    enumerated_property('extracted/DerivedDecompositionType', 'Decomposition_Type', { None => 0 }, 1);
    enumerated_property('extracted/DerivedEastAsianWidth', 'East_Asian_Width', { N => 0 }, 1);
    enumerated_property('ArabicShaping', 'Joining_Type', { U => 0 }, 2);
    CaseFolding();
    SpecialCasing();
    enumerated_property('DerivedAge',
        'Age', { Unassigned => 0 }, 1);
    binary_props('DerivedCoreProperties');
    DerivedNormalizationProps();
    enumerated_property('extracted/DerivedNumericValues',
        'Numeric_Value', { NaN => 0 }, 1);
    enumerated_property('extracted/DerivedNumericValues',
        'Numeric_Value_Numerator', { NaN => 0 }, sub {
            my @fraction = split('/', (shift->[3]));
            return $fraction[0];
        });
    enumerated_property('extracted/DerivedNumericValues',
        'Numeric_Value_Denominator', { NaN => 0 }, sub {
            my @fraction = split('/', (shift->[3]));
            return $fraction[1] || '1';
        });
    enumerated_property('extracted/DerivedNumericType',
        'Numeric_Type', { None => 0 }, 1);
    enumerated_property('HangulSyllableType',
        'Hangul_Syllable_Type', { Not_Applicable => 0 }, 1);
    enumerated_property('LineBreak', 'Line_Break', { XX => 0 }, 1);
    NamedSequences();
    binary_props('PropList');
    enumerated_property('Scripts', 'Script', { Unknown => 0 }, 1);
    # XXX StandardizedVariants.txt # no clue what this is
    grapheme_cluster_break('Grapheme', 'Grapheme_Cluster_Break');
    break_property('Sentence', 'Sentence_Break');
  skip_most:
    break_property('Word', 'Word_Break');
    tweak_nfg_qc();

    # Allocate all the things
    progress("done.\nsetting next_point for codepoints");
    set_next_points();
    progress("done.\nallocating bitfield...");
    my $allocated_bitfield_properties = allocate_bitfield();
    # Compute all the things
    progress("done.\ncomputing all properties...");
    compute_properties($allocated_bitfield_properties);
    # Make the things less
    progress("...done.\ncomputing collapsed properties table...");
    compute_bitfield($FIRST_POINT);
    # Emit all the things
    progress("...done.\nemitting unicode_db.c...");
    emit_bitfield($FIRST_POINT);
    my $extents = emit_codepoints_and_planes($FIRST_POINT);
    emit_case_changes($FIRST_POINT);
    emit_codepoint_row_lookup($extents);
    emit_property_value_lookup($allocated_bitfield_properties);
    emit_names_hash_builder($extents);
    emit_unicode_property_keypairs();
    emit_unicode_property_value_keypairs();
    emit_block_lookup();
    emit_composition_lookup();

    print "done!";
    write_file('src/strings/unicode_db.c', join_sections($DB_SECTIONS));
    write_file('src/strings/unicode_gen.h', join_sections($H_SECTIONS));
    print "\nEstimated bytes demand paged from disk: ".
        thousands($ESTIMATED_TOTAL_BYTES).
        ".\nEstimated bytes saved by various compressions: ".
        thousands($TOTAL_BYTES_SAVED).".\n";
    if ($DEBUG) {
        $LOG =~ s/('fate_really' => )(\d+)/$1$NAME_LINES[$2]/g;
        print $LOG_FH $LOG;
        close $LOG_FH;
    }
    print "\nDONE!!!\n\n";
    return 1;
}

sub thousands {
    my $in = shift;
    $in = reverse "$in"; # stringify or copy the string
    $in =~ s/(\d\d\d)(?=\d)/$1,/g;
    return reverse($in);
}

sub stack_lines {
    # interleave @$lines with separator $sep, using a different
    # separator $break every $num lines or when $wrap columns is reached
    my ($lines, $sep, $break, $num, $wrap) = @_;
    my $i = 1;
    my $out = "";
    my $first = 1;
    my $length = 0;
    my $sep_length = length($sep);
    for (@$lines) {
        my $line_length = length($_);
        if ($first) {
            $first = 0;
            $length = $line_length;
        }
        else {
            if ($num && ($i++ % $num) || $wrap && $length + $sep_length + $line_length <= $wrap) {
                $out .= $sep;
                $length += $sep_length + $line_length;
            }
            else {
                $out .= $break;
                $length = $line_length;
            }
        }
        $out .= $_;
    }
    return $out;
}

sub join_sections {
    my ($sections) = @_;
    my $content = "";
    $content .= "\n".$sections->{$_} for (sort keys %{$sections});
    return $content;
}

sub set_next_points {
    my $previous;
    for my $code (sort { $a <=> $b } keys %{$POINTS_BY_CODE}) {
        push @POINTS_SORTED, $POINTS_BY_CODE->{$code};
        $POINTS_BY_CODE->{$previous}->{next_point} = $POINTS_BY_CODE->{$code}
            if defined $previous;
        # The first code we encounter will be the lowest, so set $FIRST_POINT
        if (!defined $previous) {
            say "setting first point to $code";
            $FIRST_POINT = $POINTS_BY_CODE->{$code};
        }
        $previous = $code;
    }
    return;
}
sub get_next_point {
    my ($code, $add_to_points_by_code) = @_;
    my $point = $POINTS_BY_CODE->{$code};
    if (!$point) {
        $point = {
            code => $code,
            code_str => sprintf ("%.4X", $code),
            Any => 1,
            NFD_QC => 1, # these are defaults (inverted)
            NFC_QC => 1, # which will be unset as appropriate
            NFKD_QC => 1,
            NFKC_QC => 1,
            NFG_QC => 1,
            MVM_COLLATION_QC => 1,
            name => "",
            gencat_name => "Cn",
            General_Category => $GENERAL_CATEGORIES->{enum}->{Cn}
        };
        die if defined $POINTS_BY_CODE->{$code};
        if ($add_to_points_by_code) {
             $POINTS_BY_CODE->{$code} = $point;
        }
    }
    return $point;
}

sub apply_to_range {
    # apply a function to a range of codepoints. The starting and
    # ending codepoint of the range need not exist; the function will
    # be applied to all/any in between.
    my ($range, $fn) = @_;
    chomp($range);
    if ( !defined $range ) {
        cluck "Did not get any range in apply_to_range";
    }
    my ($first_str, $last_str) = split '\\.\\.', $range;
    $first_str ||= $range;
    $last_str ||= $first_str;
    my ($first_code, $last_code) = (hex $first_str, hex $last_str);
    my $curr_code = $first_code;
    my $point;
    while ($curr_code <= $last_code) {
        $point = get_next_point($curr_code);
        $fn->($point);
        $curr_code++;
    }
    return;
}

sub progress {
    my ($txt) = @_;
    local $| = 1;
    print $txt;
    return;
}

sub binary_props {
    # process a file, extracting binary properties and applying them to ranges
    my ($fname) = @_; # filename
    each_line($fname, sub { $_ = shift;
        my ($range, $pname) = split /\s*[;#]\s*/; # range, property name
        register_binary_property($pname); # define the property
        apply_to_range($range, sub {
            my $point = shift;
            $point->{$pname} = 1; # set the property
        });
    });
    return;
}

sub break_property {
    my ($fname, $pname) = @_;
    enumerated_property("auxiliary/${fname}BreakProperty",
        $pname, { Other => 0 }, 1);
    return;
}
sub grapheme_cluster_break {
    my ($fname, $pname) = @_;
    enumerated_property("auxiliary/${fname}BreakProperty",
        $pname, {
            # Should not be set to Other for this one ?
            Other => 0,
        }, 1);
    return;
}
# Make sure we don't assign twice to the same pvalue code
sub check_base_for_duplicates {
    my ($base) = @_;
    my %seen;
    for my $key (keys %{$base->{enum}}) {
        if ($seen{ $base->{enum}->{$key} }) {
            croak("\nError: assigned twice to the same property value code "
                . "(Property $base->{name} Both $key and $seen{ $base->{enum}->{$key} }"
                . " are assigned to pvalue code $base->{enum}->{$key}\n"
                . Dumper $base->{enum});
        }
        $seen{ ($base->{enum}->{$key}) } = $key;
    }
    my $start = 0;
    for my $key (sort { $base->{enum}->{$a} <=> $base->{enum}->{$b} } keys %{$base->{enum}}) {
        croak("\nError: property value code is not sequential for property '$base->{name}'."
            . " Expected $start but saw $base->{enum}->{$key}\n" . Dumper $base->{enum})
            if $base->{enum}->{$key} != $start;
        $start++;
    }
    return;
}
sub derived_property {
    # filename, property name, property object
    my ($fname, $pname, $base) = @_;
    # If we provided some property values already, add that number to the counter
    my $j = scalar keys %{$base};
    # wrap the provided object as the enum key in a new one
    $base = { enum => $base, name => $pname };
    each_line("extracted/Derived$fname", sub { $_ = shift;
        my ($range, $class) = split /\s*[;#]\s*/;
        unless (exists $base->{enum}->{$class}) {
            # haven't seen this property's value before
            # add it, and give it an index.
            print "\n  adding derived property for $pname: $j $class" if $DEBUG;
            $base->{enum}->{$class} = $j++;
        }
    });
    register_keys_and_set_bit_width($base, $j);
    return register_enumerated_property($pname, $base);
}
sub register_keys {
    my ($base) = @_;
    my @keys = ();
    # stash the keys in an array so they can be put in a table later
    for my $key (keys %{$base->{enum}}) {
        $keys[$base->{enum}->{$key}] = $key;
    }
    print "\n    keys = @keys" if $DEBUG;
    $base->{keys} = \@keys;
    return scalar(@keys);
}
# XXX Eventually this function should replace register_keys and setting the bitwidth
# in each function individually.
sub register_keys_and_set_bit_width {
    my ($base, $j) = @_;
    my $reg = register_keys($base);
    $base->{bit_width} = least_int_ge_lg2($reg);
    die "The number of keys and the number of \$j do not match. Keys: $reg \$j: $j"
        if (defined $j and $reg != $j);
    return;
}

sub enumerated_property {
    my ($fname, $pname, $base, $value_index, $type, $is_hex) = @_;
    my $j = scalar keys %{$base};
    $type = 'string' unless $type;
    $base = { enum => $base, name => $pname, type => $type };
    each_line($fname, sub { $_ = shift;
        my @vals = split /\s*[#;]\s*/;
        my $range = $vals[0];
        my $value = ref $value_index
            ? $value_index->(\@vals)
            : $vals[$value_index];
        $value = hex $value if $is_hex;
        my $index = $base->{enum}->{$value};
        if (not defined $index) {
            # Haven't seen this property value before. Add it, and give it an index.
            print("\n  adding enum property for $pname: $j $value") if $DEBUG;
            ($base->{enum}->{$value} = $index = $j++);
        }
        apply_to_range($range, sub {
            my $point = shift;
            $point->{$pname} = $index; # set the property's value index
        });
    });
    $base->{bit_width} = least_int_ge_lg2($j);
    print "\n    bitwidth: ",$base->{bit_width},"\n" if $DEBUG;
    register_keys($base);
    register_enumerated_property($pname, $base);
    return;
}

sub least_int_ge_lg2 {
    return int(log(shift)/log(2) - 0.00001) + 1;
}

sub each_line {
    my ($fname, $fn, $force) = @_;
    progress("done.\nprocessing $fname.txt...");
    map {
        chomp;
        $fn->($_) unless !$force && /^(?:#|\s*$)/;
    } @{read_file("UNIDATA/$fname.txt")};
    return;
}

sub allocate_bitfield {
    my @biggest = map { $ENUMERATED_PROPERTIES->{$_} }
        sort { $ENUMERATED_PROPERTIES->{$b}->{bit_width}
            <=> $ENUMERATED_PROPERTIES->{$a}->{bit_width} }
            sort keys %$ENUMERATED_PROPERTIES;
    for (sort keys %$BINARY_PROPERTIES) {
        push @biggest, $BINARY_PROPERTIES->{$_};
    }
    my $word_offset = 0;
    my $bit_offset = 0;
    my $allocated = [];
    my $index = 1;
    while (@biggest) {
        my $i = -1;
        for(;;) {
            my $prop = $biggest[++$i];
            if (!$prop) {
                while (@biggest) {
                    # ones bigger than 1 byte :(.  Don't prefer these.
                    $prop = shift @biggest;
                    $prop->{word_offset} = $word_offset;
                    $prop->{bit_offset} = $bit_offset;
                    $bit_offset += $prop->{bit_width};
                    while ($bit_offset >= $BITFIELD_CELL_BITWIDTH) {
                        $word_offset++;
                        $bit_offset -= $BITFIELD_CELL_BITWIDTH;
                    }
                    push @$allocated, $prop;
                    $prop->{field_index} = $index++;
                }
                last;
            }
            if ($bit_offset + $prop->{bit_width} <= $BITFIELD_CELL_BITWIDTH) {
                $prop->{word_offset} = $word_offset;
                $prop->{bit_offset} = $bit_offset;
                $bit_offset += $prop->{bit_width};
                if ($bit_offset == $BITFIELD_CELL_BITWIDTH) {
                    $word_offset++;
                    $bit_offset = 0;
                }
                push @$allocated, $prop;
                splice(@biggest, $i, 1);
                $prop->{field_index} = $index++;
                last;
            }
        }
    }
    $FIRST_POINT->{bitfield_width}    = $word_offset + 1;
    $H_SECTIONS->{num_property_codes} = "#define MVM_NUM_PROPERTY_CODES $index\n";
    return $allocated;
}

sub compute_properties {
    my ($fields) = @_;
    local $| = 1;
    for my $field (@$fields) {
        my $bit_offset = $field->{bit_offset};
        my $bit_width = $field->{bit_width};
        print "\n        $field->{name} bit width:$bit_width";
        my $i = 0;
        my $bit = 0;
        my $mask = 0;
        while ($bit < $BITFIELD_CELL_BITWIDTH) {
            $mask |= 2 ** $bit++;
        }
        for my $point (@POINTS_SORTED) {
            if (defined $point->{$field->{name}}) {
                my $word_offset = $field->{word_offset};
                # $x is one less than the number of words required to hold the field
                my $x = int(($bit_width - 1) / $BITFIELD_CELL_BITWIDTH);
                # move us over to the last word
                $word_offset += $x;
                # loop until we fill all the words, starting with the most
                # significant byte portion.
                while ($x + 1) {

                    $point->{bytes}->[
                        $word_offset - $x
                    ] |=
                        (
                            (
                                ($point->{$field->{name}} <<
                                    ($BITFIELD_CELL_BITWIDTH - $bit_offset - $bit_width)
                                )
                                #>> ($BITFIELD_CELL_BITWIDTH * $x)
                            ) & $mask
                        );
                    $x--;
                }
            }
        }
    }
    return;
}

sub emit_binary_search_algorithm {
    # $extents is arrayref to the heads of the gaps, spans, and
    # normal stretches of codepoints. $first and $last are the
    # indexes into $extents we're supposed to subdivide.
    # protocol: start output with a newline; don't end with a newline or indent
    my ($extents, $first, $mid, $last, $indent) = @_;
    #${indent} /* got  $first  $mid  $last  */\n";
    return emit_extent_fate($extents->[$first], $indent) if $first == $last;
    $mid = $last if $first == $mid;
    my $new_mid_high = int(($last + $mid) / 2);
    my $new_mid_low = int(($mid - 1 + $first) / 2);
    my $high = emit_binary_search_algorithm($extents, $mid, $new_mid_high,
        $last, "    $indent");
    my $low = emit_binary_search_algorithm($extents, $first, $new_mid_low,
        $mid - 1, "    $indent");
    my $rtrn = sprintf( <<"END", $extents->[$mid]->{code}, ($extents->[$mid]->{name} || 'NULL'));

${indent}if (codepoint >= 0x%X) { /* %s */$high
${indent}}
${indent}else {$low
${indent}}
END
    chomp $rtrn;
    return $rtrn;
}

my $FATE_NORMAL = 0;
my $FATE_NULL   = 1;
my $FATE_SPAN   = 2;

sub emit_extent_fate {
    my ($fate, $indent) = @_;
    my $type = $fate->{fate_type};
    return "\n${indent}return -1;" if $type == $FATE_NULL;
    return "\n${indent}return " . ($fate->{code} - $fate->{fate_offset}) . "; /* ".
        "$BITFIELD_TABLE->[$fate->{bitfield_index}]->{code_str}".
        " $BITFIELD_TABLE->[$fate->{bitfield_index}]->{name} */" if $type == $FATE_SPAN;
    return "\n${indent}return codepoint - $fate->{fate_offset};"
        . ($fate->{fate_offset} == 0 ? " /* the fast path */ " : "");
}

sub add_extent {
    my ($extents, $extent) = @_;
    if ($DEBUG) {
        $LOG .= "\n" . join '',
            grep { /code|fate|name|bitfield/ }
                sort(split /^/m, "EXTENT " . Dumper($extent));
    }
    push @$extents, $extent;
    return;
}
# Used in emit_codepoints_and_planes to push the codepoints name onto bitfield_index_lines
sub ecap_push_name_line {
    my ($name, $point, $bitfield_index_lines, $bytes, $index, $annotate_anyway) = @_;
    if (!defined $name) {
        push @$bitfield_index_lines,
            ($annotate_anyway
                ? "/*$$index*/$point->{bitfield_index}/*$point->{code_str} */"
                : "0"
            );
        push @NAME_LINES, "NULL";
    }
    else {
        $$bytes += length($point->{name}) + 1; # length + 1 for the NULL
        push @$bitfield_index_lines, "/*$$index*/$point->{bitfield_index}/* $point->{code_str} */";
        push @NAME_LINES, "/*$$index*/\"$point->{name}\"/* $point->{code_str} */";
    }
    $$bytes += 2; # hopefully these are compacted since they are trivially aligned being two bytes
    $$bytes += 8; # 8 for the pointer
    $$index++;
    return;
}
sub emit_codepoints_and_planes {
    my @bitfield_index_lines;
    my $index = 0;
    my $bytes = 0;
    my $bytes_saved = 0;
    my $code_offset = 0;
    my $extents = [];
    my $last_code = -1; # trick
    my $last_point = undef;
    $FIRST_POINT->{fate_type} = $FATE_NORMAL;
    $FIRST_POINT->{fate_offset} = $code_offset;
    #$FIRST_POINT->{fake_really} = $FIRST_POINT->{code} - $code_offset
    add_extent $extents, $FIRST_POINT;
    my $span_length = 0;

    # a bunch of spaghetti code.  Yes.
    my $toadd = undef;
    for my $point (@POINTS_SORTED) {
        # extremely simplistic compression of identical neighbors and gaps
        # this point is identical to the previous point
        if ($COMPRESS_CODEPOINTS && $last_point
                && $last_code == $point->{code} - 1
                && is_same($last_point, $point)
                && $last_point->{bitfield_index} == $point->{bitfield_index}) {
            # create a or extend the current span
            ++$last_code;
            if ($span_length) {
                ++$span_length;
            }
            else {
                $span_length = 2;
            }
            next;
        }

        # the span ended, either bridge it or skip it
        if ($span_length) {
            if ($SPAN_LENGTH_THRESHOLD <= $span_length) {
                $bytes_saved += 10 * ($span_length - 1);
                add_extent $extents, $last_point if !defined($last_point->{fate_type});
                $code_offset = $last_point->{code} - @NAME_LINES + 1;
                $last_point->{fate_type}   = $FATE_SPAN;
                $last_point->{fate_offset} = $code_offset;
                $last_point->{fate_really} = $last_point->{code} - $code_offset;
                $code_offset += $span_length - 1;
                $toadd = $point;
                $span_length = 0;
            }
            my $usually = 1;  # occasionally change NULL to the name to cut name search time
            for (; 1 < $span_length; $span_length--) {
                # catch up to last code
                $last_point = $last_point->{next_point};
                 # occasionally change NULL to the name to cut name search time
                if ($last_point->{name} =~ /^</ && $usually++ % 25) {
                    ecap_push_name_line(undef, $last_point, \@bitfield_index_lines, \$bytes, \$index, 1);
                }
                else {
                    ecap_push_name_line($last_point->{name}, $last_point, \@bitfield_index_lines, \$bytes, \$index);
                }
            }
            $span_length = 0;
        }

        if ($COMPRESS_CODEPOINTS
                && $last_code < $point->{code} - ($point->{code} % 0x10000 ? $GAP_LENGTH_THRESHOLD : 1)) {
            $bytes_saved += 10 * ($point->{code} - $last_code - 1);
            add_extent $extents, { fate_type => $FATE_NULL,
                code => $last_code + 1 };
            $code_offset += ($point->{code} - $last_code - 1);
            $last_code = $point->{code} - 1;
            $toadd = $point;
        }
        for (; $last_code < $point->{code} - 1; $last_code++) {
            ecap_push_name_line(undef, $point, \@bitfield_index_lines, \$bytes, \$index);
        }

        croak "$last_code  " . Dumper($point) unless $last_code == $point->{code} - 1;
        if ($toadd && !exists($point->{fate_type})) {
            $point->{fate_type}   = $FATE_NORMAL;
            $point->{fate_offset} = $code_offset;
            $point->{fate_really} = $point->{code} - $code_offset;
            add_extent $extents, $point;
        }
        $toadd = undef;
        # a normal codepoint that we don't want to compress
        ecap_push_name_line($point->{name}, $point, \@bitfield_index_lines, \$bytes, \$index);
        $last_code = $point->{code};
        $point->{main_index} = $index;
        $last_point = $point;
    }
    print "\nSaved " . thousands($bytes_saved) . " bytes by compressing big gaps into a binary search lookup.\n";
    $TOTAL_BYTES_SAVED += $bytes_saved;
    $ESTIMATED_TOTAL_BYTES += $bytes;
    # jnthn: Would it still use the same amount of memory to combine these tables? XXX
    $DB_SECTIONS->{BBB_codepoint_names} =
        "static const char *codepoint_names[$index] = {\n    ".
            stack_lines(\@NAME_LINES, ",", ",\n    ", 0, $WRAP_TO_COLUMNS).
            "\n};";
    $DB_SECTIONS->{BBB_codepoint_bitfield_indexes} =
        "static const MVMuint16 codepoint_bitfield_indexes[$index] = {\n    ".
            stack_lines(\@bitfield_index_lines, ",", ",\n    ", 0, $WRAP_TO_COLUMNS).
            "\n};";
    $H_SECTIONS->{codepoint_names_count} = "#define MVM_CODEPOINT_NAMES_COUNT $index";
    return $extents
}

sub emit_codepoint_row_lookup {
    my $extents = shift;
    my $SMP_start;
    my $i = 0;
    for (@$extents) {
        # handle the first recursion specially to optimize for most common BMP lookups
        if ($_->{code} >= 0x10000) {
            $SMP_start = $i;
            last;
        }
        $i++;
    }
    my $out = 'static MVMint32 MVM_codepoint_to_row_index(MVMThreadContext *tc, MVMint64 codepoint) {

    MVMint32 plane = codepoint >> 16;

    if (codepoint < 0) {
        MVM_exception_throw_adhoc(tc,
            "Internal Error: MVM_codepoint_to_row_index call requested a synthetic codepoint that does not exist.\n"
            "Requested synthetic %"PRId64" when only %"PRId32" have been created.",
            -codepoint, tc->instance->nfg->num_synthetics);
    }

    if (plane == 0) {'
    . emit_binary_search_algorithm($extents, 0, 1, $SMP_start - 1, "        ") . '
    }
    else {
        if (plane < 0 || plane > 16 || codepoint > 0x10FFFD) {
            return -1;
        }
        else {' . emit_binary_search_algorithm($extents, $SMP_start,
            int(($SMP_start + @$extents - 1)/2), @$extents - 1, "            ") . '
        }
    }
}';
    $DB_SECTIONS->{codepoint_row_lookup} = $out;
    return;
}

sub emit_case_changes {
    my $point = shift;
    my @lines = ();
    my $out = '';
    my $rows = 1;
    while ($point) {
        unless ($point->{Case_Change_Index}) {
            $point = $point->{next_point};
            next;
        }
        push @lines, "/*$rows*/{0x".($point->{suc}||0).",0x".($point->{slc}||0).",0x".($point->{stc}||0)."}/* $point->{code_str} */";
        $point = $point->{next_point};
        $rows++;
    }
    $out = "static const MVMint32 case_changes[$rows][3] = {\n    {0x0,0x0,0x0},\n    ".
        stack_lines(\@lines, ",", ",\n    ", 0, $WRAP_TO_COLUMNS)."\n};";
    $DB_SECTIONS->{BBB_case_changes} = $out;
    return;
}

sub emit_bitfield {
    my $point = shift;
    my $wide = $point->{bitfield_width};
    my @lines = ();
    my $out = '';
    my $rows = 1;
    my $line = "{";
    my $first = 1;
    my $i = 0;
    for (; $i < $wide; ++$i) {
        $line .= "," unless $first;
        $first = '0';
        $line .= '0';
    }
    push @lines, "$line}";
    while ($point) {
        $line = "/*$rows*/{";
        $first = 1;
        for ($i = 0; $i < $wide; ++$i) {
            $_ = $point->{bytes}->[$i];
            $line .= "," unless $first;
            $first = 0;
            $line .= (defined $_ ? $_."u" : 0);
        }
        push @$BITFIELD_TABLE, $point;
        push @lines, ($line . "}/* $point->{code_str} */");
        $point = $point->{next_emit_point};
        $rows++;
    }
    my $bytes_wide = 2;
    $bytes_wide *= 2 while $bytes_wide < $wide; # assume the worst
    $ESTIMATED_TOTAL_BYTES += $rows * $bytes_wide; # we hope it's all laid out with no gaps...
    my $val_type = ($BITFIELD_CELL_BITWIDTH == 8 || $BITFIELD_CELL_BITWIDTH == 16
        || $BITFIELD_CELL_BITWIDTH == 32 || $BITFIELD_CELL_BITWIDTH == 64)
        ? ("MVMuint" . $BITFIELD_CELL_BITWIDTH)
        : croak("Unknown value of \$BITFIELD_CELL_BITWIDTH: $BITFIELD_CELL_BITWIDTH");
    $out = "static const $val_type props_bitfield[$rows][$wide] = {\n    ".
        stack_lines(\@lines, ",", ",\n    ", 0, $WRAP_TO_COLUMNS)."\n};";
    $DB_SECTIONS->{BBB_main_bitfield} = $out;
    return;
}
sub is_str_enum {
    my ($prop) = @_;
    return exists $prop->{keys} && (!defined $prop->{type} || $prop->{type} ne 'int');
}
sub EPVL_gen_pvalue_defines {
    my ( $property_name_mvm, $property_name, $short_pval_name ) = @_;
    my $GCB_h;
    $GCB_h .= "\n\n/* $property_name_mvm */\n";
    my %seen;
    foreach my $key (sort keys % {$ENUMERATED_PROPERTIES->{$property_name}->{'enum'} }  ) {
        next if $seen{$key};
        my $value = $ENUMERATED_PROPERTIES->{$property_name}->{'enum'}->{$key};
        $key = 'MVM_UNICODE_PVALUE_' . $short_pval_name . '_' . uc $key;
        $key =~ tr/\./_/;
        $GCB_h .= "#define $key $value\n";
        $seen{$key} = 1;
    }
    $HOUT .= $GCB_h;
    return;
}
sub emit_property_value_lookup {
    my $allocated = shift;
    my $enumtables = "\n\n";
    my $hout = "typedef enum {\n";
    chomp(my $int_out = <<'END');

static MVMint32 MVM_unicode_get_property_int(MVMThreadContext *tc, MVMint64 codepoint, MVMint64 property_code) {
    MVMuint32 switch_val = (MVMuint32)property_code;
    MVMint32 result_val = 0; /* we'll never have negatives, but so */
    MVMuint32 codepoint_row = MVM_codepoint_to_row_index(tc, codepoint);
    MVMuint16 bitfield_row;
    /* If codepoint is not found in bitfield rows */
    if (codepoint_row == -1) {
        /* Unassigned codepoints have General Category Cn. Since this returns 0
         * for unknowns, unless we return 1 for property C then these unknows
         * won't match with <:C> */
        if (property_code == MVM_UNICODE_PROPERTY_C)
            return 1;
        return 0;
    }
    bitfield_row = codepoint_bitfield_indexes[codepoint_row];

    switch (switch_val) {
        case 0: return 0;
END

    chomp(my $str_out = <<'END');

static MVMint32 MVM_codepoint_to_row_index(MVMThreadContext *tc, MVMint64 codepoint);

static const char *bogus = "<BOGUS>"; /* only for table too short; return null string for no mapping */

static const char* MVM_unicode_get_property_str(MVMThreadContext *tc, MVMint64 codepoint, MVMint64 property_code) {
    MVMuint32 switch_val = (MVMuint32)property_code;
    MVMint32 result_val = 0; /* we'll never have negatives, but so */
    MVMuint32 codepoint_row = MVM_codepoint_to_row_index(tc, codepoint);
    MVMuint16 bitfield_row = 0;

    if (codepoint_row == -1) { /* non-existent codepoint; XXX should throw? */
        if (0x10FFFF < codepoint)
            return "";
        result_val = -1;
    }
    else {
        bitfield_row = codepoint_bitfield_indexes[codepoint_row];
    }

    switch (switch_val) {
        case 0: return "";
END
    # Checks if it is a 'str' type enum
    for my $prop (@$allocated) {
        my $enum = exists $prop->{keys};
        my $esize = 0;
        my $is_int = 0;
        $is_int = 1 if (defined $prop->{type} and ($prop->{type} eq 'int'));
        print("\n" . $prop->{name} . " is an integer enum property") if $is_int;
        if ($enum) {
            $enum = $prop->{name} . "_enums";
            $esize = scalar @{$prop->{keys}};
            $enumtables .= $is_int ? "static const int " : "static const char *";
            $enumtables .= "$enum\[$esize] = {";
            my $format = $is_int ? "\n    %s," : "\n    \"%s\",";
            for (@{$prop->{keys}}) {
                $enumtables .= sprintf($format, $_);
            }
            $enumtables .= "\n};\n\n";
        }
        $hout .= "    " . uc("MVM_unicode_property_$prop->{name}") . " = $prop->{field_index},\n";
        $PROP_NAMES->{$prop->{name}} = $prop->{field_index};
        my $case = "\n        case " . uc("MVM_unicode_property_$prop->{name}") . ":";
        $int_out .= $case;
        $str_out .= $case if is_str_enum($prop);

        my $bit_width = $prop->{bit_width};
        my $bit_offset = $prop->{bit_offset} // 0;
        my $word_offset = $prop->{word_offset} // 0;

        $int_out .= " /* $prop->{name} bits:$bit_width offset:$bit_offset */";
        $str_out .= " /* $prop->{name} bits:$bit_width offset:$bit_offset */" if is_str_enum($prop);

        my $one_word_only = $bit_offset + $bit_width <= $BITFIELD_CELL_BITWIDTH ? 1 : 0;
        while ($bit_width > 0) {
            my $original_bit_offset = $bit_offset;
            my $binary_mask = 0;
            my $binary_string = "";
            my $pos = 0;
            while ($bit_offset--) {
                $binary_string .= "0";
                $pos++;
            }
            while ($pos < $BITFIELD_CELL_BITWIDTH && $bit_width--) {
                $binary_string .= "1";
                $binary_mask += 2 ** ($BITFIELD_CELL_BITWIDTH - 1 - $pos++);
            }
            my $shift = $BITFIELD_CELL_BITWIDTH - $pos;
            while ($pos++ < $BITFIELD_CELL_BITWIDTH) {
                $binary_string .= "0";
            }
            my $hex_binary_mask     = sprintf("%x", $binary_mask);
            my $props_bitfield_line =
                "((props_bitfield[bitfield_row][$word_offset] & 0x$hex_binary_mask) >> $shift); /* mask: $binary_string */";
            # If it's an int based enum we use the same code as we do for strings
            # (the function just returns an int from the enum instead of a char *)
            if ($enum && defined($prop->{type}) && ($prop->{type} eq 'int')) {
                # XXX todo, remove unneeded variables and jank
                chomp($int_out .= <<"END");

                result_val = $props_bitfield_line
                return result_val < $esize ? (result_val == -1
                    ? $enum\[0] : $enum\[result_val]) : 0;
END
                next;
            }
            else {
                my $return_or_resultval = $one_word_only ? 'return' : 'result_val |=';
                $int_out .= "\n                $return_or_resultval $props_bitfield_line";
            }
            $str_out .= "\n            result_val |= $props_bitfield_line"
                if is_str_enum($prop);

            $word_offset++;
            $bit_offset = 0;
        }

        $int_out  .= "\n            ";
        $str_out  .= "\n            " if is_str_enum($prop);

        $int_out .= "return result_val;" unless $one_word_only;
        $str_out .= "return result_val < $esize ? (result_val == -1\n" .
            "        ? $enum\[0] : $enum\[result_val]) : bogus;" if is_str_enum($prop);
    }
    my $default_return = <<'END'

        default:
            return %s;
    }
}
END
;
    $int_out  .= sprintf $default_return, 0;
    $str_out .= sprintf $default_return, q("");
    $hout .= "} MVM_unicode_property_codes;";

    EPVL_gen_pvalue_defines('MVM_UNICODE_PROPERTY_GENERAL_CATEGORY', 'General_Category', 'GC');
    EPVL_gen_pvalue_defines('MVM_UNICODE_PROPERTY_GRAPHEME_CLUSTER_BREAK', 'Grapheme_Cluster_Break', 'GCB');
    EPVL_gen_pvalue_defines('MVM_UNICODE_PROPERTY_DECOMPOSITION_TYPE', 'Decomposition_Type', 'DT');
    EPVL_gen_pvalue_defines('MVM_UNICODE_PROPERTY_CANONICAL_COMBINING_CLASS', 'Canonical_Combining_Class', 'CCC');
    EPVL_gen_pvalue_defines('MVM_UNICODE_PROPERTY_NUMERIC_TYPE', 'Numeric_Type', 'Numeric_Type');

    $DB_SECTIONS->{MVM_unicode_get_property_int} = $enumtables . $str_out . $int_out;
    $H_SECTIONS->{property_code_definitions} = $hout;
    return;
}

sub emit_block_lookup {
    my @blocks;
    each_line('Blocks', sub {
        $_ = shift;
        my ($from, $to, $block_name) = /^(\w+)..(\w+); (.+)/;
        if ($from && $to && $block_name) {
            $block_name =~ s/[_\s-]//g;
            my $alias_name = lc($block_name);
            my $block_len  = length $block_name;
            my $alias_len  = length $alias_name;
            if ($block_len && $alias_len) {
                push @blocks, "    { 0x$from, 0x$to, \"$block_name\", $block_len, \"$alias_name\", $alias_len }";
            }
        }
    });
    my $out = <<'END';
struct UnicodeBlock {
    MVMGrapheme32 start;
    MVMGrapheme32 end;

    char *name;
    size_t name_len;
    char *alias;
    size_t alias_len;
};

static struct UnicodeBlock unicode_blocks[] = {
END

    $out .= join(",\n", @blocks) . "\n" . <<'END';
};

static int block_compare(const void *a, const void *b) {
    MVMGrapheme32 ord = *((MVMGrapheme32 *) a);
    struct UnicodeBlock *block = (struct UnicodeBlock *) b;

    if (ord < block->start) {
        return -1;
    }
    else if (ord > block->end) {
        return 1;
    }
    else {
        return 0;
    }
}

MVMint32 MVM_unicode_is_in_block(MVMThreadContext *tc, MVMString *str, MVMint64 pos, MVMString *block_name) {
    MVMGrapheme32 ord = MVM_string_get_grapheme_at_nocheck(tc, str, pos);
    MVMuint64 size;
    char *bname = MVM_string_ascii_encode(tc, block_name, &size, 0);
    MVMint32 in_block = 0;

    struct UnicodeBlock *block = bsearch(&ord, unicode_blocks, sizeof(unicode_blocks) / sizeof(struct UnicodeBlock), sizeof(struct UnicodeBlock), block_compare);

    if (block) {
        in_block = strncmp(block->name, bname, block->name_len) == 0 ||
               strncmp(block->alias, bname, block->alias_len) == 0;
    }
    MVM_free(bname);

    return in_block;
}
END
    chomp $out;

    $DB_SECTIONS->{block_lookup} = $out;
    $H_SECTIONS->{block_lookup}  = "MVMint32 MVM_unicode_is_in_block(MVMThreadContext *tc, MVMString *str, MVMint64 pos, MVMString *block_name);\n";
    return;
}

sub emit_names_hash_builder {
    my ($extents) = @_;
    my $num_extents = scalar(@$extents);
    my $out = "\nstatic const MVMint32 codepoint_extents[".($num_extents + 1)."][3] = {\n";
    $ESTIMATED_TOTAL_BYTES += 4 * 2 * ($num_extents + 1);
    for my $extent (@$extents) {
        $out .= sprintf("    {0x%04x,%d,%d},\n",
                                $extent->{code},
                                     $extent->{fate_type},
                                          ($extent->{fate_really}//0));
    }
    $H_SECTIONS->{MVM_NUM_UNICODE_EXTENTS} = "#define MVM_NUM_UNICODE_EXTENTS $num_extents\n";
    $out .= <<"END";
    {0x10FFFE,0}
};

/* Lazily constructed hashtable of Unicode names to codepoints.
    Okay not to be threadsafe since its value is deterministic
        and I don't care about the tiny potential for a memory leak
        in the event of a race condition. */
static MVMUnicodeNameRegistry *codepoints_by_name = NULL;
static void generate_codepoints_by_name(MVMThreadContext *tc) {
    MVMint32 extent_index = 0;
    MVMint32 codepoint = 0;
    MVMint32 codepoint_table_index = 0;
    MVMint16 i = num_unicode_namealias_keypairs - 1;

    MVMUnicodeNameRegistry *entry;
    for (; extent_index < MVM_NUM_UNICODE_EXTENTS; extent_index++) {
        MVMint32 length;
        codepoint = codepoint_extents[extent_index][0];
        length = codepoint_extents[extent_index + 1][0] - codepoint_extents[extent_index][0];
        if (codepoint_table_index >= MVM_CODEPOINT_NAMES_COUNT)
            continue;
        switch (codepoint_extents[extent_index][1]) {
            case $FATE_NORMAL: {
                MVMint32 extent_span_index = 0;
                codepoint_table_index = codepoint_extents[extent_index][2];
                for (; extent_span_index < length
                    && codepoint_table_index < MVM_CODEPOINT_NAMES_COUNT; extent_span_index++) {
                    const char *name = codepoint_names[codepoint_table_index];
                    if (name) {
                        MVMUnicodeNameRegistry *entry = MVM_malloc(sizeof(MVMUnicodeNameRegistry));
                        entry->name = (char *)name;
                        entry->codepoint = codepoint;
                        HASH_ADD_KEYPTR(hash_handle, codepoints_by_name, name, strlen(name), entry);
                    }
                    codepoint++;
                    codepoint_table_index++;
                }
                break;
            }
            case $FATE_NULL:
                codepoint += length;
                break;
            case $FATE_SPAN: {
                const char *name = codepoint_names[codepoint_table_index];
                if (name) {
                    MVMUnicodeNameRegistry *entry = MVM_malloc(sizeof(MVMUnicodeNameRegistry));
                    entry->name = (char *)name;
                    entry->codepoint = codepoint;
                    HASH_ADD_KEYPTR(hash_handle, codepoints_by_name, name, strlen(name), entry);
                }
                codepoint += length;
                codepoint_table_index++;
                break;
            }
        }
    }
    for (; i >= 0; i--) {
        entry = MVM_malloc(sizeof(MVMUnicodeNameRegistry));
        entry->name = uni_namealias_pairs[i].name;
        entry->codepoint =  uni_namealias_pairs[i].codepoint;
        HASH_ADD_KEYPTR(hash_handle, codepoints_by_name,  uni_namealias_pairs[i].name,  uni_namealias_pairs[i].strlen, entry);

    }

}
END
    $DB_SECTIONS->{names_hash_builder} = $out;
    return;
}

sub emit_unicode_property_keypairs {
    my @lines = ();
    my @v2a;
    each_line('PropertyAliases', sub { $_ = shift;
        my @aliases = split / \s* [#;] \s* /x;
        for my $name (@aliases) {
            if (exists $PROP_NAMES->{$name}) {
                for my $al (@aliases) {
                    $PROP_NAMES->{$al} = $PROP_NAMES->{$name}
                        unless $al eq $name;
                    my $lc = lc $al;
                    $PROP_NAMES->{$lc} = $PROP_NAMES->{$name}
                        unless $lc eq $name or $lc eq $al;
                    my $no_ = $lc;
                    $no_ =~ s/_//g;
                    $PROP_NAMES->{$no_} = $PROP_NAMES->{$name}
                        unless $no_ eq $name or $no_ eq $name or $no_ eq $al;
                    $PROP_CODES->{$al} = $name;
                }
                last;
            }
        }
    });
    my %aliases;
    my %done;
    my %lines;
    each_line('PropertyValueAliases', sub { $_ = shift;
        if (/^# (\w+) \((\w+)\)/) {
            $aliases{$2} = [$1];
            return
        }
        return if /^(?:#|\s*$)/;
        my @parts = split /\s*[#;]\s*/;
        my $propname = shift @parts;
        if (exists $PROP_NAMES->{$propname}) {
            if (($parts[0] eq 'Y' || $parts[0] eq 'N') && ($parts[1] eq 'Yes' || $parts[1] eq 'No')) {
                my $prop_val = $PROP_NAMES->{$propname};
                for ($propname, @{$aliases{$propname} // []}) {
                    $lines{$propname}->{$_} = "{\"$_\",$prop_val}";
                    $lines{$propname}->{$_} = "{\"$_\",$prop_val}" if s/_//g;
                    $lines{$propname}->{$_} = "{\"$_\",$prop_val}" if y/A-Z/a-z/;
                }
                return
            }
            if ($parts[-1] =~ /\|/) { # it's a union
                pop @parts;
                my $unionname = $parts[0];
                my $prop_val;
                if (exists $BINARY_PROPERTIES->{$unionname}) {
                    $prop_val = $BINARY_PROPERTIES->{$unionname}->{field_index};
                    $lines{$propname}->{$_} = "{\"$_\",$prop_val}";
                    $lines{$propname}->{$_} = "{\"$_\",$prop_val}" if s/_//g;
                    $lines{$propname}->{$_} = "{\"$_\",$prop_val}" if y/A-Z/a-z/;
                }
                $prop_val = $PROP_NAMES->{$propname};
                for (@parts) {
                    $lines{$propname}->{$_} = "{\"$_\",$prop_val}";
                    $lines{$propname}->{$_} = "{\"$_\",$prop_val}" if s/_//g;
                    $lines{$propname}->{$_} = "{\"$_\",$prop_val}" if y/A-Z/a-z/;
                }
            }
            else {
                my $prop_val = $PROP_NAMES->{$propname};
                for (@parts) {
                    $lines{$propname}->{$_} = "{\"$_\",$prop_val}";
                    push @{ $aliases{$propname} }, $_
                }
            }
        }
    }, 1);
    for my $propname (qw(_custom_ gc sc), sort keys %lines) {
        for (sort keys %{$lines{$propname}}) {
            # For Letter etc other unions
            if (/[#]/) {
                my $value = $lines{$propname}{$_};
                my $old   = $_;
                my $orig = $_;
                $old =~ s/ [#] .* //x;
                my @a = split / \s* ; \s* /x, $old;
                my $pname = shift @a;
                for my $name (@a) {
                    $name = trim $name;
                    my $v2 = $value;
                    $v2 =~ s/".*"/"$name"/;
                    push @v2a, $v2;
                    $lines{$propname}{$name} = $v2;
                    # NEEDED
                    $done{"$name"} = push @lines, $v2;
                }
            }
            else {
                $done{"$_"} ||= push @lines, $lines{$propname}->{$_};
            }
        }
    }
    for my $key (qw(gc sc), sort keys %$PROP_NAMES) {
        $_ = $key;
        $done{"$_"} ||= push @lines, "{\"$_\",$PROP_NAMES->{$key}}";
        $done{"$_"} ||= push @lines, "{\"$_\",$PROP_NAMES->{$key}}" if s/_//g;
        $done{"$_"} ||= push @lines, "{\"$_\",$PROP_NAMES->{$key}}" if y/A-Z/a-z/;
        for (@{ $aliases{$key} }) {
            $done{"$_"} ||= push @lines, "{\"$_\",$PROP_NAMES->{$key}}";
            $done{"$_"} ||= push @lines, "{\"$_\",$PROP_NAMES->{$key}}" if s/_//g;
            $done{"$_"} ||= push @lines, "{\"$_\",$PROP_NAMES->{$key}}" if y/A-Z/a-z/;
        }
    }
    # Reverse the @lines array so that later added entries take precedence
    @lines = reverse @lines;
    chomp(my $hout = <<'END');

struct MVMUnicodeNamedValue {
    const char *name;
    MVMint32 value;
};

END
    $hout .= "#define num_unicode_property_keypairs " . scalar(@lines) . "\n";
    my $out = "\nstatic const MVMUnicodeNamedValue unicode_property_keypairs[" . scalar(@lines) . "] = {\n" .
    "    " . stack_lines(\@lines, ",", ",\n    ", 0, $WRAP_TO_COLUMNS) . "\n" .
    "};";
    $DB_SECTIONS->{BBB_unicode_property_keypairs} = $out;
    $H_SECTIONS->{MVMUnicodeNamedValue} = $hout;
    return;
}
sub add_unicode_sequence {
    my ($filename) = @_;
    each_line($filename, sub { my $line = shift;
        if ( $line =~ /^#/ or $line =~ /^\s*$/) {
            return;
        }
        my (@list, $hex_ords, $type, $name);
        @list = split /;|   \#/, $line;
        if ($filename =~ /emoji/) {
            $hex_ords = trim shift @list;
            $type = trim shift @list;
            $name = trim shift @list;
        }
        else {
            $name = trim shift @list;
            $hex_ords = trim shift @list;
            $type = 'NamedSequences';
        }
        #\x{23} => chr 0x24
        # It's possible there could be hex unicode digits. In that case convert
        # to the actual codepoints
        while ($name =~ / \\x \{ (\d+) \} /x ) {
            my $chr = chr hex($1);
            $name =~ s/ \\x \{ $1 \} /$chr/xg;
        }
        # Make sure it's uppercase since the Emoji sequences are not all in
        # uppercase.
        $name = uc $name;
        # Emoji sequences have commas in some and these cannot be included
        # since they seperate seperate named items in ISO notation that P6 uses
        $name =~ s/,//g;
        $SEQUENCES->{$name}->{'type'} = $type;
        # Only push if we haven't seen this already
        if (!$SEQUENCES->{$name}->{'ords'}) {
            for my $hex (split ' ', $hex_ords) {
                push @{$SEQUENCES->{$name}->{'ords'}}, hex $hex;
            }
        }
    } );
    return;
}
sub gen_unicode_sequence_keypairs {
    my $count = 0;
    my $string_seq;
    my $seq_c_hash_str;
    my @seq_c_hash_array;
    my $enum_table;
    $string_seq .= "/* Unicode sequences such as Emoji sequences */\n";
    for my $thing ( sort keys %$SEQUENCES ) {
        my $seq_name = "uni_seq_$count";
        $string_seq .=  "static const MVMint32 $seq_name" . "[] = {";
        $seq_c_hash_str .= '{"' . $thing . '",' . $count . '},';
        my $ord_data;
        for my $ord ( @{$SEQUENCES->{$thing}->{'ords'}} ) {
            $ord_data .= '0x' . uc sprintf("%x", $ord) . ',';
        }
        $ord_data = scalar @{$SEQUENCES->{$thing}->{'ords'}} . ',' . $ord_data;
        $string_seq .= $ord_data;
        $ord_data =~ s/,$//;
        $string_seq =~ s/,$//;
        $string_seq = $string_seq . "}; " . "/* $thing */ /*" . $SEQUENCES->{$thing}->{'type'} . " */\n";
        $enum_table = $enum_table . "$seq_name,\n";
        $count++;
        if ( length $seq_c_hash_str > 80 ) {
            push @seq_c_hash_array, $seq_c_hash_str . "\n";
            $seq_c_hash_str = '';
        }
    }
    push @seq_c_hash_array, $seq_c_hash_str . "\n";
    $seq_c_hash_str = join '    ', @seq_c_hash_array;
    $seq_c_hash_str =~ s/\s*,\s*$//;
    $seq_c_hash_str .= "\n};";
    $seq_c_hash_str = "static const MVMUnicodeNamedValue uni_seq_pairs[$count] = {\n    " . $seq_c_hash_str;

    $enum_table =~ s/\s*,\s*$/};/;
    $enum_table = "static const MVMint32 * uni_seq_enum[$count] = {\n" . $enum_table;
    $DB_SECTIONS->{uni_seq} = $seq_c_hash_str . $string_seq . $enum_table;
    $HOUT .= "#define num_unicode_seq_keypairs " . $count ."\n";
    return;
}
sub gen_name_alias_keypairs {
    my $count = 0;
    my $seq_c_hash_str;
    my @seq_c_hash_array;
    for my $thing ( sort keys %$ALIAS_TYPES ) {
        my $ord_data;
        my $ord = $ALIAS_TYPES->{$thing}->{'code'};
        $ord_data .= '0x' . uc sprintf("%x", $ord) . ',';
        $seq_c_hash_str .= '{"' . $thing . '",' . $ord_data . (length $thing) . '},';
        $ord_data =~ s/ , $ //x;
        my $type = $ALIAS_TYPES->{$thing}->{'type'};
        $count++;
        if ( length $seq_c_hash_str > 80 ) {
            push @seq_c_hash_array, $seq_c_hash_str . "\n";
            $seq_c_hash_str = '';
        }
    }
    push @seq_c_hash_array, $seq_c_hash_str . "\n";
    $seq_c_hash_str = join '    ', @seq_c_hash_array;
    $seq_c_hash_str =~ s/ \s* , \s* $ //x;
    $seq_c_hash_str .= "\n};";
    $seq_c_hash_str = "static const MVMUnicodeNamedAlias uni_namealias_pairs[$count] = {\n    " . $seq_c_hash_str;

    $seq_c_hash_str = "/* Unicode Name Aliases */\n$seq_c_hash_str";
    $DB_SECTIONS->{Auni_namealias} = $seq_c_hash_str;
    $HOUT .= "#define num_unicode_namealias_keypairs $count\n";
    $HOUT .= <<'END'
struct MVMUnicodeNamedAlias {
    char *name;
    MVMGrapheme32 codepoint;
    MVMint16 strlen;
};
typedef struct MVMUnicodeNamedAlias MVMUnicodeNamedAlias;
END
}

# XXX Change name of this function
sub thing {
    my ($default, $propname, $prop_val, $hash, $maybe_propcode) = @_;
    $_ = $default;
    my $propcode = $maybe_propcode // $PROP_NAMES->{$propname} // $PROP_NAMES->{$default} // croak;
    # Workaround to 'space' not getting added here
    $hash->{$propname}->{space} = "{\"$propcode-space\",$prop_val}"
        if $default eq 'White_Space' and $propname eq '_custom_';
    $hash->{$propname}->{$_} = "{\"$propcode-$_\",$prop_val}";
    $hash->{$propname}->{$_} = "{\"$propcode-$_\",$prop_val}" if s/_//g;
    $hash->{$propname}->{$_} = "{\"$propcode-$_\",$prop_val}" if y/A-Z/a-z/;
    return $propcode;
}
sub emit_unicode_property_value_keypairs {
    my @lines = ();
    my $property;
    my %lines;
    my %aliases;
    for my $thing (sort keys %$BINARY_PROPERTIES) {
        my $prop_val = ($PROP_NAMES->{$thing} << 24) + 1;
        my $propcode = thing($thing, '_custom_', $prop_val, \%lines);
        my $lc_thing = lc $thing;
        my %stuff = (
            c => ['Other'],
            l => ['Letter'],
            m => ['Mark', 'Combining_Mark'],
            n => ['Number'],
            p => ['Punctuation', 'punct'],
            s => ['Symbol'],
            z => ['Separator']
            );
        if (defined $stuff{$lc_thing}) {
            for my $t (@{$stuff{$lc_thing}}) {
                thing($t, '_custom_', $prop_val, \%lines, $propcode)
            }
        }
    }
    for (sort keys %$ENUMERATED_PROPERTIES) {
        my $enum = $ENUMERATED_PROPERTIES->{$_}->{enum};
        my $toadd = {};
        for (sort keys %$enum) {
            my $key = lc("$_");
            $key =~ s/[_\-\s]/./g;
            $toadd->{$key} = $enum->{$_};
        }
        for (sort keys %$toadd) {
            $enum->{$_} = $toadd->{$_};
        }
    }
    croak "lines didn't get anything in it" if !%lines;
    my %done;
    each_line('PropertyValueAliases', sub { $_ = shift;
        if (/^# (\w+) \((\w+)\)/) {
            $aliases{$2} = $1;
            return
        }
        return if /^(?:#|\s*$)/;
        my @parts = split /\s*[#;]\s*/;
        my @parts2;
        foreach my $part (@parts) {
            $part = trim($part);
            croak if $part =~ /[;]/;
            push @parts2, trim($part);
        }
        @parts = @parts2;
        my $propname = shift @parts;
        $propname = trim $propname;
        if (exists $PROP_NAMES->{$propname}) {
            my $prop_val = $PROP_NAMES->{$propname} << 24;
            # emit binary properties
            if (($parts[0] eq 'Y' || $parts[0] eq 'N') && ($parts[1] eq 'Yes' || $parts[1] eq 'No')) {
                $prop_val++; # one bit width
                for ($propname, ($aliases{$propname} // ())) {
                    thing($_, $propname, $prop_val, \%lines);
                }
                return
            }
            if ($parts[-1] =~ /\|/) { # it's a union
                pop @parts;
                my $unionname = $parts[0];
                if (exists $BINARY_PROPERTIES->{$unionname}) {
                    my $prop_val = $BINARY_PROPERTIES->{$unionname}->{field_index} << 24;
                    my $value    = $BINARY_PROPERTIES->{$unionname}->{bit_width};
                    for (@parts) {
                        #croak Dumper @parts;
                        my $i = $_;
                        thing($i, $propname, $prop_val + $value, \%lines);
                        $_ = $i;
                        $done{"$propname$_"} = push @lines, $lines{$propname}->{$_};
                        $done{"$propname$_"} = push @lines, $lines{$propname}->{$_} if s/_//g;
                        $done{"$propname$_"} = push @lines, $lines{$propname}->{$_} if y/A-Z/a-z/;
                    }
                    croak Dumper($propname) if /^letter$/
                }
                return
            }
            my $key = $PROP_CODES->{$propname};
            my $found = 0;
            my $enum = $ALL_PROPERTIES->{$key}->{'enum'};
            croak $propname unless $enum;
            my $value;
            for (@parts) {
                my $alias = $_;
                $alias    =~ s/[_\-\s]/./g;
                $alias    = lc($alias);
                if (exists $enum->{$alias}) {
                    $value = $enum->{$alias};
                    last;
                }
            }
            #croak Dumper($enum) unless defined $value;
            unless (defined $value) {
                print "\nNote: couldn't resolve property $propname property value alias (you can disregard this for now).";
                return;
            }
            for (@parts) {
                s/[\-\s]/./g;
                next if /[\.\|]/;
                thing($_, $propname, $prop_val + $value, \%lines);
            }
        }
    }, 1);

    # Aliases like L appear in several categories, but we prefere gc and sc.
    for my $propname (qw(_custom_ gc sc), sort keys %lines) {
        for (sort keys %{$lines{$propname}}) {
            $done{"$propname$_"} ||= push @lines, $lines{$propname}->{$_};
        }
    }
    $HOUT .= "\n#define num_unicode_property_value_keypairs " . scalar(@lines) . "\n";
    my $out = "\nstatic MVMUnicodeNameRegistry **unicode_property_values_hashes;\n" .
    "static const MVMUnicodeNamedValue unicode_property_value_keypairs[" . scalar(@lines) . "] = {\n" .
    "    " . stack_lines(\@lines, ",", ",\n    ", 0, $WRAP_TO_COLUMNS) . "\n" .
    "};";
    $DB_SECTIONS->{BBB_unicode_property_value_keypairs} = $out;
    $H_SECTIONS->{num_unicode_property_value_keypairs}  = $HOUT;
    return;
}

sub emit_composition_lookup {
    # Build 3-level sparse array [plane][upper][lower] keyed on bits from the
    # first codepoint of the decomposition of a primary composite, mapped to
    # an array of [second codepoint, primary composite].
    my @lookup;
    for my $point_code (sort { $a <=> $b } keys %$POINTS_BY_CODE) {
        # Not interested in anything in the set of full composition exclusions.
        my $point = $POINTS_BY_CODE->{$point_code};
        next if $point->{Full_Composition_Exclusion};

        # Only interested in things that have a decomposition spec.
        next unless defined $point->{Decomp_Spec};
        my $decomp_spec = $ENUMERATED_PROPERTIES->{Decomp_Spec}->{keys}->[$point->{Decomp_Spec}];

        # Only interested in canonical decompositions.
        my $decomp_type = $ENUMERATED_PROPERTIES->{Decomposition_Type}->{keys}->[$point->{Decomposition_Type}];
        next unless $decomp_type eq 'Canonical';

        # Make an entry.
        my @decomp = split /\s+/, $decomp_spec;
        croak "Canonical decomposition only supports two codepoints" unless @decomp == 2;
        my $plane = 0;
        if (length($decomp[0]) == 5) {
            $plane = hex(substr($decomp[0], 0, 1));
            $decomp[0] = substr($decomp[0], 1);
        }
        elsif (length($decomp[0]) != 4) {
            croak "Invalid codepoint " . $decomp[0]
        }
        my ($upper, $lower) = (hex(substr($decomp[0], 0, 2)), hex(substr($decomp[0], 2, 2)));
        push @{$lookup[$plane]->[$upper]->[$lower]}, hex($decomp[1]), $point_code;
    }

    # Produce sparse lookup tables.
    my $entry_idx   = 0;
    my $l_table_idx = 0;
    my $u_table_idx = 0;
    my $entries     = '';
    my $l_tables    = 'static const MVMint32 *comp_l_empty[] = {' . ('NULL,' x 256) . "};\n";
    my $u_tables    = 'static const MVMint32 **comp_u_empty[] = {' . ('comp_l_empty,' x 256) . "};\n";
    my $p_table     = 'static const MVMint32 ***comp_p[] = {';
    for (my $p = 0; $p < 17; $p++) {
        unless ($lookup[$p]) {
            $p_table .= 'comp_u_empty,';
            next;
        }

        my $u_table_name = 'comp_u_' . $u_table_idx++;
        $u_tables .= 'static const MVMint32 **' . $u_table_name . '[] = {';
        for (my $u = 0; $u < 256; $u++) {
            unless ($lookup[$p]->[$u]) {
                $u_tables .= 'comp_l_empty,';
                next;
            }

            my $l_table_name = 'comp_l_' . $l_table_idx++;
            $l_tables .= 'static const MVMint32 *' . $l_table_name . '[] = {';
            for (my $l = 0; $l < 256; $l++) {
                if ($lookup[$p]->[$u]->[$l]) {
                    my @values = @{$lookup[$p]->[$u]->[$l]};
                    my $entry_name = 'comp_entry_' . $entry_idx++;
                    $entries .= 'static const MVMint32 ' . $entry_name . '[] = {';
                    $entries .= join(',', scalar(@values), @values) . "};\n";
                    $l_tables .= $entry_name . ',';
                }
                else {
                    $l_tables .= 'NULL,';
                }
            }
            $l_tables .= "};\n";
            $u_tables .= $l_table_name . ',';
        }
        $u_tables .= "};\n";
        $p_table .= $u_table_name . ',';
    }
    $p_table .= "};\n";

    # Put it all together and emit.
    my $tables = "$entries\n$l_tables\n$u_tables\n$p_table";
    $DB_SECTIONS->{composition_lookup} = "\n/* Canonical composition lookup tables. */\n$tables";
    return;
}

sub compute_bitfield {
    my $point = shift;
    my $index = 1;
    my $prophash = {};
    my $last_point = undef;
    my $bytes_saved = 0;
    while ($point) {
        my $line = '';
        $line .= '.'.(defined $_ ? $_ : 0) for @{$point->{bytes}};
        my $refer;
        if (defined($refer = $prophash->{$line})) {
            $bytes_saved += 20;
            $point->{bitfield_index} = $refer->{bitfield_index};
        }
        else {
            $point->{bitfield_index} = $index++;
            $prophash->{$line} = $point;
            $last_point->{next_emit_point} = $point if $last_point;
            $last_point = $point;
        }
        $point = $point->{next_point};
    }
    $TOTAL_BYTES_SAVED += $bytes_saved;
    print "\nSaved ".thousands($bytes_saved)." bytes by uniquing the bitfield table.\n";
    return;
}

sub header {
    my @readme = @{read_file("UNIDATA/ReadMe.txt")};
    my $lines;
    for my $line (@readme) {
        last if $line !~ /^\s*[#]/;
        $lines .= $line;
    }
    my $header = <<'EOF';
/*   DO NOT MODIFY THIS FILE!  YOU WILL LOSE YOUR CHANGES!
This file is generated by ucd2c.pl from the Unicode database.

From ReadMe.txt in the Unicode Database Sources this file was generated from:

%s
From http://unicode.org/copyright.html#Exhibit1 on 2017-11-28:

COPYRIGHT AND PERMISSION NOTICE

Copyright © 1991-2017 Unicode, Inc. All rights reserved.
Distributed under the Terms of Use in http://www.unicode.org/copyright.html.

Permission is hereby granted, free of charge, to any person obtaining
a copy of the Unicode data files and any associated documentation
(the "Data Files") or Unicode software and any associated documentation
(the "Software") to deal in the Data Files or Software
without restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, and/or sell copies of
the Data Files or Software, and to permit persons to whom the Data Files
or Software are furnished to do so, provided that either
(a) this copyright and permission notice appear with all copies
of the Data Files or Software, or
(b) this copyright and permission notice appear in associated
Documentation.

THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF
ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT OF THIRD PARTY RIGHTS.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS
NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL
DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THE DATA FILES OR SOFTWARE.

Except as contained in this notice, the name of a copyright holder
shall not be used in advertising or otherwise to promote the sale,
use or other dealings in these Data Files or Software without prior
written authorization of the copyright holder. */

#include "moar.h"

EOF
    return sprintf($header, $lines);
}
sub read_file {
    my $fname = shift;
    open my $FILE, '<', $fname or croak "Couldn't open file '$fname': $!";
    binmode $FILE, ':encoding(UTF-8)';
    my @lines = ();
    while( <$FILE> ) {
        push @lines, $_;
    }
    close $FILE;
    return \@lines;
}

sub write_file {
    my ($fname, $contents) = @_;
    open my $FILE, '>', $fname or croak "Couldn't open file '$fname': $!";
    binmode $FILE, ':encoding(UTF-8)';
    print $FILE $contents;
    close $FILE;
    return;
}

sub register_union {
    my ($unionname, $unionof) = @_;
    register_binary_property($unionname);
    push @$GC_ALIAS_CHECKERS, sub {
        return ((shift) =~ /^(?:$unionof)$/)
            ? "$unionname" : 0;
    };
    return;
}

sub UnicodeData {
    my ($bidi_classes, $general_categories, $ccclasses) = @_;
    $GENERAL_CATEGORIES = $general_categories;
    register_binary_property('Any');
    each_line('PropertyValueAliases', sub { $_ = shift;
        my @parts = split / \s* [#;] \s* /x;
        my @parts2;
        foreach my $part (@parts) {
            $part = trim $part;
            push @parts2, $part;
        }
        @parts = @parts2;
        my $propname = shift @parts;
        return if ($parts[0] eq 'Y'   || $parts[0] eq 'N')
               && ($parts[1] eq 'Yes' || $parts[1] eq 'No');
        if ($parts[-1] =~ /[|]/) { # it's a union
            my $unionname = $parts[0];
            my $unionof   = pop @parts;
            $unionof      =~ s/\s+//g;
            register_union($unionname, $unionof);
        }
    });
    register_union('Assigned', 'C[cfosn]|L[lmotu]|M[cen]|N[dlo]|P[cdefios]|S[ckmo]|Z[lps]');
    my $ideograph_start;
    my $case_count = 1;
    my $decomp_keys = [ '' ];
    my $decomp_index = 1;
    my $s = sub {
        $_ = shift;
        my ($code_str, $name, $gencat, $ccclass, $bidiclass, $decmpspec,
            $num1, $num2, $num3, $bidimirrored, $u1name, $isocomment,
            $suc, $slc, $stc) = split ';';

        my $code = hex $code_str;
        my $plane_num = $code >> 16;
        if ($name eq '<control>' ) {
            $name = sprintf '<control-%.4X>', $code;
        }
        my $point = get_next_point($code, 1);
        my $hashy = {
            # Unicode_1_Name is not used yet. We should make sure it ends up
            # in some data structure eventually
            Unicode_1_Name => $u1name,
            name => $name,
            gencat_name => $gencat,
            General_Category => $general_categories->{enum}->{$gencat},
            Canonical_Combining_Class => $ccclasses->{enum}->{$ccclass},
            Bidi_Class => $bidi_classes->{enum}->{$bidiclass},
            suc => $suc,
            slc => $slc,
            stc => $stc,
        };
        for my $key (sort keys %$hashy) {
            $point->{$key} = $hashy->{$key};
        }
        $point->{Bidi_Mirrored} = 1 if $bidimirrored eq 'Y';
        if ($decmpspec) {
            $decmpspec =~ s/<\w+>\s+//;
            $point->{Decomp_Spec} = $decomp_index;
            $decomp_keys->[$decomp_index++] = $decmpspec;
        }
        if ($suc || $slc || $stc) {
            $point->{Case_Change_Index} = $case_count++;
        }
        for my $checker (@$GC_ALIAS_CHECKERS) {
            my $res = $checker->($gencat);
            $point->{$res} = 1 if $res;
        }
        if ($name =~ /(Ideograph|Syllable|Private|Surrogate)(\s|.)*?First/) {
            $ideograph_start = $point;
            $point->{name} =~ s/, First//;
        }
        elsif ($ideograph_start) {
            $point->{name} = $ideograph_start->{name};
            my $current = $ideograph_start;
            my $count = $ideograph_start->{code} + 1;
            while ($count < $point->{code}) {
                $current = get_next_point($count, 1);
                for (sort keys %$ideograph_start) {
                    next if $_ eq "code" || $_ eq "code_str";
                    $current->{$_} = $ideograph_start->{$_};
                }
                $count++;
            }
            $ideograph_start = 0;
        }
    };
    each_line('UnicodeData', $s);
    $s->("110000;Out of Range;Cn;0;L;;;;;N;;;;;");

    register_enumerated_property('Case_Change_Index', {
        name => 'Case_Change_Index', bit_width => least_int_ge_lg2($case_count)
    });
    register_enumerated_property('Decomp_Spec', {
        name => 'Decomp_Spec',
        'keys' => $decomp_keys,
        bit_width => least_int_ge_lg2($decomp_index)
    });
    return;
}
sub is_same {
    my ($point_1, $point_2) = @_;
    my %things;
    # Return early by simply checking the name. If the names match or don't
    # exist, we need to do more work to determine if the points are equal
    if (defined $point_1->{name} and defined $point_2->{name} and $point_1->{name} ne $point_2->{name}) {
        return 0;
    }
    for my $key (keys %$point_1, keys %$point_2) {
        $things{$key} = 1;
    }
    for my $key (keys %things) {
        next if $key eq 'code' || $key eq 'code_str' || $key eq 'next_point' || $key eq 'main_index'
        || $key eq 'next_emit_point' || $key eq 'bytes' || $key eq 'bitfield_index'
        || $key eq 'fate_type' || $key eq 'fate_really' || $key eq 'fate_offset';
        unless (defined $point_1->{$key} && defined $point_2->{$key} && $point_1->{$key} eq $point_2->{$key}) {
            return 0;
        }
    }
    return 1;
}
sub CaseFolding {
    my $simple_count = 1;
    my $grows_count = 1;
    my @simple;
    my @grows;
    each_line('CaseFolding', sub { $_ = shift;
        my ($left_str, $type, $right) = split /\s*;\s*/;
        my $left_code = hex $left_str;
        return if $type eq 'S' || $type eq 'T';
        if ($type eq 'C') {
            push @simple, $right;
            $POINTS_BY_CODE->{$left_code}->{Case_Folding} = $simple_count;
            $simple_count++;
            $POINTS_BY_CODE->{$left_code}->{Case_Folding_simple} = 1;
        }
        else {
            my @parts = split ' ', $right;
            push @grows, "{0x".($parts[0]).",0x".($parts[1] || 0).",0x".($parts[2] || 0)."}";
            $POINTS_BY_CODE->{$left_code}->{Case_Folding} = $grows_count;
            $grows_count++;
        }
    });
    my $simple_out = "static const MVMint32 CaseFolding_simple_table[$simple_count] = {\n    0x0,\n    0x"
        .stack_lines(\@simple, ",0x", ",\n    0x", 0, $WRAP_TO_COLUMNS)."\n};";
    my $grows_out = "static const MVMint32 CaseFolding_grows_table[$grows_count][3] = {\n    {0x0,0x0,0x0},\n    "
        .stack_lines(\@grows, ",", ",\n    ", 0, $WRAP_TO_COLUMNS)."\n};";
    my $bit_width = least_int_ge_lg2($simple_count); # XXX surely this will always be greater?
    my $index_base = { name => 'Case_Folding', bit_width => $bit_width };
    register_enumerated_property('Case_Folding', $index_base);
    register_binary_property('Case_Folding_simple');
    $ESTIMATED_TOTAL_BYTES += $simple_count * 8 + $grows_count * 32; # XXX guessing 32 here?
    $DB_SECTIONS->{BBB_CaseFolding_simple} = $simple_out;
    $DB_SECTIONS->{BBB_CaseFolding_grows}  = $grows_out;
    return;
}

sub SpecialCasing {
    my $count = 1;
    my @entries;
    each_line('SpecialCasing', sub { $_ = shift;
        s/#.+//;
        my ($code_str, $lower, $title, $upper, $cond) = split /\s*;\s*/;
        my $code = hex $code_str;
        return if $cond;
        sub threesome {
            my @things = split ' ', shift;
            push @things, 0 while @things < 3;
            return join(", ", map { "0x$_" } @things);
        }
        push @entries, "{ { " . threesome($upper) .
                       " }, { " . threesome($lower) .
                       " }, { " . threesome($title) .
                       " } }";
        $POINTS_BY_CODE->{$code}->{Special_Casing} = $count;
        $count++;
    });
    my $out = "static const MVMint32 SpecialCasing_table[$count][3][3] = {\n    {0x0,0x0,0x0},\n    "
        .stack_lines(\@entries, ",", ",\n    ", 0, $WRAP_TO_COLUMNS)."\n};";
    my $bit_width = least_int_ge_lg2($count);
    my $index_base = { name => 'Special_Casing', bit_width => $bit_width };
    register_enumerated_property('Special_Casing', $index_base);
    $ESTIMATED_TOTAL_BYTES += $count * 4 * 3 * 3;
    $DB_SECTIONS->{BBB_SpecialCasing} = $out;
    return;
}

sub DerivedNormalizationProps {
    my $binary = {
        Full_Composition_Exclusion => 1,
        Changes_When_NFKC_Casefolded => 1
    };
    my $inverted_binary = {
        NFD_QC => 1,
        NFKD_QC => 1
    };
    register_binary_property($_) for ((sort keys %$binary),(sort keys %$inverted_binary));
    my $trinary = {
        NFC_QC => 1,
        NFKC_QC => 1,
        NFG_QC => 1,
    };
    my $trinary_values = { 'N' => 0, 'Y' => 1, 'M' => 2 };
    register_enumerated_property($_, { enum => $trinary_values, bit_width => 2, 'keys' => ['N','Y','M'] }) for (sort keys %$trinary);
    each_line('DerivedNormalizationProps', sub { $_ = shift;
        my ($range, $property_name, $value) = split /\s*[;#]\s*/;
        if (exists $binary->{$property_name}) {
            $value = 1;
        }
        elsif (exists $inverted_binary->{$property_name}) {
            $value = undef;
        }
        elsif (exists $trinary->{$property_name}) {
            $value = $trinary_values->{$value};
        }

        #elsif ($property_name eq 'NFKC_Casefold') { # XXX see how this differs from CaseFolding.txt
        #    my @parts = split ' ', $value;
        #    $value = \@parts;
        # }

        else {
            return; # deprecated
        }
        apply_to_range($range, sub {
            my $point = shift;
            $point->{$property_name} = $value;
        });

        # If it's the NFC_QC property, then use this as the default value for
        # NFG_QC also.
        if ($property_name eq 'NFC_QC') {
            apply_to_range($range, sub {
                my $point = shift;
                $point->{'NFG_QC'} = $value;
            });
        }
    });
    return;
}

sub Jamo {
    my $propname = 'Jamo_Short_Name';
    each_line('Jamo', sub { $_ = shift;
        my ($code_str, $name) = split /\s*[;#]\s*/;
        apply_to_range($code_str, sub {
            my $point = shift;
            $point->{Jamo_Short_Name} = $name;
        });
    });
    my @hangul_syllables;
    for my $key (sort keys %{$POINTS_BY_CODE}) {
        if ($POINTS_BY_CODE->{$key}->{name} and $POINTS_BY_CODE->{$key}->{name} eq '<Hangul Syllable>') {
            push @hangul_syllables, $key;
        }
    }
    my $hs = join ',', @hangul_syllables;
    my $out = `perl6 -e 'my \@cps = $hs; for \@cps -> \$cp { \$cp.chr.NFD.list.join(",").say };'`;
    my @out_lines = split "\n", $out;
    my $i = 0;
    for my $line (@out_lines) {
        my $final_name = 'Hangul Syllable ';
        my $hs_cps = $hangul_syllables[$i++];
        my @a = split ',', $line;
        for my $cp (@a) {
            if (exists %{$POINTS_BY_CODE}{$cp}->{Jamo_Short_Name}) {
                $final_name .= %{$POINTS_BY_CODE}{$cp}->{Jamo_Short_Name};
            }
        }
        %{$POINTS_BY_CODE}{$hs_cps}->{name} = $final_name;
    }
    return;
}

sub collation_get_check_index {
    my ($index, $property, $base, $value) = @_;
    my $indexy = $base->{enum}->{$value};
    # haven't seen this property value before
    # add it, and give it an index.
    print("\n  adding enum property for property: $property j: " . $index->{$property}->{j} . "value: $value")
        if $DEBUG and not defined $indexy;
    ($base->{enum}->{$value} = $indexy
        = ($index->{$property}->{j}++)) unless defined $indexy;
    return $indexy;
}
sub collation {
    my ($index, $maxes, $bases) = ( {}, {}, {} );
    my ($name_primary, $name_secondary, $name_tertiary)
        = ('MVM_COLLATION_PRIMARY', 'MVM_COLLATION_SECONDARY', 'MVM_COLLATION_TERTIARY');
    for my $name ($name_primary, $name_secondary, $name_tertiary) {
        my $base = $bases->{$name} = { enum => { 0 => 0 }, name => $name, type => 'int' };
        $index->{$base->{name}}->{j} = keys(%{$base->{enum}});
        $maxes->{$base->{name}} = 0;
    }
    ## Sample line from allkeys.txt
    #1D4FF ; [.1EE3.0020.0005] # MATHEMATICAL BOLD SCRIPT SMALL V
    my $line_no = 0;
    each_line('UCA/allkeys', sub { $_ = shift;
        my $line = $_;
        $line_no++;
        my ($code, $temp);
        my $weights = {};
        # implicit weights are handled in ./tools/Generate-Collation-Data.p6
        return if $line =~ s/ ^ \@implicitweights \s+ //xms;
        return if $line =~ / ^ \s* [#@] /x or $line =~ / ^ \s* $ /x; # Blank/comment lines
        ($code, $temp) = split / [;#]+ /x, $line;
        $code = trim $code;
        my @codes = split / /, $code;
        # We support collation for multiple codepoints in ./tools/Generate-Collation-Data.p6
        if (1 < @codes) {
            # For now set MVM_COLLATION_QC = 0 for these cp
            apply_to_range($codes[0], sub {
                my $point = shift;
                $point->{'MVM_COLLATION_QC'} = 0;
            });
            return;
        }
        # We capture the `.` or `*` before each weight. Currently we do
        # not use this information, but it may be of use later (we currently
        # don't put their values into the data structure.

        # When multiple tables are specified for a character, it is because those
        # are the composite values for the decomposed character. Since we compare
        # in NFC form not NFD, let's add these together.

        while ($temp =~ / (:? \[ ([.*]) (\p{AHex}+) ([.*]) (\p{AHex}+) ([.*]) (\p{AHex}+) \] ) /xmsg) {
            $weights->{$name_primary}   += hex $3;
            $weights->{$name_secondary} += hex $5;
            $weights->{$name_tertiary}  += hex $7;
        }
        if (!defined $code || !defined $weights->{$name_primary} || !defined $weights->{$name_secondary} || !defined $weights->{$name_tertiary}) {
            my $str;
            for my $name ($name_primary, $name_secondary, $name_tertiary) {
                $str .= "\$weights->{$name} = " . $weights->{$name} . ", ";
            }
            croak "Line no $line_no: \$line = $line, $str";
        }
        apply_to_range($code, sub {
            my $point = shift;
            my $raws = {};
            for my $base ($bases->{$name_primary}, $bases->{$name_secondary}, $bases->{$name_tertiary}) {
                my $name = $base->{name};
                # Add one to the value so we can distinguish between specified values
                # of zero for collation weight and null values.
                $raws->{$name} = 1;
                if ($weights->{$name}) {
                    $raws->{$name} += $weights->{$name};
                    $maxes->{$name} = $weights->{$name} if $weights->{$name} > $maxes->{$name};
                }
                #$point->{$base->{name}} = collation_get_check_index($index, $base->{name}, $base, $raws->{$base->{name}}); # Uncomment to make it an int enum
                $point->{$base->{name}} = $raws->{$base->{name}}; # Comment to make it an int enum
            }
        });
    });
    # Add 0 to a non-character just to make sure it ends up assigned to some codepoint
    # (or it may not properly end up in the enum)
    apply_to_range("FFFF", sub {
        my $point = shift;
        $point->{$name_tertiary} = 0;
    });

    for my $base ($bases->{$name_primary}, $bases->{$name_secondary}, $bases->{$name_tertiary}) {
        $base->{bit_width} = least_int_ge_lg2($index->{$base->{name}}->{j});
        #register_enumerated_property($base->{name}, $base); # Uncomment to make an int enum
        #register_keys($base); # Uncomment to make an int enum
        register_int_property($base->{name}, $maxes->{$base->{name}}); # Comment to make an int enum
        croak("Oh no! One of the highest collation numbers I saw is less than 1. Something is wrong" .
              "Primary max: " . $maxes->{$name_primary} . " secondary max: " . $maxes->{$name_secondary} . " tertiary_max: " . $maxes->{$name_tertiary})
            if $maxes->{$base->{name}} < 1;
    }
    register_binary_property('MVM_COLLATION_QC');
    return;
}

sub NameAliases {
    each_line('NameAliases', sub { $_ = shift;
        my ($code_str, $name, $type) = split / \s* [;#] \s* /x;
        $ALIASES->{$name} = hex $code_str;
        $ALIAS_TYPES->{$name}->{'code'} = hex $code_str;
        $ALIAS_TYPES->{$name}->{'type'} = $type;
    });
    return;
}

sub NamedSequences {
    each_line('NamedSequences', sub { $_ = shift;
        my ($name, $codes) = split / \s* [;#] \s* /x;
        my @parts = split ' ', $codes;
        $NAMED_SEQUENCES->{$name} = \@parts;
    });
    return;
}

sub tweak_nfg_qc {
    # See http://www.unicode.org/reports/tr29/tr29-27.html#Grapheme_Cluster_Boundary_Rules
    for my $point (values %$POINTS_BY_CODE) {
        my $code = $point->{'code'};

        if ($code == 0x0D                           # \r
        || $point->{'Hangul_Syllable_Type'}         # Hangul
        || ($code >= 0x1F1E6 && $code <= 0x1F1FF)   # Regional indicators
        || $code == 0x200D                          # Zero Width Joiner
        || $point->{'Grapheme_Extend'}              # Grapheme_Extend
        || $point->{'Grapheme_Cluster_Break'}       # Grapheme_Cluster_Break
        || $point->{'Prepended_Concatenation_Mark'} # Prepended_Concatenation_Mark
        || $point->{'gencat_name'} && $point->{'gencat_name'} eq 'Mc' # Spacing_Mark
        || $code == 0x0E33 || $code == 0x0EB3                         # Some specials
        ) {
            $point->{'NFG_QC'} = 0;
        }
        # For now set all Emoji to NFG_QC 0
        # Eventually we will only want to set the ones that are NOT specified
        # as ZWJ sequences XXX
        elsif ($point->{'Emoji'}) {
            $point->{'NFG_QC'} = 0;
        }
    }
    return;
}

sub register_binary_property {
    my $name = shift;
    $ALL_PROPERTIES->{$name} = $BINARY_PROPERTIES->{$name} = {
        property_index => $PROPERTY_INDEX++,
        name => $name,
        bit_width => 1
    } unless exists $BINARY_PROPERTIES->{$name};
    return;
}

sub register_int_property {
    my ( $name, $elems ) = @_;
    # add to binary_properties for now
    $ALL_PROPERTIES->{$name} = $BINARY_PROPERTIES->{$name} = {
        property_index => $PROPERTY_INDEX++,
        name => $name,
        bit_width => least_int_ge_lg2($elems)
    } unless exists $BINARY_PROPERTIES->{$name};
    return;
}

sub register_enumerated_property {
    my ($pname, $base) = @_;
    if (!defined $base->{name} || !$base->{name}) {
        $base->{name} = $pname;
        #croak("\n\$base->{name} not set for property '$pname'");
    }
    elsif ($pname ne $base->{name}) {
        croak("name doesn't match. Argument was '$pname' but was already set to '" . $base->{name});
    }
    check_base_for_duplicates($base);
    croak if exists $ENUMERATED_PROPERTIES->{$pname};
    $ALL_PROPERTIES->{$pname} = $ENUMERATED_PROPERTIES->{$pname} = $base;
    $base->{property_index} = $PROPERTY_INDEX++;
    return $base
}

main();

# vim: ft=perl6 expandtab sw=4
