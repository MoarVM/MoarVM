#!/usr/bin/env perl
use v5.14;
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

my @name_lines;
if ($DEBUG) {
    open(LOG, ">extents") or croak "can't create extents: $!";
    binmode LOG, ':encoding(UTF-8)';
}
binmode STDOUT, ':encoding(UTF-8)';
binmode STDERR, ':encoding(UTF-8)';
my $LOG;

my $db_sections = {};
my $sequences = {};
my $hout = "";
my $h_sections = {};
my $planes = [];
my $points_by_hex = {};
my $points_by_code = {};
my $enumerated_properties = {};
my $binary_properties = {};
my $first_point = undef;
my $last_point = undef;
my $aliases = {};
my $alias_types = {};
my $prop_names = {};
my $named_sequences = {};
my $bitfield_table = [];
my $prop_codes = {};
my $all_properties = {};
my $allocated_properties;
my $extents;
my $property_index = 0;
my $estimated_total_bytes = 0;
my $total_bytes_saved = 0;
my $wrap_to_columns = 120;
my $compress_codepoints = 1;
my $gap_length_threshold = 1000;
my $span_length_threshold = 100;
my $skip_most_mode = 0;
my $bitfield_cell_bitwidth = 32;
my %is_subtype = (
    Digit => {
        of => 'Numeric_Type',
    }
);
my $gc_alias_checkers = [];

sub trim {
    my $s = shift;
    $s =~ s/\s+$//g;
    $s =~ s/^\s+//g;
    if ($s =~ /^ / or $s =~ / $/) {
        croak "'$s'";
    }
    return $s;
}

sub progress($);
sub main {
    $db_sections->{'AAA_header'} = header();
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
    add_unicode_sequence('NamedSequences');
    gen_unicode_sequence_keypairs();
    NameAliases();
    gen_name_alias_keypairs();
    # Load all the things
    UnicodeData(
        derived_property('BidiClass', 'Bidi_Class', {}, 0),
        derived_property('GeneralCategory', 'General_Category', { Cn => 0 }, 0),
        derived_property('CombiningClass',
            'Canonical_Combining_Class', { Not_Reordered => 0 }, 1)
    );
    Jamo($points_by_code);
    collation();
    BidiMirroring();
    goto skip_most if $skip_most_mode;
    binary_props('extracted/DerivedBinaryProperties');
    binary_props("emoji-$highest_emoji_version/emoji-data");
    enumerated_property('ArabicShaping', 'Joining_Group', {}, 0, 3);
    enumerated_property('Blocks', 'Block', { No_Block => 0 }, 1, 1);
    # disabled because of sub Jamo
    #enumerated_property('Jamo', 'Jamo_Short_Name', {  }, 1, 1);
    enumerated_property('extracted/DerivedDecompositionType', 'Decomposition_Type', { None => 0 }, 1, 1);
    enumerated_property('extracted/DerivedEastAsianWidth', 'East_Asian_Width', {}, 0, 1);
    enumerated_property('ArabicShaping', 'Joining_Type', {}, 0, 2);
    CaseFolding();
    SpecialCasing();
    enumerated_property('DerivedAge',
        'Age', { Unassigned => 0 }, 1, 1);
    binary_props('DerivedCoreProperties');
    DerivedNormalizationProps();
    enumerated_property('extracted/DerivedNumericValues',
        'Numeric_Value', { NaN => 0 }, 1, 1);
    enumerated_property('extracted/DerivedNumericValues',
        'Numeric_Value_Numerator', { NaN => 0 }, 1, sub {
            my @fraction = split('/', (shift->[3]));
            return $fraction[0];
        });
    enumerated_property('extracted/DerivedNumericValues',
        'Numeric_Value_Denominator', { NaN => 0 }, 1, sub {
            my @fraction = split('/', (shift->[3]));
            return $fraction[1] || '1';
        });
    enumerated_property('extracted/DerivedNumericType',
        'Numeric_Type', { None => 0 }, 1, 1);
    enumerated_property('HangulSyllableType',
        'Hangul_Syllable_Type', { Not_Applicable => 0 }, 1, 1);
    LineBreak();
    NamedSequences();
    binary_props('PropList');
    enumerated_property('Scripts', 'Script', { Unknown => 0 }, 1, 1);
    # XXX StandardizedVariants.txt # no clue what this is
    grapheme_cluster_break('Grapheme', 'Grapheme_Cluster_Break');
    break_property('Sentence', 'Sentence_Break');
  skip_most:
    break_property('Word', 'Word_Break');
    tweak_nfg_qc();

    # Allocate all the things
    progress("done.\nallocating bitfield...");
    my $allocated_properties = allocate_bitfield();
    # Compute all the things
    progress("done.\ncomputing all properties...");
    compute_properties($allocated_properties);
    # Make the things less
    progress("...done.\ncomputing collapsed properties table...");
    compute_bitfield($first_point);
    # Emit all the things
    progress("...done.\nemitting unicode_db.c...");
    emit_bitfield($first_point);
    $extents = emit_codepoints_and_planes($first_point);
    emit_case_changes($first_point);
    emit_codepoint_row_lookup($extents);
    emit_property_value_lookup($allocated_properties);
    emit_names_hash_builder();
    emit_unicode_property_keypairs();
    emit_unicode_property_value_keypairs();
    emit_block_lookup();
    emit_composition_lookup();

    print "done!";
    write_file('src/strings/unicode_db.c', join_sections($db_sections));
    write_file('src/strings/unicode_gen.h', join_sections($h_sections));
    print "\nEstimated bytes demand paged from disk: ".
        thousands($estimated_total_bytes).
        ".\nEstimated bytes saved by various compressions: ".
        thousands($total_bytes_saved).".\n";
    if ($DEBUG) {
        $LOG =~ s/('fate_really' => )(\d+)/$1$name_lines[$2]/g;
        print LOG $LOG;
        close LOG;
    }
}

sub thousands {
    my $in = shift;
    $in = reverse "$in"; # stringify or copy the string
    $in =~ s/(\d\d\d)(?=\d)/$1,/g;
    reverse $in
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
    $out
}

sub join_sections {
    my $sections = shift;
    my $content = "";
    $content .= "\n".$sections->{$_} for (sort keys %{$sections});
    $content
}

sub apply_to_range {
    # apply a function to a range of codepoints. The starting and
    # ending codepoint of the range need not exist; the function will
    # be applied to all/any in between.
    my $range = shift;
    chomp($range);
    if ( !defined $range ) {
        cluck "Did not get any range in apply_to_range";
    }
    my $fn = shift;
    my ($first, $last) = split '\\.\\.', $range;
    $first ||= $range;
    $last ||= $first;
    my $point = $points_by_hex->{$first};
    if (!$point) { # go backwards to find the last one
                    # (much faster than going forwards for some reason)
        my $code = hex($first) - 1;
        $code-- until ($point = $points_by_code->{$code});
        $point = $point->{next_point};
    }
    my $last_point;
    do {
        $fn->($point);
        $last_point = $point;
        $point = $point->{next_point};
    } while ($point && $point->{code} <= hex $last);
    #croak "couldn't find code ".sprintf('%x', $last_point->{code} + 1).
    #    " got ".$point->{code_str}." for range $first..$last"
    #    unless $last_point->{code} == hex $last;
    # can't croak there because some ranges end on points that don't exist (Blocks)
}

sub progress($) {
    my $txt = shift;
    local $| = 1;
    print $txt;
}

