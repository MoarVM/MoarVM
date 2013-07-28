use warnings; use strict;
use Data::Dumper;
use Carp qw(cluck);
$Data::Dumper::Maxdepth = 1;
# Make C versions of the Unicode tables.

my $db_sections = {};
my $h_sections = {};
my $planes = [];
my $points_by_hex = {};
my $points_by_code = {};
my $enumerated_properties = {};
my $binary_properties = {};
my $first_point = undef;
my $last_point = undef;
my $aliases = {};
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

sub progress($);
sub main {
    $db_sections->{'AAA_header'} = header();

    # Load all the things
    UnicodeData(
        derived_property('BidiClass', 'Bidi_Class', {}, 0),
        derived_property('GeneralCategory', 'General_Category', {}, 0),
        derived_property('CombiningClass',
            'Canonical_Combining_Class', { Not_Reordered => 0 }, 1)
    );
    goto skip_most if $skip_most_mode;
    binary_props('extracted/DerivedBinaryProperties');
    enumerated_property('ArabicShaping', 'Joining_Type', {}, 0, 2);
    enumerated_property('ArabicShaping', 'Joining_Group', {}, 0, 3);
    enumerated_property('BidiMirroring', 'Bidi_Mirroring_Glyph', {}, 0, 1);
    enumerated_property('Blocks', 'Block', { No_Block => 0 }, 1, 1);
    enumerated_property('extracted/DerivedDecompositionType', 'Decomposition_Type', { None => 0 }, 1, 1);
    CaseFolding();
    enumerated_property('DerivedAge',
        'Age', { Unassigned => 0 }, 1, 1);
    binary_props('DerivedCoreProperties');
    DerivedNormalizationProps();
    enumerated_property('extracted/DerivedNumericValues',
        'Numeric_Value', { NaN => 0 }, 1, 1);
    enumerated_property('extracted/DerivedNumericValues',
        'Numeric_Value_Numerator', { NaN => 0 }, 1, sub {
            my @fraction = split('/', (shift));
            return $fraction[0];
        });
    enumerated_property('extracted/DerivedNumericValues',
        'Numeric_Value_Denominator', { NaN => 0 }, 1, sub {
            my @fraction = split('/', (shift));
            return $fraction[1] || '1';
        });
    enumerated_property('extracted/DerivedNumericType',
        'Numeric_Type', { None => 0 }, 1, 1);
    enumerated_property('HangulSyllableType',
        'Hangul_Syllable_Type', { Not_Applicable => 0 }, 1, 1);
    Jamo();
    LineBreak();
    NameAliases();
    NamedSequences();
    binary_props('PropList');
    enumerated_property('Scripts', 'Script', { Unknown => 0 }, 1, 1);
    # XXX SpecialCasing.txt # haven't decided how to do it
    # XXX StandardizedVariants.txt # no clue what this is
    break_property('Grapheme', 'Grapheme_Cluster_Break');
    break_property('Sentence', 'Sentence_Break');
  skip_most:
    break_property('Word', 'Word_Break');

    # Allocate all the things
    progress "done.\nallocating bitfield...";
    my $allocated_properties = allocate_bitfield();
    # Compute all the things
    progress "done.\ncomputing all properties...";
    compute_properties($allocated_properties);
    # Make the things less
    progress "...done.\ncomputing collapsed properties table...";
    compute_bitfield($first_point);
    # Emit all the things
    progress "...done.\nemitting unicode_db.c...";
    emit_bitfield($first_point);
    $extents = emit_codepoints_and_planes($first_point);
    emit_case_changes($first_point);
    emit_codepoint_row_lookup($extents);
    emit_property_value_lookup($allocated_properties);
    emit_names_hash_builder();
    emit_unicode_property_keypairs();
    emit_unicode_property_value_keypairs();

    print "done!";
    write_file('src/strings/unicode_db.c', join_sections($db_sections));
    write_file('src/strings/unicode_gen.h', join_sections($h_sections));
    print "\nEstimated bytes demand paged from disk: ".
        thousands($estimated_total_bytes).
        ".\nEstimated bytes saved by various compressions: ".
        thousands($total_bytes_saved).".\n";
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
    #die "couldn't find code ".sprintf('%x', $last_point->{code} + 1).
    #    " got ".$point->{code_str}." for range $first..$last"
    #    unless $last_point->{code} == hex $last;
    # can't die there because some ranges end on points that don't exist (Blocks)
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
sub derived_property {
    # filename, property name, property object, starting counter
    my ($fname, $pname, $base, $j) = @_;
    # wrap the provided object as the enum key in a new one
    $base = { enum => $base };
    each_line("extracted/Derived$fname", sub { $_ = shift;
        my ($range, $class) = split /\s*[;#]\s*/;
        unless (exists $base->{enum}->{$class}) {
            # haven't seen this property's value before
            # add it, and give it an index.
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
        ($base->{enum}->{$value} = $index
            = $j++) unless defined $index;
        apply_to_range($range, sub {
            my $point = shift;
            $point->{$pname} = $index; # set the property's value index
        });
    });
    my @keys = ();
    # stash the keys in an array so they can be put in a table later
    for my $key (keys %{$base->{enum}}) {
        $keys[$base->{enum}->{$key}] = $key;
    }
    $base->{keys} = \@keys;
    $base->{bit_width} = least_int_ge_lg2($j);
    register_enumerated_property($pname, $base);
}
sub least_int_ge_lg2 {
    int(log(shift)/log(2) - 0.00001) + 1;
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
            <=> $enumerated_properties->{$a}->{bit_width} }
            keys %$enumerated_properties;
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
            if ($bit_offset + $prop->{bit_width} <= $bitfield_cell_bitwidth) {
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
    $h_sections->{num_property_codes} = "#define MVMNUMPROPERTYCODES $index\n";
    $allocated
}
sub compute_properties {
    local $| = 1;
    my $fields = shift;
    for my $field (@$fields) {
        my $bit_offset = $field->{bit_offset};
        my $bit_width = $field->{bit_width};
        my $point = $first_point;
        print "..$field->{name} width:$bit_width bits";
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
    return "\n${indent}return $fate->{bitfield_index}; /* ".
        "$bitfield_table->[$fate->{bitfield_index}]->{code_str}".
        " $bitfield_table->[$fate->{bitfield_index}]->{name} */" if $type == $FATE_SPAN;
    return "\n${indent}return codepoint - $fate->{fate_offset};"
    .($fate->{fate_offset} == 0 ? " /* the fast path */ " : "");
}
sub add_extent($$) {
    my ($extents, $extent) = @_;
    push @$extents, $extent;
}
sub emit_codepoints_and_planes {
    my @bitfield_index_lines;
    my @name_lines;
    my @offsets;
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
        for my $point (@{$plane->{points}}) {
            my $toadd = undef;
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
            elsif ($span_length) {
                if ($span_length >= $span_length_threshold) {
                    $bytes_saved += 10 * ($span_length - 1);
                    if (!exists($last_point->{fate_type})) {
                        add_extent $extents, $last_point;
                    }
                    $last_point->{fate_type} = $FATE_SPAN;
                    $code_offset += $span_length - 1;
                    $toadd = $point;
                    $span_length = 0;
                }
                while ($span_length > 1) {
                    # catch up to last code
                    $last_point = $last_point->{next_point};
                    push @bitfield_index_lines,
                        "/*$index*/$last_point->{bitfield_index}/*".
                        "$last_point->{code_str} */";
                    push @name_lines, "/*$index*/".
                    ($last_point->{name} =~ /^</ ? "NULL" : "\"$last_point->{name}\"").
                        "/* $last_point->{code_str} */";
                    $index++;
                    $bytes += 10 + ($last_point->{name} =~ /^</ ? 0 : length($last_point->{name}) + 1);
                    $span_length--;
                }
                $span_length = 0;
            }
            if ($compress_codepoints
                    && $last_code < $point->{code} - $gap_length_threshold) {
                $bytes_saved += 10 * ($point->{code} - $last_code - 1);
                add_extent $extents, { fate_type => $FATE_NULL,
                    code => $last_code + 1 };
                $code_offset += ($point->{code} - $last_code - 1);
                $last_code = $point->{code} - 1;
            }
            while ($last_code < $point->{code} - 1) {
                push @bitfield_index_lines, "0";
                push @name_lines, "NULL";
                $last_code++;
                $index++;
                $bytes += 10;
            }
            die "$last_code ".Dumper($point) unless $last_code == $point->{code} - 1;
            if ($toadd || $plane->{number} == 1 && $plane->{points}->[0]->{code} == $point->{code} && !exists($point->{fate_type})) {
                $point->{fate_type} = $FATE_NORMAL;
                $point->{fate_offset} = $code_offset;
                add_extent $extents, $point;
            }
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
    $h_sections->{codepoint_names_count} = "#define MVMCODEPOINTNAMESCOUNT $index";
    $extents
}
sub emit_codepoint_row_lookup {
    my $extents = shift;
    my $SMP_start;
    my $i = 0;
    for (@$extents) {
        # handle the first recursion specially to optimize for most common BMP lookups
        if ($_->{code} == 0x10000) {
            $SMP_start = $i;
            last;
        }
        $i++;
    }
    my $out = "static MVMint32 MVM_codepoint_to_row_index(MVMThreadContext *tc, MVMint32 codepoint) {\n
    MVMint32 plane = codepoint >> 16;

    if (codepoint < 0) {
        MVM_exception_throw_adhoc(tc, \"should eventually be unreachable\");
    }

    if (plane == 0) {"
    .emit_binary_search_algorithm($extents, 0, 1, $SMP_start - 1, "        ")."
    }
    else {
        if (plane < 0 || plane > 16 || codepoint > 0x10FFFD) {
            return -1;
        }
        else {".emit_binary_search_algorithm($extents, $SMP_start,
            int(($SMP_start + scalar(@$extents)-1)/2), scalar(@$extents) - 1, "            ")."
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
        : die 'wut.';
    $out = "static const $val_type props_bitfield[$rows][$wide] = {\n    ".
        stack_lines(\@lines, ",", ",\n    ", 0, $wrap_to_columns)."\n};";
    $db_sections->{BBB_main_bitfield} = $out;
}
sub emit_property_value_lookup {
    my $allocated = shift;
    my $hout = "typedef enum {\n";
    my $out = "static MVMint32 MVM_codepoint_to_row_index(MVMThreadContext *tc, MVMint32 codepoint);
static MVMint32 MVM_unicode_get_property_value(MVMThreadContext *tc, MVMint32 codepoint, MVMint64 property_code) {
    MVMuint32 switch_val = (MVMuint32)property_code;
    MVMint32 result_val = 0; /* we'll never have negatives, but so */
    MVMuint32 codepoint_row = MVM_codepoint_to_row_index(tc, codepoint);
    MVMuint16 bitfield_row;

    if (codepoint_row == -1) /* non-existent codepoint; XXX should throw? */
        return 0;

    bitfield_row = codepoint_bitfield_indexes[codepoint_row];

    switch (switch_val) {
        case 0: return 0;";
    for my $prop (@$allocated) {
        $hout .= "    ".uc("MVM_unicode_property_$prop->{name}")." = $prop->{field_index},\n";
        $prop_names->{$prop->{name}} = $prop->{field_index};
        $out .= "
        case ".uc("MVM_unicode_property_$prop->{name}").":";
        my $bit_width = $prop->{bit_width};
        my $bit_offset = $prop->{bit_offset};
        my $word_offset = $prop->{word_offset};
        $out .= "\n/* $prop->{name} bit_width: $bit_width bit_offset: $bit_offset word_offset: $word_offset */\n";
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
            ".($one_word_only ? 'return' : 'result_val |=')." ((props_bitfield[bitfield_row][$word_offset] & 0x".
                sprintf("%x",$binary_mask).") >> $shift); /* mask: $binary_string */";
            $word_offset++;
            $bit_offset = 0;
        }
        $out .= "
            return result_val;" unless $one_word_only;
    }
    $out .= "
        default:
            return 0;
    }
}";
    $hout .= "} MVM_unicode_property_codes;";
    $db_sections->{MVM_unicode_get_property_value} = $out;
    $h_sections->{property_code_definitions} = $hout;
}
sub emit_names_hash_builder {
    my $num_extents = scalar(@$extents);
    my $out = "
static MVMint32 codepoint_extents[".($num_extents + 1)."][2] = {";
    $estimated_total_bytes += 4 * 2 * ($num_extents + 1);
    for my $extent (@$extents) {
        $out .= "
    {0x".sprintf("%x",$extent->{code}).",$extent->{fate_type}},";
    }
    $h_sections->{MVMNUMUNICODEEXTENTS} = "#define MVMNUMUNICODEEXTENTS $num_extents\n";
    $out .= "
    {0x10FFFE,0}
};

/* Lazily constructed hashtable of Unicode names to codepoints.
    Okay not to be threadsafe since its value is deterministic
        and I don't care about the tiny potential for a memory leak
        in the event of a race condition. */
static MVMUnicodeNameHashEntry *codepoints_by_name = NULL;
static void generate_codepoints_by_name(MVMThreadContext *tc) {
    MVMint32 extent_index = 0;
    MVMint32 codepoint = 0;
    MVMint32 codepoint_table_index = 0;
    for (; extent_index < MVMNUMUNICODEEXTENTS; extent_index++) {
        MVMint32 length;
        codepoint = codepoint_extents[extent_index][0];
        length = codepoint_extents[extent_index + 1][0] - codepoint_extents[extent_index][0];
        if (codepoint_table_index >= MVMCODEPOINTNAMESCOUNT)
            continue;
        switch (codepoint_extents[extent_index][1]) {
            case $FATE_NORMAL: {
                MVMint32 extent_span_index = 0;
                for (; extent_span_index < length
                    && codepoint_table_index < MVMCODEPOINTNAMESCOUNT; extent_span_index++) {
                    const char *name = codepoint_names[codepoint_table_index];
                    if (name) {
                        MVMUnicodeNameHashEntry *entry = malloc(sizeof(MVMUnicodeNameHashEntry));
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
                    MVMUnicodeNameHashEntry *entry = malloc(sizeof(MVMUnicodeNameHashEntry));
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
}
";
    $db_sections->{names_hash_builder} = $out;
}#"
sub emit_unicode_property_keypairs {
    my $hout = "
typedef struct _MVMUnicodeNamedValue {
    const char *name;
    MVMint32 value;
} MVMUnicodeNamedValue;";
    my @lines = ();
    each_line('PropertyAliases', sub { $_ = shift;
        my @aliases = split /\s*[#;]\s*/;
        for my $name (@aliases) {
            if (exists $prop_names->{$name}) {
                for (@aliases) {
                    $prop_names->{$_} = $prop_names->{$name}
                        unless $_ eq $name;
                    $prop_codes->{$_} = $name;
                }
                last;
            }
        }
    });
    for my $key (keys %$prop_names) {
        my $k = lc($key);
        $k =~ s/_//g;
        push @lines, "{\"$key\",$prop_names->{$key}}";
        # add a canonical one for fallback "fuzzy" matching
        push @lines, "{\"$k\",$prop_names->{$key}}"
            unless exists $prop_names->{$k};
    }
    $hout .= "
#define num_unicode_property_keypairs ".scalar(@lines)."\n";
    my $out = "
static const MVMUnicodeNamedValue unicode_property_keypairs[".scalar(@lines)."] = {
    ".stack_lines(\@lines, ",", ",\n    ", 0, $wrap_to_columns)."
};";
    $db_sections->{BBB_unicode_property_keypairs} = $out;
    $h_sections->{MVMUnicodeNamedValue} = $hout;
}
sub emit_unicode_property_value_keypairs {
    my $hout = "";
    my @lines = ();
    my $property;
    for (keys %$enumerated_properties) {
        my $enum = $enumerated_properties->{$_}->{enum};
        my $toadd = {};
        for (keys %$enum) {
            my $key = lc("$_");
            $key =~ s/[_\-\s]/./g;
            $toadd->{$key} = $enum->{$_};
        }
        for (keys %$toadd) {
            $enum->{$_} = $toadd->{$_};
        }
    }
    each_line('PropertyValueAliases', sub { $_ = shift;
        my @parts = split /\s*[#;]\s*/;
        my $propname = shift @parts;
        if (exists $prop_names->{$propname}) {
            return if $parts[0] eq 'Y' || $parts[0] eq 'N';
            my @others = ();
            for my $alias (@parts) {
                my $newalias = lc("$alias");
                $newalias =~ s/[_\-\s]/./g;
                push @others, $newalias;
            }
            for my $alias (@others) {
                push @parts, $alias;
            }
            my $prop_val = $prop_names->{$propname} << 24;
            my $key = $prop_codes->{$propname};
            my $found = 0;
            my $enum = $all_properties->{$key}->{'enum'};
            die $propname unless $enum;
            my $value;
            my $first;
            for my $alias (@parts) {
                $first = $alias unless defined $first;
                if (exists $enum->{$alias}) {
                    $value = $enum->{$alias};
                    last;
                }
            }
            #die Dumper($enum) unless defined $value;
            unless (defined $value) {
                #print "warning: couldn't resolve property $propname property value alias $first\n";
                return;
            }
            for my $alias (@parts) {
                push @lines, "{\"$alias\",".($prop_val + $value)."}" unless $alias =~ /\./;
            }
        }
    });
    $hout .= "
#define num_unicode_property_value_keypairs ".scalar(@lines)."\n";
    my $out = "
static MVMUnicodeNameHashEntry **unicode_property_values_hashes;
static const MVMUnicodeNamedValue unicode_property_value_keypairs[".scalar(@lines)."] = {
    ".stack_lines(\@lines, ",", ",\n    ", 0, $wrap_to_columns)."
};";
    $db_sections->{BBB_unicode_property_value_keypairs} = $out;
    $h_sections->{num_unicode_property_value_keypairs} = $hout;
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
    my ($fname, $contents) = @_;
    open FILE, ">$fname" or die "Couldn't open file '$fname': $!";
    print FILE $contents;
    close FILE;
}
sub UnicodeData {
    my ($bidi_classes, $general_categories, $ccclasses) = @_;
    my $plane = {
        number => 0,
        points => []
    };
    push @$planes, $plane;
    my $ideograph_start;
    my $case_count = 1;
    my $decomp_keys = [ '' ];
    my $decomp_index = 1;
    each_line('UnicodeData', sub {
        $_ = shift;
        my ($code_str, $name, $gencat, $ccclass, $bidiclass, $decmpspec,
            $num1, $num2, $num3, $bidimirrored, $u1name, $isocomment,
            $suc, $slc, $stc) = split ';';

        my $code = hex $code_str;
        my $plane_num = $code >> 16;
        my $point = {
            code_str => $code_str,
            name => $name,
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
            code => $code
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
        if ($name =~ /(Ideograph|Syllable|Private|Surrogate)(\s|.)*?First/) {
            $ideograph_start = $point;
            $point->{name} =~ s/, First//;
        }
        elsif ($ideograph_start) {
            $point->{name} = $ideograph_start->{name};
            my $current = $ideograph_start;
            while ($current->{code} < $point->{code} - 1) {
                my $new = {};
                for (keys %$current) {
                    $new->{$_} = $current->{$_};
                }
                $new->{code}++;
                $code_str = uc(sprintf '%x', $new->{code});
                while(length($code_str) < 4) {
                    $code_str = "0$code_str";
                }
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
    });
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
sub DerivedNormalizationProps {
    my $binary = {
        Full_Composition_Exclusion => 1,
        Changes_When_NFKC_Casefolded => 1
    };
    my $inverted_binary = {
        NFD_QC => 1,
        NFKD_QC => 1
    };
    register_binary_property($_) for ((keys %$binary),(keys %$inverted_binary));
    my $trinary = {
        NFC_QC => 1,
        NFKC_QC => 1
    };
    my $trinary_values = { 'N' => 0, 'Y' => 1, 'M' => 2 };
    register_enumerated_property($_, { enum => $trinary_values, bit_width => 2, 'keys' => ['N','Y','M'] }) for (keys %$trinary);
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
    $base->{bit_width} = int(log($j)/log(2) - 0.00001) + 1;
    register_enumerated_property('Line_Break', $base);
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
sub register_binary_property {
    my $name = shift;
    $all_properties->{$name} = $binary_properties->{$name} = {
        property_index => $property_index++,
        name => $name,
        bit_width => 1
    } unless exists $binary_properties->{$name};
}
sub register_enumerated_property {
    my ($pname, $obj) = @_;
    die if exists $enumerated_properties->{$pname};
    $all_properties->{$pname} = $enumerated_properties->{$pname} = $obj;
    $obj->{name} = $pname;
    $obj->{property_index} = $property_index++;
    $obj
}
main();
