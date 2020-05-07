#include "moar.h"

/* This file implements the various steps involved in getting 6model
 * bootstrapped from the ground up - that is, getting to having a
 * KnowHOW meta-object type so that userland can start building up
 * more interesting meta-objects. Mostly it just has to make objects
 * with some "holes", and later go back and fill them out. This is
 * due to the circular nature of things.
 */

/* Creates a stub VMString. Note we didn't initialize the
 * representation yet, so have to do this somewhat pokily. */
static void create_stub_VMString(MVMThreadContext *tc) {
    /* Need to create the REPR function table "in advance". */
    const MVMREPROps *repr = MVMString_initialize(tc);

    /* Now we can create a type object; note we have no HOW yet,
     * though. */
    tc->instance->VMString = repr->type_object_for(tc, NULL);
}

/* KnowHOW.new_type method. Creates a new type with this HOW as its meta-object. */
static void new_type(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMObject *self, *HOW, *type_object, *BOOTHash, *stash;
    MVMArgInfo repr_arg, name_arg;
    MVMString *repr_name, *name;
    const MVMREPROps *repr_to_use;
    MVMInstance       *instance = tc->instance;

    /* Get arguments. */
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 1, 1);
    self = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    repr_arg = MVM_args_get_named_str(tc, &arg_ctx, instance->str_consts.repr, MVM_ARG_OPTIONAL);
    name_arg = MVM_args_get_named_str(tc, &arg_ctx, instance->str_consts.name, MVM_ARG_OPTIONAL);
    MVM_args_proc_cleanup(tc, &arg_ctx);
    if (REPR(self)->ID != MVM_REPR_ID_KnowHOWREPR)
        MVM_exception_throw_adhoc(tc, "KnowHOW methods must be called on object with REPR KnowHOWREPR");

    /* See if we have a representation name; if not default to P6opaque. */
    repr_name = repr_arg.exists ? repr_arg.arg.s : instance->str_consts.P6opaque;
    repr_to_use = MVM_repr_get_by_name(tc, repr_name);

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&name_arg);

    /* We first create a new HOW instance. */
    HOW = REPR(self)->allocate(tc, STABLE(self));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&HOW);

    /* Create a new type object of the desired REPR. (Note that we can't
     * default to KnowHOWREPR here, since it doesn't know how to actually
     * store attributes, it's just for bootstrapping knowhow's. */
    type_object = repr_to_use->type_object_for(tc, HOW);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&type_object);

    /* This may move name_arg.arg.s so do it first: */
    REPR(HOW)->initialize(tc, STABLE(HOW), HOW, OBJECT_BODY(HOW));
    /* See if we were given a name; put it into the meta-object if so. */
    name = name_arg.exists ? name_arg.arg.s : instance->str_consts.anon;
    MVM_ASSIGN_REF(tc, &(HOW->header), ((MVMKnowHOWREPR *)HOW)->body.name, name);
    type_object->st->debug_name = MVM_string_utf8_encode_C_string(tc, name);

    /* Set .WHO to an empty hash. */
    BOOTHash = tc->instance->boot_types.BOOTHash;
    stash = REPR(BOOTHash)->allocate(tc, STABLE(BOOTHash));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&stash);
    MVM_ASSIGN_REF(tc, &(STABLE(type_object)->header), STABLE(type_object)->WHO, stash);

    /* Return the type object. */
    MVM_args_set_result_obj(tc, type_object, MVM_RETURN_CURRENT_FRAME);

    MVM_gc_root_temp_pop_n(tc, 4);
}

/* Adds a method. */
static void add_method(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMObject *self, *method, *method_table;
    MVMString *name;

    /* Get arguments. */
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 4, 4);
    self     = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    name     = MVM_args_get_required_pos_str(tc, &arg_ctx, 2);
    method   = MVM_args_get_required_pos_obj(tc, &arg_ctx, 3);
    MVM_args_proc_cleanup(tc, &arg_ctx);
    if (!self || !IS_CONCRETE(self) || REPR(self)->ID != MVM_REPR_ID_KnowHOWREPR)
        MVM_exception_throw_adhoc(tc, "KnowHOW methods must be called on object instance with REPR KnowHOWREPR");

    /* Add to method table. */
    method_table = ((MVMKnowHOWREPR *)self)->body.methods;
    MVM_repr_bind_key_o(tc, method_table, name, method);

    /* Return added method as result. */
    MVM_args_set_result_obj(tc, method, MVM_RETURN_CURRENT_FRAME);
}

