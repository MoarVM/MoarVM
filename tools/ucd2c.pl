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

my $have_rakudo = `rakudo -e 'say "Hello world"'`;
die "You need rakudo in your path to run this script\n"
    unless $have_rakudo =~ /\AHello world/;

my $DEBUG = $ENV{UCD2CDEBUG} // 0;

binmode STDOUT, ':encoding(UTF-8)';
binmode STDERR, ':encoding(UTF-8)';
my $LOG;
# All globals should use UPPERCASE
my $DB_SECTIONS = {};
my $H_SECTIONS = {};
my @POINTS_SORTED;
my $POINTS_BY_CODE = {};
my $ALIASES = {};
my $ALIAS_TYPES = {};
my $PROP_NAMES = {};
my $BITFIELD_TABLE = [];
my $ALL_PROPERTIES = {};
my $ENUMERATED_PROPERTIES = {};
my $BINARY_PROPERTIES = {};
my $GENERAL_CATEGORIES = {};
my $PROPERTY_INDEX = 0;
# Estimated total bytes is only calculated for bitfield and names
my $ESTIMATED_TOTAL_BYTES = 0;
my $TOTAL_BYTES_SAVED = 0;
# Constants
my $WRAP_TO_COLUMNS = 120;
my $COMPRESS_CODEPOINTS = 1;
my $GAP_LENGTH_THRESHOLD = 1000;
my $SPAN_LENGTH_THRESHOLD = 100;
my $SKIP_MOST_MODE = 0;
my $BITFIELD_CELL_BITWIDTH = 32;

sub trim {
    my ($str) = @_;
    $str =~ s/   \s+ $ //xmsg;
    $str =~ s/ ^ \s+   //xmsg;
    return $str;
}
sub trim_trailing {
    my ($str) = @_;
    $str =~ s/ [ \t]+ $ //xmsg;
    return $str;
}
# Get the highest version number from the supplied array. Iterate and compare
# each segment. Ensures if there ever are subversions like 5.0.5 then it will work.
sub get_highest_version {
    my ($versions) = @_;
    my @highest_ver;
    for my $ver_str (@$versions) {
        my @ver = split / [.] /x, $ver_str;
        if (!@highest_ver) {
            @highest_ver = @ver;
            next;
        }
        for (my $i = 0; $i < @ver; $i++) {
            if ($highest_ver[$i] < $ver[$i]) {
                @highest_ver = @ver;
            }
            elsif ($highest_ver[$i] == $ver[$i]) {
                next;
            }
            last;
        }
    }
    return join '.', @highest_ver;
}

sub add_emoji_sequences {
    my ($named_sequences) = @_;
    my $directory = "UNIDATA";
    # Find all the emoji dirs
    opendir my $UNIDATA_DIR, $directory or croak $!;
    my @versions;
    while (my $file = readdir $UNIDATA_DIR) {
        push @versions, $file if -d "$directory/$file" && $file =~ s/ ^ emoji- //x;
    }
    croak "Couldn't find any emoji folders. Please run UCD-download.raku again"
        if !@versions;
    my $highest_emoji_version = get_highest_version(\@versions);
    say "Highest emoji version found is $highest_emoji_version";
    for my $version (@versions) {
        add_unicode_sequence("emoji-$version/emoji-sequences", $named_sequences);
        add_unicode_sequence("emoji-$version/emoji-zwj-sequences", $named_sequences);
    }
    return $highest_emoji_version;
}

sub main {
    $DB_SECTIONS->{'AAA_header'} = header();
    # For adding Emoji and standard sequences to
    my $named_sequences = {};
    my $highest_emoji_version = add_emoji_sequences($named_sequences);
    add_unicode_sequence('NamedSequences', $named_sequences);
    my $hout = emit_unicode_sequence_keypairs($named_sequences);
    NameAliases();
    $hout .= gen_name_alias_keypairs();
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
    if (-e "emoji-$highest_emoji_version/emoji-data") {
        binary_props("emoji-$highest_emoji_version/emoji-data") # v12.1 and earlier
    }
    else {
        binary_props("emoji/emoji-data"); # v13.0 and later
    }
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
    binary_props('PropList');
    enumerated_property('Scripts', 'Script', { Unknown => 0 }, 1);
    # XXX StandardizedVariants.txt # no clue what this is
    grapheme_cluster_break('Grapheme', 'Grapheme_Cluster_Break');
    break_property('Sentence', 'Sentence_Break');
  skip_most:
    break_property('Word', 'Word_Break');
    tweak_nfg_qc();
    find_quick_prop_data();
    # Allocate all the things
    progress("done.\nsetting next_point for codepoints");
    my $first_point = set_next_points();
    progress("done.\nallocating bitfield...");
    my $allocated_bitfield_properties = allocate_bitfield($first_point);
    # Compute all the things
    progress("done.\ncomputing all properties...");
    compute_properties($allocated_bitfield_properties);
    # Make the things less
    progress("...done.\ncomputing collapsed properties table...");
    compute_bitfield($first_point);
    # Emit all the things
    progress("...done.\nemitting unicode_db.c...");
    emit_bitfield($first_point);
    my $extents = emit_codepoints_and_planes($first_point);
    $DB_SECTIONS->{BBB_case_changes} = emit_case_changes($first_point);
    $DB_SECTIONS->{codepoint_row_lookup} = emit_codepoint_row_lookup($extents);
    $hout .= emit_property_value_lookup($allocated_bitfield_properties);
    emit_names_hash_builder($extents);
    my $prop_codes = emit_unicode_property_keypairs();
    $H_SECTIONS->{num_unicode_property_value_keypairs} = $hout .
        emit_unicode_property_value_keypairs($prop_codes);
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
        write_file("ucd2c_extents.log", $LOG);
    }
    print "\nDONE!!!\n\n";
    print "Make sure you update tests in roast by following docs/unicode-generated-tests.asciidoc in the roast repo\n";
    return 1;
}
sub find_quick_prop_data {
    my @wanted_val_str = (
        [ 'gencat_name', 'Zl', 'Zp' ],
    );
    my @wanted_val_bool = (
        [ 'White_Space', 1 ]
    );
    my %gencat_wanted_h;
    my @result;
    for my $code (sort { $a <=> $b } keys %{$POINTS_BY_CODE}) {
        for my $cat_data (@wanted_val_str) {
            my $propname = $cat_data->[0];
            my $i;
            for ($i = 1; $i < @$cat_data; $i++) {
                my $pval = $cat_data->[$i];
                push @{$gencat_wanted_h{$propname . "_" . $pval}}, $code if $POINTS_BY_CODE->{$code}->{$propname} eq $pval;
            }
        }
        for my $cat_data (@wanted_val_bool) {
            my $propname = $cat_data->[0];
            push @{$gencat_wanted_h{$propname}}, $code if $POINTS_BY_CODE->{$code}->{$propname};
        }
    }
    say Dumper(%gencat_wanted_h);
    for my $pname (sort keys %gencat_wanted_h) {
        my @text;
        for my $cp (@{$gencat_wanted_h{$pname}}) {
            push @text, "((cp) == $cp)";
        }
        push @result, ("#define MVM_CP_is_$pname(cp) (" . join(' || ', @text) . ')');
    }
    write_file("src/strings/unicode_prop_macros.h", (join("\n", @result) . "\n"));
}
sub thousands {
    my $in = shift;
    $in = reverse "$in"; # stringify or copy the string
    $in =~ s/ (\d\d\d) (?= \d) /$1,/xg;
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
    # %prefs is a list of sections that need to come before others
    # The values are sections that need to come before the key.
    # So: C => [ 'A', 'B' ] would result in A, B, C;
    my %prefs = (
        MVM_unicode_get_property_int => ['block_lookup']
    );
    my %done;
    my ($sections) = @_;
    my $content = "";
    for my $sec (sort keys %{$sections}) {
        if ($prefs{$sec}) {
            for my $sec_before (@{$prefs{$sec}}) {
                next if $done{$sec_before};
                $content .= "\n".$sections->{$sec_before};
                $done{$sec_before} = 1;
            }
        }
        next if $done{$sec};
        $content .= "\n".$sections->{$sec};
        $done{$sec} = 1;
    }
    return $content;
}

