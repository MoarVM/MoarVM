#include "moarvm.h"

/* This file implements the various steps involved in getting 6model
 * bootstrapped from the ground up - that is, getting to having a
 * KnowHOW meta-object type so that userland can start building up
 * more interesting meta-objects. Mostly it just has to make objects
 * with some "holes", and later go back and fill them out. This is
 * due to the circular nature of things.
 */
 
/* Can do something better than statics later... */
static MVMString *str_repr       = NULL;
static MVMString *str_name       = NULL;
static MVMString *str_anon       = NULL;
static MVMString *str_P6opaque   = NULL;
static MVMString *str_type       = NULL;
static MVMString *str_box_target = NULL;
static MVMString *str_attribute  = NULL;
static MVMString *str_array      = NULL;

/* Creates a stub VMString. Note we didn't initialize the
 * representation yet, so have to do this somewhat pokily. */
static void create_stub_VMString(MVMThreadContext *tc) {
    /* Need to create the REPR function table "in advance"; the
     * MVMString REPR specially knows not to duplicately create
     * this. */
    MVMREPROps *repr = MVMString_initialize(tc);
    
    /* Now we can create a type object; note we have no HOW yet,
     * though. */
    MVMSTable *st  = MVM_gc_allocate_stable(tc, repr, NULL);
    
    /* REPR normally sets up size, but we'll have to do that manually
     * here also. */
    st->size = sizeof(MVMString);
    
    /* We can now go for the type object. */
    tc->instance->VMString = MVM_gc_allocate_type_object(tc, st);
    
    /* Set the WHAT in the STable we just made to point to the type
     * object (this is completely normal). */
    st->WHAT = tc->instance->VMString;
}

/* Creates a stub BOOTInt (missing a meta-object). */
static void create_stub_BOOTInt(MVMThreadContext *tc) {
    MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_P6int);
    tc->instance->boot_types->BOOTInt = repr->type_object_for(tc, NULL);
}

/* Creates a stub BOOTNum (missing a meta-object). */
static void create_stub_BOOTNum(MVMThreadContext *tc) {
    MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_P6num);
    tc->instance->boot_types->BOOTNum = repr->type_object_for(tc, NULL);
}

/* Creates a stub BOOTStr (missing a meta-object). */
static void create_stub_BOOTStr(MVMThreadContext *tc) {
    MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_P6str);
    tc->instance->boot_types->BOOTStr = repr->type_object_for(tc, NULL);
}

/* Creates a stub BOOTArray (missing a meta-object). */
static void create_stub_BOOTArray(MVMThreadContext *tc) {
    MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_MVMArray);
    tc->instance->boot_types->BOOTArray = repr->type_object_for(tc, NULL);
}

/* Creates a stub BOOTHash (missing a meta-object). */
static void create_stub_BOOTHash(MVMThreadContext *tc) {
    MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_MVMHash);
    tc->instance->boot_types->BOOTHash = repr->type_object_for(tc, NULL);
}

/* Creates a stub BOOTCCode (missing a meta-object). */
static void create_stub_BOOTCCode(MVMThreadContext *tc) {
    MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_MVMCFunction);
    tc->instance->boot_types->BOOTCCode = repr->type_object_for(tc, NULL);
}

/* Creates a stub BOOTCode (missing a meta-object). */
static void create_stub_BOOTCode(MVMThreadContext *tc) {
    MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_MVMCode);
    tc->instance->boot_types->BOOTCode = repr->type_object_for(tc, NULL);
}

/* Creates a stub BOOTThread (missing a meta-object). */
static void create_stub_BOOTThread(MVMThreadContext *tc) {
    MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_MVMThread);
    tc->instance->boot_types->BOOTThread = repr->type_object_for(tc, NULL);
}

/* Creates a stub BOOTIter (missing a meta-object). */
static void create_stub_BOOTIter(MVMThreadContext *tc) {
    MVMObject *type;
    MVMBoolificationSpec *bs;
    MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_MVMIter);
    type = tc->instance->boot_types->BOOTIter = repr->type_object_for(tc, NULL);
    bs = malloc(sizeof(MVMBoolificationSpec));
    bs->mode = MVM_BOOL_MODE_ITER;
    bs->method = NULL;
    type->st->boolification_spec = bs;
}

/* Creates a stub BOOTContext (missing a meta-object). */
static void create_stub_BOOTContext(MVMThreadContext *tc) {
    MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_MVMContext);
    tc->instance->boot_types->BOOTContext = repr->type_object_for(tc, NULL);
}

/* Creates a stub SCRef (missing a meta-object). */
static void create_stub_SCRef(MVMThreadContext *tc) {
    MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_SCRef);
    tc->instance->SCRef = repr->type_object_for(tc, NULL);
}

