void MVM_6model_add_container_config(PARROT_INTERP, STRING *name,
        ContainerConfigurer *configurer);
ContainerConfigurer * MVM_6model_get_container_config(PARROT_INTERP, STRING *name);
void MVM_6model_containers_setup(PARROT_INTERP);