/* Adds an method. */
static void add_attribute(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMObject *self, *attr, *attributes;

    /* Get arguments. */
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 3, 3);
    self     = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    attr     = MVM_args_get_required_pos_obj(tc, &arg_ctx, 2);
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
static void compose(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMObject *self, *type_obj, *method_table, *attributes, *BOOTArray, *BOOTHash,
              *repr_info_hash, *repr_info, *type_info, *attr_info_list, *parent_info;
    MVMuint64   num_attrs, i;
    MVMInstance *instance = tc->instance;

    /* Get arguments. */
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 2, 2);
    self     = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    type_obj = MVM_args_get_required_pos_obj(tc, &arg_ctx, 1);
    MVM_args_proc_cleanup(tc, &arg_ctx);
    if (!self || !IS_CONCRETE(self) || REPR(self)->ID != MVM_REPR_ID_KnowHOWREPR)
        MVM_exception_throw_adhoc(tc, "KnowHOW methods must be called on object instance with REPR KnowHOWREPR");

    /* Fill out STable. */
    method_table = ((MVMKnowHOWREPR *)self)->body.methods;
    MVM_ASSIGN_REF(tc, &(STABLE(type_obj)->header), STABLE(type_obj)->method_cache, method_table);
    STABLE(type_obj)->mode_flags              = MVM_METHOD_CACHE_AUTHORITATIVE;
    STABLE(type_obj)->type_check_cache_length = 1;
    STABLE(type_obj)->type_check_cache        = MVM_malloc(sizeof(MVMObject *));
    MVM_ASSIGN_REF(tc, &(STABLE(type_obj)->header), STABLE(type_obj)->type_check_cache[0], type_obj);
    attributes = ((MVMKnowHOWREPR *)self)->body.attributes;

    /* Next steps will allocate, so make sure we keep hold of the type
     * object and ourself. */
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&attributes);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&type_obj);

    /* Use any attribute information to produce attribute protocol
     * data. The protocol consists of an array... */
    BOOTArray = instance->boot_types.BOOTArray;
    BOOTHash = instance->boot_types.BOOTHash;
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&BOOTArray);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&BOOTHash);
    repr_info = REPR(BOOTArray)->allocate(tc, STABLE(BOOTArray));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&repr_info);

    /* ...which contains an array per MRO entry (just us)... */
    type_info = REPR(BOOTArray)->allocate(tc, STABLE(BOOTArray));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&type_info);
    MVM_repr_push_o(tc, repr_info, type_info);

    /* ...which in turn contains this type... */
    MVM_repr_push_o(tc, type_info, type_obj);

    /* ...then an array of hashes per attribute... */
    attr_info_list = REPR(BOOTArray)->allocate(tc, STABLE(BOOTArray));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&attr_info_list);
    MVM_repr_push_o(tc, type_info, attr_info_list);
    num_attrs = REPR(attributes)->elems(tc, STABLE(attributes),
        attributes, OBJECT_BODY(attributes));
    for (i = 0; i < num_attrs; i++) {
        MVMObject *attr_info = REPR(BOOTHash)->allocate(tc, STABLE(BOOTHash));
        MVMKnowHOWAttributeREPR *attribute = (MVMKnowHOWAttributeREPR *)
            MVM_repr_at_pos_o(tc, attributes, i);
        MVMROOT2(tc, attr_info, attribute, {
            if (REPR((MVMObject *)attribute)->ID != MVM_REPR_ID_KnowHOWAttributeREPR)
                MVM_exception_throw_adhoc(tc, "KnowHOW attributes must use KnowHOWAttributeREPR");

            MVM_repr_init(tc, attr_info);
            MVM_repr_bind_key_o(tc, attr_info, instance->str_consts.name, (MVMObject *)attribute->body.name);
            MVM_repr_bind_key_o(tc, attr_info, instance->str_consts.type, attribute->body.type);
            if (attribute->body.box_target) {
                /* Merely having the key serves as a "yes". */
                MVM_repr_bind_key_o(tc, attr_info, instance->str_consts.box_target, attr_info);
            }

            MVM_repr_push_o(tc, attr_info_list, attr_info);
        });
    }

    /* ...followed by a list of parents (none). */
    parent_info = REPR(BOOTArray)->allocate(tc, STABLE(BOOTArray));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&parent_info);
    MVM_repr_init(tc, parent_info);
    MVM_repr_push_o(tc, type_info, parent_info);

    /* Finally, this all goes in a hash under the key 'attribute'. */
    repr_info_hash = REPR(BOOTHash)->allocate(tc, STABLE(BOOTHash));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&repr_info_hash);
    MVM_repr_init(tc, repr_info_hash);
    MVM_repr_bind_key_o(tc, repr_info_hash, instance->str_consts.attribute, repr_info);

    /* Compose the representation using it. */
    MVM_repr_compose(tc, type_obj, repr_info_hash);

    /* Clear temporary roots. */
    MVM_gc_root_temp_pop_n(tc, 9);

    /* Return type object. */
    MVM_args_set_result_obj(tc, type_obj, MVM_RETURN_CURRENT_FRAME);
}