/* KnowHOW.new_type method. Creates a new type with this HOW as its meta-object. */
static void new_type(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
    MVMObject   *self, *HOW, *type_object, *BOOTHash, *stash;
    MVMRegister *repr_arg, *name_arg;
    MVMString   *repr_name, *name;
    MVMREPROps  *repr_to_use;
    
    /* Get arguments. */
    MVMArgProcContext arg_ctx; arg_ctx.named_used = NULL;
    MVM_args_proc_init(tc, &arg_ctx, callsite, args);
    self     = MVM_args_get_pos_obj(tc, &arg_ctx, 0, MVM_ARG_REQUIRED)->o;
    repr_arg = MVM_args_get_named_str(tc, &arg_ctx, str_repr, MVM_ARG_OPTIONAL);
    name_arg = MVM_args_get_named_str(tc, &arg_ctx, str_name, MVM_ARG_OPTIONAL);
    MVM_args_proc_cleanup(tc, &arg_ctx);
    if (REPR(self)->ID != MVM_REPR_ID_KnowHOWREPR)
        MVM_exception_throw_adhoc(tc, "KnowHOW methods must be called on object with REPR KnowHOWREPR");
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&self);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&repr_arg);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&name_arg);
    
    /* We first create a new HOW instance. */
    HOW  = REPR(self)->allocate(tc, STABLE(self));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&HOW);
    
    /* See if we have a representation name; if not default to P6opaque. */
    repr_name = repr_arg ? repr_arg->s : str_P6opaque;
        
    /* Create a new type object of the desired REPR. (Note that we can't
     * default to KnowHOWREPR here, since it doesn't know how to actually
     * store attributes, it's just for bootstrapping knowhow's. */
    repr_to_use = MVM_repr_get_by_name(tc, repr_name);
    type_object = repr_to_use->type_object_for(tc, HOW);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&type_object);
    
    /* See if we were given a name; put it into the meta-object if so. */
    name = name_arg ? name_arg->s : str_anon;
    REPR(HOW)->initialize(tc, STABLE(HOW), HOW, OBJECT_BODY(HOW));
    MVM_ASSIGN_REF(tc, HOW, ((MVMKnowHOWREPR *)HOW)->body.name, name);
    
    /* Set .WHO to an empty hash. */
    BOOTHash = tc->instance->boot_types->BOOTHash;
    stash = REPR(BOOTHash)->allocate(tc, STABLE(BOOTHash));
    REPR(stash)->initialize(tc, STABLE(stash), stash, OBJECT_BODY(stash));
    MVM_ASSIGN_REF(tc, STABLE(type_object), STABLE(type_object)->WHO, stash);

    /* Return the type object. */
    MVM_args_set_result_obj(tc, type_object, MVM_RETURN_CURRENT_FRAME);
    
    MVM_gc_root_temp_pop_n(tc, 5);
}

/* Adds a method. */
static void add_method(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
    MVMObject *self, *type_obj, *method, *method_table;
    MVMString *name;
    
    /* Get arguments. */
    MVMArgProcContext arg_ctx; arg_ctx.named_used = NULL;
    MVM_args_proc_init(tc, &arg_ctx, callsite, args);
    self     = MVM_args_get_pos_obj(tc, &arg_ctx, 0, MVM_ARG_REQUIRED)->o;
    type_obj = MVM_args_get_pos_obj(tc, &arg_ctx, 1, MVM_ARG_REQUIRED)->o;
    name     = MVM_args_get_pos_str(tc, &arg_ctx, 2, MVM_ARG_REQUIRED)->s;
    method   = MVM_args_get_pos_obj(tc, &arg_ctx, 3, MVM_ARG_REQUIRED)->o;
    MVM_args_proc_cleanup(tc, &arg_ctx);
    if (!self || !IS_CONCRETE(self) || REPR(self)->ID != MVM_REPR_ID_KnowHOWREPR)
        MVM_exception_throw_adhoc(tc, "KnowHOW methods must be called on object instance with REPR KnowHOWREPR");
    
    /* Add to method table. */
    method_table = ((MVMKnowHOWREPR *)self)->body.methods;
    REPR(method_table)->ass_funcs->bind_key_boxed(tc, STABLE(method_table),
        method_table, OBJECT_BODY(method_table), (MVMObject *)name, method);
    
    /* Return added method as result. */
    MVM_args_set_result_obj(tc, method, MVM_RETURN_CURRENT_FRAME);
}

