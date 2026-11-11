---
name: A hole in the safety of the language
about: A program without import unsafe that corrupts memory, reads a value that is gone, or has a data race
labels: bug, safety
---

A program without `import unsafe` that does one of these is a bug of Cromo: [What Cromo checks, and what it does not](https://cromolang.org/book/en/appendix-safety.html) lists what should hold.

**The version and the system** (the output of `cromo version`, and Windows or Linux):

**The program:**

```cromo

```

**The rule it breaks, and what happens when it runs:**