#define introspect_member(member, set_result, result) \
static void member(MVMThreadContext *tc, MVMArgs arg_info) { \
    MVMObject *self, *member; \
    MVMArgProcContext arg_ctx; \
    MVM_args_proc_setup(tc, &arg_ctx, arg_info); \
    MVM_args_checkarity(tc, &arg_ctx, 2, 2); \
    self     = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0); \
    MVM_args_proc_cleanup(tc, &arg_ctx); \
    if (!self || !IS_CONCRETE(self) || REPR(self)->ID != MVM_REPR_ID_KnowHOWREPR) \
        MVM_exception_throw_adhoc(tc, "KnowHOW methods must be called on object instance with REPR KnowHOWREPR"); \
    member = (MVMObject *)((MVMKnowHOWREPR *)self)->body.member; \
    set_result(tc, result, MVM_RETURN_CURRENT_FRAME); \
}

/* Introspects the attributes. For now just hand back real list. */
introspect_member(attributes, MVM_args_set_result_obj, attributes)

/* Introspects the methods. */
introspect_member(methods, MVM_args_set_result_obj, methods)

/* Introspects the name. */
introspect_member(name, MVM_args_set_result_str, (MVMString *)name)

/* Adds a method into the KnowHOW.HOW method table. */
static void add_knowhow_how_method(MVMThreadContext *tc, MVMKnowHOWREPR *knowhow_how,
        char *name, void (*func) (MVMThreadContext *, MVMArgs)) {
    MVMObject *BOOTCCode, *code_obj, *method_table;
    MVMString *name_str;

    /* Create string for name. */
    name_str = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, name);

    /* Allocate a BOOTCCode and put pointer in. */
    BOOTCCode = tc->instance->boot_types.BOOTCCode;
    code_obj = REPR(BOOTCCode)->allocate(tc, STABLE(BOOTCCode));
    ((MVMCFunction *)code_obj)->body.func = func;

    /* Add into the table. */
    method_table = knowhow_how->body.methods;
    MVM_repr_bind_key_o(tc, method_table, name_str, code_obj);
}