/* Adds an method. */
static void add_attribute(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
    MVMObject *self, *type_obj, *attr, *attributes;
    
    /* Get arguments. */
    MVMArgProcContext arg_ctx; arg_ctx.named_used = NULL;
    MVM_args_proc_init(tc, &arg_ctx, callsite, args);
    self     = MVM_args_get_pos_obj(tc, &arg_ctx, 0, MVM_ARG_REQUIRED)->o;
    type_obj = MVM_args_get_pos_obj(tc, &arg_ctx, 1, MVM_ARG_REQUIRED)->o;
    attr     = MVM_args_get_pos_obj(tc, &arg_ctx, 2, MVM_ARG_REQUIRED)->o;
    MVM_args_proc_cleanup(tc, &arg_ctx);
    
    /* Ensure we have the required representations. */
    if (!self || !IS_CONCRETE(self) || REPR(self)->ID != MVM_REPR_ID_KnowHOWREPR)
        MVM_exception_throw_adhoc(tc, "KnowHOW methods must be called on object instance with REPR KnowHOWREPR");
    if (REPR(attr)->ID != MVM_REPR_ID_KnowHOWAttributeREPR)
        MVM_exception_throw_adhoc(tc, "KnowHOW attributes must use KnowHOWAttributeREPR");
    
    /* Add to method table. */
    attributes = ((MVMKnowHOWREPR *)self)->body.attributes;
    MVM_repr_push_o(tc, attributes, attr);
    
    /* Return added attribute as result. */
    MVM_args_set_result_obj(tc, attr, MVM_RETURN_CURRENT_FRAME);
}

/* Composes the meta-object. */
static void compose(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
    MVMObject *self, *type_obj, *method_table, *attributes, *BOOTArray, *BOOTHash,
              *repr_info_hash, *repr_info, *type_info, *attr_info_list, *parent_info;
    MVMint64   num_attrs, i;
    
    /* Get arguments. */
    MVMArgProcContext arg_ctx; arg_ctx.named_used = NULL;
    MVM_args_proc_init(tc, &arg_ctx, callsite, args);
    self     = MVM_args_get_pos_obj(tc, &arg_ctx, 0, MVM_ARG_REQUIRED)->o;
    type_obj = MVM_args_get_pos_obj(tc, &arg_ctx, 1, MVM_ARG_REQUIRED)->o;
    MVM_args_proc_cleanup(tc, &arg_ctx);
    if (!self || !IS_CONCRETE(self) || REPR(self)->ID != MVM_REPR_ID_KnowHOWREPR)
        MVM_exception_throw_adhoc(tc, "KnowHOW methods must be called on object instance with REPR KnowHOWREPR");
    
    /* Fill out STable. */
    method_table = ((MVMKnowHOWREPR *)self)->body.methods;
    MVM_ASSIGN_REF(tc, STABLE(type_obj), STABLE(type_obj)->method_cache, method_table);
    STABLE(type_obj)->mode_flags              = MVM_METHOD_CACHE_AUTHORITATIVE;
    STABLE(type_obj)->type_check_cache_length = 1;
    STABLE(type_obj)->type_check_cache        = malloc(sizeof(MVMObject *));
    MVM_ASSIGN_REF(tc, STABLE(type_obj), STABLE(type_obj)->type_check_cache[0], type_obj);
    
    /* Next steps will allocate, so make sure we keep hold of the type
     * object and ourself. */
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&self);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&type_obj);
    
    /* Use any attribute information to produce attribute protocol
     * data. The protocol consists of an array... */
    BOOTArray = tc->instance->boot_types->BOOTArray;
    BOOTHash = tc->instance->boot_types->BOOTHash;
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&BOOTArray);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&BOOTHash);
    repr_info = REPR(BOOTArray)->allocate(tc, STABLE(BOOTArray));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&repr_info);
    REPR(repr_info)->initialize(tc, STABLE(repr_info), repr_info, OBJECT_BODY(repr_info));
    
    /* ...which contains an array per MRO entry (just us)... */
    type_info = REPR(BOOTArray)->allocate(tc, STABLE(BOOTArray));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&type_info);
    REPR(type_info)->initialize(tc, STABLE(type_info), type_info, OBJECT_BODY(type_info));
    MVM_repr_push_o(tc, repr_info, type_info);
        
    /* ...which in turn contains this type... */
    MVM_repr_push_o(tc, type_info, type_obj);
    
    /* ...then an array of hashes per attribute... */
    attr_info_list = REPR(BOOTArray)->allocate(tc, STABLE(BOOTArray));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&attr_info_list);
    REPR(attr_info_list)->initialize(tc, STABLE(attr_info_list), attr_info_list,
        OBJECT_BODY(attr_info_list));
    MVM_repr_push_o(tc, type_info, attr_info_list);
    attributes = ((MVMKnowHOWREPR *)self)->body.attributes;
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&attributes);
    num_attrs = REPR(attributes)->elems(tc, STABLE(attributes),
        attributes, OBJECT_BODY(attributes));
    for (i = 0; i < num_attrs; i++) {
        MVMObject *attr_info = REPR(BOOTHash)->allocate(tc, STABLE(BOOTHash));
        MVMKnowHOWAttributeREPR *attribute = (MVMKnowHOWAttributeREPR *)
            MVM_repr_at_pos_o(tc, attributes, i);
        MVM_gc_root_temp_push(tc, (MVMCollectable **)&attr_info);
        MVM_gc_root_temp_push(tc, (MVMCollectable **)&attribute);
        if (REPR((MVMObject *)attribute)->ID != MVM_REPR_ID_KnowHOWAttributeREPR)
            MVM_exception_throw_adhoc(tc, "KnowHOW attributes must use KnowHOWAttributeREPR");
        
        REPR(attr_info)->initialize(tc, STABLE(attr_info), attr_info,
            OBJECT_BODY(attr_info));
        REPR(attr_info)->ass_funcs->bind_key_boxed(tc, STABLE(attr_info),
            attr_info, OBJECT_BODY(attr_info), (MVMObject *)str_name, (MVMObject *)attribute->body.name);
        REPR(attr_info)->ass_funcs->bind_key_boxed(tc, STABLE(attr_info),
            attr_info, OBJECT_BODY(attr_info), (MVMObject *)str_type, attribute->body.type);
        if (attribute->body.box_target) {
            /* Merely having the key serves as a "yes". */
            REPR(attr_info)->ass_funcs->bind_key_boxed(tc, STABLE(attr_info),
                attr_info, OBJECT_BODY(attr_info), (MVMObject *)str_box_target, attr_info);
        }
        
        MVM_repr_push_o(tc, attr_info_list, attr_info);
        MVM_gc_root_temp_pop_n(tc, 2);
    }
    
    /* ...followed by a list of parents (none). */
    parent_info = REPR(BOOTArray)->allocate(tc, STABLE(BOOTArray));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&parent_info);
    REPR(parent_info)->initialize(tc, STABLE(parent_info), parent_info,
        OBJECT_BODY(parent_info));
    MVM_repr_push_o(tc, type_info, parent_info);
    
    /* Finally, this all goes in a hash under the key 'attribute'. */
    repr_info_hash = REPR(BOOTHash)->allocate(tc, STABLE(BOOTHash));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&repr_info_hash);
    REPR(repr_info_hash)->initialize(tc, STABLE(repr_info_hash), repr_info_hash, OBJECT_BODY(repr_info_hash));
    REPR(repr_info_hash)->ass_funcs->bind_key_boxed(tc, STABLE(repr_info_hash),
            repr_info_hash, OBJECT_BODY(repr_info_hash), (MVMObject *)str_attribute, repr_info);

    /* Compose the representation using it. */
    REPR(type_obj)->compose(tc, STABLE(type_obj), repr_info_hash);
    
    /* Clear temporary roots. */
    MVM_gc_root_temp_pop_n(tc, 10);
    
    /* Return type object. */
    MVM_args_set_result_obj(tc, type_obj, MVM_RETURN_CURRENT_FRAME);
}

