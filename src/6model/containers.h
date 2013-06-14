void MVM_6model_add_container_config(MVMThreadContext *tc, MVMString *name, MVMContainerConfigurer *configurer);
ContainerConfigurer * MVM_6model_get_container_config(MVMThreadContext *tc, MVMString *name);
void MVM_6model_containers_setup(MVMThreadContext *tc);
