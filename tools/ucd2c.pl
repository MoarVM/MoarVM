#!/usr/bin/env perl
# Make C versions of the Unicode tables.

# NOTE: Before running this program, run tools/UCD-download.raku from the top
# level of your MoarVM checkout to download all of the Unicode data files and
# extract them into a UNIDATA directory, which that script will create for you.

use 5.014;
use strict;
use warnings;
use utf8;
use feature 'unicode_strings';
use Time::HiRes 'time';
use Data::Dumper;
use Carp qw(cluck croak);


### CONFIG
my $WRAP_TO_COLUMNS        = 120;
my $COLORED_PROGRESS       = 1;
my $COMPRESS_CODEPOINTS    = 1;
my $GAP_LENGTH_THRESHOLD   = 1000;
my $SPAN_LENGTH_THRESHOLD  = 100;
my $BITFIELD_CELL_BITWIDTH = 32;

### DEBUG/LOGGING
my $DEBUG                  = $ENV{UCD2CDEBUG} // 0;
my $LOG;

### METRIC GLOBALS
my $ESTIMATED_TOTAL_BYTES  = 0;  # XXXX: Only calculated for bitfield and names
my $TOTAL_BYTES_SAVED      = 0;
my $PREV_START_TIME        = 0;

### DATA STRUCTURE ROOTS
my $DB_SECTIONS            = {};
my $H_SECTIONS             = {};
my @POINTS_SORTED;
my $POINTS_BY_CODE         = {};
my $PROP_NAMES             = {};
my $BITFIELD_TABLE         = [];
my $ALL_PROPERTIES         = {};
my $ENUMERATED_PROPERTIES  = {};
my $BINARY_PROPERTIES      = {};
my $GENERAL_CATEGORIES     = {};
my $PROPERTY_INDEX         = 0;


### MAIN PROGRAM

sub main {
    init();

    progress_header('Building header');
    $DB_SECTIONS->{'AAA_header'} = header();

    progress_header('Processing sequences');
    my ($emoji_versions, $hout) = process_sequences();
    my  $highest_emoji_version  = $emoji_versions->[-1];
    print "\n-- Highest emoji version found was $highest_emoji_version\n";

    progress_header('Processing aliases');
    $hout .= process_aliases();

    progress_header('Processing UnicodeData');
    process_UnicodeData();

    progress_header('Computing collation weights');
    compute_collation_weights();

    progress_header('Setting Hangul syllable Jamo names');
    set_hangul_syllable_jamo_names();

    progress_header('Processing binary property files');
    binary_props('extracted/DerivedBinaryProperties');  # Bidi_Mirrored
    binary_props('DerivedCoreProperties');
    binary_props('PropList');

    # The emoji binary properties file moved between v12.1 and v13.0
    if (-e "emoji-$highest_emoji_version/emoji-data") {
        # Directory layout in v12.1 and earlier
        binary_props("emoji-$highest_emoji_version/emoji-data")
    }
    else {
        # Directory layout in v13.0 and later
        binary_props("emoji/emoji-data");
    }

    progress_header('Processing enumerated property files');
    process_basic_enumerated_properties();

    progress_header('Building case folding tables');
    CaseFolding();
    SpecialCasing();

    progress_header('Processing derived normalization properties');
    DerivedNormalizationProps();

    progress_header('Processing break properties');
    grapheme_cluster_break('Grapheme', 'Grapheme_Cluster_Break');
    break_property('Sentence', 'Sentence_Break');
    break_property('Word', 'Word_Break');

    progress_header('Tweaking NFG rules');
    tweak_nfg_qc();

    progress_header('Postprocessing codepoint structures');
    sort_points();
    set_next_points();

    progress_header('Allocating and packing property bitfields');
    my $allocated_bitfield_properties = allocate_property_bitfield();
    pack_codepoint_properties($allocated_bitfield_properties);

    progress_header('Uniquing bitfields to save memory');
    uniquify_bitfields();

    progress_header('Emitting unicode_db.c chunks');
    emit_bitfield();
    emit_case_changes();

    progress_header('Writing quick property macro header');
    macroize_quick_props();

    # XXXX: Not yet refactored portion
    progress_header('Processing rest of original main program');
    rest_of_main($allocated_bitfield_properties, $hout);
}

# Startup checks and init
sub init {
    my $have_rakudo = `rakudo -e 'say "Hello world"'`;
    die "You need rakudo in your path to run this script\n"
        unless $have_rakudo =~ /\AHello world/;

    die "Please run `rakudo tools/USD-download.raku` to build UNIDATA directory\n"
        unless rxd_paths('UNIDATA', 'UNIDATA/UCA', 'UNIDATA/extracted');

    die "Unknown value of \$BITFIELD_CELL_BITWIDTH: $BITFIELD_CELL_BITWIDTH\n"
        unless $BITFIELD_CELL_BITWIDTH == 8  || $BITFIELD_CELL_BITWIDTH == 16
            || $BITFIELD_CELL_BITWIDTH == 32 || $BITFIELD_CELL_BITWIDTH == 64;

    $Data::Dumper::Maxdepth = 1;

    binmode STDOUT, ':encoding(UTF-8)';
    binmode STDERR, ':encoding(UTF-8)';
}


### GENERAL UTILITY ROUTINES

# Determine if a set of paths ALL refer to Readable and eXectuable Directories
sub rxd_paths {
    for (@_) {
        return 0 unless -d $_ && -r _ && -x _;
    }
    return 1;
}

# Output a section header progress message
sub progress_header {
    my $ms_spent = int(1000 * (time - $PREV_START_TIME));
    my $time_msg = "Completed in $ms_spent ms";
    $time_msg = "\e[34m" . $time_msg . "\e[0m" if $COLORED_PROGRESS;
    $time_msg = $PREV_START_TIME ? "$time_msg\n" : '';

    my $formatted = uc("@_");
    $formatted = "\e[1;33m" . $formatted . "\e[0m" if $COLORED_PROGRESS;

    progress("$time_msg\n$formatted\n");
    $PREV_START_TIME = time;
}

# Show progress messages, forcing autoflush
sub progress {
    local $| = 1;
    print @_;
}

# Trim both leading and trailing whitespace from each line of a string
sub trim {
    my ($str) = @_;
    $str =~ s/ ^ \s+   //xmsg;
    $str =~ s/   \s+ $ //xmsg;
    return $str;
}

# Trim only trailing spaces/tabs from each line a string
sub trim_trailing {
    my ($str) = @_;
    $str =~ s/ [ \t]+ $ //xmsg;
    return $str;
}

# Compute least integer greater than or equal to the binary log of a number
sub least_int_ge_lg2 {
    # XXXX: This looks really suspect and hacky
    return int(log(shift)/log(2) - 0.00001) + 1;
}

# Add commas every 3 decimal digits; ironically ignores the fact that digit
# separation is very locale-specific.  But since we don't have the CLDR yet ...
sub commify_thousands {
    my $in = shift;
    $in = reverse "$in"; # stringify or copy the string
    $in =~ s/ (\d\d\d) (?= \d) /$1,/xg;
    return reverse($in);
}

# Sort .-separated version strings into correct order by major/minor/revision
sub sort_versions {
    # Schwartzian transform FTW
    map  $_->[0],
    sort {    ($a->[1] || 0) <=> ($b->[1] || 0)
           || ($a->[2] || 0) <=> ($b->[2] || 0)
           || ($a->[3] || 0) <=> ($b->[3] || 0) }
    map  [$_, split(/ [.] /x, $_)], @_;
}

