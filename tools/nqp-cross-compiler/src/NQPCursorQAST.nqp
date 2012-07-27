
# This class builds up the QAST representing the
# construction of the NQPCursor and related types.
# Below the class are several "shortcut" routines 
# that facilitate QAST construction, and several
# routines that build up classes' method bodies
class NQPCursorQAST {
    method build_types() {
        
        my @ins := nqp::list();
        
        my $cursor_type_name := 'NQPCursor';
        my $cursor_type_local_name := $*QASTCOMPILER.unique("type_$cursor_type_name");
        merge_qast(@ins, simple_type_from_repr(
            $cursor_type_name, $cursor_type_local_name, 'P6opaque'));
        
        my $cursor_type := local($cursor_type_local_name);
        
        merge_qast(@ins, add_attribute($cursor_type, '$!orig'));
        merge_qast(@ins, add_attribute($cursor_type, '$!target')); # later, str
        merge_qast(@ins, add_attribute($cursor_type, '$!from')); # later, int
        merge_qast(@ins, add_attribute($cursor_type, '$!pos')); # later, int
        merge_qast(@ins, add_attribute($cursor_type, '$!match'));
        merge_qast(@ins, add_attribute($cursor_type, '$!name'));
        merge_qast(@ins, add_attribute($cursor_type, '$!bstack'));
        merge_qast(@ins, add_attribute($cursor_type, '$!cstack'));
        merge_qast(@ins, add_attribute($cursor_type, '$!regexsub'));
        
        merge_qast(@ins, add_method($cursor_type, 'target', Cursor_target()));
        
        #compose(@ins, $cursor_type);
        
        @ins
    }
    
    sub annot($file, $line, *@ins) {
        QAST::Annotated.new(:file($file), :line($line), :instructions(@ins));
    }
    
    sub ival($val, $named = "") { QAST::IVal.new( :value($val), :named($named) ) }
    sub nval($val, $named = "") { QAST::NVal.new( :value($val), :named($named) ) }
    sub sval($val, $named = "") { QAST::SVal.new( :value($val), :named($named) ) }
    sub bval($val, $named = "") { QAST::BVal.new( :value($val), :named($named) ) }
    
    sub push_ilist(@dest, $src) {
        nqp::splice(@dest, $src.instructions, +@dest, 0);
    }
    
    sub merge_qast(@ins, $qast) {
        my $mast := $*QASTCOMPILER.as_mast( $qast );
        push_ilist(@ins, $mast);
        $mast;
    }
    
    sub local($name) { QAST::Var.new( :name($name), :scope('local') ) }
    
    sub locald($name, $type = NQPMu) {
        QAST::Var.new( :name($name), :decl('var'), :returns($type), :scope('local') )
    }
    
    sub localn($name, $named) {
        QAST::Var.new( :name($name), :scope('local'), :named($named) )
    }
    
    sub localp($name, $type = NQPMu) {
        QAST::Var.new( :name($name), :decl('param'), :returns($type), :scope('local') )
    }
    
    sub stmt(*@stmts) { QAST::Stmt.new( |@stmts ) }
    sub stmts(*@stmts) { QAST::Stmts.new( |@stmts ) }
    
    sub op($op, :$returns = NQPMu, *@args) {
        QAST::Op.new( :op($op), :returns($returns), |@args);
    }
    
    sub vm($op, *@args) { QAST::VM.new( :moarop($op), |@args) }
    sub uniq($str) { $*QASTCOMPILER.unique($str) }
    sub block(*@ins) { QAST::Block.new(|@ins) }
    
    sub simple_type_from_repr($name_str, $var_name, $repr_str) {
        my $howlocal := uniq('how');
        stmts(
            locald($var_name),
            stmt(
                op('bind',
                    locald($howlocal),
                    vm('knowhow') ),
                op('bind',
                    local($var_name),
                    op('call',
                        vm('findmeth',
                            local($howlocal),
                            sval('new_type') ),
                        local($howlocal),
                        sval($name_str, 'name' ),
                        sval($repr_str, 'repr' ) ) ) ) )
    }
    
    sub add_attribute($type, $name_str) {
        my $howlocal := uniq('how');
        my $attr := uniq('attr');
        stmt(
            op('bind',
                locald($howlocal),
                vm('gethow',
                    $type ) ),
            op('bind',
                locald($attr),
                vm('knowhowattr') ),
            op('call',
                vm('findmeth',
                    local($howlocal),
                    sval('add_attribute') ),
                local($howlocal),
                $type,
                op('call',
                    vm('findmeth',
                        local($attr),
                        sval('new') ),
                    local($attr),
                    sval($name_str, 'name'),
                    localn($attr, 'type') ) ) )
    }
    
    sub add_method($type, $name_str, $qast_block) {
        my $howlocal := uniq('how');
        stmt(
            $qast_block,
            op('bind',
                locald($howlocal),
                vm('gethow',
                    $type ) ),
            op('call',
                vm('findmeth',
                    local($howlocal),
                    sval('add_method') ),
                local($howlocal),
                $type,
                sval($name_str),
                bval($qast_block) ) )
    }
    
    sub Cursor_target() {
        block(
            localp('self'),
            vm('getattr',
                local('self'),
                vm('getwhat',
                    local('self') ),
                sval('target'),
                ival(-1) ) )
    }
    
}

class QAST::Annotated is QAST::Node {
    has str $!file;
    has int $!line;
    has @!instructions;
    
    method new(:$file = '<anon>', :$line!, :@instructions!) {
        my $obj := nqp::create(self);
        nqp::bindattr_s($obj, QAST::Annotated, '$!file', $file);
        nqp::bindattr_i($obj, QAST::Annotated, '$!line', $line);
        nqp::bindattr($obj, QAST::Annotated, '@!instructions', @instructions);
        $obj
    }
    method file() { $!file }
    method line() { $!line }
    method instructions() { @!instructions }
}
