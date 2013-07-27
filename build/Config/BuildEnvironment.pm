package Config::BuildEnvironment;
use strict;
use warnings;

my %BUILDDEF = (
  'Unix'
     => {
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
        },
  'Windows'
        # Defaults for Windows
     => {
            # Misc
            os   => 'Windows',

            # Filename conventions
            exe  => '.exe',
            o    => '.obj',

            # Command names
            rm   => 'del',
            cat  => 'type',
        },
);

my %TOOLCHAINS = (
  'cl' =>
            {
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
                lmiscflags  => '/nologo /NODEFAULTLIB kernel32.lib ws2_32.lib mswsock.lib rpcrt4.lib oldnames.lib advapi32.lib shell32.lib libcmt.lib',
                # XXXX: Why are libraries stuffed into lmiscflags above?
                llibs       => '',

                # Optional settings
                copt        => '/Ox /GL',
                cdebug      => '/Zi',
                cinstrument => '',
                lopt        => '/LTCG',
                ldebug      => '/debug',
                linstrument => '/Profile',
            },
  'gcc' =>
            {
                # Command names
                cc          => 'gcc',
                link        => 'gcc',

                # Required flags
                cmiscflags  => '-D_REENTRANT -D_LARGEFILE64_SOURCE -Wparentheses -Wreturn-type',
                lmiscflags  => '-L3rdparty/apr/.libs',
                llibs       => '-Wl,-Bstatic -lapr-1 -Wl,-Bdynamic -lpthread -lm',

                # Optional settings
                # XXXX: What instrumentation is available for GCC?
                copt        => '-O3',
                cdebug      => '-g',
                cinstrument => '',
                lopt        => '-O3',
                ldebug      => '-g',
                linstrument => '',
            },
  'clang' =>
            {
                # Command names
                cc          => 'clang',
                link        => 'clang',

                # Required flags
                cmiscflags  => '-fno-omit-frame-pointer -fno-optimize-sibling-calls',
                lmiscflags  => '-L3rdparty/apr/.libs',
                llibs       => '-Wl,-Bstatic -lapr-1 -Wl,-Bdynamic -lpthread -lm',

                # Optional settings
                copt        => '-O3',
                cdebug      => '-g',
                cinstrument => '-fsanitize=address',
                lopt        => '-O3',
                ldebug      => '-g',
                linstrument => '-fsanitize=address',
            },
);

# originally taken from Module::Build by Ken Williams et al.
my %OSTYPES = qw[
  aix         Unix
  bsdos       Unix
  beos        Unix
  dgux        Unix
  dragonfly   Unix
  dynixptx    Unix
  freebsd     Unix
  linux       Unix
  haiku       Unix
  hpux        Unix
  iphoneos    Unix
  irix        Unix
  darwin      Unix
  machten     Unix
  midnightbsd Unix
  mirbsd      Unix
  next        Unix
  openbsd     Unix
  netbsd      Unix
  dec_osf     Unix
  nto         Unix
  svr4        Unix
  svr5        Unix
  sco_sv      Unix
  unicos      Unix
  unicosmk    Unix
  solaris     Unix
  sunos       Unix
  cygwin      Unix
  os2         Unix
  interix     Unix
  gnu         Unix
  gnukfreebsd Unix
  nto         Unix
  qnx         Unix

  dos         Windows
  MSWin32     Windows

  os390       EBCDIC
  os400       EBCDIC
  posix-bc    EBCDIC
  vmesa       EBCDIC

  MacOS       MacOS
  VMS         VMS
  vos         VOS
  riscos      RiscOS
  amigaos     Amiga
  mpeix       MPEiX
];

sub detect {
    my $opts = shift;
    my %config;

    my $ostype = os_type();

    if ($ostype eq 'Windows') {
        # Defaults for Windows

        %config = %{ $BUILDDEF{ $ostype } };

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
                %config, %{ $TOOLCHAINS{'cl'} },
            );
            options( \%config, $opts );
        }
        else {
            return (excuse => 'So far, we only support building with the Microsoft toolchain on Windows.');
        }

        # On 32-bit, define AO_ASSUME_WINDOWS98.
        if (`cl 2>&1` =~ /x86/) {
            $config{'cmiscflags'} .= ' -DAO_ASSUME_WINDOWS98';
        }
    }
    elsif ($ostype eq 'Unix') {

        %config = %{ $BUILDDEF{ $ostype } };
        $config{os} = $^O; # to be generic, one must go with the flow

        my $gcc   = can_run('gcc');
        my $clang = can_run('clang');
        my $compiler;

        $compiler = 'clang' if $clang && ( $opts->{clang} || $^O =~ m!^(freebsd|darwin)$! || !$gcc );
        $compiler = 'gcc'   if !$compiler && $gcc;

        return ( excuse => 'No recognized operating system or compiler found.'."  found: $^O")
          unless $compiler;

        %config = (
           # Defaults
           %config, %{ $TOOLCHAINS{$compiler} },
        );
        options( \%config, $opts );

        $config{llibs} = '-lapr-1 -lpthread -lm' if $^O eq 'darwin' and $compiler eq 'clang';
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

sub os_type {
  my ($os) = @_;
  $os = $^O unless defined $os;
  return $OSTYPES{ $os } || q{};
}

sub is_os_type {
  my ($type, $os) = @_;
  return unless $type;
  $os = $^O unless defined $os;
  return os_type($os) eq $type;
}

sub options {
  my ($config,$opts) = @_;
  $config->{copt} = '' unless $opts->{optimize};
  $config->{cdebug} = '' unless $opts->{debug};
  $config->{cinstrument} = '' unless $opts->{instrument};
  $config->{lopt} = '' unless $opts->{optimize};
  $config->{ldebug} = '' unless $opts->{debug};
  $config->{linstrument} = '' unless $opts->{instrument};
  return;
}

'Leffe';