/* Bootstraps the KnowHOW type. */
static void bootstrap_KnowHOW(MVMThreadContext *tc) {
    MVMObject *VMString  = tc->instance->VMString;

    /* Create our KnowHOW type object. Note we don't have a HOW just yet, so
     * pass in NULL. */
    const MVMREPROps *REPR    = MVM_repr_get_by_id(tc, MVM_REPR_ID_KnowHOWREPR);
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

    /* Stash the created KnowHOW. */
    tc->instance->KnowHOW = (MVMObject *)knowhow;
    MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&tc->instance->KnowHOW, "KnowHOW");
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
        MVM_ASSIGN_REF(tc, &(STABLE(type_obj)->header), STABLE(type_obj)->HOW, meta_obj);

        /* Set name. */
        name_str = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, name);
        MVM_ASSIGN_REF(tc, &(meta_obj->header), ((MVMKnowHOWREPR *)meta_obj)->body.name, name_str);
        type_obj->st->debug_name = strdup(name);
    });
}

/* Creates a new attribute meta-object. */
static void attr_new(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMObject   *self, *obj;
    MVMArgInfo   type_arg, name_arg, bt_arg;
    const MVMREPROps  *repr;
    MVMInstance       *instance = tc->instance;

    /* Process arguments. */
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 1, 1);
    self     = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    name_arg = MVM_args_get_named_str(tc, &arg_ctx, instance->str_consts.name, MVM_ARG_REQUIRED);
    type_arg = MVM_args_get_named_obj(tc, &arg_ctx, instance->str_consts.type, MVM_ARG_OPTIONAL);
    bt_arg   = MVM_args_get_named_int(tc, &arg_ctx, instance->str_consts.box_target, MVM_ARG_OPTIONAL);
    MVM_args_proc_cleanup(tc, &arg_ctx);

    /* Anchor all the things. */
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&name_arg);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&type_arg);

    /* Allocate attribute object. */
    repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_KnowHOWAttributeREPR);
    obj = repr->allocate(tc, STABLE(self));

    /* Populate it. */
    MVM_ASSIGN_REF(tc, &(obj->header), ((MVMKnowHOWAttributeREPR *)obj)->body.name, name_arg.arg.s);
    MVM_ASSIGN_REF(tc, &(obj->header), ((MVMKnowHOWAttributeREPR *)obj)->body.type, type_arg.exists ? type_arg.arg.o : tc->instance->KnowHOW);
    ((MVMKnowHOWAttributeREPR *)obj)->body.box_target = bt_arg.exists ? bt_arg.arg.i64 : 0;

    /* Return produced object. */
    MVM_gc_root_temp_pop_n(tc, 2);
    MVM_args_set_result_obj(tc, obj, MVM_RETURN_CURRENT_FRAME);
}

/* Composes the attribute; actually, nothing to do really. */
static void attr_compose(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMObject *self;
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 1, 1);
    self = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVM_args_proc_cleanup(tc, &arg_ctx);
    MVM_args_set_result_obj(tc, self, MVM_RETURN_CURRENT_FRAME);
}

/* Introspects the attribute's name. */
static void attr_name(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMObject *self;
    MVMString *name;
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 1, 1);
    self = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVM_args_proc_cleanup(tc, &arg_ctx);
    name = ((MVMKnowHOWAttributeREPR *)self)->body.name;
    MVM_args_set_result_str(tc, name, MVM_RETURN_CURRENT_FRAME);
}

/* Introspects the attribute's type. */
static void attr_type(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMObject *self, *type;
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 1, 1);
    self = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVM_args_proc_cleanup(tc, &arg_ctx);
    type = ((MVMKnowHOWAttributeREPR *)self)->body.type;
    MVM_args_set_result_obj(tc, type, MVM_RETURN_CURRENT_FRAME);
}

/* Introspects the attribute's box target flag. */
static void attr_box_target(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMObject *self;
    MVMint64   box_target;
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 1, 1);
    self = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVM_args_proc_cleanup(tc, &arg_ctx);
    box_target = ((MVMKnowHOWAttributeREPR *)self)->body.box_target;
    MVM_args_set_result_int(tc, box_target, MVM_RETURN_CURRENT_FRAME);
}