/* Introspects the attributes. For now just hand back real list. */
static void attributes(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
    MVMObject *self, *type_obj, *attributes;
    MVMArgProcContext arg_ctx; arg_ctx.named_used = NULL;
    MVM_args_proc_init(tc, &arg_ctx, callsite, args);
    self     = MVM_args_get_pos_obj(tc, &arg_ctx, 0, MVM_ARG_REQUIRED)->o;
    type_obj = MVM_args_get_pos_obj(tc, &arg_ctx, 1, MVM_ARG_REQUIRED)->o;
    MVM_args_proc_cleanup(tc, &arg_ctx);
    if (!self || !IS_CONCRETE(self) || REPR(self)->ID != MVM_REPR_ID_KnowHOWREPR)
        MVM_exception_throw_adhoc(tc, "KnowHOW methods must be called on object instance with REPR KnowHOWREPR");
    attributes = ((MVMKnowHOWREPR *)self)->body.attributes;
    MVM_args_set_result_obj(tc, attributes, MVM_RETURN_CURRENT_FRAME);
}

/* Introspects the methods. */
static void methods(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
    MVMObject *self, *type_obj, *method_table;
    MVMArgProcContext arg_ctx; arg_ctx.named_used = NULL;
    MVM_args_proc_init(tc, &arg_ctx, callsite, args);
    self     = MVM_args_get_pos_obj(tc, &arg_ctx, 0, MVM_ARG_REQUIRED)->o;
    type_obj = MVM_args_get_pos_obj(tc, &arg_ctx, 1, MVM_ARG_REQUIRED)->o;
    MVM_args_proc_cleanup(tc, &arg_ctx);
    if (!self || !IS_CONCRETE(self) || REPR(self)->ID != MVM_REPR_ID_KnowHOWREPR)
        MVM_exception_throw_adhoc(tc, "KnowHOW methods must be called on object instance with REPR KnowHOWREPR");
    method_table = ((MVMKnowHOWREPR *)self)->body.methods;
    MVM_args_set_result_obj(tc, method_table, MVM_RETURN_CURRENT_FRAME);
}

