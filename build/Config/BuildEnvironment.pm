package Config::BuildEnvironment;
use strict;
use warnings;

sub detect {
    my $opts = shift;
    my %config;

    if ($^O =~ /MSWin32/) {
        # Defaults for Windows
        %config = (
            # Misc
            os   => 'Windows',

            # Filename conventions
            exe  => '.exe',
            o    => '.obj',

            # Command names
            rm   => 'del',
            cat  => 'type',
        );

        # We support the Microsoft toolchain only on Windows right now.
        if (can_run('cl /nologo /?')) {
            # Ensure we have the other bits.
            return (excuse => 'It appears you have the MS C compiler, but no link!')
                unless can_run('link /nologo /?');
            return (excuse => 'It appears you have the MS C compiler, but no nmake!')
                unless can_run('nmake /nologo /?');

            # Config settings for MS toolchain
            %config = (
                # Defaults
                %config,

                # Command names
                cc          => 'cl',
                link        => 'link',
                make        => 'nmake',

                # Compiler attribute declaration differences
                noreturn    => '__declspec(noreturn)',
                noreturngcc => '',

                # Required flags
                couto       => '-Fo',
                louto       => '-out:',
                cmiscflags  => '/nologo -DWIN32',
                lmiscflags  => '/nologo /NODEFAULTLIB kernel32.lib ws2_32.lib msvcrt.lib mswsock.lib rpcrt4.lib oldnames.lib advapi32.lib shell32.lib',
                # XXXX: Why are libraries stuffed into lmiscflags above?
                llibs       => '',

                # Optional settings
                copt        => $opts->{optimize}   ? '/Ox /GL'  : '',
                cdebug      => $opts->{debug}      ? '/Zi'      : '',
                cinstrument => $opts->{instrument} ? ''         : '',
                lopt        => $opts->{optimize}   ? '/LTCG'    : '',
                ldebug      => $opts->{debug}      ? '/debug'   : '',
                linstrument => $opts->{instrument} ? '/Profile' : '',
            );
        }
        else {
            return (excuse => 'So far, we only support building with the Microsoft toolchain on Windows.');
        }
        
        # On 32-bit, define AO_ASSUME_WINDOWS98.
        if (`cl 2>&1` =~ /80x86/) {
            $config{'cmiscflags'} .= ' -DAO_ASSUME_WINDOWS98';
        }
    }
    elsif ($^O =~ /linux/) {
        # Defaults for Linux
        %config = (
            # Misc
            os          => 'Linux',

            # Filename conventions
            exe         => '',
            o           => '.o',

            # Command names
            rm          => 'rm -f',
            cat         => 'cat',
            make        => 'make',

            # Compiler attribute declaration differences
            noreturn    => '',
            noreturngcc => '__attribute__((noreturn))',

            # Required flags
            couto       => '-o ',
            louto       => '-o ',
        );

        if (can_run('gcc')) {
            # Config settings for GCC toolchain
            %config = (
                # Defaults
                %config,

                # Command names
                cc          => 'gcc',
                link        => 'gcc',

                # Required flags
                cmiscflags  => '-D_REENTRANT -D_LARGEFILE64_SOURCE -Wparentheses -Wreturn-type',
                lmiscflags  => '-L 3rdparty/apr/.libs',
                llibs       => '-Wl,-Bstatic -lapr-1 -Wl,-Bdynamic -lpthread -lm',

                # Optional settings
                # XXXX: What instrumentation is available for GCC?
                copt        => $opts->{optimize}   ? '-O3'                : '',
                cdebug      => $opts->{debug}      ? '-g'                 : '',
                cinstrument => $opts->{instrument} ? ''                   : '',
                lopt        => $opts->{optimize}   ? '-O3'                : '',
                ldebug      => $opts->{debug}      ? '-g'                 : '',
                linstrument => $opts->{instrument} ? ''                   : '',
            );
        }
        elsif (can_run('clang')) {
            # Config settings for Clang toolchain
            %config = (
                # Defaults
                %config,

                # Command names
                cc          => 'clang',
                link        => 'clang',

                # Required flags
                cmiscflags  => '-fno-omit-frame-pointer -fno-optimize-sibling-calls',
                lmiscflags  => '-L 3rdparty/apr/.libs',
                llibs       => '-Wl,-Bstatic -lapr-1 -Wl,-Bdynamic -lpthread -lm',

                # Optional settings
                copt        => $opts->{optimize}   ? '-O3'                : '',
                cdebug      => $opts->{debug}      ? '-g'                 : '',
                cinstrument => $opts->{instrument} ? '-fsanitize=address' : '',
                lopt        => $opts->{optimize}   ? '-O3'                : '',
                ldebug      => $opts->{debug}      ? '-g'                 : '',
                linstrument => $opts->{instrument} ? '-fsanitize=address' : '',
            );
        }
        else {
            return (excuse => 'So far, we only support building with clang or gcc on Linux.');
        }
    }
    elsif ($^O =~ /darwin/) {
        # Defaults for Darwin
        %config = (
            # Misc
            os          => 'Darwin',

            # Filename conventions
            exe         => '',
            o           => '.o',

            # Command names
            rm          => 'rm -f',
            cat         => 'cat',
            make        => 'make',

            # Compiler attribute declaration differences
            noreturn    => '',
            noreturngcc => '__attribute__((noreturn))',

            # Required flags
            couto       => '-o ',
            louto       => '-o ',
        );

        if (can_run('clang')) {
            # Config settings for Clang toolchain
            %config = (
                # Defaults
                %config,

                # Command names
                cc          => 'clang',
                link        => 'clang',

                # Required flags
                cmiscflags  => '-fno-omit-frame-pointer -fno-optimize-sibling-calls',
                lmiscflags  => '-L 3rdparty/apr/.libs',
                llibs       => '-lapr-1 -lpthread -lm',

                # Optional settings
                copt        => $opts->{optimize}   ? '-O3'                : '',
                cdebug      => $opts->{debug}      ? '-g'                 : '',
                cinstrument => $opts->{instrument} ? '-fsanitize=address' : '',
                lopt        => $opts->{optimize}   ? '-O3'                : '',
                ldebug      => $opts->{debug}      ? '-g'                 : '',
                linstrument => $opts->{instrument} ? '-fsanitize=address' : '',
            );
        }
        elsif (can_run('gcc')) {
            # Config settings for GCC toolchain
            %config = (
                # Defaults
                %config,

                # Command names
                cc          => 'gcc',
                link        => 'gcc',

                # Required flags
                cmiscflags  => '-D_REENTRANT -D_LARGEFILE64_SOURCE -Wparentheses -Wreturn-type',
                lmiscflags  => '-L 3rdparty/apr/.libs',
                llibs       => '-Wl,-Bstatic -lapr-1 -Wl,-Bdynamic -lpthread -lm',

                # Optional settings
                # XXXX: What instrumentation is available for GCC?
                copt        => $opts->{optimize}   ? '-O3'                : '',
                cdebug      => $opts->{debug}      ? '-g'                 : '',
                cinstrument => $opts->{instrument} ? ''                   : '',
                lopt        => $opts->{optimize}   ? '-O3'                : '',
                ldebug      => $opts->{debug}      ? '-g'                 : '',
                linstrument => $opts->{instrument} ? ''                   : '',
            );
        }
        else {
            return (excuse => 'So far, we only support building with clang or gcc on Darwin.');
        }
    }
    else {
        return (excuse => 'No recognized operating system or compiler found.'."  found: $^O");
    }

    $config{cflags}  = join ' ' => @config{qw( cmiscflags cinstrument cdebug copt )};
    $config{ldflags} = join ' ' => @config{qw( lmiscflags linstrument ldebug lopt )};

    return %config;
}

sub can_run {
    my $try = shift;
    my $out = `$try 2>&1`;
    return defined $out && $out ne '';
}

'Leffe';