/* Creates and installs the KnowHOWAttribute type. */
static void create_KnowHOWAttribute(MVMThreadContext *tc) {
    MVMObject      *meta_obj, *type_obj;
    MVMString      *name_str;
    const MVMREPROps     *repr;

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
        MVM_ASSIGN_REF(tc, &(meta_obj->header), ((MVMKnowHOWREPR *)meta_obj)->body.name, name_str);

        /* Create a new type object with the correct REPR. */
        repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_KnowHOWAttributeREPR);
        type_obj = repr->type_object_for(tc, meta_obj);

        /* Set up method dispatch cache. */
        STABLE(type_obj)->method_cache = ((MVMKnowHOWREPR *)meta_obj)->body.methods;
        STABLE(type_obj)->mode_flags   = MVM_METHOD_CACHE_AUTHORITATIVE;

        /* Stash the created type object. */
        tc->instance->KnowHOWAttribute = (MVMObject *)type_obj;
        MVM_gc_root_add_permanent_desc(tc,
            (MVMCollectable **)&tc->instance->KnowHOWAttribute,
            "KnowHOWAttribute");
    });
}

/* Bootstraps a typed array. */
static MVMObject * boot_typed_array(MVMThreadContext *tc, char *name, MVMObject *type) {
    MVMBoolificationSpec *bs;
    MVMObject  *repr_info;
    MVMInstance  *instance  = tc->instance;
    const MVMREPROps *repr  = MVM_repr_get_by_id(tc, MVM_REPR_ID_VMArray);
    MVMObject  *array = repr->type_object_for(tc, NULL);
    MVMROOT(tc, array, {
        /* Give it a meta-object. */
        add_meta_object(tc, array, name);

        /* Now need to compose it with the specified type. */
        repr_info = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTHash);
        MVMROOT(tc, repr_info, {
            MVMObject *arr_info = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTHash);
            MVM_repr_bind_key_o(tc, arr_info, instance->str_consts.type, type);
            MVM_repr_bind_key_o(tc, repr_info, instance->str_consts.array, arr_info);
            MVM_repr_compose(tc, array, repr_info);
        });

        /* Also give it a boolification spec. */
        bs = MVM_malloc(sizeof(MVMBoolificationSpec));
        bs->mode = MVM_BOOL_MODE_HAS_ELEMS;
        bs->method = NULL;
        array->st->boolification_spec = bs;
    });
    return array;
}

/* Sets up the core serialization context. It is marked as the SC of various
 * rooted objects, which means in turn it will never be collected. */
static void setup_core_sc(MVMThreadContext *tc) {
    MVMString *handle = MVM_string_ascii_decode_nt(tc,
        tc->instance->VMString, "__6MODEL_CORE__");
    MVMSerializationContext * const sc = (MVMSerializationContext *)MVM_sc_create(tc, handle);
    MVMint32 obj_index = 0;
    MVMint32 st_index  = 0;

#define add_to_sc_with_st(tc, sc, variable) do { \
    MVM_sc_set_object(tc, sc, obj_index++, variable); \
    MVM_sc_set_obj_sc(tc, variable, sc); \
    MVM_sc_set_stable(tc, sc, st_index++, STABLE(variable)); \
    MVM_sc_set_stable_sc(tc, STABLE(variable), sc); \
} while (0)
#define add_to_sc_with_st_and_mo(tc, sc, variable) do { \
    add_to_sc_with_st(tc, sc, variable); \
    MVM_sc_set_object(tc, sc, obj_index++, STABLE(variable)->HOW); \
    MVM_sc_set_obj_sc(tc, STABLE(variable)->HOW, sc); \
} while (0)

    /* KnowHOW */
    add_to_sc_with_st(tc, sc, tc->instance->KnowHOW);

    /* KnowHOW.HOW */
    add_to_sc_with_st(tc, sc, STABLE(tc->instance->KnowHOW)->HOW);

    /* KnowHOWAttribute */
    add_to_sc_with_st(tc, sc, tc->instance->KnowHOWAttribute);

    /* BOOT* */
    add_to_sc_with_st_and_mo(tc, sc, tc->instance->boot_types.BOOTArray);
    add_to_sc_with_st_and_mo(tc, sc, tc->instance->boot_types.BOOTHash);
    add_to_sc_with_st_and_mo(tc, sc, tc->instance->boot_types.BOOTIter);
    add_to_sc_with_st_and_mo(tc, sc, tc->instance->boot_types.BOOTInt);
    add_to_sc_with_st_and_mo(tc, sc, tc->instance->boot_types.BOOTNum);
    add_to_sc_with_st_and_mo(tc, sc, tc->instance->boot_types.BOOTStr);
    add_to_sc_with_st_and_mo(tc, sc, tc->instance->boot_types.BOOTCode);
}