/* Introspects the name. */
static void name(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
    MVMObject *self, *type_obj;
    MVMString *name;
    MVMArgProcContext arg_ctx; arg_ctx.named_used = NULL;
    MVM_args_proc_init(tc, &arg_ctx, callsite, args);
    self     = MVM_args_get_pos_obj(tc, &arg_ctx, 0, MVM_ARG_REQUIRED)->o;
    type_obj = MVM_args_get_pos_obj(tc, &arg_ctx, 1, MVM_ARG_REQUIRED)->o;
    MVM_args_proc_cleanup(tc, &arg_ctx);
    if (!self || !IS_CONCRETE(self) || REPR(self)->ID != MVM_REPR_ID_KnowHOWREPR)
        MVM_exception_throw_adhoc(tc, "KnowHOW methods must be called on object instance with REPR KnowHOWREPR");
    name = ((MVMKnowHOWREPR *)self)->body.name;
    MVM_args_set_result_str(tc, name, MVM_RETURN_CURRENT_FRAME);
}

/* Adds a method into the KnowHOW.HOW method table. */
static void add_knowhow_how_method(MVMThreadContext *tc, MVMKnowHOWREPR *knowhow_how,
        char *name, void (*func) (MVMThreadContext *, MVMCallsite *, MVMRegister *)) {
    MVMObject *BOOTCCode, *code_obj, *method_table, *name_str;
    
    /* Create string for name. */
    name_str = (MVMObject *)MVM_string_ascii_decode_nt(tc,
        tc->instance->VMString, name);
    
    /* Allocate a BOOTCCode and put pointer in. */
    BOOTCCode = tc->instance->boot_types->BOOTCCode;
    code_obj = REPR(BOOTCCode)->allocate(tc, STABLE(BOOTCCode));
    ((MVMCFunction *)code_obj)->body.func = func;
    
    /* Add into the table. */
    method_table = knowhow_how->body.methods;
    REPR(method_table)->ass_funcs->bind_key_boxed(tc, STABLE(method_table),
        method_table, OBJECT_BODY(method_table), name_str, code_obj);
}

/* Bootstraps the KnowHOW type. */
static void bootstrap_KnowHOW(MVMThreadContext *tc) {
    MVMObject *VMString  = tc->instance->VMString;
    MVMObject *BOOTArray = tc->instance->boot_types->BOOTArray;
    MVMObject *BOOTHash  = tc->instance->boot_types->BOOTHash;
    
    /* Create our KnowHOW type object. Note we don't have a HOW just yet, so
     * pass in NULL. */
    MVMREPROps *REPR    = MVM_repr_get_by_id(tc, MVM_REPR_ID_KnowHOWREPR);
    MVMObject  *knowhow = REPR->type_object_for(tc, NULL);

    /* We create a KnowHOW instance that can describe itself. This means
     * (once we tie the knot) that .HOW.HOW.HOW.HOW etc will always return
     * that, which closes the model up. Note that the STable for it must
     * be allocated first, since that holds the allocation size. */
    MVMKnowHOWREPR *knowhow_how;
    MVMSTable *st = MVM_gc_allocate_stable(tc, REPR, NULL);
    st->WHAT      = (MVMObject *)knowhow;
    st->size      = sizeof(MVMKnowHOWREPR);
    knowhow_how   = (MVMKnowHOWREPR *)REPR->allocate(tc, st);
    st->HOW       = (MVMObject *)knowhow_how;
    knowhow_how->common.st = st;
    
    /* Add various methods to the KnowHOW's HOW. */
    REPR->initialize(tc, NULL, (MVMObject *)knowhow_how, &knowhow_how->body);
    add_knowhow_how_method(tc, knowhow_how, "new_type", new_type);
    add_knowhow_how_method(tc, knowhow_how, "add_method", add_method);
    add_knowhow_how_method(tc, knowhow_how, "add_attribute", add_attribute);
    add_knowhow_how_method(tc, knowhow_how, "compose", compose);
    add_knowhow_how_method(tc, knowhow_how, "attributes", attributes);
    add_knowhow_how_method(tc, knowhow_how, "methods", methods);
    add_knowhow_how_method(tc, knowhow_how, "name", name);
    
    /* Set name KnowHOW for the KnowHOW's HOW. */
    knowhow_how->body.name = MVM_string_ascii_decode_nt(tc, VMString, "KnowHOW");

    /* Set this built up HOW as the KnowHOW's HOW. */
    STABLE(knowhow)->HOW = (MVMObject *)knowhow_how;
    
    /* Give it an authoritative method cache; this in turn will make the
     * method dispatch bottom out. */
    STABLE(knowhow)->method_cache = knowhow_how->body.methods;
    STABLE(knowhow)->mode_flags   = MVM_METHOD_CACHE_AUTHORITATIVE;
    STABLE(knowhow_how)->method_cache = knowhow_how->body.methods;
    STABLE(knowhow_how)->mode_flags   = MVM_METHOD_CACHE_AUTHORITATIVE;
    
    /* Associate the created objects with the initial core serialization
     * context. */
    /* XXX TODO */

    /* Stash the created KnowHOW. */
    tc->instance->KnowHOW = (MVMObject *)knowhow;
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->KnowHOW);
}
 
