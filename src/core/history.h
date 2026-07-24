#pragma once

#include "document.h"

#define HISTORY_MAX 24  /* entries kept per stack; oldest dropped first */

History *history_new (void);
void     history_free(History *h);

void history_push(Document *doc);  /* call BEFORE modifying pixels */
void history_undo(Document *doc);
void history_redo(Document *doc);
