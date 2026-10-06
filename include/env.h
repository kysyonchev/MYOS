#ifndef ENV_H
#define ENV_H

#define ENV_MAX        16
#define ENV_NAME_MAX   24
#define ENV_VALUE_MAX  48

void env_init(void);
int  env_set(const char* name, const char* value);
const char* env_get(const char* name);
int  env_unset(const char* name);
void env_list(void);
int  env_expand(const char* in, char* out, int max);

#endif