/* Takes a stub object that existed before we had bootstrapped things and
 * gives it a meta-object. */
static void add_meta_object(MVMThreadContext *tc, MVMObject *type_obj, char *name) {
    MVMObject *meta_obj;
    MVMString *name_str;
    
    /* Create meta-object. */
    meta_obj = MVM_repr_alloc_init(tc, STABLE(tc->instance->KnowHOW)->HOW);
    MVMROOT(tc, meta_obj, {
        /* Put it in place. */
        MVM_ASSIGN_REF(tc, STABLE(type_obj), STABLE(type_obj)->HOW, meta_obj);
        
        /* Set name. */
        name_str = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, name);
        MVM_ASSIGN_REF(tc, meta_obj, ((MVMKnowHOWREPR *)meta_obj)->body.name, name_str);
    });
}

/* Creates a new attribute meta-object. */
static void attr_new(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
    MVMObject   *self, *obj;
    MVMRegister *type_arg, *name_arg, *bt_arg;
    MVMREPROps  *repr;
    
    /* Process arguments. */
    MVMArgProcContext arg_ctx; arg_ctx.named_used = NULL;
    MVM_args_proc_init(tc, &arg_ctx, callsite, args);
    self     = MVM_args_get_pos_obj(tc, &arg_ctx, 0, MVM_ARG_REQUIRED)->o;
    name_arg = MVM_args_get_named_str(tc, &arg_ctx, str_name, MVM_ARG_REQUIRED);
    type_arg = MVM_args_get_named_obj(tc, &arg_ctx, str_type, MVM_ARG_REQUIRED);
    bt_arg   = MVM_args_get_named_int(tc, &arg_ctx, str_box_target, MVM_ARG_OPTIONAL);
    MVM_args_proc_cleanup(tc, &arg_ctx);
    
    /* Anchor all the things. */
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&self);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&name_arg);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&type_arg);
    
    /* Allocate attribute object. */
    repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_KnowHOWAttributeREPR);
    obj = repr->allocate(tc, STABLE(self));
    
    /* Populate it. */
    MVM_ASSIGN_REF(tc, obj, ((MVMKnowHOWAttributeREPR *)obj)->body.name, name_arg->s);
    MVM_ASSIGN_REF(tc, obj, ((MVMKnowHOWAttributeREPR *)obj)->body.type, type_arg->o);
    ((MVMKnowHOWAttributeREPR *)obj)->body.box_target = bt_arg ? bt_arg->i64 : 0;
    
    /* Return produced object. */
    MVM_gc_root_temp_pop_n(tc, 3);
    MVM_args_set_result_obj(tc, obj, MVM_RETURN_CURRENT_FRAME);
}

/* Composes the attribute; actually, nothing to do really. */
static void attr_compose(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
    MVMObject *self;
    MVMArgProcContext arg_ctx; arg_ctx.named_used = NULL;
    MVM_args_proc_init(tc, &arg_ctx, callsite, args);
    self = MVM_args_get_pos_obj(tc, &arg_ctx, 0, MVM_ARG_REQUIRED)->o;
    MVM_args_proc_cleanup(tc, &arg_ctx);
    MVM_args_set_result_obj(tc, self, MVM_RETURN_CURRENT_FRAME);
}

/* Introspects the attribute's name. */
static void attr_name(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
    MVMObject *self;
    MVMString *name;
    MVMArgProcContext arg_ctx; arg_ctx.named_used = NULL;
    MVM_args_proc_init(tc, &arg_ctx, callsite, args);
    self = MVM_args_get_pos_obj(tc, &arg_ctx, 0, MVM_ARG_REQUIRED)->o;
    MVM_args_proc_cleanup(tc, &arg_ctx);
    name = ((MVMKnowHOWAttributeREPR *)self)->body.name;
    MVM_args_set_result_str(tc, name, MVM_RETURN_CURRENT_FRAME);
}

/* Introspects the attribute's type. */
static void attr_type(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
    MVMObject *self, *type;
    MVMArgProcContext arg_ctx; arg_ctx.named_used = NULL;
    MVM_args_proc_init(tc, &arg_ctx, callsite, args);
    self = MVM_args_get_pos_obj(tc, &arg_ctx, 0, MVM_ARG_REQUIRED)->o;
    MVM_args_proc_cleanup(tc, &arg_ctx);
    type = ((MVMKnowHOWAttributeREPR *)self)->body.type;
    MVM_args_set_result_obj(tc, type, MVM_RETURN_CURRENT_FRAME);
}

