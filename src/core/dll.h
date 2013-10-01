struct MVMDLLRegistry {
    DLLib *lib;
    MVMString *name;
    MVMuint32 refcount;
    UT_hash_handle hash_handle;
};
