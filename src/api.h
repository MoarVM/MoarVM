#ifndef MVM_API_H
#define MVM_API_H

#include <stddef.h>

#ifndef MVM_TYPES_INCLUDED
#include <moar/types.h>
#endif

#ifndef MVM_PUBLIC
#ifdef _WIN32
#define MVM_PUBLIC __declspec(dllimport)
#else
#define MVM_PUBLIC
#endif
#endif

#ifndef MVM_NO_RETURN
#define MVM_NO_RETURN
#endif

#ifndef MVM_NO_RETURN_ATTRIBUTE
#define MVM_NO_RETURN_ATTRIBUTE
#endif

MVM_PUBLIC MVMInstance * MVM_vm_create_instance(void);
MVM_PUBLIC void MVM_vm_run_file(MVMInstance *instance, const char *filename);
MVM_PUBLIC void MVM_vm_run_bytecode(MVMInstance *instance,
        const void *bytes, size_t size);
MVM_PUBLIC void MVM_vm_dump_file(MVMInstance *instance, const char *filename);
MVM_PUBLIC MVM_NO_RETURN void MVM_vm_exit(MVMInstance *instance)
        MVM_NO_RETURN_ATTRIBUTE;
MVM_PUBLIC void MVM_vm_destroy_instance(MVMInstance *instance);
MVM_PUBLIC void MVM_vm_set_clargs(MVMInstance *instance, int argc, char **argv);
MVM_PUBLIC void MVM_vm_set_exec_name(MVMInstance *instance, const char *exec_name);
MVM_PUBLIC void MVM_vm_set_prog_name(MVMInstance *instance, const char *prog_name);
MVM_PUBLIC void MVM_vm_set_lib_path(MVMInstance *instance,
        int count, const char **lib_path);

#endif