/* Introspects the attribute's box target flag. */
static void attr_box_target(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
    MVMObject *self;
    MVMint64   box_target;
    MVMArgProcContext arg_ctx; arg_ctx.named_used = NULL;
    MVM_args_proc_init(tc, &arg_ctx, callsite, args);
    self = MVM_args_get_pos_obj(tc, &arg_ctx, 0, MVM_ARG_REQUIRED)->o;
    MVM_args_proc_cleanup(tc, &arg_ctx);
    box_target = ((MVMKnowHOWAttributeREPR *)self)->body.box_target;
    MVM_args_set_result_int(tc, box_target, MVM_RETURN_CURRENT_FRAME);
}

/* Creates and installs the KnowHOWAttribute type. */
static void create_KnowHOWAttribute(MVMThreadContext *tc) {
    MVMObject      *knowhow_how, *meta_obj, *type_obj;
    MVMString      *name_str;
    MVMREPROps     *repr;
    
    /* Create meta-object. */
    meta_obj = MVM_repr_alloc_init(tc, STABLE(tc->instance->KnowHOW)->HOW);
    MVMROOT(tc, meta_obj, {
        /* Add methods. */
        add_knowhow_how_method(tc, (MVMKnowHOWREPR *)meta_obj, "new", attr_new);
        add_knowhow_how_method(tc, (MVMKnowHOWREPR *)meta_obj, "compose", attr_compose);
        add_knowhow_how_method(tc, (MVMKnowHOWREPR *)meta_obj, "name", attr_name);
        add_knowhow_how_method(tc, (MVMKnowHOWREPR *)meta_obj, "type", attr_type);
        add_knowhow_how_method(tc, (MVMKnowHOWREPR *)meta_obj, "box_target", attr_box_target);
        
        /* Set name. */
        name_str = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "KnowHOWAttribute");
        MVM_ASSIGN_REF(tc, meta_obj, ((MVMKnowHOWREPR *)meta_obj)->body.name, name_str);
        
        /* Create a new type object with the correct REPR. */
        repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_KnowHOWAttributeREPR);
        type_obj = repr->type_object_for(tc, meta_obj);
        
        /* Set up method dispatch cache. */
        STABLE(type_obj)->method_cache = ((MVMKnowHOWREPR *)meta_obj)->body.methods;
        STABLE(type_obj)->mode_flags   = MVM_METHOD_CACHE_AUTHORITATIVE;
        
        /* Stash the created type object. */
        tc->instance->KnowHOWAttribute = (MVMObject *)type_obj;
        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->KnowHOWAttribute);
    });
}

/* Bootstraps a typed array. */
static MVMObject * boot_typed_array(MVMThreadContext *tc, char *name, MVMObject *type) {
    MVMObject  *repr_info;
    MVMREPROps *repr  = MVM_repr_get_by_id(tc, MVM_REPR_ID_MVMArray);
    MVMObject  *array = repr->type_object_for(tc, NULL);
    MVMROOT(tc, array, {
        /* Give it a meta-object. */
        add_meta_object(tc, array, name);
        
        /* Now need to compose it with the specified type. */
        repr_info = MVM_repr_alloc_init(tc, tc->instance->boot_types->BOOTHash);
        MVMROOT(tc, repr_info, {
            MVMObject *arr_info = MVM_repr_alloc_init(tc, tc->instance->boot_types->BOOTHash);
            MVMROOT(tc, arr_info, {
                MVM_repr_bind_key_boxed(tc, repr_info, str_array, arr_info);
                MVM_repr_bind_key_boxed(tc, arr_info, str_type, type);
                REPR(array)->compose(tc, STABLE(array), repr_info);
            });
        });
    });
    return array;
}

/* Sets up the core serialization context. It is marked as the SC of various
 * rooted objects, which means in turn it will never be collected. */
static void setup_core_sc(MVMThreadContext *tc) {
    MVMString *handle;
    MVMSerializationContext *sc;
    
    handle = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "__6MODEL_CORE__");
    sc = (MVMSerializationContext *)MVM_sc_create(tc, handle);
    
    MVMROOT(tc, sc, {
        /* KnowHOW */
        MVM_sc_set_object(tc, sc, 0, tc->instance->KnowHOW);
        MVM_sc_set_obj_sc(tc, tc->instance->KnowHOW, sc);
        MVM_sc_set_stable(tc, sc, 0, STABLE(tc->instance->KnowHOW));
        MVM_sc_set_stable_sc(tc, STABLE(tc->instance->KnowHOW), sc);
        
        /* KnowHOW.HOW */
        MVM_sc_set_object(tc, sc, 1, STABLE(tc->instance->KnowHOW)->HOW);
        MVM_sc_set_obj_sc(tc, STABLE(tc->instance->KnowHOW)->HOW, sc);
        MVM_sc_set_stable(tc, sc, 1, STABLE(STABLE(tc->instance->KnowHOW)->HOW));
        MVM_sc_set_stable_sc(tc, STABLE(STABLE(tc->instance->KnowHOW)->HOW), sc);
        
        /* KnowHOWAttribute */
        MVM_sc_set_object(tc, sc, 2, tc->instance->KnowHOWAttribute);
        MVM_sc_set_obj_sc(tc, tc->instance->KnowHOWAttribute, sc);
        MVM_sc_set_stable(tc, sc, 2, STABLE(tc->instance->KnowHOWAttribute));
        MVM_sc_set_stable_sc(tc, STABLE(tc->instance->KnowHOWAttribute), sc);
    });
}
 