/* Sets up some string constants. */
static void string_consts(MVMThreadContext *tc) {
    MVMInstance * const instance = tc->instance;

/* Set up some strings. */
#define string_creator(variable, name) do { \
    instance->str_consts.variable = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, (name)); \
    MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&(instance->str_consts.variable), "VM string constant"); \
} while (0)

    string_creator(empty, "");
    string_creator(Int, "Int");
    string_creator(Str, "Str");
    string_creator(Num, "Num");
    string_creator(integer, "integer");
    string_creator(float_str, "float");
    string_creator(bits, "bits");
    string_creator(unsigned_str, "unsigned");
    string_creator(find_method, "find_method");
    string_creator(type_check, "type_check");
    string_creator(accepts_type, "accepts_type");
    string_creator(name, "name");
    string_creator(attribute, "attribute");
    string_creator(of, "of");
    string_creator(rw, "rw");
    string_creator(type, "type");
    string_creator(typeobj, "typeobj");
    string_creator(free_str, "free_str");
    string_creator(callback_args, "callback_args");
    string_creator(encoding, "encoding");
    string_creator(inlined, "inlined");
    string_creator(repr, "repr");
    string_creator(anon, "<anon>");
    string_creator(P6opaque, "P6opaque");
    string_creator(box_target, "box_target");
    string_creator(array, "array");
    string_creator(positional_delegate, "positional_delegate");
    string_creator(associative_delegate, "associative_delegate");
    string_creator(auto_viv_container, "auto_viv_container");
    string_creator(done, "done");
    string_creator(error, "error");
    string_creator(stdout_bytes, "stdout_bytes");
    string_creator(stderr_bytes, "stderr_bytes");
    string_creator(merge_bytes, "merge_bytes");
    string_creator(buf_type, "buf_type");
    string_creator(write, "write");
    string_creator(stdin_fd, "stdin_fd");
    string_creator(stdin_fd_close, "stdin_fd_close");
    string_creator(stdout_fd, "stdout_fd");
    string_creator(stderr_fd, "stderr_fd");
    string_creator(nativeref, "nativeref");
    string_creator(refkind, "refkind");
    string_creator(positional, "positional");
    string_creator(lexical, "lexical");
    string_creator(dimensions, "dimensions");
    string_creator(ready, "ready");
    string_creator(multidim, "multidim");
    string_creator(entry_point, "entry_point");
    string_creator(resolve_lib_name, "resolve_lib_name");
    string_creator(resolve_lib_name_arg, "resolve_lib_name_arg");
    string_creator(kind, "kind");
    string_creator(instrumented, "instrumented");
    string_creator(heap, "heap");
    string_creator(translate_newlines, "translate_newlines");
    string_creator(platform_newline, MVM_TRANSLATE_NEWLINE_OUTPUT ? "\r\n" : "\n");
    string_creator(path, "path");
    string_creator(config, "config");
    string_creator(replacement, "replacement");
    string_creator(dot, ".");
}