# Call a function on each line of a UNIDATA file specified by file basename,
# skipping blank and comment lines unless $force is true.
sub for_each_line {
    my ($basename, $fn, $force) = @_;

    my $filename = "UNIDATA/$basename.txt";
    progress("Processing $basename.txt\n");

    for my $line (@{read_file($filename)}) {
        chomp $line;
        # XXXX: Should this skip comment lines with leading whitespace?
        $fn->($line) if $force || $line !~ / ^ (?: [#] | \s* $ ) /x;
    }
}

# Helper sub for reformatting hex lists that must be at least 3 entries long
sub hex_triplet {
    my ($spaced, $sep) = @_;
    $sep //= ', ';

    my @hex = split ' ', $spaced;
    push @hex, 0 while @hex < 3;
    return join $sep, map "0x$_", @hex;
}


### CODEPOINT UTILITY ROUTINES

# Search for codepoint info for a given code, or create it if missing
sub get_point_info_for_code {
    my ($code, $add_to_points_by_code) = @_;

    my  $point = $POINTS_BY_CODE->{$code};
    if ($point) {
        croak "$code is already defined" if $add_to_points_by_code;
    }
    else {
        $point = {
            name             => '',
            code             => $code,
            code_str         => sprintf('%.4X', $code),
            gencat_name      => "Cn",
            General_Category => $GENERAL_CATEGORIES->{enum}->{Cn},

            Any              => 1,
            NFD_QC           => 1, # these are defaults (inverted)
            NFC_QC           => 1, # which will be unset as appropriate
            NFKD_QC          => 1,
            NFKC_QC          => 1,
            NFG_QC           => 1,
            MVM_COLLATION_QC => 1,
        };
        $POINTS_BY_CODE->{$code} = $point if $add_to_points_by_code;
    }
    return $point;
}

# Apply a function to a range of codepoints. The starting and ending
# codepoint of the range need not exist; the function will be applied
# to all/any in between.
sub apply_to_cp_range {
    my ($range, $fn) = @_;

    # Check for undefined range info
    if (!defined $range) {
        cluck "Did not get a defined range in apply_to_cp_range";
    }

    # Determine code endpoints of range, defaulting as needed
    chomp($range);
    my ($first_str,  $last_str)  = split '\\.\\.', $range;
    $first_str                 ||= $range;
    $last_str                  ||= $first_str;
    my ($first_code, $last_code) = (hex $first_str, hex $last_str);

    # Apply function to all codepoints in the range in increasing order
    my $curr_code = $first_code;
    while ($curr_code <= $last_code) {
        # This might apply the function to a stub codepoint,
        # which will then be dropped on the floor at end of scope
        my $point = get_point_info_for_code($curr_code);
        $fn->($point);
        $curr_code++;
    }
}


### SEQUENCE PROCESSING

# Process emoji and standard sequences
sub process_sequences {
    my $named_sequences = {};

    my $emoji_versions = add_emoji_sequences($named_sequences);
    add_unicode_sequence('NamedSequences', $named_sequences);

    my $h_chunk = emit_unicode_sequence_keypairs($named_sequences);

    return ($emoji_versions, $h_chunk);
}

# Find all emoji versions and process sequence information for each
sub add_emoji_sequences {
    my ($named_sequences) = @_;

    # Find all the versioned emoji dirs
    my @versions;
    opendir my $UNIDATA_DIR, 'UNIDATA' or croak $!;
    while (my $entry = readdir $UNIDATA_DIR) {
        push @versions, $entry if -r "UNIDATA/$entry/emoji-sequences.txt"
                               && $entry =~ s/ ^ emoji- //x;
    }
    die "Couldn't find any emoji folders. Please run UCD-download.raku again.\n"
        if !@versions;

    @versions = sort_versions(@versions);

    # Add sequence info for all emoji versions into $named_sequences
    for my $version (@versions) {
        add_unicode_sequence("emoji-$version/emoji-sequences", $named_sequences);
        add_unicode_sequence("emoji-$version/emoji-zwj-sequences", $named_sequences);
    }

    # Return processed emoji versions
    return \@versions;
}

# Load unicode sequence info from a particular sequence file
sub add_unicode_sequence {
    my ($basename, $named_sequences) = @_;

    my $is_emoji = $basename =~ / emoji /x;

    for_each_line $basename, sub {
        my $line = shift;

        # Handle both sequence file formats
        my ($hex_ords, $type, $name);
        my  @list = map trim($_), split / ; | \s{3}[#] /x, $line;

        if ($is_emoji) {
            ($hex_ords, $type, $name) = @list;

            # Don't process non-sequences
            return if $hex_ords =~ /\.\./;
            return if $hex_ords !~ / /;
        }
        else {
            ($name, $hex_ords) = @list;
            $type = 'NamedSequences';
        }

        # There could be hex-encoded unicode codepoint numbers in the name. In
        # that case convert to the actual codepoints, so '\\x{23}' is replaced
        # with chr(0x23), '#'.
        while ($name =~ / \\x \{ ([[:xdigit:]]+) \} /x ) {
            my $chr = chr hex($1);
            $name =~ s/ \\x \{ $1 \} /$chr/xg;
        }

        # Make sure the name is uppercased as Raku expects since the emoji
        # sequence names are not all in uppercase in the data files.
        $name = uc $name;

        # Some emoji sequence names contain commas which cannot be included
        # since they separate named items in the ISO notation that Raku uses.
        $name =~ s/,//xg;

        # Store sequence info for this name
        $named_sequences->{$name}->{'type'} = $type;

        # Only push if we haven't seen this already
        if (!$named_sequences->{$name}->{'ords'}) {
            for my $hex (split ' ', $hex_ords) {
                push @{$named_sequences->{$name}->{'ords'}}, hex $hex;
            }
        }
    }
}

# Build uni_seq DB section
sub emit_unicode_sequence_keypairs {
    my ($named_sequences) = @_;

    my @seq_c_hash_wrapped;
    my $seq_c_hash_str = '';
    my $enum_table     = '';
    my $string_seq     = "/* Unicode sequences such as Emoji sequences */\n";
    my $count          = 0;

    for my $thing ( sort keys %$named_sequences ) {
        # Set C constant name for this sequence and add to enumerant table
        my $seq_c_name = "uni_seq_$count";
        $enum_table   .= "$seq_c_name,\n";

        # Add to sequence name => sequence number hash, wrapping entries
        $seq_c_hash_str .= '{"' . $thing . '",' . $count++ . '},';
        if (length $seq_c_hash_str > 80) {
            push @seq_c_hash_wrapped, $seq_c_hash_str . "\n";
            $seq_c_hash_str = '';
        }

        # Add codepoint info for this sequence
        my $seq_info = $named_sequences->{$thing};
        my $ords     = $seq_info->{'ords'};
        my $type     = $seq_info->{'type'};
        my $ord_data = join ',', scalar(@$ords), map sprintf("0x%X", $_), @$ords;
        $string_seq .= "static const MVMint32 $seq_c_name\[] = {$ord_data};"
                    .  " /* $thing */ /* $type */\n";
    }

    # Catch last partial line of hash data and finish formatting seq_c_hash
    push @seq_c_hash_wrapped, $seq_c_hash_str;
    $seq_c_hash_str = join '    ',
                      "static const MVMUnicodeNamedValue uni_seq_pairs[$count] = {\n",
                      @seq_c_hash_wrapped;
    $seq_c_hash_str =~ s/ \s* , \s* $ /\n};\n/x;

    # Finish formatting enum table
    # XXXX: Should we wrap the enum table?
    $enum_table =~ s/ \s* , \s* $ /};/x;
    $enum_table = "static const MVMint32 * uni_seq_enum[$count] = {\n" . $enum_table;

    # Emit the uni_seq DB section and provide a header macro for the sequence count
    $DB_SECTIONS->{uni_seq} = $seq_c_hash_str . $string_seq . $enum_table;
    return "#define num_unicode_seq_keypairs $count \n";
}


### NAMES ALIAS PROCESSING

# Process and emit alias info
sub process_aliases {
    my $alias_info = add_name_aliases();
    my $h_chunk    = emit_name_alias_keypairs($alias_info);

    return $h_chunk;
}

# Gather type and code info for each alias name
sub add_name_aliases {
    my %alias_info;

    for_each_line 'NameAliases', sub {
        $_ = shift;
        my ($code_str, $name, $type) = split / \s* [;#] \s* /x;
        $alias_info{$name}->{'code'} = hex $code_str;
        $alias_info{$name}->{'type'} = $type;
    };

    return \%alias_info;
}

sub emit_name_alias_keypairs {
    my ($alias_info) = @_;

    my @seq_c_hash_wrapped;
    my $seq_c_hash_str = '';
    my $count          = 0;

    for my $name ( sort keys %$alias_info ) {
        # Update count and format codepoint in C-style hex
        $count++;
        my $ord = $alias_info->{$name}->{'code'};
        my $ord_data = sprintf '0x%X', $ord;

        # Add to character alias name => codepoint hash, wrapping entries
        $seq_c_hash_str .= qq({"$name", $ord_data) . ',' . length($name) . '},';
        if ( length $seq_c_hash_str > 80 ) {
            push @seq_c_hash_wrapped, $seq_c_hash_str . "\n";
            $seq_c_hash_str = '';
        }
    }

    # Catch last partial line of hash data and finish formatting seq_c_hash
    push @seq_c_hash_wrapped, "$seq_c_hash_str\n";
    $seq_c_hash_str = join '    ', @seq_c_hash_wrapped;
    $seq_c_hash_str =~ s/ \s* , \s* $ //x;

    # Emit the Auni_namealias DB section
    chomp($DB_SECTIONS->{Auni_namealias} = <<"END");
/* Unicode Name Aliases */
static const MVMUnicodeNamedAlias uni_namealias_pairs[$count] = {
    $seq_c_hash_str
};
END

    # Return a header chunk defining MVMUnicodeNamedAlias and the keypair count
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


### PROPERTY PROCESSING

# Process the pile of enumerated property files that don't need special treatment
# Note: Some must be processed more than once to pull out different enumerations
sub process_basic_enumerated_properties {
    enumerated_property('ArabicShaping',      'Joining_Group', {}, 3);
    enumerated_property('ArabicShaping',      'Joining_Type', { U => 0 }, 2);

    enumerated_property('BidiMirroring',      'Bidi_Mirroring_Glyph',
                        { 0 => 0 }, 1, 'int', 1);
    enumerated_property('Blocks',             'Block', { No_Block => 0 }, 1);
    enumerated_property('DerivedAge',         'Age', { Unassigned => 0 }, 1);
    enumerated_property('HangulSyllableType', 'Hangul_Syllable_Type',
                        { Not_Applicable => 0 }, 1);
    enumerated_property('LineBreak',          'Line_Break', { XX => 0 }, 1);
    enumerated_property('Scripts',            'Script', { Unknown => 0 }, 1);

    enumerated_property('extracted/DerivedDecompositionType',
                        'Decomposition_Type', { None => 0 }, 1);
    enumerated_property('extracted/DerivedEastAsianWidth',
                        'East_Asian_Width', { N => 0 }, 1);

    enumerated_property('extracted/DerivedNumericType',
                        'Numeric_Type', { None => 0 }, 1);
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

    # set_hangul_syllable_jamo_names() currently processes the Jamo names
    # into Hangul syllables, but does not save Jamo_Short_Name as a property
    # of its own.  Leaving this here in case it is needed in the future.

    # enumerated_property('Jamo', 'Jamo_Short_Name', {  }, 1, 1);
}

# Process a derived property file
sub derived_property {
    # filename, property name, enumeration base (generally entry 0)
    my ($fname, $pname, $base) = @_;

    # If we provided some property values already, start the enum counter there
    my $num_keys = scalar keys %$base;

    # Wrap the provided enumeration base as the enum key in a higher level struct
    my $property = { enum => $base, name => $pname };

    # Scan derived property file for unknown property values
    for_each_line "extracted/Derived$fname", sub {
        $_ = shift;
        my ($range, $class) = split / \s* [;#] \s* /x;

        # If we haven't seen this class value for this property before,
        # add it to the property enum with the correct enum value
        unless (exists $property->{enum}->{$class}) {
            printf "\n  adding derived property value for %s: %4d %s", $pname, $num_keys, $class if $DEBUG;
            $property->{enum}->{$class} = $num_keys++;
        }
    };

    # Register enum keys, calculate bit width, and register property with globals
    register_keys_and_set_bit_width($property, $num_keys);
    return register_enumerated_property($pname, $property);
}

# Process an enumerated property file (of which there are many!)
sub enumerated_property {
    my ($fname, $pname, $base, $value_index, $type, $is_hex) = @_;
    $type = 'string' unless $type;

    my $num_keys = scalar keys %$base;
    my $property = { enum => $base, name => $pname, type => $type };

    for_each_line $fname, sub {
        # Determine codepoint range and relevant values to process
        $_ = shift;
        my @vals  = split / \s* [#;] \s* /x;
        my $range = $vals[0];

        # $value_index may be either a normal integer index into @vals
        # or a coderef that computes the value using the @vals array
        my $value = ref $value_index ? $value_index->(\@vals)
                                     : $vals[$value_index];

        # Convert hex values to normal integers if requested
        $value = hex $value if $is_hex;

        # Determine enumeration index for the value, creating a new one if needed
        my $index = $base->{$value};
        if (not defined $index) {
            print("\n  adding enum property for $pname: $num_keys $value") if $DEBUG;
            $base->{$value} = $index = $num_keys++;
        }

        # Apply this enumerant index to all codepoints in range
        apply_to_cp_range $range, sub {
            my $point = shift;
            $point->{$pname} = $index;
        };
    };

    # Register enum keys, calculate bit width, and register property with globals
    register_keys_and_set_bit_width($property, $num_keys);
    register_enumerated_property($pname, $property);
}

# Set enumerant field bit width based on discovered enumerant keys
sub register_keys_and_set_bit_width {
    my ($property, $num_keys) = @_;

    # Register (order and cache) property enumerant keys
    my $registered = register_keys($property);

    # Check whether derived_property() and register_keys()
    # produced the same number of property value keys
    croak "The number of property value keys do not match. Registered: $registered, parsed: $num_keys"
        if defined $num_keys and $registered != $num_keys;

    # Determine minimum bit width of fields storing the enumerant value
    $property->{bit_width} = least_int_ge_lg2($registered);
    printf "\n    bitwidth: %d\n", $property->{bit_width} if $DEBUG;
}

# Order and cache property enumerant keys
sub register_keys {
    my ($property) = @_;

    # Stash the property enum keys in an ordered array
    # so they can be put in a table later
    my @keys;
    for my $key (keys %{$property->{enum}}) {
        $keys[$property->{enum}->{$key}] = $key;
    }
    $property->{keys} = \@keys;

    print "\n    keys = @keys" if $DEBUG;

    # Return count of keys found
    return scalar @keys;
}

# Validate property and add to property globals if valid
sub register_enumerated_property {
    my ($pname, $property) = @_;

    # Ensure property is new (never before processed)
    croak "Property '$pname' has been processed already."
        if exists $ALL_PROPERTIES->{$pname}
        || exists $ENUMERATED_PROPERTIES->{$pname};

    # Ensure property name set correctly
    if (!$property->{name}) {
        $property->{name} = $pname;
    }
    elsif ($pname ne $property->{name}) {
        croak("Property name doesn't match. Argument was '$pname' but was already set to '" . $property->{name} . "'");
    }

    # Check that property value enumeration is well formed
    ensure_property_enum_is_well_formed($property);

    # Register property into property globals and set property index
    $ALL_PROPERTIES->{$pname} = $ENUMERATED_PROPERTIES->{$pname} = $property;
    $property->{property_index} = $PROPERTY_INDEX++;
    return $property;
}

# Make sure we don't assign twice to the same pvalue or skip any pvalues
sub ensure_property_enum_is_well_formed {
    my ($property) = @_;
    my $enum = $property->{enum};
    my $name = $property->{name};

    # Check for duplicate property values
    my %seen;
    for my $key (keys %$enum) {
        if (defined $seen{ $enum->{$key} }) {
            croak("\nError: Assigned twice to the same property value code "
                . "for property '$name'.  Both $key and $seen{ $enum->{$key} }"
                . " are assigned to pvalue code $enum->{$key}.\n"
                . Dumper $enum);
        }
        $seen{ $enum->{$key} } = $key;
    }

    # Check for skipped property values
    my $start = 0;
    for my $key (sort { $enum->{$a} <=> $enum->{$b} } keys %{$enum}) {
        croak("\nError: property value code is not sequential for property '$name'."
            . " Expected $start but saw $enum->{$key}\n" . Dumper $enum)
            if $enum->{$key} != $start;
        $start++;
    }
}

# Process a file containing multiple binary properties and apply them
# to specified ranges
sub binary_props {
    my ($fname) = @_;

    for_each_line $fname, sub {
        # Determine codepoint range and property name to apply
        $_ = shift;
        my ($range, $pname) = split / \s* [;#] \s* /x;

        # Ensure property has been registered
        register_binary_property($pname);

        # Actually apply the property to every point in the range
        apply_to_cp_range $range, sub {
            my $point = shift;
            $point->{$pname} = 1;
        };
    };
}

# Register a binary (single bit wide) property if it hasn't already been
sub register_binary_property {
    my $name = shift;

    $ALL_PROPERTIES->{$name} = $BINARY_PROPERTIES->{$name} = {
        name           => $name,
        bit_width      => 1,
        property_index => $PROPERTY_INDEX++
    } unless exists $BINARY_PROPERTIES->{$name};
}

# Register a plain int property
sub register_int_property {
    my ($name, $elems) = @_;

    # XXXX: Add to binary_properties for now
    $ALL_PROPERTIES->{$name} = $BINARY_PROPERTIES->{$name} = {
        name           => $name,
        bit_width      => least_int_ge_lg2($elems),
        property_index => $PROPERTY_INDEX++,
    } unless exists $BINARY_PROPERTIES->{$name};
}

# Register a union property
sub register_union {
    my ($unionname, $unionof, $gc_alias_checkers) = @_;

    # Register a binary property for the union
    register_binary_property($unionname);

    # Add a general category alias checker to be run later
    push @$gc_alias_checkers, sub {
        return ((shift) =~ /^(?:$unionof)$/) ? $unionname : 0;
    };
}


### PROCESS PRIMARY UnicodeData FILE

# Process the prerequisite derived properties and then the UnicodeData file
sub process_UnicodeData {
    UnicodeData(
        derived_property('BidiClass',       'Bidi_Class',       { L  => 0 }),
        derived_property('GeneralCategory', 'General_Category', { Cn => 0 }),
        derived_property('CombiningClass',  'Canonical_Combining_Class',
                         { Not_Reordered => 0 })
    );
}

# Process primary UnicodeData file given known BDC, GC, and CCC enumerations
sub UnicodeData {
    my ($bidi_classes, $general_categories, $ccclasses) = @_;
    $GENERAL_CATEGORIES = $general_categories;

    # Register the 'Any' binary property
    register_binary_property('Any');

    # Register the property value unions and collect the alias checker routines
    my $gc_alias_checkers = register_pvalue_alias_unions();

    # State variables needed for parser routine below
    my $ideograph_start;
    my $case_count   = 1;
    my $decomp_index = 1;
    my $decomp_keys  = [ '' ];

    # Define the parser/data cleanup routine to be applied to UnicodeData lines
    my $UnicodeDataParser = sub {
        # Split apart the codepoint info fields
        $_ = shift;
        my ($code_str, $name, $gencat, $ccclass, $bidiclass, $decmpspec,
            $num1, $num2, $num3, $bidimirrored, $u1name, $isocomment,
            $suc, $slc, $stc) = split ';';

        # Decode the hex codepoint and determine its Unicode plane
        my $code      = hex $code_str;
        my $plane_num = $code >> 16;

        # Start building the point info structure for this code
        my $point = get_point_info_for_code($code, 1);

        # Fill in some point info entries from the codepoint info fields
        # XXXX: Unicode_1_Name is not used yet; we should make sure
        #       it ends up in some data structure eventually
        $point->{Unicode_1_Name}   = $u1name;
        $point->{name}             = $name;
        $point->{gencat_name}      = $gencat;
        $point->{General_Category} = $general_categories->{enum}->{$gencat};
        $point->{Canonical_Combining_Class} = $ccclasses->{enum}->{$ccclass};
        $point->{Bidi_Class}       = $bidi_classes->{enum}->{$bidiclass};
        $point->{Bidi_Mirrored}    = 1 if $bidimirrored eq 'Y';

        # Run the general category alias checkers and mark any found
        for my $checker (@$gc_alias_checkers) {
            my $res = $checker->($gencat);
            $point->{$res} = 1 if $res;
        }

        # Track letter case changes
        # XXXX: What does the 's' in these field names stand for?
        $point->{suc} = $suc;
        $point->{slc} = $slc;
        $point->{stc} = $stc;
        if ($suc || $slc || $stc) {
            $point->{Case_Change_Index} = $case_count++;
        }

        # Clean and assign Decomp Spec if any
        if ($decmpspec) {
            $decmpspec =~ s/ [<] \w+ [>] \s+ //x;
            $point->{Decomp_Spec} = $decomp_index;
            $decomp_keys->[$decomp_index++] = $decmpspec;
        }

        # Process First/Last range pairs for points with computed names
        if ($name =~ /(Ideograph|Syllable|Private|Surrogate) (\s|.)*? First/x) {
            # 'First' entry for known range type; start the range
            $point->{name}   =~ s/, First//;
            $ideograph_start = $point;
        }
        elsif ($name =~ / First/) {
            # Unknown range type; croak
            croak "Looks like support for a new thing needs to be added: $name";
        }
        elsif ($ideograph_start) {
            # Range already started, so this must be the 'Last' entry for the
            # range; fill in all the intermediate points using the start point
            # as a template
            $point->{name} = $ideograph_start->{name};
            for (my $code = $ideograph_start->{code} + 1; $code < $point->{code}; $code++) {
                my $current = get_point_info_for_code($code, 1);
                for (keys %$ideograph_start) {
                    next if $_ eq "code" || $_ eq "code_str";
                    $current->{$_} = $ideograph_start->{$_};
                }
            }

            # This range is now finished, make sure start marker is falsey
            $ideograph_start = 0;
        }

        # Set correct names for computed range names
        if (substr($point->{name}, 0, 1) eq '<') {
            if ($point->{name} eq '<CJK Ideograph>'
             || $point->{name} eq '<CJK Ideograph Extension A>'
             || $point->{name} eq '<CJK Ideograph Extension B>'
             || $point->{name} eq '<CJK Ideograph Extension C>'
             || $point->{name} eq '<CJK Ideograph Extension D>'
             || $point->{name} eq '<CJK Ideograph Extension E>'
             || $point->{name} eq '<CJK Ideograph Extension F>'
             || $point->{name} eq '<CJK Ideograph Extension G>'
             || $point->{name} eq '<CJK Ideograph Extension H>'
             || $point->{name} eq '<CJK Ideograph Extension I>') {
                $point->{name} = '<CJK UNIFIED IDEOGRAPH>'
            }
            elsif ($point->{name} eq '<Tangut Ideograph>'
                || $point->{name} eq '<Tangut Ideograph Supplement>') {
                $point->{name} = '<TANGUT IDEOGRAPH>';
            }
            elsif ($point->{name} eq '<Hangul Syllable>') {
                $point->{name} = '<HANGUL SYLLABLE>';
            }
            elsif ($point->{name} eq '<Private Use>'
                || $point->{name} =~ /^<Plane \d+ Private Use>$/)
            {
                croak "Unexpected private use name for general category '$gencat'"
                    unless $gencat eq 'Co';
                $point->{name} = '<private-use>';
            }
            elsif ($point->{name} eq '<Non Private Use High Surrogate>'
                || $point->{name} eq '<Private Use High Surrogate>'
                || $point->{name} eq '<Low Surrogate>')
            {
                croak "Unexpected surrogate name for general category '$gencat'"
                    unless $gencat eq 'Cs';
                $point->{name} = '<surrogate>';
            }

            # Check for unexpected special angle bracketed names
            my $instructions_line_no = __LINE__;
            my $instructions = <<~'END';
              Don't add any cases above *unless* Unicode has added a new
              "Name Derivation Rule Prefix String" in the right format!

              For example if a new CJK Unified Ideograph extension is added
              named <CJK Ideograph Extension X> **AND** the range for that
              extension has been added to the Name Derivation Rule Prefix
              String table with the prefix 'CJK UNIFIED IDEOGRAPH-',
              **ONLY THEN** you can add a case for it above.

              Extensions to *existing* prefixes should be normalized from
              their format in the data file to the correct prefix string.
              END

            if ($point->{name} eq '<control>'
             || $point->{name} eq '<surrogate>'
             || $point->{name} eq '<private-use>'
             || $point->{name} eq '<HANGUL SYLLABLE>'
             || $point->{name} eq '<TANGUT IDEOGRAPH>'
             || $point->{name} eq '<CJK UNIFIED IDEOGRAPH>') {
                # No error, these are all fine
            }
            else {
                die <<~"END";
                ##############################
                Error: Special name '$point->{name}' encountered at code $code_str
                ##############################
                IMPORTANT: READ BELOW

                Make sure to check Table 4-8. Name Derivation Rule Prefix Strings at
                https://www.unicode.org/versions/latest/core-spec/chapter-4/#G144161
                and use this table as a reference to decide whether a new case needs
                to be added for codepoint range names.

                Read more about this on comment line $instructions_line_no of ucd2c.pl.

                You will likely have to make a change to MVM_unicode_get_name() and
                add a test to nqp.
                END
            }
        }

        # Fix name for CJK COMPATIBILITY IDEOGRAPHs
        if ($point->{name} =~ /^CJK COMPATIBILITY IDEOGRAPH-([A-F0-9]+)$/) {
            croak "CJK CI with mismatched hex code, $code versus $1"
                unless sprintf('%.4X', $code) eq $1;
            $point->{name} = "<CJK COMPATIBILITY IDEOGRAPH>";
        }
    };

    # Parse each line of the UnicodeData file, plus a special 'Out of Range' marker
    for_each_line('UnicodeData', $UnicodeDataParser);
    $UnicodeDataParser->("110000;Out of Range;Cn;0;L;;;;;N;;;;;");

    # Register enumerated properties for Case_Change_Index and Decomp_Spec
    register_enumerated_property('Case_Change_Index', {
        name      => 'Case_Change_Index',
        bit_width => least_int_ge_lg2($case_count)
    });
    register_enumerated_property('Decomp_Spec', {
        name      => 'Decomp_Spec',
        'keys'    => $decomp_keys,
        bit_width => least_int_ge_lg2($decomp_index)
    });
}


# Register property value alias unions
sub register_pvalue_alias_unions {
    # Scan PropertyValueAliases for unions and register them
    my @gc_alias_checkers;
    for_each_line 'PropertyValueAliases', sub {
        # Trim the property value alias fields and shift off the property name
        $_ = shift;
        my @pv_alias_parts = map trim($_), split / [#;] /x;
        my $propname = shift @pv_alias_parts;

        # Nothing to do for basic boolean aliases
        return if ($pv_alias_parts[0] eq 'Y'   || $pv_alias_parts[0] eq 'N')
               && ($pv_alias_parts[1] eq 'Yes' || $pv_alias_parts[1] eq 'No');

        # If it's a union, decode and register it
        if ($pv_alias_parts[-1] =~ /[|]/x) {
            my $unionname = $pv_alias_parts[0];
            my $unionof   = pop @pv_alias_parts;
            $unionof      =~ s/ \s+ //xg;
            register_union($unionname, $unionof, \@gc_alias_checkers);
        }
    };

    # Register a special 'Assigned' union for assigned general categories
    register_union('Assigned',
                   'C[cfosn]|L[lmotu]|M[cen]|N[dlo]|P[cdefios]|S[ckmo]|Z[lps]',
                   \@gc_alias_checkers);

    # Return collected general category alias checkers
    return \@gc_alias_checkers;
}


### COLLATION WEIGHT COMPUTATION

# Build collation tables based on UCA/allkeys.txt
sub compute_collation_weights {
    # Set up base enums and state for collation properties
    my ($index, $maxes, $bases) = ( {}, {}, {} );
    my ($name_primary, $name_secondary, $name_tertiary)
        = ('MVM_COLLATION_PRIMARY', 'MVM_COLLATION_SECONDARY', 'MVM_COLLATION_TERTIARY');

    for my $name ($name_primary, $name_secondary, $name_tertiary) {
        my $base = { enum => { 0 => 0 }, name => $name, type => 'int' };

        $bases->{$name} = $base;
        $maxes->{$name} = 0;
        $index->{$name}->{j} = keys %{$base->{enum}};
    }

    # Parse the lines of allkeys; sample line:
    #1D4FF ; [.1EE3.0020.0005] # MATHEMATICAL BOLD SCRIPT SMALL V
    my $line_no = 0;
    for_each_line 'UCA/allkeys', sub {
        # Track current line number
        my $line = shift;
        $line_no++;

        # Skip blank/comment lines and @implicitweights lines
        # Implicit weights are handled in tools/Generate-Collation-Data.raku
        # XXXX: Is blank/comment skipping necessary since for_each_line does this?
        # XXXX: Should other @ lines be considered comments?

        return if $line =~ s/ ^ \@implicitweights \s+ //xms;
        return if $line =~ / ^ \s* [#@] /x or $line =~ / ^ \s* $ /x;

        # Extract code and weight list from line
        my ($code, $weight_list) = split / [;#]+ /x, $line;
        $code = trim $code;

        # Collation for multiple codepoints is handled in tools/Generate-Collation-Data.raku
        my @codes = split ' ', $code;
        if (1 < @codes) {
            # For now set MVM_COLLATION_QC = 0 for the starting codepoint and return
            apply_to_cp_range $codes[0], sub {
                my $point = shift;
                $point->{'MVM_COLLATION_QC'} = 0;
            };
            return;
        }

        # Process the weight list
        #
        # We capture the `.` or `*` before each weight.  Currently we do
        # not use this information, but it may be of use later (we currently
        # don't put their values into the data structure).
        #
        # When multiple tables are specified for a character, it is because
        # those are the composite values for the decomposed character. Since
        # we compare in NFC form not NFD, let's add these together.

        my $weights = {};
        while ($weight_list =~ / (:? \[ ([.*]) (\p{AHex}+) ([.*]) (\p{AHex}+) ([.*]) (\p{AHex}+) \] ) /xmsg) {
            $weights->{$name_primary}   += hex $3;
            $weights->{$name_secondary} += hex $5;
            $weights->{$name_tertiary}  += hex $7;
        }

        # Verify we've parsed the line sensibly
        if (   !defined $code
            || !defined $weights->{$name_primary}
            || !defined $weights->{$name_secondary}
            || !defined $weights->{$name_tertiary}) {
            my $str;
            for my $name ($name_primary, $name_secondary, $name_tertiary) {
                $str .= ", \$weights->{$name} = " . $weights->{$name};
            }
            croak "Line no $line_no: \$line = $line$str";
        }

        # Apply total weights to codepoint
        apply_to_cp_range $code, sub {
            my $point = shift;
            my $raws  = {};
            for my $base ($bases->{$name_primary},
                          $bases->{$name_secondary},
                          $bases->{$name_tertiary}) {
                my $name = $base->{name};

                # Add one to the value so we can distinguish between specified
                # values of zero for collation weight and null values.
                $raws->{$name} = 1;
                if ($weights->{$name}) {
                    $raws->{$name} += $weights->{$name};
                    $maxes->{$name} = $weights->{$name} if $weights->{$name} > $maxes->{$name};
                }

                # $point->{$name} = collation_get_check_index($index, $name, $base, $raws->{$name}); # Uncomment to make it an int enum
                $point->{$name} = $raws->{$name}; # Comment to make it an int enum
            }
        };
    };

    # Add 0 to a non-character just to make sure it ends up assigned to some codepoint
    # (or it may not properly end up in the enum)
    apply_to_cp_range "FFFF", sub {
        my $point = shift;
        $point->{$name_tertiary} = 0;
    };

    for my $base ($bases->{$name_primary},
                  $bases->{$name_secondary},
                  $bases->{$name_tertiary}) {
        my $name = $base->{name};

        # Check for nonsensical max collation numbers
        croak "Oh no! One of the highest collation numbers I saw is less than 1. " .
              " Primary max: "   . $maxes->{$name_primary} .
              " secondary max: " . $maxes->{$name_secondary} .
              " tertiary max: "  . $maxes->{$name_tertiary}
            if $maxes->{$name} < 1;

        # Register a property for the max collation values
        # register_enumerated_property($name, $base); # Uncomment to make an int enum
        # register_keys_and_set_bit_width($base, $index->{$name}->{j}); # Uncomment to make an int enum
        register_int_property($name, $maxes->{$name}); # Comment to make an int enum
    }

    # Register a binary property for COLLATION_QC
    register_binary_property('MVM_COLLATION_QC');
}

# XXXX: Only used if collations will be an enum (call currently commented out)
sub collation_get_check_index {
    my ($index, $property, $base, $value) = @_;

    my $indexy = $base->{enum}->{$value};

    if (!defined $indexy) {
        # Haven't seen this property value before; add it and give it an index
        print("\n  adding enum property for property: $property j: " .
              $index->{$property}->{j} . " value: $value") if $DEBUG;

        $base->{enum}->{$value} = $indexy = ($index->{$property}->{j}++);
    }

    return $indexy;
}


### JAMO PROCESSING

# Use Jamo file to build up correct Hangul syllable codepoint names
sub set_hangul_syllable_jamo_names {
    # Set Jamo_Short_Name property for each Jamo codepoint
    for_each_line 'Jamo', sub {
        $_ = shift;
        my ($code_str, $name) = split / \s* [#;] \s* /x;
        apply_to_cp_range $code_str, sub {
            my $point = shift;
            $point->{Jamo_Short_Name} = $name;
        };
    };

    # Collect known Hangul syllables
    my @hangul_syllables;
    # XXXX: Can we use @POINTS_SORTED here yet?
    for my $code (sort keys %$POINTS_BY_CODE) {
        my $name = $POINTS_BY_CODE->{$code}->{name};
        push @hangul_syllables, $code if $name and $name eq '<HANGUL SYLLABLE>';
    }

    # XXXX: Use existing rakudo to convert syllable codepoints to NFD form
    my $hs = join ',', @hangul_syllables;
    my $out = `rakudo -e 'my \@cps = $hs; for \@cps -> \$cp { \$cp.chr.NFD.list.join(",").say };'`;
    die "Problem running rakudo to process Hangul syllables: \$? was $?" if $?;

    # Process NFD Jamo lists to build up full Hangul syllable names
    my $i = 0;
    for my $line (split "\n", $out) {
        my $final_name = 'HANGUL SYLLABLE ';
        my $hs_cps = $hangul_syllables[$i++];

        for my $cp (split ',', $line) {
            my $jamo_name = $POINTS_BY_CODE->{$cp}->{Jamo_Short_Name};
            $final_name .= $jamo_name if defined $jamo_name;
        }

        $POINTS_BY_CODE->{$hs_cps}->{name} = $final_name;
    }
}


### CASE FOLDING AND SPECIAL CASE HANDLING

# Build case folding tables for folding types C and F (Common and Full)
sub CaseFolding {
    my $simple_count = 1;
    my $grows_count  = 1;
    my @simple;
    my @grows;

    for_each_line 'CaseFolding', sub {
        $_ = shift;
        my ($code_str, $type, $mapping) = split / \s* ; \s* /x;

        # Ignore Simple folding maps that differ from Full folding
        # XXXX: Also ignore Turkic handling of uppercase I / dotted uppercase I
        return if $type eq 'S' || $type eq 'T';

        # Grab the codepoint to be mapped *from*
        my $code  = hex $code_str;
        my $point = $POINTS_BY_CODE->{$code};

        # Common entries are valid for *both* Simple and Full mappings;
        # otherwise we choose the Full mapping where S and F both exist
        # but are different.
        if ($type eq 'C') {
            push @simple, "0x$mapping";
            $point->{Case_Folding} = $simple_count++;
            $point->{Case_Folding_simple} = 1;
        }
        else {
            push @grows, '{' . hex_triplet($mapping, ',') . '}';
            $point->{Case_Folding} = $grows_count++;
        }
    };

    # Finish formatting simple casefolding table and add it to DB_SECTIONS
    my $simple_out = "static const MVMint32 CaseFolding_simple_table[$simple_count] = {\n"
                   . "    0x0,\n    "
                   . stack_lines(\@simple, ",", ",\n    ", 0, $WRAP_TO_COLUMNS)
                   . "\n};";
    $DB_SECTIONS->{BBB_CaseFolding_simple} = $simple_out;

    # Finish formatting full (growing) casefolding table and add to DB_SECTIONS
    my $grows_out = "static const MVMint32 CaseFolding_grows_table[$grows_count][3] = {\n"
                  . "    {0x0,0x0,0x0},\n    "
                  . stack_lines(\@grows, ",", ",\n    ", 0, $WRAP_TO_COLUMNS)
                  . "\n};";
    $DB_SECTIONS->{BBB_CaseFolding_grows}  = $grows_out;

    # Add to estimate of total bytes required by C tables/structures
    # XXXX: Where do 8 and 32 come from?  And are they bytes or *bits*?
    $ESTIMATED_TOTAL_BYTES += $simple_count * 8 + $grows_count * 32;

    # Determine bitfield width required for both folding tables
    my $max_count = $simple_count >= $grows_count ? $simple_count : $grows_count;
    my $bit_width = least_int_ge_lg2($max_count);

    # Register an enumerated property for Case_Folding
    # and a binary property for Case_Folding_simple
    my $index_base = { name => 'Case_Folding', bit_width => $bit_width };
    register_enumerated_property('Case_Folding', $index_base);
    register_binary_property('Case_Folding_simple');
}

# Process *unconditional* mappings in SpecialCasing file
sub SpecialCasing {
    my $count = 1;
    my @entries;
    for_each_line 'SpecialCasing', sub {
        # Parse line into fields
        $_ = shift;
        s/ [#] .+ //x;
        my ($code_str, $lower, $title, $upper, $cond) = split / \s* ; \s* /x;

        # XXXX: Skip processing if mapping is conditional
        #       (contextual or language-specific)
        return if $cond;

        # Add a C-formatted entry to the SpecialCasing table
        push @entries, "{ { "   . hex_triplet($upper) .
                       " }, { " . hex_triplet($lower) .
                       " }, { " . hex_triplet($title) .
                       " } }";

        # Track which SpecialCasing entry matches this codepoint
        my $code = hex $code_str;
        $POINTS_BY_CODE->{$code}->{Special_Casing} = $count++;
    };

    # Finish formatting the C SpecialCasing table and add it to DB_SECTIONS
    my $out = "static const MVMint32 SpecialCasing_table[$count][3][3] = {\n"
            . "{0x0,0x0,0x0},\n    "
            . stack_lines(\@entries, ",", ",\n    ", 0, $WRAP_TO_COLUMNS)
            . "\n};";
    $DB_SECTIONS->{BBB_SpecialCasing} = $out;

    # Add to estimate of total bytes required by C tables/structures
    $ESTIMATED_TOTAL_BYTES += $count * 4 * 3 * 3;

    # Register an enumerated property for Special_Casing
    my $bit_width  = least_int_ge_lg2($count);
    my $index_base = { name => 'Special_Casing', bit_width => $bit_width };
    register_enumerated_property('Special_Casing', $index_base);
}


### NORMALIZATION AND BREAK RULES

# Process the DerivedNormalizationProps file, skipping over deprecated properties
sub DerivedNormalizationProps {
    # Register the binary and inverted binary normalization properties
    my $binary = {
        Full_Composition_Exclusion   => 1,
        Changes_When_NFKC_Casefolded => 1
    };
    my $inverted_binary = {
        NFD_QC  => 1,
        NFKD_QC => 1
    };
    register_binary_property($_) for ((sort keys %$binary),
                                      (sort keys %$inverted_binary));

    # Register the trinary normalization properties as enumerated properties
    my $trinary = {
        NFC_QC  => 1,
        NFKC_QC => 1,
        NFG_QC  => 1
    };
    my $trinary_values = { 'N' => 0, 'Y' => 1, 'M' => 2 };
    register_enumerated_property($_, {
        enum      => $trinary_values,
        bit_width => 2,
        'keys'    => ['N','Y','M']
    }) for sort keys %$trinary;

    # Make use of registered properties above to process the
    # DerivedNormalizationProps file
    for_each_line 'DerivedNormalizationProps', sub {
        $_ = shift;
        my ($range, $property_name, $value) = split / \s* [;#] \s* /x;

        # Figure out what actual value to use based on property type
        # and handle deprecated/unknown properties
        if (exists $binary->{$property_name}) {
            $value = 1;
        }
        elsif (exists $inverted_binary->{$property_name}) {
            $value = undef;
        }
        elsif (exists $trinary->{$property_name}) {
            $value = $trinary_values->{$value};
        }
        elsif ($property_name eq 'NFKC_Casefold') {
            # XXXX: see how this differs from CaseFolding.txt
            # my @parts = split ' ', $value;
            # $value = \@parts;
        }
        elsif (   $property_name eq 'FC_NFKC'
               || $property_name eq 'Expands_On_NFD'
               || $property_name eq 'Expands_On_NFC'
               || $property_name eq 'Expands_On_NFKD'
               || $property_name eq 'Expands_On_NFKC')
        {
            # All of these are deprecated as of Unicode 6.0.0, so skip silently
            return;
        }
        elsif (   $property_name eq 'NFKC_CF'
               || $property_name eq 'NFKC_SCF') {
            # XXXX: NOT HANDLED
        }
        else {
            croak "Unknown DerivedNormalizationProps property '$property_name'"
        }

        # Apply property's computed value to all points in range
        apply_to_cp_range $range, sub {
            my $point = shift;
            $point->{$property_name} = $value;
        };

        # If it's the NFC_QC property, then use this as the default value for
        # NFG_QC also, and apply that to the range as well.  See tweak_nfg_qc()
        # for the other half of this.
        if ($property_name eq 'NFC_QC') {
            apply_to_cp_range $range, sub {
                my $point = shift;
                $point->{'NFG_QC'} = $value;
            };
        }
    };
}

# Wrapper for enumerated_property for general break properties
sub break_property {
    my ($fname, $pname) = @_;
    enumerated_property("auxiliary/${fname}BreakProperty",
                        $pname, { Other => 0 }, 1);
}

# Wrapper for enumerated_property for grapheme cluster break properties
sub grapheme_cluster_break {
    my ($fname, $pname) = @_;
    enumerated_property("auxiliary/${fname}BreakProperty",
                        # XXXX: Should not be set to Other for this one ?
                        $pname, { Other => 0 }, 1);
}

# Tweak NFG_QC values that differ from NFC_QC (as set in DerivedNormalizationProps)
sub tweak_nfg_qc {
    # See http://www.unicode.org/reports/tr29/tr29-27.html#Grapheme_Cluster_Boundary_Rules
    for my $point (values %$POINTS_BY_CODE) {
        my $code    =  $point->{'code'};
        my $special =  $code >= 0x1F1E6 && $code <= 0x1F1FF # Regional indicators
                    || $code == 0x0D    # CARRIAGE RETURN (CR, \r)
                    || $code == 0x200D  # ZERO WIDTH JOINER
                    || $code == 0x0E33  # THAI CHARACTER SARA AM (<compat>)
                    || $code == 0x0EB3; # LAO VOWEL SIGN AM (<compat>)

        if ($special
        || $point->{'Hangul_Syllable_Type'}         # Hangul
        || $point->{'Grapheme_Extend'}              # Grapheme_Extend
        || $point->{'Grapheme_Cluster_Break'}       # Grapheme_Cluster_Break
        || $point->{'Prepended_Concatenation_Mark'} # Prepended_Concatenation_Mark
        || $point->{'gencat_name'} && $point->{'gencat_name'} eq 'Mc' # Spacing_Mark
        ) {
            $point->{'NFG_QC'} = 0;
        }
        # XXXX: For now set all Emoji to NFG_QC = 0; eventually we will only
        #       want to set the ones that are NOT specified as ZWJ sequences
        elsif ($point->{'Emoji'}) {
            $point->{'NFG_QC'} = 0;
        }
    }
}


### POSTPROCESS CODEPOINTS

# Sort all known points by code
sub sort_points {
    @POINTS_SORTED = map  $POINTS_BY_CODE->{$_},
                     sort { $a <=> $b }
                     keys %$POINTS_BY_CODE;
}

# Set next_point for all points (allowing gap skips)
sub set_next_points {
    my $previous;
    my $first_point = {};

    for my $point (@POINTS_SORTED) {
        $POINTS_BY_CODE->{$previous}->{next_point} = $point if defined $previous;
        $previous = $point->{code};
    }

    return $POINTS_SORTED[0];
}

# Pack bitfield properties as tightly as possible into word cells
sub allocate_property_bitfield {
    # Sort enumerated and binary properties by bit_width and then property name
    my @biggest = map  { $ENUMERATED_PROPERTIES->{$_} }
                  sort { $ENUMERATED_PROPERTIES->{$b}->{bit_width}
                     <=> $ENUMERATED_PROPERTIES->{$a}->{bit_width} }
                  sort # Default, by name
                  keys  %$ENUMERATED_PROPERTIES;

    # XXXX: Because int properties are added to BINARY_PROPERTIES in
    #       register_int_property(), a few larger properties are mixed in here
    for (sort keys %$BINARY_PROPERTIES) {
        push @biggest, $BINARY_PROPERTIES->{$_};
    }

    if ($DEBUG) {
        # Display (hopefully) sorted properties
        printf("-- %2d  %s\n", $_->{bit_width}, $_->{name}) for @biggest;
    }

    # State for packing algorithm
    my $word_offset = 0;
    my $bit_offset  = 0;
    my $allocated   = [];
    my $index       = 1;

    # While there are still properties to pack ...
    while (@biggest) {
        # Scan for biggest remaining property that will fit in current word cell
        my $i = -1;
        for(;;) {
            my $prop = $biggest[++$i];

            # If out of available properties that will fit *within* words,
            # scan again for remaining large (wide) properties and pack them
            # *across* words; this acts as a last resort pass for the packer
            if (!$prop) {
                while (@biggest) {
                    # Shift next property off @biggest
                    $prop = shift @biggest;

                    # Set word and bit offsets for current property
                    $prop->{word_offset} = $word_offset;
                    $prop->{bit_offset}  = $bit_offset;

                    # Update bit offset and normalize for crossing word boundaries
                    $bit_offset += $prop->{bit_width};
                    while ($BITFIELD_CELL_BITWIDTH <= $bit_offset) {
                        $word_offset++;
                        $bit_offset -= $BITFIELD_CELL_BITWIDTH;
                    }

                    # Update property's field index and add it to allocated list
                    $prop->{field_index} = $index++;
                    push @$allocated, $prop;
                }

                # Finished this final pass; bail out
                last;
            }

            # If current property will fit in remaining current cell ...
            if ($bit_offset + $prop->{bit_width} <= $BITFIELD_CELL_BITWIDTH) {
                # Remove this property from middle of @biggest
                splice(@biggest, $i, 1);

                # Set word and bit offsets for current property
                $prop->{word_offset} = $word_offset;
                $prop->{bit_offset}  = $bit_offset;

                # Update bit offset and check for reaching word boundary
                $bit_offset += $prop->{bit_width};
                if ($bit_offset == $BITFIELD_CELL_BITWIDTH) {
                    $bit_offset = 0;
                    $word_offset++;
                }

                # Update property's field index and add it to allocated list
                $prop->{field_index} = $index++;
                push @$allocated, $prop;

                # Done with most recent fitting property, exit current scan
                last;
            }
        }
    }

    # Save total bitfield width to first point info
    my $first_point = $POINTS_SORTED[0];
    $first_point->{bitfield_width} = $word_offset + ($bit_offset != 0);

    # Add property code count to header sections and return allocated array
    $H_SECTIONS->{num_property_codes} = "#define MVM_NUM_PROPERTY_CODES $index\n";
    return $allocated;
}

# Pack bitfield-encoded property values into each point's actual bitfield
sub pack_codepoint_properties {
    my ($fields) = @_;

    # Make a bitmask covering a bitfield cell without overflowing
    # the calculation if the cell width is a full machine word
    my $bit  = 0;
    my $mask = 0;
    while ($bit < $BITFIELD_CELL_BITWIDTH) {
        $mask |= 1 << $bit++;
    }

    my $word = 0;
    for my $field (@$fields) {
        my $bit_offset = $field->{bit_offset};
        my $bit_width  = $field->{bit_width};

        if ($bit_offset + $bit_width > $BITFIELD_CELL_BITWIDTH) {
            # XXXX: The algorithm will need to be fixed if this ever triggers
            croak "Word setting algorithm doesn't handle fields that cross between cells";
        }

        if ($DEBUG) {
            # Display word packings as fields are handled
            local $| = 1;
            printf "        -- WORD %2d -----------\n", $word++ unless $bit_offset;
            printf "        %2d bit%s at offset %2d - %s\n",
                $bit_width, $bit_width == 1 ? ' ' : 's', $bit_offset, $field->{name};
        }

        # For all points with a defined value for this property,
        # pack each point's property value into that point's bitfield
        for my $point (@POINTS_SORTED) {
            my $pvalue = $point->{$field->{name}};
            if (defined $pvalue) {
                # $point has a value for this field, so OR that value into place
                # within the point's packed bitfield (in $point->{bytes})
                my $word_offset = $field->{word_offset};
                my $field_shift = $BITFIELD_CELL_BITWIDTH - $bit_offset - $bit_width;
                # XXXX: bytes should probably be called words
                $point->{bytes}->[$word_offset] |= $pvalue << $field_shift;
            }
        }
    }
}

# Save bitfield space by uniquing and storing just index for each point
sub uniquify_bitfields {
    my $index      = 0;
    my $prophash   = {};
    my $references = 0;
    my $last_point = undef;

    for my $point (@POINTS_SORTED) {
        # Compute uniquing key
        my $key = join '.', '', map { $_ // 0 } @{$point->{bytes}};

        if (defined(my $refer = $prophash->{$key})) {
            # If key already seen, just use the original point's index
            $point->{bitfield_index} = $refer->{bitfield_index};
            $references++;
        }
        else {
            # Otherwise, set this point as the primary for a new index
            # XXXX: Why does the index start at 1?
            $point->{bitfield_index} = ++$index;
            $prophash->{$key} = $point;

            # Chain together bitfield emitting points
            $last_point->{next_emit_point} = $point if $last_point;
            $last_point = $point;
        }
    }

    # Compute savings from uniqued structure:
    #     (size_of_unneeded_bitfields - cost_of_indirection_indexes)
    my $cell_width = $BITFIELD_CELL_BITWIDTH / 8;
    my $bf_cells   = $POINTS_SORTED[0]->{bitfield_width};
    my $bf_size    = $cell_width * $bf_cells;
    my $index_size = 2; # Indexes packed as MVMuint16 array
    my $savings    = $bf_size * $references
                   - $index_size * ($index + $references);

    $TOTAL_BYTES_SAVED += $savings;
    print "Saved " . commify_thousands($savings)
        . " bytes by uniquing the bitfield table.\n";
}


### EMITTING UNICODE_DB.C SECTIONS

# Build props_bitfield C table and emit to DB_SECTIONS
sub emit_bitfield {
    # Determine starting point and expected bitfield table row width in cells
    my $point = $POINTS_SORTED[0];
    my $width = $point->{bitfield_width};

    # Initialize with a starting line of all 0 cells
    my $line  = join ',', (0) x $width;
    my @lines = ("{$line}");

    # Build bitfield cell rows and add to bitfield table
    my $rows  = 1;
    while ($point) {
        $line = "/*$rows*/{";
        my $first = 1;
        for (my $i = 0; $i < $width; ++$i) {
            $_ = $point->{bytes}->[$i];
            $line .= "," unless $first;
            $first = 0;
            $line .= defined $_ ? $_."u" : 0;
        }

        push @$BITFIELD_TABLE, $point;
        push @lines, ($line . "}/* $point->{code_str} */");
        $point = $point->{next_emit_point};
        $rows++;
    }

    # Calculate worst case packing for bitfield table
    # (each bitfield padded to the next power of two bytes)
    my $bytes_wide  = 2;
       $bytes_wide *= 2 while $bytes_wide < $width;
    $ESTIMATED_TOTAL_BYTES += $rows * $bytes_wide;

    # Finish formatting props_bitfield table and add to DB_SECTIONS
    my $val_type = 'MVMuint' . $BITFIELD_CELL_BITWIDTH;
    my $out = "static const $val_type props_bitfield[$rows][$width] = {\n    "
            . stack_lines(\@lines, ",", ",\n    ", 0, $WRAP_TO_COLUMNS)."\n};";
    $DB_SECTIONS->{BBB_main_bitfield} = $out;
}

# Emit suc/slc/stc values at codepoint locations of case changes
sub emit_case_changes {
    my @lines = ();
    my $rows  = 1;
    for my $point (@POINTS_SORTED) {
        next unless $point->{Case_Change_Index};

        push @lines, "/*$rows*/{0x" . ($point->{suc} || 0) .
                              ",0x" . ($point->{slc} || 0) .
                              ",0x" . ($point->{stc} || 0) .
                              "}/* $point->{code_str} */";
        $rows++;
    }

    my $out = "static const MVMint32 case_changes[$rows][3] = {\n" .
              "    {0x0,0x0,0x0},\n    " .
              stack_lines(\@lines, ",", ",\n    ", 0, $WRAP_TO_COLUMNS) .
              "\n};";
    $DB_SECTIONS->{BBB_case_changes} = $out;
}


### WRITING FILES

# Write a C header file for macros for quick lookup properties
sub macroize_quick_props {
    # Configure categories to be turned into quick macros
    my @wanted_val_str = (
        [ 'gencat_name', 'Zl', 'Zp' ],
    );
    my @wanted_val_bool = (
        [ 'White_Space', 1 ]
    );

    # Determine which codepoints have each wanted property and value
    my %gencat_wanted_h;
    for my $point (@POINTS_SORTED) {
        my $code = $point->{code};
        for my $cat_data (@wanted_val_str) {
            my $propname = $cat_data->[0];
            my $cp_value = $point->{$propname};

            for (my $i = 1; $i < @$cat_data; $i++) {
                my $pval = $cat_data->[$i];
                push @{$gencat_wanted_h{$propname . '_' . $pval}}, $code
                    if $cp_value eq $pval;
            }
        }
        for my $cat_data (@wanted_val_bool) {
            my $propname = $cat_data->[0];
            push @{$gencat_wanted_h{$propname}}, $code if $point->{$propname};
        }
    }

    # Build up array of test macros (MVM_CP_is_*)
    my @result;
    for my $pname (sort keys %gencat_wanted_h) {
        my @text = map "((cp) == $_)", @{$gencat_wanted_h{$pname}};

        # XXXX: Possibly inefficient for White_Space property, which includes ranges
        push @result, ("#define MVM_CP_is_$pname(cp) (" . join(' || ', @text) . ')');
    }

    # Write test macros to special C header file
    write_file("src/strings/unicode_prop_macros.h", (join("\n", @result) . "\n"));
}

# Spurt UTF-8 contents to a file
sub write_file {
    my ($fname, $contents) = @_;

    # Ensure generated files always end with a newline.
    $contents .= "\n" unless $contents =~ /\n\z/;

    # Open the file for writing with UTF-8 encoding
    open my $FILE, '>', $fname or croak "Couldn't open file '$fname': $!";
    binmode $FILE, ':encoding(UTF-8)';

    # Clean up trailing horizontal whitespace, spurt the contents, and close
    print $FILE trim_trailing($contents);
    close $FILE;
}


### NOT YET REFACTORED

sub rest_of_main {
    my ($allocated_bitfield_properties, $hout) = @_;

    # XXX StandardizedVariants.txt # no clue what this is

    # Emit all the things
    my $first_point = $POINTS_SORTED[0];
    my $extents = emit_codepoints_and_planes();
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
        commify_thousands($ESTIMATED_TOTAL_BYTES).
        ".\nEstimated bytes saved by various compressions: ".
        commify_thousands($TOTAL_BYTES_SAVED).".\n";
    if ($DEBUG) {
        write_file("ucd2c_extents.log", $LOG);
    }
    print "\nDONE!!!\n\n";
    print "Make sure you update tests in roast by following docs/unicode-generated-tests.asciidoc in the roast repo\n";
    return 1;
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
    print "\nSaved " . commify_thousands($bytes_saved) . " bytes by compressing big gaps into a binary search lookup.\n";
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
    for_each_line('Blocks', sub {
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
    for_each_line('PropertyAliases', sub { $_ = shift;
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
    for_each_line('PropertyValueAliases', sub { $_ = shift;
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
    # These are the only ones (iirc) that are guaranteed in Rakudo.
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
    for_each_line('PropertyValueAliases', sub { $_ = shift;
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
            croak "ALL_PROPERTIES has no enum for property '$propname'" unless $enum;
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
    # XXXX: Can we use @POINTS_SORTED here?
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


main();

# vim: ft=raku expandtab sw=4
