#pragma once

typedef struct __sigset_t { unsigned long __bits[128/sizeof(long)]; } sigset_t;
