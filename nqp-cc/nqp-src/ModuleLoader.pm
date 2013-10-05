knowhow ModuleLoader {
    my %modules_loaded;
    my %settings_loaded;

    method search_path($explicit_path) {
        my @search_paths;

        # Put any explicitly specified path on the start of the list.
        my $explicit;
        try { $explicit := nqp::ifnull(nqp::ifnull(%*COMPILING, {})<%?OPTIONS>, {}){$explicit_path}; }
        if !nqp::isnull($explicit) && nqp::defined($explicit) {
            nqp::push(@search_paths, $explicit);
        }

        # Add CWD.
        nqp::push(@search_paths, '.');

        @search_paths
    }

    method ctxsave() {
        $*MAIN_CTX := nqp::ctxcaller(nqp::ctx());
        $*CTXSAVE := 0;
    }

    method load_module($module_name, *@global_merge_target) {
        # If we didn't already do so, load the module and capture
        # its mainline. Otherwise, we already loaded it so go on
        # with what we already have.
        my $module_ctx;
        my $path := nqp::join('/', nqp::split('::', $module_name)) ~ '.moarvm';
        my @prefixes := self.search_path('module-path');
        for @prefixes -> $prefix {
            if nqp::stat("$prefix/$path", 0) {
                $path := "$prefix/$path";
                last;
            }
        }
        if nqp::existskey(%modules_loaded, $path) {
            $module_ctx := %modules_loaded{$path};
        }
        else {
            my $*CTXSAVE := self;
            my $*MAIN_CTX := ModuleLoader;
            my $preserve_global := nqp::getcurhllsym('GLOBAL');
            nqp::loadbytecode($path);
            nqp::bindcurhllsym('GLOBAL', $preserve_global);
            %modules_loaded{$path} := $module_ctx := $*MAIN_CTX;
        }

        # Provided we have a mainline...
        if nqp::defined($module_ctx) {
            # Merge any globals.
            my $UNIT := nqp::ctxlexpad($module_ctx);
            unless nqp::isnull($UNIT<GLOBALish>) {
                if nqp::elems(@global_merge_target) {
                    merge_globals(@global_merge_target[0], $UNIT<GLOBALish>);
                }
            }
        }

        $module_ctx;
    }

    # XXX This is a really dumb and minimalistic GLOBAL merger.
    # For a much more complete one, see sorear++'s work in
    # Niecza. This one will likely evolve towards that, but for
    # now I just need something that's just good enough for the
    # immediate needs of NQP bootstrapping.
    my $stub_how := 'KnowHOW';
    sub merge_globals($target, $source) {
        # XXX For now just merge the top level symbols;
        # if there's a conflict then don't dig any deeper.
        # Obviously, just a first cut at this. :-)
        my %known_symbols;
        for $target.WHO {
            %known_symbols{nqp::iterkey_s($_)} := 1;
        }
        for $source.WHO {
            my $sym := nqp::iterkey_s($_);
            my $val := nqp::iterval($_);
            if !nqp::existskey(%known_symbols, $sym) {
                my $source_is_stub := 0;
                # XXX TODO: Exceptions.
                #try {
                    my $source_mo := $val.HOW;
                    $source_is_stub := $source_mo.WHAT.HOW.name($source_mo) eq $stub_how &&
                        !nqp::isnull(nqp::who($val)) && nqp::who($val);
                #}
                if $source_is_stub {
                    my $source := $val;
                    my $source_clone := $source.HOW.new_type(:name($source.HOW.name($source)));
                    $source_clone.HOW.compose($source_clone);
                    my %WHO_clone;
                    for nqp::who($source) {
                        %WHO_clone{nqp::iterkey_s($_)} := nqp::iterval($_);
                    }
                    nqp::setwho($source_clone, %WHO_clone);
                    ($target.WHO){$sym} := $source_clone;
                }
                else {
                    ($target.WHO){$sym} := $val;
                }
            }
            elsif ($target.WHO){$sym} =:= $val {
                # No problemo; a symbol can't conflict with itself.
            }
            else {
                my $source_mo := $val.HOW;
                my $source_is_stub := $source_mo.WHAT.HOW.name($source_mo) eq $stub_how;
                my $target_mo := ($target.WHO){$sym}.HOW;
                my $target_is_stub := $target_mo.WHAT.HOW.name($target_mo) eq $stub_how;
                if $source_is_stub && $target_is_stub {
                    # Leave target as is, and merge the nested symbols.
                    merge_globals(($target.WHO){$sym}, $val);
                }
                else {
                    nqp::die("Merging GLOBAL symbols failed: duplicate definition of symbol $sym");
                }
            }
        }
    }

    method load_setting($setting_name) {
        my $setting;

        if $setting_name ne 'NULL' {
            # Add path prefix and .setting suffix.
            my $path := "$setting_name.setting.moarvm";
            my @prefixes := self.search_path('setting-path');
            for @prefixes -> $prefix {
            #    if nqp::stat("$prefix/$path", 0) {
                    $path := "$prefix/$path";
            #        last;
            #    }
            }

            # Unless we already did so, load the setting.
            unless nqp::existskey(%settings_loaded, $path) {
                my $*CTXSAVE := self;
                my $*MAIN_CTX := ModuleLoader;
                my $preserve_global := nqp::getcurhllsym('GLOBAL');
                nqp::loadbytecode($path);
                nqp::bindcurhllsym('GLOBAL', $preserve_global);
                unless nqp::defined($*MAIN_CTX) {
                    nqp::die("Unable to load setting $setting_name; maybe it is missing a YOU_ARE_HERE?");
                }
                %settings_loaded{$path} := $*MAIN_CTX;
            }

            $setting := %settings_loaded{$path};
        }

        $setting;
    }
}

# Since this *is* the module loader, we can't locate it the normal way by
# GLOBAL merging. So instead we stash it away in the Parrot namespace tree.
nqp::bindcurhllsym('ModuleLoader', ModuleLoader);