/* Drives the overall bootstrap process. */
void MVM_6model_bootstrap(MVMThreadContext *tc) {
    /* First, we have to get the VMString type to exist; this has to
     * come even before REPR registry setup because it relies on
     * being able to create strings. */
    create_stub_VMString(tc);

    /* Set up some string constants commonly used. */
    string_consts(tc);

    /* Now we've enough to actually create the REPR registry. */
    MVM_repr_initialize_registry(tc);

    /* Create stub VMNull, BOOTInt, BOOTNum, BOOTStr, BOOTArray, BOOTHash,
     * BOOTCCode, BOOTCode, BOOTThread, BOOTIter, BOOTContext, SCRef,
     * CallCapture, BOOTIO, BOOTException, BOOTQueue, BOOTAsync,
     * BOOTReentrantMutex, and BOOTCapture types. */
#define create_stub_boot_type(tc, reprid, slot, makeboolspec, boolspec) do { \
    const MVMREPROps *repr = MVM_repr_get_by_id(tc, reprid); \
    MVMObject *type = tc->instance->slot = repr->type_object_for(tc, NULL); \
    if (makeboolspec) { \
        MVMBoolificationSpec *bs; \
        bs = MVM_malloc(sizeof(MVMBoolificationSpec)); \
        bs->mode = boolspec; \
        bs->method = NULL; \
        type->st->boolification_spec = bs; \
    } \
} while (0)
    create_stub_boot_type(tc, MVM_REPR_ID_MVMNull, VMNull, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_P6int, boot_types.BOOTInt, 1, MVM_BOOL_MODE_UNBOX_INT);
    create_stub_boot_type(tc, MVM_REPR_ID_P6num, boot_types.BOOTNum, 1, MVM_BOOL_MODE_UNBOX_NUM);
    create_stub_boot_type(tc, MVM_REPR_ID_P6str, boot_types.BOOTStr, 1, MVM_BOOL_MODE_UNBOX_STR_NOT_EMPTY);
    create_stub_boot_type(tc, MVM_REPR_ID_VMArray, boot_types.BOOTArray, 1, MVM_BOOL_MODE_HAS_ELEMS);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMHash, boot_types.BOOTHash, 1, MVM_BOOL_MODE_HAS_ELEMS);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMCFunction, boot_types.BOOTCCode, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMCode, boot_types.BOOTCode, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMThread, boot_types.BOOTThread, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMIter, boot_types.BOOTIter, 1, MVM_BOOL_MODE_ITER);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMContext, boot_types.BOOTContext, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_SCRef, SCRef, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMCallCapture, CallCapture, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMOSHandle, boot_types.BOOTIO, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMException, boot_types.BOOTException, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMStaticFrame, boot_types.BOOTStaticFrame, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMCompUnit, boot_types.BOOTCompUnit, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMMultiCache, boot_types.BOOTMultiCache, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMContinuation, boot_types.BOOTContinuation, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMThread, Thread, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_ConcBlockingQueue, boot_types.BOOTQueue, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMAsyncTask, boot_types.BOOTAsync, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_ReentrantMutex, boot_types.BOOTReentrantMutex, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMSpeshLog, SpeshLog, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMStaticFrameSpesh, StaticFrameSpesh, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMSpeshPluginState, SpeshPluginState, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMSpeshCandidate, SpeshCandidate, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);
    create_stub_boot_type(tc, MVM_REPR_ID_MVMCapture, boot_types.BOOTCapture, 0, MVM_BOOL_MODE_NOT_TYPE_OBJECT);

    /* Bootstrap the KnowHOW type, giving it a meta-object. */
    bootstrap_KnowHOW(tc);

    /* Give stub types meta-objects. */