sub binary_props {
    # process a file, extracting binary properties and applying them to ranges
    my $fname = shift; # filename
    each_line($fname, sub { $_ = shift;
        my ($range, $pname) = split /\s*[;#]\s*/; # range, property name
        register_binary_property($pname); # define the property
        apply_to_range($range, sub {
            my $point = shift;
            $point->{$pname} = 1; # set the property
        });
    });
}

sub break_property {
    my ($fname, $pname) = @_;
    enumerated_property("auxiliary/${fname}BreakProperty",
        $pname, { Other => 0 }, 1, 1);
}
sub grapheme_cluster_break {
    my ($fname, $pname) = @_;
    enumerated_property("auxiliary/${fname}BreakProperty",
        $pname, {
            # Should not be set to Other for this one ?
            Other => 0,

        }, 1, 1);
}

sub derived_property {
    # filename, property name, property object
    my ($fname, $pname, $base) = @_;
    my $j = 0;
    # wrap the provided object as the enum key in a new one
    $base = { enum => $base };
    # If we provided some property values already, add that number to the counter
    $j += (scalar keys %{$base->{enum}});
    each_line("extracted/Derived$fname", sub { $_ = shift;
        my ($range, $class) = split /\s*[;#]\s*/;
        unless (exists $base->{enum}->{$class}) {
            # haven't seen this property's value before
            # add it, and give it an index.
            print "\n  adding derived property for $pname: $j $class" if $DEBUG;
            $base->{enum}->{$class} = $j++;
        }
    });
    my @keys = ();
    # stash the keys in an array so they can be put in a table later
    for my $key (keys %{$base->{enum}}) {
        $keys[$base->{enum}->{$key}] = $key;
    }
    $base->{keys} = \@keys;
    $base->{bit_width} = least_int_ge_lg2($j);
    my %seen;
    # Make sure we don't assign twice to the same pvalue code
    for my $key (keys %{$base->{enum}}) {
        if ($seen{ $base->{enum}->{$key} }) {
            say "\nError: assigned twice to the same property value code. Both $key and "
                . $seen{ $base->{enum}->{$key} }
                . " are assigned to pvalue code "
                . $base->{enum}->{$key};
        }
        $seen{ ($base->{enum}->{$key}) } = $key;
    }
    register_enumerated_property($pname, $base);
}

sub enumerated_property {
    my ($fname, $pname, $base, $j, $value_index) = @_;
    $base = { enum => $base };
    each_line($fname, sub { $_ = shift;
        my @vals = split /\s*[#;]\s*/;
        my $range = $vals[0];
        my $value = ref $value_index
            ? $value_index->(\@vals)
            : $vals[$value_index];
        my $index = $base->{enum}->{$value};
        # haven't seen this property value before
        # add it, and give it an index.
        print("\n  adding enum property for $pname: $j $value")
            if $DEBUG and not defined $index;
        ($base->{enum}->{$value} = $index
            = $j++) unless defined $index;
        apply_to_range($range, sub {
            my $point = shift;
            $point->{$pname} = $index; # set the property's value index
        });
    });
    $base->{bit_width} = least_int_ge_lg2($j);
    print "\n    bitwidth: ",$base->{bit_width},"\n" if $DEBUG;
    my @keys = ();
    # stash the keys in an array so they can be put in a table later
    for my $key (keys %{$base->{enum}}) {
        if ($is_subtype{$key}) {
            register_enumerated_property($key, {%$base});
            delete $base->{enum}->{$key};
        }
        else {
            $keys[$base->{enum}->{$key}] = $key;
        }
    }
    print "\n    keys = @keys" if $DEBUG;
    $base->{keys} = \@keys;
    register_enumerated_property($pname, $base);
}

sub least_int_ge_lg2 {
    int(log(shift)/log(2) - 0.00001) + 1;
}

sub each_line {
    my ($fname, $fn, $force) = @_;
    progress("done.\nprocessing $fname.txt...");
    map {
        chomp;
        $fn->($_) unless !$force && /^(?:#|\s*$)/;
    } @{read_file("UNIDATA/$fname.txt")};
}

sub allocate_bitfield {
    my @biggest = map { $enumerated_properties->{$_} }
        sort { $enumerated_properties->{$b}->{bit_width}
            <=> $enumerated_properties->{$a}->{bit_width} }
            sort keys %$enumerated_properties;
    for (sort keys %$binary_properties) {
        push @biggest, $binary_properties->{$_};
    }
    my $word_offset = 0;
    my $bit_offset = 0;
    my $allocated = [];
    my $index = 1;
    while (scalar @biggest) {
        my $i = -1;
        for(;;) {
            my $prop = $biggest[++$i];
            if (!$prop) {
                while (scalar @biggest) {
                    # ones bigger than 1 byte :(.  Don't prefer these.
                    $prop = shift @biggest;
                    $prop->{word_offset} = $word_offset;
                    $prop->{bit_offset} = $bit_offset;
                    $bit_offset += $prop->{bit_width};
                    while ($bit_offset >= $bitfield_cell_bitwidth) {
                        $word_offset++;
                        $bit_offset -= $bitfield_cell_bitwidth;
                    }
                    push @$allocated, $prop;
                    $prop->{field_index} = $index++;
                }
                last;
            }
            if ($is_subtype{$prop->{name}}) {
                $prop->{word_offset} = $enumerated_properties->{ $is_subtype{$prop->{name}}{of} }{word_offset};
                $prop->{bit_offset}  = $enumerated_properties->{ $is_subtype{$prop->{name}}{of} }{bit_offset};
                push @$allocated, $prop;
                splice(@biggest, $i, 1);
                $prop->{field_index} = $index++;
                last;
            }
            elsif ($bit_offset + $prop->{bit_width} <= $bitfield_cell_bitwidth) {
                $prop->{word_offset} = $word_offset;
                $prop->{bit_offset} = $bit_offset;
                $bit_offset += $prop->{bit_width};
                if ($bit_offset == $bitfield_cell_bitwidth) {
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
    $first_point->{bitfield_width} = $word_offset+1;
    $h_sections->{num_property_codes} = "#define MVM_NUM_PROPERTY_CODES $index\n";
    $allocated
}

sub compute_properties {
    local $| = 1;
    my $fields = shift;
    for my $field (@$fields) {
        my $bit_offset = $field->{bit_offset};
        my $bit_width = $field->{bit_width};
        my $point = $first_point;
        print "\n        $field->{name} bit width:$bit_width";
        my $i = 0;
        my $bit = 0;
        my $mask = 0;
        while ($bit < $bitfield_cell_bitwidth) {
            $mask |= 2 ** $bit++;
        }
        while (defined $point) {
            if (defined $point->{$field->{name}}) {
                my $word_offset = $field->{word_offset};
                # $x is one less than the number of words required to hold the field
                my $x = int(($bit_width - 1) / $bitfield_cell_bitwidth);
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
                                    ($bitfield_cell_bitwidth - $bit_offset - $bit_width)
                                )
                                #>> ($bitfield_cell_bitwidth * $x)
                            ) & $mask
                        );
                    $x--;
                }
            }
            $point = $point->{next_point};
        }
    }
}

sub emit_binary_search_algorithm {
    # $extents is arrayref to the heads of the gaps, spans, and
    # normal stretches of codepoints. $first and $last are the
    # indexes into $extents we're supposed to subdivide.
    # protocol: start output with a newline; don't end with a newline or indent
    my ($extents, $first, $mid, $last, $indent) = @_;
    my $out = "";
#${indent} /* got  $first  $mid  $last  */\n";
    return $out.emit_extent_fate($extents->[$first], $indent) if $first == $last;
    $mid = $last if $first == $mid;
    my $new_mid_high = int(($last + $mid) / 2);
    my $new_mid_low = int(($mid - 1 + $first) / 2);
    my $high = emit_binary_search_algorithm($extents,
        $mid, $new_mid_high, $last, "    $indent");
    my $low = emit_binary_search_algorithm($extents,
        $first, $new_mid_low, $mid - 1, "    $indent");
    return $out."
${indent}if (codepoint >= 0x".uc(sprintf("%x", $extents->[$mid]->{code})).") {".
        " /* ".($extents->[$mid]->{name} || 'NULL')." */$high
${indent}}
${indent}else {$low
${indent}}";
}

my $FATE_NORMAL = 0;
my $FATE_NULL = 1;
my $FATE_SPAN = 2;

sub emit_extent_fate {
    my ($fate, $indent) = @_;
    my $type = $fate->{fate_type};
    return "\n${indent}return -1;" if $type == $FATE_NULL;
    return "\n${indent}return " . ($fate->{code} - $fate->{fate_offset}) . "; /* ".
        "$bitfield_table->[$fate->{bitfield_index}]->{code_str}".
        " $bitfield_table->[$fate->{bitfield_index}]->{name} */" if $type == $FATE_SPAN;
    return "\n${indent}return codepoint - $fate->{fate_offset};"
    .($fate->{fate_offset} == 0 ? " /* the fast path */ " : "");
}

sub add_extent($$) {
    my ($extents, $extent) = @_;
    if ($DEBUG) {
        $LOG .= "\n" . join '',
            grep /code|fate|name|bitfield/,
            sort split /^/m, "EXTENT " . Dumper($extent);
    }
    push @$extents, $extent;
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
    $first_point->{fate_type} = $FATE_NORMAL;
    $first_point->{fate_offset} = $code_offset;
    add_extent $extents, $first_point;
    my $span_length = 0;

    # a bunch of spaghetti code.  Yes.
    for my $plane (@$planes) {
        my $toadd = undef;
        for my $point (@{$plane->{points}}) {
            # extremely simplistic compression of identical neighbors and gaps
            # this point is identical to the previous point
            if ($compress_codepoints && $last_point
                    && $last_code == $point->{code} - 1
                    && $point->{name} eq $last_point->{name}
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
                if ($span_length >= $span_length_threshold) {
                    $bytes_saved += 10 * ($span_length - 1);
                    if (!exists($last_point->{fate_type})) {
                        add_extent $extents, $last_point;
                    }
                    $last_point->{fate_type} = $FATE_SPAN;
                    $code_offset = $last_point->{code} - @name_lines + 1;
                    $last_point->{fate_offset} = $code_offset;
                    $last_point->{fate_really} = $last_point->{code} - $code_offset;
                    $code_offset += $span_length - 1;
                    $toadd = $point;
                    $span_length = 0;
                }
                my $usually = 1;  # occasionally change NULL to the name to cut name search time
                while ($span_length > 1) {
                    # catch up to last code
                    $last_point = $last_point->{next_point};
                    push @bitfield_index_lines,
                        "/*$index*/$last_point->{bitfield_index}/*".
                        "$last_point->{code_str} */";
                    push @name_lines, "/*$index*/".
                    ($last_point->{name} =~ /^</ && $usually++ % 25 ? "NULL" : "\"$last_point->{name}\"").
                        "/* $last_point->{code_str} */";
                    $code_offset = $last_point->{code} - @name_lines;
                    $last_point->{fate_offset} = $code_offset;
                    $last_point->{fate_really} = $last_point->{code} - $code_offset;
                    $index++;
                    $bytes += 10 + ($last_point->{name} =~ /^</ ? 0 : length($last_point->{name}) + 1);
                    $span_length--;
                }
                $span_length = 0;
            }

            if ($compress_codepoints
                    && $last_code < $point->{code} - ($point->{code} % 0x10000 ? $gap_length_threshold : 1)) {
                $bytes_saved += 10 * ($point->{code} - $last_code - 1);
                add_extent $extents, { fate_type => $FATE_NULL,
                    code => $last_code + 1 };
                $code_offset += ($point->{code} - $last_code - 1);
                $last_code = $point->{code} - 1;
                $toadd = $point;
            }

            while ($last_code < $point->{code} - 1) {
                push @bitfield_index_lines, "0";
                push @name_lines, "NULL";
                $last_code++;
                $index++;
                $bytes += 10;
            }

            croak "$last_code ".Dumper($point) unless $last_code == $point->{code} - 1;
            if ($toadd && !exists($point->{fate_type})) {
                $point->{fate_type} = $FATE_NORMAL;
                $point->{fate_offset} = $code_offset;
                $point->{fate_really} = $point->{code} - $code_offset;
                add_extent $extents, $point;
            }
            $toadd = undef;
            # a normal codepoint that we don't want to compress
            push @bitfield_index_lines, "/*$index*/$point->{bitfield_index}/* $point->{code_str} */";
            $bytes += 2; # hopefully these are compacted since they are trivially aligned being two bytes
            push @name_lines, "/*$index*/\"$point->{name}\"/* $point->{code_str} */";
            $bytes += length($point->{name}) + 9; # 8 for the pointer, 1 for the NUL
            $last_code = $point->{code};
            $point->{main_index} = $index++;
            $last_point = $point;
        }
    }
    print "\nSaved ".thousands($bytes_saved)." bytes by compressing big gaps into a binary search lookup.\n";
    $total_bytes_saved += $bytes_saved;
    $estimated_total_bytes += $bytes;
    # jnthn: Would it still use the same amount of memory to combine these tables? XXX
    $db_sections->{BBB_codepoint_names} =
        "static const char *codepoint_names[$index] = {\n    ".
            stack_lines(\@name_lines, ",", ",\n    ", 0, $wrap_to_columns).
            "\n};";
    $db_sections->{BBB_codepoint_bitfield_indexes} =
        "static const MVMuint16 codepoint_bitfield_indexes[$index] = {\n    ".
            stack_lines(\@bitfield_index_lines, ",", ",\n    ", 0, $wrap_to_columns).
            "\n};";
    $h_sections->{codepoint_names_count} = "#define MVM_CODEPOINT_NAMES_COUNT $index";
    $extents
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
    my $out = "static MVMint32 MVM_codepoint_to_row_index(MVMThreadContext *tc, MVMint64 codepoint) {\n
    MVMint32 plane = codepoint >> 16;

    if (codepoint < 0) {
        MVM_exception_throw_adhoc(tc, \"Error, MoarVM cannot get Unicode codepoint property for synthetic codepoint \%\"PRId64\"\", codepoint);
    }

    if (plane == 0) {"
    . emit_binary_search_algorithm($extents, 0, 1, $SMP_start - 1, "        ") . "
    }
    else {
        if (plane < 0 || plane > 16 || codepoint > 0x10FFFD) {
            return -1;
        }
        else {" . emit_binary_search_algorithm($extents, $SMP_start,
            int(($SMP_start + scalar(@$extents)-1)/2), scalar(@$extents) - 1, "            ") . "
        }
    }
}";
    $db_sections->{codepoint_row_lookup} = $out;
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
        stack_lines(\@lines, ",", ",\n    ", 0, $wrap_to_columns)."\n};";
    $db_sections->{BBB_case_changes} = $out;
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
        $first = 0;
        $line .= 0;
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
        push @$bitfield_table, $point;
        push @lines, ($line . "}/* $point->{code_str} */");
        $point = $point->{next_emit_point};
        $rows++;
    }
    my $bytes_wide = 2;
    $bytes_wide *= 2 while $bytes_wide < $wide; # assume the worst
    $estimated_total_bytes += $rows * $bytes_wide; # we hope it's all laid out with no gaps...
    my $val_type = $bitfield_cell_bitwidth == 8
        ? 'MVMuint8'
        : $bitfield_cell_bitwidth == 16
        ? 'MVMuint16'
        : $bitfield_cell_bitwidth == 32
        ? 'MVMuint32'
        : $bitfield_cell_bitwidth == 64
        ? 'MVMuint64'
        : croak 'wut.';
    $out = "static const $val_type props_bitfield[$rows][$wide] = {\n    ".
        stack_lines(\@lines, ",", ",\n    ", 0, $wrap_to_columns)."\n};";
    $db_sections->{BBB_main_bitfield} = $out;
}

sub emit_property_value_lookup {
    my $allocated = shift;
    my $enumtables = "\n\n";
    our $hout = "typedef enum {\n";
    my $out = "
static MVMint32 MVM_unicode_get_property_int(MVMThreadContext *tc, MVMint64 codepoint, MVMint64 property_code) {
    MVMuint32 switch_val = (MVMuint32)property_code;
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
        case 0: return 0;";

    my $eout = "
static MVMint32 MVM_codepoint_to_row_index(MVMThreadContext *tc, MVMint64 codepoint);

static const char *bogus = \"<BOGUS>\"; /* only for table too short; return null string for no mapping */

static const char* MVM_unicode_get_property_str(MVMThreadContext *tc, MVMint64 codepoint, MVMint64 property_code) {
    MVMuint32 switch_val = (MVMuint32)property_code;
    MVMint32 result_val = 0; /* we'll never have negatives, but so */
    MVMuint32 codepoint_row = MVM_codepoint_to_row_index(tc, codepoint);
    MVMuint16 bitfield_row = 0;

    if (codepoint_row == -1) { /* non-existent codepoint; XXX should throw? */
        if (0x10FFFF < codepoint)
            return \"\";
        result_val = -1;
    }
    else {
        bitfield_row = codepoint_bitfield_indexes[codepoint_row];
    }

    switch (switch_val) {
        case 0: return \"\";";

    for my $prop (@$allocated) {
        my $enum = exists $prop->{keys};
        my $esize = 0;
        if ($enum) {
            $enum = $prop->{name} . "_enums";
            $esize = scalar @{$prop->{keys}};
            $enumtables .= "static char *$enum\[$esize] = {";
            $enumtables .= "\n    \"$_\"," for @{$prop->{keys}};
            $enumtables .= "\n};\n\n";
        }
        $hout .= "    ".uc("MVM_unicode_property_$prop->{name}")." = $prop->{field_index},\n";
        $prop_names->{$prop->{name}} = $prop->{field_index};

        $out .= "
        case ".uc("MVM_unicode_property_$prop->{name}").":";
        $eout .= "
        case ".uc("MVM_unicode_property_$prop->{name}").":" if $enum;

        my $bit_width = $prop->{bit_width};
        my $bit_offset = $prop->{bit_offset} // 0;
        my $word_offset = $prop->{word_offset} // 0;

        $out .= " /* $prop->{name} bits:$bit_width offset:$bit_offset */";
        $eout .= " /* $prop->{name} bits:$bit_width offset:$bit_offset */" if $enum;

        my $one_word_only = $bit_offset + $bit_width <= $bitfield_cell_bitwidth ? 1 : 0;
        while ($bit_width > 0) {
            my $original_bit_offset = $bit_offset;
            my $binary_mask = 0;
            my $binary_string = "";
            my $pos = 0;
            while ($bit_offset--) {
                $binary_string .= "0";
                $pos++;
            }
            while ($pos < $bitfield_cell_bitwidth && $bit_width--) {
                $binary_string .= "1";
                $binary_mask += 2 ** ($bitfield_cell_bitwidth - 1 - $pos++);
            }
            my $shift = $bitfield_cell_bitwidth - $pos;
            while ($pos++ < $bitfield_cell_bitwidth) {
                $binary_string .= "0";
            }

            $out .= "
            " . ($one_word_only ? 'return' : 'result_val |=') . " ((props_bitfield[bitfield_row][$word_offset] & 0x"
            . sprintf("%x",$binary_mask).") >> $shift); /* mask: $binary_string */";
            $eout .= "
            result_val |= ((props_bitfield[bitfield_row][$word_offset] & 0x".
                sprintf("%x",$binary_mask).") >> $shift); /* mask: $binary_string */" if $enum;

            $word_offset++;
            $bit_offset = 0;
        }

        $out .= "
            ";
        $eout .= "
            " if $enum;

        $out .= "return result_val;" unless $one_word_only;
        $eout .= "return result_val < $esize ? (result_val == -1
        ? $enum\[0] : $enum\[result_val]) : bogus;" if $enum;
    }

    $out .= "
        default:
            return 0;
    }
}
";
    $eout .= "
        default:
            return \"\";
    }
}
";  # or should we try to stringify numeric value?

    $hout .= "} MVM_unicode_property_codes;";

    sub gen_pvalue_defines {
        my ( $property_name_mvm, $property_name, $short_pval_name ) = @_;
        my $GCB_h;
        $GCB_h .= "\n\n/* $property_name_mvm */\n";
        my %seen;
        foreach my $key (sort keys % {$enumerated_properties->{$property_name}->{'enum'} }  ) {
            next if $seen{$key};
            my $value = $enumerated_properties->{$property_name}->{'enum'}->{$key};
            $key = 'MVM_UNICODE_PVALUE_' . $short_pval_name . '_' . uc $key;
            $key =~ tr/\./_/;
            $GCB_h .= "#define $key $value\n";
            $seen{$key} = 1;
        }
        $hout .= $GCB_h;
    }
    gen_pvalue_defines('MVM_UNICODE_PROPERTY_GENERAL_CATEGORY', 'General_Category', 'GC');
    gen_pvalue_defines('MVM_UNICODE_PROPERTY_GRAPHEME_CLUSTER_BREAK', 'Grapheme_Cluster_Break', 'GCB');
    gen_pvalue_defines('MVM_UNICODE_PROPERTY_DECOMPOSITION_TYPE', 'Decomposition_Type', 'DT');
    gen_pvalue_defines('MVM_UNICODE_PROPERTY_CANONICAL_COMBINING_CLASS', 'Canonical_Combining_Class', 'CCC');
    gen_pvalue_defines('MVM_UNICODE_PROPERTY_NUMERIC_TYPE', 'Numeric_Type', 'Numeric_Type');

    $db_sections->{MVM_unicode_get_property_int} = $enumtables . $eout . $out;
    $h_sections->{property_code_definitions} = $hout;
}

sub emit_block_lookup {
    my $hout = "MVMint32 MVM_unicode_is_in_block(MVMThreadContext *tc, MVMString *str, MVMint64 pos, MVMString *block_name);\n";
    my $out  = "struct UnicodeBlock {
    MVMGrapheme32 start;
    MVMGrapheme32 end;

    char *name;
    size_t name_len;
    char *alias;
    size_t alias_len;
};

static struct UnicodeBlock unicode_blocks[] = {
";

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

    $out .= join(",\n", @blocks) . "\n";

    $out .= "};

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
}";
    $db_sections->{block_lookup} = $out;
    $h_sections->{block_lookup} = $hout;
}

sub emit_names_hash_builder {
    my $num_extents = scalar(@$extents);
    my $out = "
static const MVMint32 codepoint_extents[".($num_extents + 1)."][3] = {\n";
    $estimated_total_bytes += 4 * 2 * ($num_extents + 1);
    for my $extent (@$extents) {
        $out .= sprintf("    {0x%04x,%d,%d},\n",
                                $extent->{code},
                                     $extent->{fate_type},
                                          ($extent->{fate_really}//0));
    }
    $h_sections->{MVM_NUM_UNICODE_EXTENTS} = "#define MVM_NUM_UNICODE_EXTENTS $num_extents\n";
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
    $db_sections->{names_hash_builder} = $out;
}#"
my @v2a;
sub emit_unicode_property_keypairs {
    my $hout = "
struct MVMUnicodeNamedValue {
    const char *name;
    MVMint32 value;
};";
    my @lines = ();
    each_line('PropertyAliases', sub { $_ = shift;
        my @aliases = split /\s*[#;]\s*/;
        for my $name (@aliases) {
            if (exists $prop_names->{$name}) {
                for my $al (@aliases) {
                    $prop_names->{$al} = $prop_names->{$name}
                        unless $al eq $name;
                    my $lc = lc $al;
                    $prop_names->{$lc} = $prop_names->{$name}
                        unless $lc eq $name or $lc eq $al;
                    my $no_ = $lc;
                    $no_ =~ s/_//g;
                    $prop_names->{$no_} = $prop_names->{$name}
                        unless $no_ eq $name or $no_ eq $name or $no_ eq $al;
                    $prop_codes->{$al} = $name;
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
        if (exists $prop_names->{$propname}) {
            if (($parts[0] eq 'Y' || $parts[0] eq 'N') && ($parts[1] eq 'Yes' || $parts[1] eq 'No')) {
                my $prop_val = $prop_names->{$propname};
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
                if (exists $binary_properties->{$unionname}) {
                    $prop_val = $binary_properties->{$unionname}->{field_index};
                    $lines{$propname}->{$_} = "{\"$_\",$prop_val}";
                    $lines{$propname}->{$_} = "{\"$_\",$prop_val}" if s/_//g;
                    $lines{$propname}->{$_} = "{\"$_\",$prop_val}" if y/A-Z/a-z/;
                }
                $prop_val = $prop_names->{$propname};
                for (@parts) {
                    $lines{$propname}->{$_} = "{\"$_\",$prop_val}";
                    $lines{$propname}->{$_} = "{\"$_\",$prop_val}" if s/_//g;
                    $lines{$propname}->{$_} = "{\"$_\",$prop_val}" if y/A-Z/a-z/;
                }
            }
            else {
                my $prop_val = $prop_names->{$propname};
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
                $old =~ s/[#].*//;
                my @a = split /\s*;\s*/, $old;
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
    for my $key (qw(gc sc), sort keys %$prop_names) {
        $_ = $key;
        $done{"$_"} ||= push @lines, "{\"$_\",$prop_names->{$key}}";
        $done{"$_"} ||= push @lines, "{\"$_\",$prop_names->{$key}}" if s/_//g;
        $done{"$_"} ||= push @lines, "{\"$_\",$prop_names->{$key}}" if y/A-Z/a-z/;
        for (@{ $aliases{$key} }) {
            $done{"$_"} ||= push @lines, "{\"$_\",$prop_names->{$key}}";
            $done{"$_"} ||= push @lines, "{\"$_\",$prop_names->{$key}}" if s/_//g;
            $done{"$_"} ||= push @lines, "{\"$_\",$prop_names->{$key}}" if y/A-Z/a-z/;
        }
    }
    # Reverse the @lines array so that later added entries take precedence
    @lines = reverse @lines;
    $hout .= "
#define num_unicode_property_keypairs ".scalar(@lines)."\n";
    my $out = "
static const MVMUnicodeNamedValue unicode_property_keypairs[".scalar(@lines)."] = {
    ".stack_lines(\@lines, ",", ",\n    ", 0, $wrap_to_columns)."
};";
    $db_sections->{BBB_unicode_property_keypairs} = $out;
    $h_sections->{MVMUnicodeNamedValue} = $hout;
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
        $sequences->{$name}->{'type'} = $type;
        # Only push if we haven't seen this already
        if (!$sequences->{$name}->{'ords'}) {
            for my $hex (split ' ', $hex_ords) {
                push @{$sequences->{$name}->{'ords'}}, hex $hex;
            }
        }
    } );
}
sub gen_unicode_sequence_keypairs {
    my $count = 0;
    my $string_seq;
    my $seq_c_hash_str;
    my @seq_c_hash_array;
    my $enum_table;
    $string_seq .= "/* Unicode sequences such as Emoji sequences */\n";
    for my $thing ( sort keys %$sequences ) {
        my $seq_name = "uni_seq_$count";
        $string_seq .=  "static const MVMint32 $seq_name" . "[] = {";
        $seq_c_hash_str .= '{"' . $thing . '",' . $count . '},';
        my $ord_data;
        for my $ord ( @{$sequences->{$thing}->{'ords'}} ) {
            $ord_data .= '0x' . uc sprintf("%x", $ord) . ',';
        }
        $ord_data = scalar @{$sequences->{$thing}->{'ords'}} . ',' . $ord_data;
        $string_seq .= $ord_data;
        $ord_data =~ s/,$//;
        $string_seq =~ s/,$//;
        $string_seq = $string_seq . "}; " . "/* $thing */ /*" . $sequences->{$thing}->{'type'} . " */\n";
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
    $db_sections->{uni_seq} = $seq_c_hash_str . $string_seq . $enum_table;
    $hout .= "#define num_unicode_seq_keypairs " . $count ."\n";
}
sub gen_name_alias_keypairs {
    my $count = 0;
    my $seq_c_hash_str;
    my @seq_c_hash_array;
    for my $thing ( sort keys %$alias_types ) {
        my $ord_data;
        my $ord = $alias_types->{$thing}->{'code'};
        $ord_data .= '0x' . uc sprintf("%x", $ord) . ',';
        $seq_c_hash_str .= '{"' . $thing . '",' . $ord_data . (length $thing) . '},';
        $ord_data =~ s/,$//;
        my $type = $alias_types->{$thing}->{'type'};
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
    $seq_c_hash_str = "static const MVMUnicodeNamedAlias uni_namealias_pairs[$count] = {\n    " . $seq_c_hash_str;

    $seq_c_hash_str = "/* Unicode Name Aliases */\n" . $seq_c_hash_str;
    $db_sections->{Auni_namealias} = $seq_c_hash_str;
    $hout .= "#define num_unicode_namealias_keypairs " . $count ."\n";
    $hout .= <<'END'
struct MVMUnicodeNamedAlias {
    char *name;
    MVMGrapheme32 codepoint;
    MVMint16 strlen;
};
typedef struct MVMUnicodeNamedAlias MVMUnicodeNamedAlias;
END
}
my %special_data;
sub thing {
    my ($default, $propname, $prop_val, $hash, $maybe_propcode) = @_;
    $_ = $default;
    my $propcode = $maybe_propcode // $prop_names->{$propname} // $prop_names->{$default} // croak;
    # Workaround to 'space' not getting added here
    $hash->{$propname}->{space} = "{\"$propcode-space\",$prop_val}"
        if $default eq 'White_Space' and $propname eq '_custom_';
    $hash->{$propname}->{$_} = "{\"$propcode-$_\",$prop_val}";
    $hash->{$propname}->{$_} = "{\"$propcode-$_\",$prop_val}" if s/_//g;
    $hash->{$propname}->{$_} = "{\"$propcode-$_\",$prop_val}" if y/A-Z/a-z/;
    $propcode;
}
sub emit_unicode_property_value_keypairs {
    my @lines = ();
    my $property;
    my %lines;
    my %aliases;
    for (sort keys %$binary_properties) {
        my $prop_val = ($prop_names->{$_} << 24) + 1;
        my $propcode = thing($_, '_custom_', $prop_val, \%lines);
        if (lc($_) eq 'c') {
            thing('Other', '_custom_', $prop_val, \%lines, $propcode);
        }
        if (lc($_) eq 'l') {
            thing('Letter', '_custom_', $prop_val, \%lines, $propcode);
        }
        if (lc($_) eq 'm') {
            thing('Mark', '_custom_', $prop_val, \%lines, $propcode);
            thing('Combining_Mark', '_custom_', $prop_val, \%lines, $propcode);
        }
        if (lc($_) eq 'n') {
            thing('Number', '_custom_', $prop_val, \%lines, $propcode);
        }
        if (lc($_) eq 'p') {
            thing('Punctuation', '_custom_', $prop_val, \%lines, $propcode);
            thing('punct', '_custom_', $prop_val, \%lines, $propcode);
        }
        if (lc($_) eq 's') {
            thing('Symbol', '_custom_', $prop_val, \%lines, $propcode);
        }
        if (lc($_) eq 'z') {
            thing('Separator', '_custom_', $prop_val, \%lines, $propcode);
        }
    }
    for (sort keys %$enumerated_properties) {
        my $enum = $enumerated_properties->{$_}->{enum};
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
    if (!%lines) {
        croak "lines didn't get anything in it";
    }
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
            if ($part =~ /[;]/) {
                croak;
            }
            push @parts2, trim($part);
        }
        @parts = @parts2;
        my $propname = shift @parts;
        $propname = trim $propname;
        if (exists $prop_names->{$propname}) {
            my $prop_val = $prop_names->{$propname} << 24;
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
                if (exists $binary_properties->{$unionname}) {
                    my $prop_val = $binary_properties->{$unionname}->{field_index} << 24;
                    my $value    = $binary_properties->{$unionname}->{bit_width};
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
            my $key = $prop_codes->{$propname};
            my $found = 0;
            my $enum = $all_properties->{$key}->{'enum'};
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
                #print "warning: couldn't resolve property $propname property value alias $first\n";
                return;
            }
            for (@parts) {
                s/[\-\s]/./g;
                next if /[\.\|]/;
                thing($_, $propname, $prop_val + $value, \%lines);
            }
        }
    }, 1);
    #for my $v (@v2a) {
        #{"punct",88}
        #$v =~ /"(.*)"\s*,\s*(\d+)/;
        #my $name = $1;
        #my $num = $2;
        #$lines{'gc'}->{$name} = q/{"$num-$name",}
        #thing(x, 'gc',

    #}
    # Aliases like L appear in several categories, but we prefere gc and sc.
    for my $propname (qw(_custom_ gc sc), sort keys %lines) {
        for (sort keys %{$lines{$propname}}) {
            my $item = $_;
            #if ($propname eq 'gc' and length $item == 1) {
            #    my $thing = $item;
            #    croak "item: $item " . (Dumper $prop_names->{'gc'});
            #}
            $done{"$propname$_"} ||= push @lines, $lines{$propname}->{$_};
        }
    }
    $hout .= "
#define num_unicode_property_value_keypairs ".scalar(@lines)."\n";
    my $out = "
static MVMUnicodeNameRegistry **unicode_property_values_hashes;
static const MVMUnicodeNamedValue unicode_property_value_keypairs[".scalar(@lines)."] = {
    ".stack_lines(\@lines, ",", ",\n    ", 0, $wrap_to_columns)."
};";
    $db_sections->{BBB_unicode_property_value_keypairs} = $out;
    $h_sections->{num_unicode_property_value_keypairs} = $hout;
}

sub emit_composition_lookup {
    # Build 3-level sparse array [plane][upper][lower] keyed on bits from the
    # first codepoint of the decomposition of a primary composite, mapped to
    # an array of [second codepoint, primary composite].
    my @lookup;
    for my $point_hex (sort keys %$points_by_hex) {
        # Not interested in anything in the set of full composition exclusions.
        my $point = $points_by_hex->{$point_hex};
        next if $point->{Full_Composition_Exclusion};

        # Only interested in things that have a decomposition spec.
        next unless defined $point->{Decomp_Spec};
        my $decomp_spec = $enumerated_properties->{Decomp_Spec}->{keys}->[$point->{Decomp_Spec}];

        # Only interested in canonical decompositions.
        my $decomp_type = $enumerated_properties->{Decomposition_Type}->{keys}->[$point->{Decomposition_Type}];
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
        push @{$lookup[$plane]->[$upper]->[$lower]}, hex($decomp[1]), hex($point_hex);
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
    $db_sections->{composition_lookup} = "\n/* Canonical composition lookup tables. */\n$tables";
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
    $total_bytes_saved += $bytes_saved;
    print "\nSaved ".thousands($bytes_saved)." bytes by uniquing the bitfield table.\n";
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

#include "moar.h"
'}
sub read_file {
    my $fname = shift;
    open FILE, $fname or croak "Couldn't open file '$fname': $!";
    binmode FILE, ':encoding(UTF-8)';
    my @lines = ();
    while( <FILE> ) {
        push @lines, $_;
    }
    close FILE;
    \@lines;
}

sub write_file {
    my ($fname, $contents) = @_;
    open FILE, ">$fname" or croak "Couldn't open file '$fname': $!";
    binmode FILE, ':encoding(UTF-8)';
    print FILE $contents;
    close FILE;
}

sub register_union {
    my ($unionname, $unionof) = @_;
    register_binary_property($unionname);
    push @$gc_alias_checkers, eval 'sub {
        return ((shift) =~ /^(?:'.$unionof.')$/)
            ? "'.$unionname.'" : 0;
    }';
}

sub UnicodeData {
    my ($bidi_classes, $general_categories, $ccclasses) = @_;
    my $plane = {
        number => 0,
        points => []
    };
    register_binary_property('Any');
    each_line('PropertyValueAliases', sub { $_ = shift;
        my @parts = split /\s*[#;]\s*/;
        my @parts2;
        foreach my $part (@parts) {
            $part = trim $part;
            push @parts2, $part;
            #croak "moo\n'$part'" if ($part =~ / /);
        }
        @parts = @parts2;
        my $propname = shift @parts;
        return if ($parts[0] eq 'Y'   || $parts[0] eq 'N')
               && ($parts[1] eq 'Yes' || $parts[1] eq 'No');
        if ($parts[-1] =~ /\|/) { # it's a union
            my $unionname = $parts[0];
            my $unionof   = pop @parts;
            $unionof      =~ s/\s+//g;
            register_union($unionname, $unionof);
        }
    });
    register_union('Assigned', 'C[cfosn]|L[lmotu]|M[cen]|N[dlo]|P[cdefios]|S[ckmo]|Z[lps]');
    push @$planes, $plane;
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
        my $point = {
            # Unicode_1_Name is not used yet. We should make sure it ends up
            # in some data structure
            Unicode_1_Name => $u1name,
            code_str => $code_str,
            name => $name,
            gencat_name => $gencat,
            General_Category => $general_categories->{enum}->{$gencat},
            Canonical_Combining_Class => $ccclasses->{enum}->{$ccclass},
            Bidi_Class => $bidi_classes->{enum}->{$bidiclass},
            suc => $suc,
            slc => $slc,
            stc => $stc,
            NFD_QC => 1, # these are defaults (inverted)
            NFC_QC => 1, # which will be unset as appropriate
            NFKD_QC => 1,
            NFKC_QC => 1,
            NFG_QC => 1,
            MVM_COLLATION_QC => 1,
            code => $code,
            Any => 1
        };
        $point->{Bidi_Mirrored} = 1 if $bidimirrored eq 'Y';
        if ($decmpspec) {
            $decmpspec =~ s/<\w+>\s+//;
            $point->{Decomp_Spec} = $decomp_index;
            $decomp_keys->[$decomp_index++] = $decmpspec;
        }
        if ($suc || $slc || $stc) {
            $point->{Case_Change_Index} = $case_count++;
        }
        while ($plane->{number} < $plane_num) {
            push(@$planes, ($plane = {
                number => $plane->{number} + 1,
                points => []
            }));
        }
        for my $checker (@$gc_alias_checkers) {
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
            while ($current->{code} < $point->{code} - 1) {
                my $new = { Any => 1 };
                for (sort keys %$current) {
                    $new->{$_} = $current->{$_};
                }
                $new->{code}++;
                $code_str = uc(sprintf '%04x', $new->{code});
                $new->{code_str} = $code_str;
                push @{$plane->{points}}, $new;
                $points_by_hex->{$new->{code_str}} = $points_by_code->{$new->{code}} =
                    $current = $current->{next_point} = $new;
            }
            $last_point = $current;
            $ideograph_start = 0;
        }
        push @{$plane->{points}}, $point;
        $points_by_hex->{$code_str} = $points_by_code->{$code} = $point;

        if ($last_point) {
            $last_point = $last_point->{next_point} = $point;
        }
        else {
            $last_point = $first_point = $point;
        }
    };
    each_line('UnicodeData', $s);
    $s->("110000;Out of Range;Cn;0;L;;;;;N;;;;;");

    register_enumerated_property('Case_Change_Index', {
        bit_width => least_int_ge_lg2($case_count)
    });
    register_enumerated_property('Decomp_Spec', {
        'keys' => $decomp_keys,
        bit_width => least_int_ge_lg2($decomp_index)
    });
}

sub CaseFolding {
    my $simple_count = 1;
    my $grows_count = 1;
    my @simple;
    my @grows;
    each_line('CaseFolding', sub { $_ = shift;
        my ($left, $type, $right) = split /\s*;\s*/;
        return if $type eq 'S' || $type eq 'T';
        if ($type eq 'C') {
            push @simple, $right;
            $points_by_hex->{$left}->{Case_Folding} = $simple_count;
            $simple_count++;
            $points_by_hex->{$left}->{Case_Folding_simple} = 1;
        }
        else {
            my @parts = split ' ', $right;
            push @grows, "{0x".($parts[0]).",0x".($parts[1] || 0).",0x".($parts[2] || 0)."}";
            $points_by_hex->{$left}->{Case_Folding} = $grows_count;
            $grows_count++;
        }
    });
    my $simple_out = "static const MVMint32 CaseFolding_simple_table[$simple_count] = {\n    0x0,\n    0x"
        .stack_lines(\@simple, ",0x", ",\n    0x", 0, $wrap_to_columns)."\n};";
    my $grows_out = "static const MVMint32 CaseFolding_grows_table[$grows_count][3] = {\n    {0x0,0x0,0x0},\n    "
        .stack_lines(\@grows, ",", ",\n    ", 0, $wrap_to_columns)."\n};";
    my $bit_width = least_int_ge_lg2($simple_count); # XXX surely this will always be greater?
    my $index_base = { bit_width => $bit_width };
    register_enumerated_property('Case_Folding', $index_base);
    register_binary_property('Case_Folding_simple');
    $estimated_total_bytes += $simple_count * 8 + $grows_count * 32; # XXX guessing 32 here?
    $db_sections->{BBB_CaseFolding_simple} = $simple_out;
    $db_sections->{BBB_CaseFolding_grows} = $grows_out;
}

sub SpecialCasing {
    my $count = 1;
    my @entries;
    each_line('SpecialCasing', sub { $_ = shift;
        s/#.+//;
        my ($code, $lower, $title, $upper, $cond) = split /\s*;\s*/;
        return if $cond;
        sub threesome {
            my @things = split ' ', shift;
            push @things, 0 while @things < 3;
            join ", ", map { "0x$_" } @things
        }
        push @entries, "{ { " . threesome($upper) .
                       " }, { " . threesome($lower) .
                       " }, { " . threesome($title) .
                       " } }";
        $points_by_hex->{$code}->{Special_Casing} = $count;
        $count++;
    });
    my $out = "static const MVMint32 SpecialCasing_table[$count][3][3] = {\n    {0x0,0x0,0x0},\n    "
        .stack_lines(\@entries, ",", ",\n    ", 0, $wrap_to_columns)."\n};";
    my $bit_width = least_int_ge_lg2($count);
    my $index_base = { bit_width => $bit_width };
    register_enumerated_property('Special_Casing', $index_base);
    $estimated_total_bytes += $count * 4 * 3 * 3;
    $db_sections->{BBB_SpecialCasing} = $out;
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
}

sub Jamo {
    my $points_by_code = shift;
    my $propname = 'Jamo_Short_Name';
    each_line('Jamo', sub { $_ = shift;
        my ($code_str, $name) = split /\s*[;#]\s*/;
        apply_to_range($code_str, sub {
            my $point = shift;
            $point->{Jamo_Short_Name} = $name;
        });
    });
    my @hangul_syllables;
    for my $key (sort keys %{$points_by_code}) {
        if (%{$points_by_code}{$key}->{name} eq '<Hangul Syllable>') {
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
            if (exists %{$points_by_code}{$cp}->{Jamo_Short_Name}) {
                $final_name .= %{$points_by_code}{$cp}->{Jamo_Short_Name};
            }
        }
        %{$points_by_code}{$hs_cps}->{name} = $final_name;
    }
}
sub BidiMirroring {
    my $file = 'BidiMirroring';
    my $propname = 'Bidi_Mirroring_Glyph';
    my $max_size = 0;
    each_line('BidiMirroring', sub { $_ = shift;
        my $line = $_;
        my ($range, $int) = split /\s*[;#]\s*/, $line;
        $int = hex $int or croak;
        $max_size = $int if $max_size < $int;
        apply_to_range($range, sub {
            my $point = shift;
            $point->{$propname} = $int;
        });
    });

    register_int_property($propname, $max_size);
}
sub collation {
    my $enum = {};
    my $base = { enum => $enum };
    my $j = 0;
    my $name_primary = 'MVM_COLLATION_PRIMARY';
    my $name_secondary = 'MVM_COLLATION_SECONDARY';
    my $name_tertiary = 'MVM_COLLATION_TERTIARY';
    my $implicit = 0;
    # Record the highest value we see so we can save some bits in the bitfield
    my $primary_max = 0;
    my $secondary_max = 0;
    my $tertiary_max = 0;
    ## Sample line from allkeys.txt
    #1D4FF ; [.1EE3.0020.0005] # MATHEMATICAL BOLD SCRIPT SMALL V
    my $line_no = 0;
    each_line('UCA/allkeys', sub { $_ = shift;
        my $line = $_;
        $line_no++;
        my ($code, $weight1, $weight2, $weight3, $temp);
        if ($line =~ s/ ^ \@implicitweights \s+ //xms ) {
            ($code, $weight1) = split / (?: [;\[\]]|\s )+ /xms, $line;
            $weight1 = hex $weight1 or croak;
            $code or croak;
            $implicit = 1;
        }
        elsif ( $line =~ /^\s*[#@]/ or $line =~ /^\s*$/ ) {
            $line = '';
            return;
        }
        else {
            ($code, $temp) = split /[;#]+/, $line;
            $code = trim $code;
            my @codes = split / /, $code;
            # we don't yet support collation for multiple codepoints
            if ( scalar @codes > 1 ) {
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

            while ( $temp =~ / (:? \[ ([.*]) (\p{AHex}+) ([.*]) (\p{AHex}+) ([.*]) (\p{AHex}+) \] ) /xmsg ) {
                $weight1 += hex $3;
                $weight2 += hex $5;
                $weight3 += hex $7;
            }

        }
        if ( !defined $code or !defined $weight1 or !defined $weight2 or !defined $weight3 ) {
            unless ( $implicit and defined $weight1 ) {
                croak "Line no $line_no: line:[$line] weight1:[$weight1] weight2:[$weight2] weight3:[$weight3]";
            }
        }
        apply_to_range($code, sub {
            my $point = shift;
            # Add one to the value so we can distinguish between specified values
            # of zero for collation weight and null values.
            $point->{$name_primary} = 1;
            if ($weight1) {
                $point->{$name_primary} += $weight1 if $weight1;
                $primary_max = $weight1 if $weight1 > $primary_max;
            }
            $point->{$name_secondary} = 1;
            if ($weight2) {
                $point->{$name_secondary} += $weight2;
                $secondary_max = $weight2 if $weight2 > $secondary_max;
            }
            $point->{$name_tertiary}  = 1;
            if ($weight3) {
                $point->{$name_tertiary} += $weight3  if $weight3 ;
                $tertiary_max = $weight3 if $weight3 > $tertiary_max;
            }
        });
    });
    for ( $primary_max, $secondary_max, $tertiary_max ) {
        if ( $_ < 1 ) {
            croak "Oh no! One of the highest collation numbers I saw is less than 0. Something is wrong" .
              "Primary max: $primary_max secondary max: $secondary_max tertiary_max: $tertiary_max";
        }
    }

    register_int_property($name_primary, $primary_max);
    register_int_property($name_secondary, $secondary_max);
    register_int_property($name_tertiary, $tertiary_max);
    register_binary_property('MVM_COLLATION_QC');
}
sub LineBreak {
    my $enum = {};
    my $base = { enum => $enum };
    my $j = 0;
    $enum->{$_} = $j++ for ("BK", "CM", "CR", "GL", "LF", "NL", "SP",
        "WJ", "ZW", "ZWJ", "AI", "AL", "B2", "BA", "BB", "CB", "CJ", "CL", "CP", "EB",
        "EM", "EX", "H2", "H3", "HL", "HY", "ID", "IN", "IS", "JL",
        "JT", "JV", "NS", "NU", "OP", "PO", "PR", "QU", "RI", "SA",
        "SG", "SY", "XX"
        );
    each_line('LineBreak', sub { $_ = shift;
        my ($range, $name) = split /\s*[;#]\s*/;
        croak "Can't find Line_Break property $name in the enum" unless exists $enum->{$name}; # only normative
        apply_to_range($range, sub {
            my $point = shift;
            $point->{Line_Break} = $enum->{$name};
        });
    });
    my @keys = ();
    for my $key (sort keys %{$base->{enum}}) {
        $keys[$base->{enum}->{$key}] = $key;
    }
    $base->{keys} = \@keys;
    $base->{bit_width} = least_int_ge_lg2($j);
    register_enumerated_property('Line_Break', $base);
}

sub NameAliases {
    each_line('NameAliases', sub { $_ = shift;
        my ($code_str, $name, $type) = split /\s*[;#]\s*/;
        $aliases->{$name} = hex $code_str;
        $alias_types->{$name}->{'code'} = hex $code_str;
        $alias_types->{$name}->{'type'} = $type;
    });
}

sub NamedSequences {
    each_line('NamedSequences', sub { $_ = shift;
        my ($name, $codes) = split /\s*[;#]\s*/;
        my @parts = split ' ', $codes;
        $named_sequences->{$name} = \@parts;
    });
}

sub tweak_nfg_qc {
    # See http://www.unicode.org/reports/tr29/tr29-27.html#Grapheme_Cluster_Boundary_Rules
    for my $point (values %$points_by_code) {
        my $code = $point->{'code'};

        # \r
        if ($code == 0x0D) {
            $point->{'NFG_QC'} = 0;
        }

        # Hangul
        elsif ($point->{'Hangul_Syllable_Type'}) {
            $point->{'NFG_QC'} = 0;
        }

        # Regional indicators
        elsif ($code >= 0x1F1E6 && $code <= 0x1F1FF) {
            $point->{'NFG_QC'} = 0;
        }

        # Zero Width Joiner
        elsif ($code == 0x200D) {
            $point->{'NFG_QC'} = 0;
        }

        # Grapheme_Extend
        elsif ($point->{'Grapheme_Extend'}) {
            $point->{'NFG_QC'} = 0;
        }

        # SpacingMark, and a couple of specials
        elsif ($point->{'gencat_name'} eq 'Mc' || $code == 0x0E33 || $code == 0x0EB3) {
            $point->{'NFG_QC'} = 0;
        }
        # For now set all Emoji to NFG_QC 0
        # Eventually we will only want to set the ones that are NOT specified
        # as ZWJ sequences
        elsif ($point->{'Emoji'} ) {
            $point->{'NFG_QC'} = 0;
        }
        elsif ($point->{'Grapheme_Cluster_Break'}) {
            $point->{'NFG_QC'} = 0;
        }
        elsif ($point->{'Prepended_Concatenation_Mark'}) {
            $point->{'NFG_QC'} = 0;
        }
    }
}

sub register_binary_property {
    my $name = shift;
    $all_properties->{$name} = $binary_properties->{$name} = {
        property_index => $property_index++,
        name => $name,
        bit_width => 1
    } unless exists $binary_properties->{$name};
}

sub register_int_property {
    my ( $name, $elems ) = @_;
    # add to binary_properties for now
    $all_properties->{$name} = $binary_properties->{$name} = {
        property_index => $property_index++,
        name => $name,
        bit_width => least_int_ge_lg2($elems)
    } unless exists $binary_properties->{$name};
}

sub register_enumerated_property {
    my ($pname, $obj) = @_;
    croak if exists $enumerated_properties->{$pname};
    $all_properties->{$pname} = $enumerated_properties->{$pname} = $obj;
    $obj->{name} = $pname;
    $obj->{property_index} = $property_index++;
    $obj
}

main();

# vim: ft=perl6 expandtab sw=4