/* Drives the overall bootstrap process. */
void MVM_6model_bootstrap(MVMThreadContext *tc) {
    /* First, we have to get the VMString type to exist; this has to
     * come even before REPR registry setup because it relies on
     * being able to create strings. */
    create_stub_VMString(tc);

    /* Now we've enough to actually create the REPR registry. */
    MVM_repr_initialize_registry(tc);

    /* Create stub BOOTInt, BOOTNum, BOOTStr, BOOTArray, BOOTHash, BOOTCCode,
     * BOOTCode, BOOTThread, BOOTIter, BOOTContext, and SCRef types. */
    create_stub_BOOTInt(tc);
    create_stub_BOOTNum(tc);
    create_stub_BOOTStr(tc);
    create_stub_BOOTArray(tc);
    create_stub_BOOTHash(tc);
    create_stub_BOOTCCode(tc);
    create_stub_BOOTCode(tc);
    create_stub_BOOTThread(tc);
    create_stub_BOOTIter(tc);
    create_stub_BOOTContext(tc);
    create_stub_SCRef(tc);

    /* Set up some strings. */
    str_repr     = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "repr");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_repr);
    str_name     = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "name");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_name);
    str_anon     = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "<anon>");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_anon);
    str_P6opaque = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "P6opaque");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_P6opaque);
    str_type     = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "type");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_type);
    str_box_target = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "box_target");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_box_target);
    str_attribute = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "attribute");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_attribute);
    str_array = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "array");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_array);
    
    /* Bootstrap the KnowHOW type, giving it a meta-object. */
    bootstrap_KnowHOW(tc);
    
    /* Give stub types meta-objects. */
    add_meta_object(tc, tc->instance->VMString, "VMString");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->VMString);
    add_meta_object(tc, tc->instance->boot_types->BOOTInt, "BOOTInt");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->boot_types->BOOTInt);
    add_meta_object(tc, tc->instance->boot_types->BOOTNum, "BOOTNum");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->boot_types->BOOTNum);
    add_meta_object(tc, tc->instance->boot_types->BOOTStr, "BOOTStr");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->boot_types->BOOTStr);
    add_meta_object(tc, tc->instance->boot_types->BOOTArray, "BOOTArray");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->boot_types->BOOTArray);
    add_meta_object(tc, tc->instance->boot_types->BOOTHash, "BOOTHash");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->boot_types->BOOTHash);
    add_meta_object(tc, tc->instance->boot_types->BOOTCCode, "BOOTCCode");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->boot_types->BOOTCCode);
    add_meta_object(tc, tc->instance->boot_types->BOOTCode, "BOOTCode");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->boot_types->BOOTCode);
    add_meta_object(tc, tc->instance->boot_types->BOOTThread, "BOOTThread");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->boot_types->BOOTThread);
    add_meta_object(tc, tc->instance->boot_types->BOOTIter, "BOOTIter");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->boot_types->BOOTIter);
    add_meta_object(tc, tc->instance->boot_types->BOOTContext, "BOOTContext");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->boot_types->BOOTContext);
    add_meta_object(tc, tc->instance->SCRef, "SCRef");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->SCRef);
    
    /* Create the KnowHOWAttribute type. */
    create_KnowHOWAttribute(tc);
    
    /* Bootstrap typed arrays. */
    tc->instance->boot_types->BOOTIntArray = boot_typed_array(tc, "BOOTIntArray",
        tc->instance->boot_types->BOOTInt);
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->boot_types->BOOTIntArray);
    tc->instance->boot_types->BOOTNumArray = boot_typed_array(tc, "BOOTNumArray",
        tc->instance->boot_types->BOOTNum);
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->boot_types->BOOTNumArray);
    tc->instance->boot_types->BOOTStrArray = boot_typed_array(tc, "BOOTStrArray",
        tc->instance->boot_types->BOOTStr);
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->boot_types->BOOTStrArray);
    
    /* Get initial __6MODEL_CORE__ serialization context set up. */
    setup_core_sc(tc);
}