#define meta_objectifier(tc, slot, name) do { \
    add_meta_object((tc), (tc)->instance->slot, (name)); \
    MVM_gc_root_add_permanent_desc((tc), (MVMCollectable **)&(tc)->instance->slot, name); \
} while (0)
    meta_objectifier(tc, VMString, "VMString");
    meta_objectifier(tc, VMNull, "VMNull");
    meta_objectifier(tc, boot_types.BOOTInt, "BOOTInt");
    meta_objectifier(tc, boot_types.BOOTNum, "BOOTNum");
    meta_objectifier(tc, boot_types.BOOTStr, "BOOTStr");
    meta_objectifier(tc, boot_types.BOOTArray, "BOOTArray");
    meta_objectifier(tc, boot_types.BOOTHash, "BOOTHash");
    meta_objectifier(tc, boot_types.BOOTCCode, "BOOTCCode");
    meta_objectifier(tc, boot_types.BOOTCode, "BOOTCode");
    meta_objectifier(tc, boot_types.BOOTThread, "BOOTThread");
    meta_objectifier(tc, boot_types.BOOTIter, "BOOTIter");
    meta_objectifier(tc, boot_types.BOOTContext, "BOOTContext");
    meta_objectifier(tc, SCRef, "SCRef");
    meta_objectifier(tc, CallCapture, "CallCapture");
    meta_objectifier(tc, boot_types.BOOTIO, "BOOTIO");
    meta_objectifier(tc, boot_types.BOOTException, "BOOTException");
    meta_objectifier(tc, boot_types.BOOTStaticFrame, "BOOTStaticFrame");
    meta_objectifier(tc, boot_types.BOOTCompUnit, "BOOTCompUnit");
    meta_objectifier(tc, boot_types.BOOTMultiCache, "BOOTMultiCache");
    meta_objectifier(tc, boot_types.BOOTContinuation, "BOOTContinuation");
    meta_objectifier(tc, Thread, "Thread");
    meta_objectifier(tc, boot_types.BOOTQueue, "BOOTQueue");
    meta_objectifier(tc, boot_types.BOOTAsync, "BOOTAsync");
    meta_objectifier(tc, boot_types.BOOTReentrantMutex, "BOOTReentrantMutex");
    meta_objectifier(tc, SpeshLog, "SpeshLog");
    meta_objectifier(tc, StaticFrameSpesh, "StaticFrameSpesh");
    meta_objectifier(tc, SpeshPluginState, "SpeshPluginState");
    meta_objectifier(tc, SpeshCandidate, "SpeshCandidate");

    /* Create the KnowHOWAttribute type. */
    create_KnowHOWAttribute(tc);

    /* Bootstrap typed arrays. */
    tc->instance->boot_types.BOOTIntArray = boot_typed_array(tc, "BOOTIntArray",
        tc->instance->boot_types.BOOTInt);
    MVM_gc_root_add_permanent_desc(tc,
        (MVMCollectable **)&tc->instance->boot_types.BOOTIntArray,
        "BOOTIntArray");
    tc->instance->boot_types.BOOTNumArray = boot_typed_array(tc, "BOOTNumArray",
        tc->instance->boot_types.BOOTNum);
    MVM_gc_root_add_permanent_desc(tc,
        (MVMCollectable **)&tc->instance->boot_types.BOOTNumArray,
        "BOOTNumArray");
    tc->instance->boot_types.BOOTStrArray = boot_typed_array(tc, "BOOTStrArray",
        tc->instance->boot_types.BOOTStr);
    MVM_gc_root_add_permanent_desc(tc,
        (MVMCollectable **)&tc->instance->boot_types.BOOTStrArray,
        "BOOTStrArray");

    /* Set some fields on the first TC to exist, since MVM_tc_create
     * runs before the bootstrap, but tries to initialize these fields to
     * VMNull regardless */
    tc->last_payload = tc->instance->VMNull;
    tc->plugin_guard_args = tc->instance->VMNull;

    /* Set up HLL roles. */
    STABLE(tc->instance->boot_types.BOOTInt)->hll_role   = MVM_HLL_ROLE_INT;
    STABLE(tc->instance->boot_types.BOOTNum)->hll_role   = MVM_HLL_ROLE_NUM;
    STABLE(tc->instance->boot_types.BOOTStr)->hll_role   = MVM_HLL_ROLE_STR;
    STABLE(tc->instance->boot_types.BOOTArray)->hll_role = MVM_HLL_ROLE_ARRAY;
    STABLE(tc->instance->boot_types.BOOTHash)->hll_role  = MVM_HLL_ROLE_HASH;
    STABLE(tc->instance->boot_types.BOOTCode)->hll_role  = MVM_HLL_ROLE_CODE;

    /* Get initial __6MODEL_CORE__ serialization context set up. */
    setup_core_sc(tc);
    MVM_6model_containers_setup(tc);

    MVM_intcache_for(tc, tc->instance->boot_types.BOOTInt);
}
