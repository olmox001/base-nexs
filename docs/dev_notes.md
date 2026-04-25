---
name: User feedback and preferences
description: How the user communicates and what patterns to follow
type: feedback
---

User mixes Italian and English in requests. Technical instructions are precise even when informal.

**Why:** Native Italian speaker, comfortable switching languages mid-message.
**How to apply:** Respond in English (the code language), understand Italian instructions without asking for clarification.

---

Prefer to move old code to `old/` rather than deleting, unless explicitly told to delete.

**Why:** User said "sposta src e basereg.h in old" instead of "elimina".
**How to apply:** When restructuring, use `old/` as a graveyard. Only delete when user says "elimina" or "delete".

---

User expects IPC and pointer syntax to be internally consistent — same names in keyword and function-call forms.

**Why:** User flagged `sendmsg` vs `sendmessage` inconsistency.
**How to apply:** Keyword forms and their fn_table counterparts must share the same name.