sub set_next_points {
    my $previous;
    my $first_point = {};
    for my $code (sort { $a <=> $b } keys %{$POINTS_BY_CODE}) {
        push @POINTS_SORTED, $POINTS_BY_CODE->{$code};
        $POINTS_BY_CODE->{$previous}->{next_point} = $POINTS_BY_CODE->{$code}
            if defined $previous;
        # The first code we encounter will be the lowest, so set $first_point
        if (!defined $previous) {
            say "setting first point to $code";
            $first_point = $POINTS_BY_CODE->{$code};
        }
        $previous = $code;
    }
    return $first_point;
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
        croak "$code is already defined" if defined $POINTS_BY_CODE->{$code};
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
        my ($range, $pname) = split / \s* [;#] \s* /x; # range, property name
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
    my $num_keys = scalar keys %{$base};
    # wrap the provided object as the enum key in a new one
    $base = { enum => $base, name => $pname };
    each_line("extracted/Derived$fname", sub { $_ = shift;
        my ($range, $class) = split / \s* [;#] \s* /x;
        unless (exists $base->{enum}->{$class}) {
            # haven't seen this property's value before
            # add it, and give it an index.
            print "\n  adding derived property for $pname: $num_keys $class" if $DEBUG;
            $base->{enum}->{$class} = $num_keys++;
        }
    });
    register_keys_and_set_bit_width($base, $num_keys);
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

sub register_keys_and_set_bit_width {
    my ($base, $num_keys) = @_;
    my $reg = register_keys($base);
    $base->{bit_width} = least_int_ge_lg2($reg);
    print "\n    bitwidth: ", $base->{bit_width}, "\n" if $DEBUG;

    croak "The number of keys and the number of \$num_keys do not match. Keys: $reg \$num_keys: $num_keys"
        if (defined $num_keys and $reg != $num_keys);
    return;
}

sub enumerated_property {
    my ($fname, $pname, $base, $value_index, $type, $is_hex) = @_;
    my $num_keys = scalar keys %{$base};
    $type = 'string' unless $type;
    $base = { enum => $base, name => $pname, type => $type };
    each_line($fname, sub { $_ = shift;
        my @vals = split / \s* [#;] \s* /x;
        my $range = $vals[0];
        my $value = ref $value_index
            ? $value_index->(\@vals)
            : $vals[$value_index];
        $value = hex $value if $is_hex;
        my $index = $base->{enum}->{$value};
        if (not defined $index) {
            # Haven't seen this property value before. Add it, and give it an index.
            print("\n  adding enum property for $pname: $num_keys $value") if $DEBUG;
            ($base->{enum}->{$value} = $index = $num_keys++);
        }
        apply_to_range($range, sub {
            my $point = shift;
            $point->{$pname} = $index; # set the property's value index
        });
    });
    register_keys_and_set_bit_width($base, $num_keys);
    register_enumerated_property($pname, $base);
    return;
}

sub least_int_ge_lg2 {
    return int(log(shift)/log(2) - 0.00001) + 1;
}

sub each_line {
    my ($fname, $fn, $force) = @_;
    progress("done.\nprocessing $fname.txt...");
    for my $line (@{read_file("UNIDATA/$fname.txt")}) {
        chomp $line;
        # If it's forced, or it is a proper line (line is not blank and doesn't start with a #)
        $fn->($line) if $force || $line !~ / ^ (?: [#] | \s* $ ) /x;
    }
    return;
}

sub allocate_bitfield {
    my ($first_point) = @_;
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
                    while ($BITFIELD_CELL_BITWIDTH <= $bit_offset) {
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
    $first_point->{bitfield_width}    = $word_offset + 1;
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
    # normal stretches of codepoints. $low and $high are the
    # indexes into $extents we're supposed to subdivide.
    # protocol: start output with a newline; don't end with a newline or indent
    my ($extents, $low, $mid, $high, $indent) = @_;
    #${indent} /* got  $low  $mid  $high  */\n";
    return emit_extent_fate($extents->[$low], $indent) if $low == $high;
    $mid = $high if $low == $mid;
    my $new_mid_high = int(($high + $mid) / 2);
    my $new_mid_low = int(($mid - 1 + $low) / 2);
    my $high_str = emit_binary_search_algorithm($extents, $mid, $new_mid_high,
        $high, "    $indent");
    my $low_str = emit_binary_search_algorithm($extents, $low, $new_mid_low,
        $mid - 1, "    $indent");
    my $rtrn = sprintf( <<"END", $extents->[$mid]->{code}, ($extents->[$mid]->{name} || 'NULL'));

${indent}if (codepoint >= 0x%X) { /* %s */$high_str
${indent}}
${indent}else {$low_str
${indent}}
END
    chomp $rtrn;
    return $rtrn;
}
# Constants
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
            grep { / code | fate | name | bitfield /x }
                sort(split / ^ /xm, "EXTENT " . Dumper($extent));
    }
    push @$extents, $extent;
    return;
}
# Used in emit_codepoints_and_planes to push the codepoints name onto bitfield_index_lines
sub ecap_push_name_line {
    my ($name_lines, $name, $point, $bitfield_index_lines, $index) = @_;
    my $bytes;
    if (!defined $name) {
        push @$bitfield_index_lines, "0";
        push @$name_lines, "NULL";
    }
    else {
        $bytes = length($point->{name}) + 1; # length + 1 for the NULL
        push @$bitfield_index_lines, "/*$$index*/$point->{bitfield_index}/* $point->{code_str} */";
        push @$name_lines, "/*$$index*/\"$point->{name}\"/* $point->{code_str} */";
    }
    ++$$index;
    $bytes += 2; # hopefully these are compacted since they are trivially aligned being two bytes
    $bytes += 8; # 8 for the pointer
    return $bytes;
}
sub emit_codepoints_and_planes {
    my ($first_point) = @_;
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
    my $toadd = undef;
    my @name_lines;
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
                $code_offset = $last_point->{code} - @name_lines + 1;
                $last_point->{fate_type}   = $FATE_SPAN;
                $last_point->{fate_offset} = $code_offset;
                $last_point->{fate_really} = $last_point->{code} - $code_offset;
                $code_offset += $span_length - 1;
                $toadd = $point;
                $span_length = 0;
            }
            for (; 1 < $span_length; $span_length--) {
                # catch up to last code
                $last_point = $last_point->{next_point};
                $bytes += ecap_push_name_line(\@name_lines, $last_point->{name}, $last_point, \@bitfield_index_lines, \$index);
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
            $bytes += ecap_push_name_line(\@name_lines, undef, $point, \@bitfield_index_lines, \$index);
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
        $bytes += ecap_push_name_line(\@name_lines, $point->{name}, $point, \@bitfield_index_lines, \$index);
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
            stack_lines(\@name_lines, ",", ",\n    ", 0, 0).
            "\n};";
    $DB_SECTIONS->{BBB_codepoint_bitfield_indexes} =
        "static const MVMuint16 codepoint_bitfield_indexes[$index] = {\n    ".
            stack_lines(\@bitfield_index_lines, ",", ",\n    ", 0, $WRAP_TO_COLUMNS).
            "\n};";
    $H_SECTIONS->{codepoint_names_count} = "#define MVM_CODEPOINT_NAMES_COUNT $index";
    if ($DEBUG) {
        $LOG =~ s/ ( 'fate_really' \s => \s ) (\d+) /$1$name_lines[$2]/xg;
    }
    return $extents
}

sub emit_codepoint_row_lookup {
    my $extents = shift;
    my $SMP_start;
    my $i = 0;
    for (@$extents) {
        # handle the first recursion specially to optimize for most common BMP lookups
        if (0x10000 <= $_->{code}) {
            $SMP_start = $i;
            last;
        }
        $i++;
    }
    my $plane_0      = emit_binary_search_algorithm($extents, 0, 1, $SMP_start - 1, "        ");
    my $other_planes = emit_binary_search_algorithm($extents, $SMP_start,
        int(($SMP_start + @$extents - 1)/2), @$extents - 1, "            ");
    chomp(my $out = <<'END');
static MVMint32 MVM_codepoint_to_row_index(MVMThreadContext *tc, MVMint64 codepoint) {

    MVMint32 plane = codepoint >> 16;

    if (MVM_UNLIKELY(codepoint < 0)) {
        MVM_exception_throw_adhoc(tc,
            "Internal Error: MVM_codepoint_to_row_index call requested a synthetic codepoint that does not exist.\n"
            "Requested synthetic %%"PRId64" when only %%"PRId32" have been created.",
            -codepoint, tc->instance->nfg->num_synthetics);
    }

    if (MVM_LIKELY(plane == 0)) {%s
    }
    else {
        if (MVM_UNLIKELY(plane < 0 || plane > 16 || codepoint > 0x10FFFD)) {
            return -1;
        }
        else {%s
        }
    }
}
END
    return sprintf $out, $plane_0, $other_planes;
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
    return $out;
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
    return $GCB_h;
}
sub emit_property_value_lookup {
    my $allocated = shift;
    my $enumtables = "\n\n";
    my $hout = "typedef enum {\n";
    chomp(my $int_out = <<'END');

static MVMint32 MVM_unicode_get_property_int(MVMThreadContext *tc, MVMint64 codepoint, MVMint64 property_code) {
    MVMint32 result_val = 0; /* we'll never have negatives, but so */
    MVMint32 codepoint_row = MVM_codepoint_to_row_index(tc, codepoint);
    MVMuint16 bitfield_row;
    /* If codepoint is not found in bitfield rows */
    if (codepoint_row == -1) {
        /* Unassigned codepoints have General Category Cn. Since this returns 0
         * for unknowns, unless we return 1 for property C then these unknows
         * won't match with <:C> */
        return property_code == MVM_UNICODE_PROPERTY_C ? 1 : 0;
    }
    bitfield_row = codepoint_bitfield_indexes[codepoint_row];

    switch (MVM_EXPECT(property_code, MVM_UNICODE_PROPERTY_GENERAL_CATEGORY)) {
        case 0: return 0;
END

    chomp(my $str_out = <<'END');

static MVMint32 MVM_codepoint_to_row_index(MVMThreadContext *tc, MVMint64 codepoint);

static const char *bogus = "<BOGUS>"; /* only for table too short; return null string for no mapping */

static const char* MVM_unicode_get_property_str(MVMThreadContext *tc, MVMint64 codepoint, MVMint64 property_code) {
    MVMuint32 switch_val = (MVMuint32)property_code;
    MVMint32 result_val = 0; /* we'll never have negatives, but so */
    MVMint32 codepoint_row;
    MVMuint16 bitfield_row = 0;

    if (switch_val == MVM_UNICODE_PROPERTY_BLOCK) {
        MVMint32 ord = codepoint;
        struct UnicodeBlock *block = bsearch(&ord, unicode_blocks, sizeof(unicode_blocks) / sizeof(struct UnicodeBlock), sizeof(struct UnicodeBlock), block_compare);
        int num = ((char*)block - (char*)unicode_blocks) / sizeof(struct UnicodeBlock);
        if (block) {
            return block ? Block_enums[num+1] : Block_enums[0];
        }
    }
    codepoint_row = MVM_codepoint_to_row_index(tc, codepoint);
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
    $str_out  .= sprintf $default_return, q("");
    $hout     .= "} MVM_unicode_property_codes;";

    $DB_SECTIONS->{MVM_unicode_get_property_int} = $enumtables . $str_out . $int_out;
    $H_SECTIONS->{property_code_definitions} = $hout;
    return EPVL_gen_pvalue_defines('MVM_UNICODE_PROPERTY_GENERAL_CATEGORY', 'General_Category', 'GC') .
        EPVL_gen_pvalue_defines('MVM_UNICODE_PROPERTY_GRAPHEME_CLUSTER_BREAK', 'Grapheme_Cluster_Break', 'GCB') .
        EPVL_gen_pvalue_defines('MVM_UNICODE_PROPERTY_DECOMPOSITION_TYPE', 'Decomposition_Type', 'DT') .
        EPVL_gen_pvalue_defines('MVM_UNICODE_PROPERTY_CANONICAL_COMBINING_CLASS', 'Canonical_Combining_Class', 'CCC') .
        EPVL_gen_pvalue_defines('MVM_UNICODE_PROPERTY_NUMERIC_TYPE', 'Numeric_Type', 'Numeric_Type');
}

sub emit_block_lookup {
    my @blocks;
    each_line('Blocks', sub {
        my $line = shift;
        my ($from, $to, $block_name) = $line =~ / ^ (\w+) .. (\w+) ; \s (.+) /x;
        if ($from && $to && $block_name) {
            $block_name =~ s/ [-_\s] //xg;
            my $alias_name = lc $block_name;
            my $block_len  = length $block_name;
            my $alias_len  = length $alias_name;
            if ($block_len && $alias_len) {
                push @blocks, "    { 0x$from, 0x$to, \"$block_name\", $block_len, \"$alias_name\", $alias_len }";
            }
        }
        else {
            croak "Failed to parse Blocks.txt. Line:\n$line";
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

static void generate_codepoints_by_name(MVMThreadContext *tc) {
    MVMint32 extent_index = 0;
    MVMint32 codepoint = 0;
    MVMint32 codepoint_table_index = 0;
    MVMint16 i = num_unicode_namealias_keypairs - 1;

    for (; extent_index < MVM_NUM_UNICODE_EXTENTS; extent_index++) {
        MVMint32 length;
        codepoint = codepoint_extents[extent_index][0];
        length = codepoint_extents[extent_index + 1][0] - codepoint_extents[extent_index][0];
        if (codepoint_table_index >= MVM_CODEPOINT_NAMES_COUNT)
            continue;
        switch (codepoint_extents[extent_index][1]) {
            /* Fate Normal */
            case $FATE_NORMAL: {
                MVMint32 extent_span_index = 0;
                codepoint_table_index = codepoint_extents[extent_index][2];
                for (; extent_span_index < length
                    && codepoint_table_index < MVM_CODEPOINT_NAMES_COUNT; extent_span_index++) {
                    const char *name = codepoint_names[codepoint_table_index];
                    /* We want to skip various placeholder names that are duplicated:
                     * <control> <CJK UNIFIED IDEOGRAPH> <CJK COMPATIBILITY IDEOGRAPH>
                     * <surrogate> <TANGUT IDEOGRAPH> <private-use> */
                    if (name && *name != '<') {
                        MVM_uni_hash_insert(tc, &tc->instance->codepoints_by_name, name, codepoint);
                    }
                    codepoint++;
                    codepoint_table_index++;
                }
                break;
            }
            /* Fate NULL */
            case $FATE_NULL:
                break;
            /* Fate Span */
            case $FATE_SPAN: {
                const char *name = codepoint_names[codepoint_table_index];
                if (name && *name != '<') {
                    MVM_uni_hash_insert(tc, &tc->instance->codepoints_by_name, name, codepoint);
                }
                codepoint_table_index++;
                break;
            }
        }
    }
    for (; i >= 0; i--) {
        MVM_uni_hash_insert(tc, &tc->instance->codepoints_by_name, uni_namealias_pairs[i].name, uni_namealias_pairs[i].codepoint);
    }

}
END
    $DB_SECTIONS->{names_hash_builder} = $out;
    return;
}

sub emit_unicode_property_keypairs {
    my $prop_codes = {};
    # Add property name aliases to $PROP_NAMES
    each_line('PropertyAliases', sub { $_ = shift;
        my @aliases = split / \s* [#;] \s* /x;
        for my $name (@aliases) {
            if (exists $PROP_NAMES->{$name}) {
                for my $al (@aliases) {
                    $prop_codes->{$al} = $name;
                    do_for_each_case($al, sub { $_ = shift;
                        $PROP_NAMES->{$_} = $PROP_NAMES->{$name};
                    });
                }
                last;
            }
        }
    });
    my %aliases;
    my %lines_h;
    # Get the aliases to put into Property Name Keypairs
    each_line('PropertyValueAliases', sub { $_ = shift;
        # Capture heading lines: `# Bidi_Control (Bidi_C)`
        # TODO maybe best to get this data from PropertyAliases?
        # emit_unicode_property_keypairs() in general can be simplified more
        if (/ ^ [#] \s (\w+) \s [(] (\w+) [)] /x) {
            $aliases{$2} = [$1];
            return;
        }
        return if / ^ (?: [#] | \s* $ ) /x; # Return if comment or empty line
        my @pv_alias_parts = split / \s* [#;] \s* /x;
        # Since it's the first field in the file, $propname is actually the short
        # property name. So 'sc' or 'gc' for example (Script, General_Category respectively).
        my $propname = shift @pv_alias_parts;
        if (exists $PROP_NAMES->{$propname}) {
            my $prop_val = $PROP_NAMES->{$propname};
            if (($pv_alias_parts[0] eq 'Y'   || $pv_alias_parts[0] eq 'N') &&
                ($pv_alias_parts[1] eq 'Yes' || $pv_alias_parts[1] eq 'No')) {
                for my $name ($propname, @{$aliases{$propname} // []}) {
                    do_for_each_case($name, sub { $_ = shift;
                        return if exists $PROP_NAMES->{$_}; # return because we'll already add
                        # the ones from $PROP_NAMES later
                        $lines_h{$propname}->{$_} = "{\"$_\",$prop_val}";
                    });
                }
                return
            }
            # Orig Line: `gc ; C ; Other  # Cc | Cf | Cn | Co | Cs`
            if ($pv_alias_parts[-1] =~ / [|] /x) { # it's a union
                # Pop the part after the `#` in the original line off
                pop @pv_alias_parts; # i.e. `Cc | Cf | Cn | Co | Cs`
                my $unionname = $pv_alias_parts[0]; # i.e. `C`
                croak "Couldn't find Binary Property (union) `$unionname`"
                    unless exists $BINARY_PROPERTIES->{$unionname};
                $prop_val = $BINARY_PROPERTIES->{$unionname}->{field_index};
                for my $alias_part (@pv_alias_parts) {
                    do_for_each_case($alias_part, sub { $_ = shift;
                        return if exists $PROP_NAMES->{$_};
                        $lines_h{$propname}->{$_} = "{\"$_\",$prop_val}";
                    });
                }
            }
            else {
                for my $alias_part (@pv_alias_parts) {
                    # If the property alias name conflicts with a Property Name
                    # don't put it in %lines_h or it will cause conflicts
                    next if exists $PROP_NAMES->{$alias_part};
                    $lines_h{$propname}->{$alias_part} = "{\"$alias_part\",$prop_val}";
                    push @{ $aliases{$propname} }, $alias_part;
                }
            }
        }
    }, 1);
    # Fix to ensure space has the same propcode as White_Space
    $PROP_NAMES->{space} = $PROP_NAMES->{White_Space};
    my @lines;
    my %done;
    # Copy the keys in $PROP_NAMES first
    for my $key (sort keys %$PROP_NAMES) {
        do_for_each_case($key, sub { $_ = shift;
            $done{$_} ||= push @lines, "{\"$_\",$PROP_NAMES->{$key}}";
        });
    }
    # Then copy the rest. Because we use `$done{} ||= push @lines` it will only
    # push to @lines if it is not in %done already.
    for my $propname (qw(_custom_ gc sc), sort keys %lines_h) {
        for (sort keys %{$lines_h{$propname}}) {
            $done{$_} ||= push @lines, $lines_h{$propname}->{$_};
        }
    }
    # Make sure General_Category and Script Property values are added first.
    # These are the only ones (iirc) that are guaranteed in Perl 6.
    for my $key (qw(gc sc), sort keys %$PROP_NAMES) {
        for (@{ $aliases{$key} }) {
            next if $PROP_NAMES->{$_};
            do_for_each_case($_, sub { $_ = shift;
                $done{$_} ||= push @lines, "{\"$_\",$PROP_NAMES->{$key}}";
            });
        }
    }
    # Sort the @lines array so it always appears in the same order
    @lines = sort @lines;
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
    return $prop_codes;
}
sub add_unicode_sequence {
    my ($filename, $named_sequences) = @_;
    each_line($filename, sub { my $line = shift;
        return if $line =~ /^ [#] /x or $line =~ /^ \s* $/x;
        my (@list, $hex_ords, $type, $name);
        @list = split / ; | \s{3}[#] /x, $line;
        if ($filename =~ / emoji /x) {
            $hex_ords = trim shift @list;
            $type     = trim shift @list;
            $name     = trim shift @list;
            # Don't process non sequences
            return if $hex_ords =~ /\.\./;
            return if $hex_ords !~ / /;
        }
        else {
            $name     = trim shift @list;
            $hex_ords = trim shift @list;
            $type     = 'NamedSequences';
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
        # since they seperate seperate named items in ISO notation that Raku uses
        $name =~ s/,//xg;
        $named_sequences->{$name}->{'type'} = $type;
        # Only push if we haven't seen this already
        if (!$named_sequences->{$name}->{'ords'}) {
            for my $hex (split ' ', $hex_ords) {
                push @{$named_sequences->{$name}->{'ords'}}, hex $hex;
            }
        }
    } );
    return $named_sequences;
}
sub emit_unicode_sequence_keypairs {
    my ($named_sequences) = @_;
    my $count = 0;
    my $seq_c_hash_str = '';
    my @seq_c_hash_array;
    my $enum_table = '';
    my $string_seq = "/* Unicode sequences such as Emoji sequences */\n";
    for my $thing ( sort keys %$named_sequences ) {
        my $seq_name = "uni_seq_$count";
        $string_seq .=  "static const MVMint32 $seq_name\[] = {";
        $seq_c_hash_str .= '{"' . $thing . '",' . $count . '},';
        my $ord_data;
        for my $ord ( @{$named_sequences->{$thing}->{'ords'}} ) {
            $ord_data .= sprintf "0x%X,", $ord;
        }
        $ord_data = scalar @{$named_sequences->{$thing}->{'ords'}} . ',' . $ord_data;
        $string_seq .= $ord_data;
        $ord_data   =~ s/ , $ //x;
        $string_seq =~ s/ , $ //x;
        $string_seq = $string_seq . "}; " . "/* $thing */ /*" . $named_sequences->{$thing}->{'type'} . " */\n";
        $enum_table .= "$seq_name,\n";
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
    $seq_c_hash_str = "static const MVMUnicodeNamedValue uni_seq_pairs[$count] = {\n    $seq_c_hash_str";

    $enum_table =~ s/ \s* , \s* $ /};/x;
    $enum_table = "static const MVMint32 * uni_seq_enum[$count] = {\n" . $enum_table;
    $DB_SECTIONS->{uni_seq} = $seq_c_hash_str . $string_seq . $enum_table;
    return "#define num_unicode_seq_keypairs $count \n";
}
sub gen_name_alias_keypairs {
    my $count = 0;
    my $seq_c_hash_str;
    my @seq_c_hash_array;
    for my $thing ( sort keys %$ALIAS_TYPES ) {
        my $ord_data;
        my $ord = $ALIAS_TYPES->{$thing}->{'code'};
        $ord_data .= sprintf '0x%X,', $ord;
        $seq_c_hash_str .= qq({"$thing",$ord_data) . length($thing) . '},';
        $ord_data =~ s/ , $ //x;
        my $type = $ALIAS_TYPES->{$thing}->{'type'};
        $count++;
        if ( length $seq_c_hash_str > 80 ) {
            push @seq_c_hash_array, $seq_c_hash_str . "\n";
            $seq_c_hash_str = '';
        }
    }
    push @seq_c_hash_array, "$seq_c_hash_str\n";
    $seq_c_hash_str = join '    ', @seq_c_hash_array;
    $seq_c_hash_str =~ s/ \s* , \s* $ //x;
    chomp($DB_SECTIONS->{Auni_namealias} = <<"END");
/* Unicode Name Aliases */
static const MVMUnicodeNamedAlias uni_namealias_pairs[$count] = {
    $seq_c_hash_str
};
END
    return <<"END"
#define num_unicode_namealias_keypairs $count
struct MVMUnicodeNamedAlias {
    char *name;
    MVMGrapheme32 codepoint;
    MVMint16 strlen;
};
typedef struct MVMUnicodeNamedAlias MVMUnicodeNamedAlias;
END
}

sub set_lines_for_each_case {
    my ($default, $propname, $prop_val, $hash, $maybe_propcode) = @_;
    my $propcode = $maybe_propcode // $PROP_NAMES->{$propname} // $PROP_NAMES->{$default} // croak;
    # Workaround to 'space' not getting added here
    $hash->{$propname}->{space} = "{\"$propcode-space\",$prop_val}"
        if $default eq 'White_Space' and $propname eq '_custom_';
    do_for_each_case($default, sub { $_ = shift;
        $hash->{$propname}->{$_} = "{\"$propcode-$_\",$prop_val}";
    });
    return $propcode;
}
sub do_for_each_case {
    my ($str, $sub) = @_;
    my $str2 = $str;
    $sub->($str);                         # Foo_Bar (original)
    $sub->($str)  if $str  =~ s/_//xg;    # FooBar
    $sub->($str)  if $str  =~ y/A-Z/a-z/; # foobar
    $sub->($str2) if $str2 =~ y/A-Z/a-z/; # foo_bar
    return $str;
}
sub emit_unicode_property_value_keypairs {
    my ($prop_codes) = @_;
    my @lines = ();
    my $property;
    my %lines;
    my %aliases;
    for my $property (sort keys %$BINARY_PROPERTIES) {
        my $prop_val = ($PROP_NAMES->{$property} << 24) + 1;
        my $propcode = set_lines_for_each_case($property, '_custom_', $prop_val, \%lines);
        my $lc_thing = lc $property;
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
                set_lines_for_each_case($t, '_custom_', $prop_val, \%lines, $propcode)
            }
        }
    }
    for (sort keys %$ENUMERATED_PROPERTIES) {
        my $enum = $ENUMERATED_PROPERTIES->{$_}->{enum};
        my $toadd = {};
        for (sort keys %$enum) {
            my $key = lc $_;
            $key =~ s/[-_\s]/./xg;
            $toadd->{$key} = $enum->{$_};
        }
        for (sort keys %$toadd) {
            $enum->{$_} = $toadd->{$_};
        }
    }
    croak "lines didn't get anything in it" if !%lines;
    my %done;
    each_line('PropertyValueAliases', sub { $_ = shift;
        if (/ ^ [#] \s (\w+) \s [(] (\w+) [)] /x) {
            $aliases{$2} = $1;
            return
        }
        return if / ^ (?: [#] | \s* $ ) /x;
        my @pv_alias_parts = split(/ \s* [#;] \s* /x);
        for my $part (@pv_alias_parts) {
            $part = trim($part);
            croak if $part =~ / [;] /x;
        }
        my $propname = shift @pv_alias_parts;
        $propname = trim $propname;
        if (exists $PROP_NAMES->{$propname}) {
            my $prop_val = $PROP_NAMES->{$propname} << 24;
            # emit binary properties
            if (($pv_alias_parts[0] eq 'Y' || $pv_alias_parts[0] eq 'N') && ($pv_alias_parts[1] eq 'Yes' || $pv_alias_parts[1] eq 'No')) {
                $prop_val++; # one bit width
                for ($propname, ($aliases{$propname} // ())) {
                    set_lines_for_each_case($_, $propname, $prop_val, \%lines);
                }
                return
            }
            if ($pv_alias_parts[-1] =~ /\|/x) { # it's a union
                pop @pv_alias_parts;
                my $unionname = $pv_alias_parts[0];
                if (exists $BINARY_PROPERTIES->{$unionname}) {
                    my $prop_val = $BINARY_PROPERTIES->{$unionname}->{field_index} << 24;
                    my $value    = $BINARY_PROPERTIES->{$unionname}->{bit_width};
                    for my $i (@pv_alias_parts) {
                        set_lines_for_each_case($i, $propname, $prop_val + $value, \%lines);
                        do_for_each_case($i, sub { $_ = shift;
                            $done{"$propname$_"} = push @lines, $lines{$propname}->{$_};
                        });
                        $_ = $i; # For the conditional / ^ letter $ /x below
                    }
                    croak Dumper($propname) if / ^ letter $ /x;
                }
                return
            }
            my $key   = $prop_codes->{$propname};
            my $found = 0;
            my $enum  = $ALL_PROPERTIES->{$key}->{'enum'};
            croak $propname unless $enum;
            my $value;
            for (@pv_alias_parts) {
                my $alias = $_;
                $alias    =~ s/[-_\s]/./xg;
                $alias    = lc($alias);
                if (exists $enum->{$alias}) {
                    $value = $enum->{$alias};
                    last;
                }
            }
            unless (defined $value) {
                print "\nNote: couldn't resolve property $propname property value alias (you can disregard this for now).";
                return;
            }
            for (@pv_alias_parts) {
                s/[-\s]/./xg;
                next if /[.|]/x;
                set_lines_for_each_case($_, $propname, $prop_val + $value, \%lines);
            }
        }
    }, 1);

    # Aliases like L appear in several categories, but we prefere gc and sc.
    for my $propname (qw(_custom_ gc sc), sort keys %lines) {
        for (sort keys %{$lines{$propname}}) {
            $done{"$propname$_"} ||= push @lines, $lines{$propname}->{$_};
        }
    }
    my $out = "\n" .
    "static const MVMUnicodeNamedValue unicode_property_value_keypairs[" . scalar(@lines) . "] = {\n" .
    "    " . stack_lines(\@lines, ",", ",\n    ", 0, $WRAP_TO_COLUMNS) . "\n" .
    "};";
    $DB_SECTIONS->{BBB_unicode_property_value_keypairs} = $out;
    return "\n#define num_unicode_property_value_keypairs " . scalar(@lines) . "\n";
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
        my @decomp = split / \s+ /x, $decomp_spec;
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
    my @lines = <$FILE>;
    close $FILE;
    return \@lines;
}

sub write_file {
    my ($fname, $contents) = @_;
    open my $FILE, '>', $fname or croak "Couldn't open file '$fname': $!";
    binmode $FILE, ':encoding(UTF-8)';
    # Ensure generated files always end with a newline.
    $contents .= "\n"
        unless $contents =~ /\n\z/;
    print $FILE trim_trailing($contents);
    close $FILE;
    return;
}

sub register_union {
    my ($unionname, $unionof, $gc_alias_checkers) = @_;
    register_binary_property($unionname);
    push @$gc_alias_checkers, sub {
        return ((shift) =~ /^(?:$unionof)$/)
            ? "$unionname" : 0;
    };
    return;
}

sub UnicodeData {
    my ($bidi_classes, $general_categories, $ccclasses) = @_;
    $GENERAL_CATEGORIES = $general_categories;
    register_binary_property('Any');
    my @gc_alias_checkers;
    each_line('PropertyValueAliases', sub { $_ = shift;
        my @pv_alias_parts = split / \s* [#;] \s* /x;
        # Make sure everything is trimmed
        for my $part (@pv_alias_parts) {
            $part = trim $part;
        }
        my $propname = shift @pv_alias_parts;
        return if ($pv_alias_parts[0] eq 'Y'   || $pv_alias_parts[0] eq 'N')
               && ($pv_alias_parts[1] eq 'Yes' || $pv_alias_parts[1] eq 'No');
        if ($pv_alias_parts[-1] =~ /[|]/x) { # it's a union
            my $unionname = $pv_alias_parts[0];
            my $unionof   = pop @pv_alias_parts;
            $unionof      =~ s/ \s+ //xg;
            register_union($unionname, $unionof, \@gc_alias_checkers);
        }
    });
    register_union('Assigned', 'C[cfosn]|L[lmotu]|M[cen]|N[dlo]|P[cdefios]|S[ckmo]|Z[lps]', \@gc_alias_checkers);
    my $ideograph_start;
    my $case_count   = 1;
    my $decomp_keys  = [ '' ];
    my $decomp_index = 1;
    my $s = sub {
        $_ = shift;
        my ($code_str, $name, $gencat, $ccclass, $bidiclass, $decmpspec,
            $num1, $num2, $num3, $bidimirrored, $u1name, $isocomment,
            $suc, $slc, $stc) = split ';';

        my $code = hex $code_str;
        my $plane_num = $code >> 16;
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
            $decmpspec =~ s/ [<] \w+ [>] \s+ //x;
            $point->{Decomp_Spec} = $decomp_index;
            $decomp_keys->[$decomp_index++] = $decmpspec;
        }
        if ($suc || $slc || $stc) {
            $point->{Case_Change_Index} = $case_count++;
        }
        for my $checker (@gc_alias_checkers) {
            my $res = $checker->($gencat);
            $point->{$res} = 1 if $res;
        }
        if ($name =~ /(Ideograph|Syllable|Private|Surrogate) (\s|.)*? First/x) {
            $ideograph_start = $point;
            $point->{name}   =~ s/, First//;
        }
        elsif ($name =~ / First/) {
          die "Looks like support for a new thing needs to be added: $name"
        }
        elsif ($ideograph_start) {
            $point->{name} = $ideograph_start->{name};
            my $current    = $ideograph_start;
            for (my $count = $ideograph_start->{code} + 1; $count < $point->{code}; $count++) {
                $current = get_next_point($count, 1);
                for (sort keys %$ideograph_start) {
                    next if $_ eq "code" || $_ eq "code_str";
                    $current->{$_} = $ideograph_start->{$_};
                }
            }
            $ideograph_start = 0;
        }
        if (substr($point->{name}, 0, 1) eq '<') {
            if ($point->{name} eq '<CJK Ideograph Extension A>'
             || $point->{name} eq '<CJK Ideograph>'
             || $point->{name} eq '<CJK Ideograph Extension B>'
             || $point->{name} eq '<CJK Ideograph Extension C>'
             || $point->{name} eq '<CJK Ideograph Extension D>'
             || $point->{name} eq '<CJK Ideograph Extension E>'
             || $point->{name} eq '<CJK Ideograph Extension F>'
             || $point->{name} eq '<CJK Ideograph Extension G>'
             || $point->{name} eq '<CJK Ideograph Extension H>')
            {
                $point->{name} = '<CJK UNIFIED IDEOGRAPH>'
            }
            elsif ($point->{name} eq '<Tangut Ideograph>') {
                $point->{name} = '<TANGUT IDEOGRAPH>';
            }
            elsif ($point->{name} eq '<Tangut Ideograph Supplement>') {
                $point->{name} = '<TANGUT IDEOGRAPH>';
            }
            elsif ($point->{name} eq '<Hangul Syllable>') {
                $point->{name} = '<HANGUL SYLLABLE>';
            }
            elsif ($point->{name} eq '<Private Use>'
                || $point->{name} =~ /^<Plane \d+ Private Use>$/)
            {
                die unless $gencat eq 'Co';
                $point->{name} = '<private-use>';
            }
            elsif ($point->{name} eq '<Non Private Use High Surrogate>'
                || $point->{name} eq '<Private Use High Surrogate>'
                || $point->{name} eq '<Low Surrogate>')
            {
                die unless $gencat eq 'Cs';
                $point->{name} = '<surrogate>';
            }
            my $instructions_line_no = __LINE__;
            my $instructions_1 = <<~'END';
              Don't add anything here *unless*
              Unicode has added a new "Name Derevation Rule Prefix String".
              Extensions to *existing* prefixes should be normalized from their
              format in the data file to the correct prefix string.
              For example: if a new CJK Unified Ideograph extension is added
              <CJK Ideograph Extension X> **AND** the "Name Derivation Rule Prefix Strings"
              END
            my $instructions_2 = <<~'END';
              the table in the unicode ch04 documentation specifies the codepoint
              in question results in prefix is CJK UNIFIED IDEOGRAPH then you can
              modify the normalizing code above to change <CJK Ideograph Extension X>
              into <CJK UNIFIED IDEOGRAPH>, which is already whitelisted
              END
            if ($point->{name} eq '<HANGUL SYLLABLE>'
             || $point->{name} eq '<control>'
             || $point->{name} eq '<CJK UNIFIED IDEOGRAPH>'
             || $point->{name} eq '<private-use>'
             || $point->{name} eq '<surrogate>'
             || $point->{name} eq '<TANGUT IDEOGRAPH>') {
            }
            else {
                die <<~"END";
                $point->{name} encountered. code_str: '$code_str'
                ##############################
                IMPORTANT: READ BELOW
                Make sure to check
                https://www.unicode.org/versions/latest/bookmarks.html
                and click the link for Table 4-8. Name Derivation Rule Prefix Strings,
                using this table as a reference along with
                $instructions_2
                Read more about this on comment line $instructions_line_no
                You will likely have to make a change to MVM_unicode_get_name() and add a test to nqp
                END
            }
        }
        if ($point->{name} =~ /^CJK COMPATIBILITY IDEOGRAPH-([A-F0-9]+)$/) {
            die unless sprintf("%.4X", $code) eq $1;
            $point->{name} = "<CJK COMPATIBILITY IDEOGRAPH>";
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
        my ($left_str, $type, $right) = split / \s* ; \s* /x;
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
        s/ [#] .+ //x;
        my ($code_str, $lower, $title, $upper, $cond) = split / \s* ; \s* /x;
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
    register_enumerated_property($_, {
        enum => $trinary_values,
        bit_width => 2,
        'keys' => ['N','Y','M']
    }) for sort keys %$trinary;
    each_line('DerivedNormalizationProps', sub { $_ = shift;
        my ($range, $property_name, $value) = split / \s* [;#] \s* /x;
        if (exists $binary->{$property_name}) {
            $value = 1;
        }
        elsif (exists $inverted_binary->{$property_name}) {
            $value = undef;
        }
        elsif (exists $trinary->{$property_name}) {
            $value = $trinary_values->{$value};
        }
        elsif ($property_name eq 'NFKC_Casefold') { # XXX see how this differs from CaseFolding.txt
        #    my @parts = split ' ', $value;
        #    $value = \@parts;
        }
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
        my ($code_str, $name) = split / \s* [#;] \s* /x;
        apply_to_range($code_str, sub {
            my $point = shift;
            $point->{Jamo_Short_Name} = $name;
        });
    });
    my @hangul_syllables;
    for my $key (sort keys %{$POINTS_BY_CODE}) {
        if ($POINTS_BY_CODE->{$key}->{name} and $POINTS_BY_CODE->{$key}->{name} eq '<HANGUL SYLLABLE>') {
            push @hangul_syllables, $key;
        }
    }
    my $hs = join ',', @hangul_syllables;
    my $out = `rakudo -e 'my \@cps = $hs; for \@cps -> \$cp { \$cp.chr.NFD.list.join(",").say };'`;
    die "Problem running rakudo to process Hangul syllables: \$? was $?"
        if $?;
    my @out_lines = split "\n", $out;
    my $i = 0;
    for my $line (@out_lines) {
        my $final_name = 'HANGUL SYLLABLE ';
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
        # implicit weights are handled in ./tools/Generate-Collation-Data.raku
        return if $line =~ s/ ^ \@implicitweights \s+ //xms;
        return if $line =~ / ^ \s* [#@] /x or $line =~ / ^ \s* $ /x; # Blank/comment lines
        ($code, $temp) = split / [;#]+ /x, $line;
        $code = trim $code;
        my @codes = split ' ', $code;
        # We support collation for multiple codepoints in ./tools/Generate-Collation-Data.raku
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
        #register_enumerated_property($base->{name}, $base); # Uncomment to make an int enum
        #register_keys_and_set_bit_width($base, $index->{$base->{name}}->{j}); # Uncomment to make an int enum
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
