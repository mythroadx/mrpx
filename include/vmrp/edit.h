#ifndef VMRP_EDIT_H
#define VMRP_EDIT_H

#include <stdbool.h>
#include <stdint.h>

bool editWaiting(void);
uint32_t editGeneration(void);
int submitEditText(const char *utf8);
int cancelEdit(void);

#endif
