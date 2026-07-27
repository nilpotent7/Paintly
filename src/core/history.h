#pragma once

#include "document.h"

#define HISTORY_MAX 24  /* entries kept per stack; oldest dropped first */

History *history_new (void);
void     history_free(History *h);

void history_push(Document *doc);  /* call BEFORE modifying pixels */

void history_push_canvas(Document *doc);

gboolean history_undo(Document *doc);
gboolean history_redo(Document *doc);
