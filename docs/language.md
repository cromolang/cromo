# Cromo: draft specification

> **Status: draft.** The final choices are only those marked *Decided* in [decisions.md](decisions.md). Next to each part is the decision it comes from (D1, D2, …); the parts marked as a proposal are not decided yet.

## 1. Principles

1. **Simplicity above all** (D8): a rule that a C/C++ programmer understands immediately is worth more than a more powerful rule.
2. **Every value has a single owner** and its lifetime follows the scopes (D6).
3. **No pointers, no `null`, no garbage collector** (D4, D5, D47).
4. **No undefined behavior** in normal code (D45), and no uninitialized values (D31).
5. **Visible costs:** no hidden copies (D23) and no hidden allocations. What happens to a variable can be read in the call (`ref`, `move`, D22).
6. **Performance from guarantees:** every rule of the language (no aliasing, no null, no undefined overflow) becomes information for the LLVM optimizer.

## 2. Lexical structure

`cromo fmt` writes all the code in one style, without options (D161): 4 spaces, `{` at the end of the line and `} else {` (D162). It keeps the line breaks of the author (D163).

- Sources in UTF-8, with the `.cro` extension (D29).
- **A statement ends at the end of the line**, no `;` (D17). A statement continues on the next line if the line ends with a binary operator, a comma or an open parenthesis, or if there are round or square brackets still open. Only one statement per line (D32).
- Comments: `// line` and `/* block */`.
- Identifiers: letters, digits and `_`, not starting with a digit.
- Literals:
  - integers: `42`, `0xFF`, `0b1010`, `1_000_000`
  - floating-point: `3.14`, `1e-9`
  - characters: `'a'`, one byte of type `char`, ASCII only; escapes `'\n'`, `'\t'`, `'\\'`, `'\''`, `'\x41'` (D46)
  - strings: `"text\n"`
  - booleans: `true`, `false`
- **Provisional** keywords: `struct interface enum import extern unsafe const void if else for in break continue ret switch case default try catch finally throw ref move public true false`. Among these, the following are decided:
  - `if`, `else`, `for` (D15);
  - `try`, `catch`, `finally` (D16);
  - `ret` (D18);
  - `struct`, `void`, `break`, `continue`, `throw` (D20);
  - `ref`, `move` (D22, D23);
  - `const` for methods and variables (D24, D26), `in` (D25);
  - `switch`, `case`, `default` (D34), `enum` (D104);
  - `unsafe` (D37), `interface` (D40), `import` (D41), `extern` (D42), `public` (D50).
- `print` and `println` are not keywords but special compiler functions (D21, D38).

## 3. Types

| Category | Examples (draft) | Reference |
|---|---|---|
| Integers | `i8 i16 i32 i64 u8 u16 u32 u64` | D9 |
| Floating-point | `f32 f64` | D9 |
| Booleans | `bool` | D9 |
| Characters | `char`, a synonym of `u8`: one byte | D14, D46 |
| 128-bit integers | postponed until after the first version | D14 |
| Strings | `string`, UTF-8 text | D10 |
| Structs | `struct Point` with the fields `f64 x` and `f64 y` | D11, §6 |
| Dynamic containers | `list<T>`, and `map<K, V>` with the keys that implement `Key` | D11, D13, D197, D198 |
| Interfaces | `interface Shape { ... }` | D40, D49, §12 |
| Generics | `T max<T : Comparable>(T a, T b)` | D39, D52 |
| Enums with data | `enum Shape { Circle(f64 radius) ... }` | D34, D104, §9 |
| Fixed-size arrays | `i32[4]`, `f64[3][3]` | D196, §16 |

- **There are no pointer types** (D4) **and no types whose size depends on the platform** such as `usize` (D9): every type has the same size on all platforms. Indices and lengths are `i64` (D12).
- **Numeric conversions** (D33): implicit only when no information is lost (`i32` → `i64`, `i32` → `f64`), otherwise explicit: `i32(x)`, `f64(n)`.
- **Overflow** (D30): in debug it stops the program, in release the value wraps around from the other end of the range.
- **Operators** (D59, D193–D195): from the strongest, `* / %`, `+ -`, `<< >>`, `&`, `^`, `|`, the comparisons, `&&`, `||`. The bitwise operators bind more than the comparisons, so `flags & MASK == 0` is `(flags & MASK) == 0`. `!` negates a `bool` and inverts the bits of an integer. A shift beyond the width of the type is a panic in debug, and in release its count is taken modulo the width; `>>` keeps the sign of a signed integer.
- **Numbers from text** (D199): `i64("42")`, `f64("2.5")` read a number, and throw an exception when the text is not a number of the type.

## 4. Variables

```cromo
i32 x = 5
i64 y = x + 1           // the type is always written (D54)
const f64 PI = 3.14159  // immutable

i32 n                   // 0
string name             // empty string
list<f64> grades        // empty list
```

- The type is written before the name, always: there is no type inference (D20, D54).
- A literal takes the type of its context (D55): in `i64 y = x + 1`, `1` has the type of `x`; the result is an `i32`, converted to `i64` without loss (D33).
- **Without a value, a variable takes the default** (D31): `0` for numbers, `false` for `bool`, empty string and empty list. For a struct, every field takes its default, and no constructor is called (D48). A field can declare its initial value (`i32 fd = -1`), and an enum starts from the first case (D51). There are no uninitialized values.
- A `const` variable must always have a value (D51).
- Variables are mutable by default; `const` makes them immutable (D26).
- `const` is deep: on a `const` `string` or `list` only `const` methods can be called, and it cannot be passed with `ref`.
- A `const` variable can be moved with `move`: the value does not change, it only passes to another owner (D26).

## 5. Functions and parameter passing (D22)

```cromo
i32 add(i32 a, i32 b) {
    ret a + b
}
```

A function is exited with `ret` (D18). Every path of a non-`void` function must end with a `ret` of a value of the declared type, otherwise it is a compile error; in a `void` function the final `ret` is optional (D47).

There are no optional types (D47). When a function may have no result, you use a conventional value, an exception, or a `bool` with the result in a `ref` parameter:

```cromo
bool find(list<Student> students, string name, ref i64 pos) {
    for (i64 i = 0; i < students.len(); i++) {
        if (students[i].name == name) {
            pos = i
            ret true
        }
    }
    ret false
}

i64 pos
if (find(students, "Anna", ref pos)) {
    println("found at position {}", pos)
}
```

A parameter is passed in one of three ways, and **the word that appears in the signature is repeated in the call**:

| Mode | In the signature | In the call | Meaning | How the compiler implements it |
|---|---|---|---|---|
| Read (default) | `Student s` | `f(anna)` | the function only reads, the caller remains the owner | up to 16 bytes by copy, beyond that by reference |
| Modify | `ref Student s` | `f(ref anna)` | the function modifies the caller's object | reference marked `noalias` |
| Ownership transfer | `move string n` | `f(move name)` | the function becomes the owner, and the variable is no longer usable | by value |

```cromo
void addGrade(ref Student s, f64 grade) {
    s.grades.add(grade)
}

Student anna = Student("Anna")
addGrade(ref anna, 8.5)            // you can see that anna may change
addGrade(ref students[0], 9.0)     // an element is passed with ref too (D35)
students.add(move anna)            // you can see that anna passes to the list
students.add(Student("Marco"))     // a temporary does not need move
```

**The keywords state the intent, not the mechanism.** In C++ `&` means "by reference" and `const` means "not modifiable", and they combine into `const T&`. In Cromo the mode says what the function does: it reads, modifies (`ref`) or takes ownership (`move`). How to pass the value is decided by the compiler:
- the read parameter **is already C++'s `const T&`**: beyond 16 bytes it is **always** passed by reference, without a copy (D22);
- up to 16 bytes (an `i32`, a struct of two `f64`) it is passed by copy, because that costs less than following an address;
- during the call the object cannot change: references do not outlive the call and exclusivity holds. This is why the compiler can safely pass it by reference.

This is why `ref const` does not exist: it would be a second way of writing the read parameter.

**Rules:**
- **`ref` exists only for parameters:** no `ref` local variables, fields or return values. This way a reference never outlives the object, without the need for a borrow checker.
- **Exclusivity:** an argument passed with `ref` cannot refer to the same object as another argument of the same call. With variable indices (`swap(ref v[i], ref v[j])`) the check is done at runtime (D35).
- **`move const`:** a `move` parameter can also be `const` (`move const string n`): the function becomes its owner but cannot modify it.
- **The object of a method** that is not `const` counts as a `ref` argument for exclusivity. A read argument of a simple type that overlaps something the call modifies is passed as a copy, so `c.add(c.step)` is allowed (D79).
- **Return values:** they are always returned by value, that is, moved. The compiler avoids the copy by constructing the result directly in the destination.

**`main`** returns `i32`, the exit code, or nothing. It can receive the arguments of the program (D154):

```cromo
i32 main(list<string> args) {
    for (string arg in args) {   // args[0] is the name of the program, as in C
        println("{}", arg)
    }
    ret 0
}
```

## 6. Structs, constructors and destructors (RAII)

```cromo
struct File {
    i32 fd = -1             // initial value: the default of File is "no open file" (D51)

    File(string path) {     // constructor
        fd = os.open(path)
    }

    ~File() {               // destructor
        if (fd >= 0) {
            os.close(fd)
        }
    }

    string readLine() { ... }

    bool isOpen() const {   // const: does not modify the fields
        ret fd >= 0
    }
}
```

The shape of the struct (fields, constructor `Name(...)`, destructor `~Name()`, methods) is decided (D20).
- A struct has at most one constructor (D69). Without a constructor, `Point()` gives the default value (D71).
- Inside methods, constructor and destructor, the fields are used by name; there is no `this`, and a local variable or parameter cannot have the name of a field (D70). A call `f()` refers first to a method of the same struct, then to a free function (D72).
- For now the initial value of a field is a literal (D73).
- The fields are laid out in memory in the order of declaration, with the alignment of C (D74).
- A destructor is never called by hand (D68).
- Until `move` arrives, a struct that needs destruction is not copied: it is created directly in its variable, returned with `ret` of a local variable, or replaced with a new object (D75).
- `==` and printing with `{}` do not work on structs yet (D76).

**Lifetime rules (D68, modeled on C++):**
1. An object is destroyed at the end of the scope in which it is declared. Objects of the same scope are destroyed in reverse order of declaration.
2. Temporary objects are destroyed at the end of the statement that contains them.
3. After the destructor body, the fields are destroyed, in reverse order of declaration.
4. Early exits (`ret`, `break`, `continue`) and the propagation of an exception destroy everything that is alive in the scopes being left.
5. An object moved with `move` is not destroyed by the old owner (D23).
6. The fields that the constructor does not assign take their default value (D31).
7. Destructors must not throw exceptions (D36).
8. If a struct does not define a destructor, the compiler generates one that destroys the fields.
9. A struct with its own destructor is never a simple type: it is moved with `move` and copied only if it defines `copy()` (D23, D35).

**`const` methods (D24).** A method can modify the fields, unless it is marked `const` after the parameters, as in C++.
- On a read-only object (a parameter without `ref`, the element of a `for (T x in items)`, a `const` variable) only `const` methods can be called.
- Without pointers, `const` is deep: a `const` method cannot modify even the strings and lists contained in the object.

A constructor that fails, for example because the file does not open, throws an exception (D16). The fields already constructed are destroyed and the object does not exist.

## 7. Moving and copying (D23)

```cromo
string s = "hello"
string t = move s       // move: t is now the owner
println("{}", s)        // compile ERROR: 's' has been moved
string u = t.copy()     // explicit copy
string v = t            // ERROR: a string needs move or copy()

i32 a = 5
i32 b = a               // simple types are always copied
```

- **Simple types** (numbers, `bool`, structs made only of simple types and without their own destructor): they are copied implicitly.
- **Resource-owning types** (`string`, `list`, structs that contain them or that have their own destructor): no implicit copy. You write `move x` or `x.copy()`, both in calls and in assignments.
- **Temporaries:** they are moved without `move`.
- **`ret x`** of a local variable moves it without `move`.
- **After a `move`**, using the variable is a compile error, even when it was moved only on some paths.
- **Moving a field or a `ref` parameter** leaves the default value in its place: `File f = move s.log` empties `s.log`, and `s` stays usable (D78).
- **`move` of a simple type** copies it, and the variable stays usable (D80). Passing it to a `move` parameter, `move` can be omitted (D94).
- **`copy()` for structs** is generated by the compiler if all the fields can be copied and the struct does not have its own destructor (D35).

## 8. Dynamic memory (D11)

Heap memory is obtained **only** through the dynamic containers and `string`. They are implemented in the standard library, in the `unsafe` dialect that only the modules with `import unsafe` use (D37, D127). There is no garbage collector: when a variable goes out of scope, everything it contains is freed, at any depth.

```cromo
struct Student {
    string name        // text on the heap
    list<f64> grades   // elements on the heap
    u8 year
}

void example() {
    list<Student> students = list<Student>()
    students.add(Student("Anna"))
    students.add(Student("Marco"))
    // ...
}   // students goes out of scope: the names and the grade lists are freed,
    // and finally the list itself
```

Recursive structures go through a container:

```cromo
struct Node {
    i32 value
    list<Node> children   // allowed: the children are on the heap
}

struct Wrong {
    Wrong other    // ERROR: infinite size
}
```

Graphs are modeled with indices into a list. Out-of-bounds accesses terminate the program (D30).

What `list<T>` and `string` offer in M5 (D99, D100):

```cromo
list<i64> v = list<i64>()     // empty, like the default value
v.add(5)                      // add, insert(i, x), remove(i), clear(), reserve(n), copy()
v[0] = 7                      // the element itself: it can be assigned, passed with ref, moved
i64 n = v.len()

string s = "città"            // a literal is a string where a string is expected
i64 bytes = s.len()           // 6: the length is in bytes
string t = s + "!"            // ==, !=, <, <=, >, >=, + and += (D95)
```

## 9. Control flow (D15, D32)

### Conditions

```cromo
if (x > 0) {
    ...
} else if (x < 0) {
    ...
} else {
    ...
}
```

- Braces are **mandatory**. The `{` can be on the same line or on the next line.
- Conditions must be `bool`: `if (n)` is an error, you write `if (n != 0)`.

### Loops

There is only one loop, `for`, in four forms. `while` and `do … while` do not exist.

```cromo
for {                                    // infinite loop
    ...
}

for (active) {                           // condition only, instead of while
    ...
}

for (i64 i = 0; i < n; i++) {            // classic for
    ...
}

for (Student s in students) {            // over the elements, read-only (D25)
    println("{}", s.name)
}

for (ref Student s in students) {        // over the elements, modifiable
    s.addGrade(1.0)
}
```

- The variable declared in the first part of the classic `for` lives only inside the loop.
- In the header of the classic `for` the three parts are separated by `;`: this is the only place where it appears (D20).
- `break` and `continue` exist, without labels. `i++` and `i--` are used only as a statement on their own, never inside expressions (D32).
- While a `for` over the elements is running, the list cannot change length (D22): in the body it can be read, but not modified, except its elements through the variable of the loop (D101).

### Enums and switch (D34, D104–D112)

```cromo
enum Shape {
    Circle(f64 radius)          // a case with its data, written like parameters
    Rect(f64 width, f64 height)
    Empty
}

f64 area(Shape shape) {
    switch (shape) {
        case Circle(f64 r) {    // the data, read-only and without a copy
            ret 3.14 * r * r
        }
        case Rect(f64 w, f64 h) {
            ret w * h
        }
        case Empty {
            ret 0.0
        }
    }
}

Shape s = Shape.Circle(1.0)     // a value is created with the name of the enum
```

- On an enum the `switch` must cover all the cases, or have `default` (D34), which is written `default { ... }`.
- The data of a case get a name with their type; with `ref` they can be modified (D107). Without the parentheses the data are ignored.
- The default value of an enum is its first case (D51).
- No fall-through from one case to the next, and several cases without data in one branch: `case Red, Green { ... }`. There is no `goto` (D108).
- `switch` also works on integers and strings, with literals as cases; there `default` is optional (D109).
- Enums can have methods, implement interfaces and be generic, like structs (D110). An enum whose cases have no data can be compared with `==` (D112).
- In a method of the enum, `switch { ... }` without a value is the `switch` on the enum itself (D117).
- In an enum without data, the cases can have a value, `Red = 1`. `i32(color)` gives the value of the case, and `Color(n)` the case with the value `n`, with a panic if there is none (D135).
- `break` and `continue` written directly in a branch of a `switch` are errors: the branch ends by itself, and a `break` written out of C habit would leave the loop instead (D116).
- While a branch uses the data of a case, the value of the `switch` cannot be modified (D125).

## 10. Errors and exceptions (D16, D36)

```cromo
string firstLine(string path) {
    File f = File(path)       // if the file does not open, the constructor throws
    ret f.readLine()
}                             // f is closed here, even if readLine throws

i32 main() {
    try {
        println("{}", firstLine("data.txt"))
    } catch (IoError e) {
        println("error: {}", e.message())
        ret 1
    } finally {
        println("done")
    }
    ret 0
}
```

- The types that are thrown implement the `Exception` interface of the library, `string message() const`; the library also has a ready-made `Error` with a message: `throw Error("bad input")` (D115).
- The exception of a `catch` is read-only, and `throw` alone rethrows it (D114). `catch (Exception e)` catches everything.
- Exceptions are not declared in the signature.
- While an exception propagates, all the live objects in the scopes it leaves are destroyed (RAII).
- `finally` is executed on every exit from the `try` block: normal, `ret`, `break`, `continue` or exception.
- Programming errors (index out of bounds, overflow, division by zero) are not exceptions: they terminate the program.
- `panic("reason")` terminates the program on purpose, as those errors do (D62). Like `ret`, it ends a path of the function (D102).
- Destructors and `finally` cannot let an exception out: it must be caught inside them, or it is a compile error (D113).
- An exception that leaves `main` stops the program with `uncaught exception: <message>` (D121).

## 11. Printing and formatting (D21, D38)

```cromo
print("no newline")
println("Hello {}, you are {} years old", name, age)
println("average: {:.2f}", average)          // two decimal places
println("[{:>8}] [{:<8}] [{:^8}]", a, b, c)  // right-aligned, left-aligned, centered
println("{0} and again {0}", x)              // argument by position
println("hex {:x}, binary {:b}", n, n)
println("braces: {{ and }}")                 // literal braces
string s = format("{} + {} = {}", 2, 3, 5)   // returns the string
eprintln("error: {}", message)               // to stderr
```

- `print` does not add a newline, `println` does.
- The placeholder syntax is that of C++'s `std::format` / `{fmt}`. Specifiers: fill and alignment (`<`, `>`, `^`), sign, `#`, `0`, width, `.precision` and type (`d`, `x`, `X`, `b`, `o`, `e`, `f`, `g`, `s`).
- The format string is a literal: the compiler checks the number and the types of the arguments.
- How to make user-defined types printable will be decided together with the standard library, with a dedicated interface (D49).

## 12. Interfaces and generics (D39, D40)

There is no inheritance: polymorphism goes through interfaces. The syntax of interfaces (D49) and of generic constraints (D52) is decided. Inside an interface, `Self` is the type that implements it (D84).

```cromo
interface Shape {
    f64 area() const
}

struct Circle : Shape {
    f64 r
    f64 area() const { ret 3.14159 * r * r }
}

struct Square : Shape {
    f64 side
    f64 area() const { ret side * side }
}

// generic with a constraint: one version for each type, no runtime cost
f64 doubleArea<T : Shape>(T f) {
    ret 2.0 * f.area()
}

// interface-typed parameter: dynamic call
void printArea(Shape f) {
    println("{:.2f}", f.area())
}

list<Shape> shapes = list<Shape>()   // objects of different types in the same list
shapes.add(Circle(1.0))
shapes.add(Square(2.0))
```

- Interface-typed parameters are always allowed: they are passed by reference, without allocating anything.
- Objects of different types can be together only in a `list` of interfaces, where each element is allocated on the heap with its method table.
- Variables and fields of interface type are not allowed in the first version: a generic is used instead.
- Calling a generic function, the type arguments are deduced from the arguments: `doubleArea(c)`. They are written when they cannot be deduced, `zero<i64>()` (D86). A generic struct is always written with its type arguments: `Pair<i32, f64>`.
- A generic is checked once, at its definition: on a `T` only the methods of its constraints can be called (D52). Whether a `T` can be copied is checked for each type it is used with (D85).
- An interface that uses `Self` can only be a constraint: `T max<T : Comparable>(T a, T b)` (D84).
- The numbers and `bool` do not implement interfaces yet: the built-in interfaces for comparison, equality, printing and arithmetic will come with the standard library (D87).

## 13. Modules (D41, D50, D126)

```cromo
// file geometry.cro
public struct Point {
    public f64 x                  // visible to whoever imports the module
    public f64 y
    i32 visits                    // private to the module
}

public f64 distance2(Point a, Point b) {
    ret square(a.x - b.x) + square(a.y - b.y)
}

f64 square(f64 v) {               // private to the module
    ret v * v
}
```

```cromo
// file main.cro, in the same folder
import geometry

i32 main() {
    Point a = Point()             // the names of the module, used directly (D126)
    geometry.Point b = geometry.Point()
    println("{}", distance2(a, b))
    ret 0
}
```

- There is no preprocessor and there are no header files.
- A file is a module; a folder is a module that contains the modules of its files. `import a.b` corresponds to `a/b.cro`, or to the folder `a/b`, starting from the folder of the main file (D130).
- Everything is private to the module, except what is marked `public`: also the fields, the methods and the constructor of a struct (D132).
- The public names of an imported module are used directly. The module in front, with the last name of its path, is needed only when two imported modules give the same name; the names of the module itself come first (D126).
- The imports come at the top of the file. They are not transitive, and two modules can import each other (D131).
- The standard library is seen by every module without importing it.

## 14. Interoperability with C (D42, D127–D129, D136–D138, D143)

```cromo
import unsafe

extern "kernel32" {
    i32 QueryPerformanceCounter(ref i64 count)
    i32 QueryPerformanceFrequency(ref i64 frequency)
}

public struct Stopwatch {
    i64 start
    i64 frequency

    public Stopwatch() {
        QueryPerformanceFrequency(ref frequency)
        QueryPerformanceCounter(ref start)
    }
}
```

- A module that writes `import unsafe` can declare C functions in an `extern` block, use the type `addr` and the built-in functions of the library, as `alloc` and `free` (D127). The other modules use only its public interface, which must be safe.
- The block names its library: `cromo` links it without options (D128). The C functions keep their name and are private to the module (D133).
- `extern "name"` is `name.dll`, and `cromo` writes its import library: the Windows SDK is not needed. A `name.lib` next to the module is used instead, for example a static C library (D136).
- `cromo` reads the functions exported by the DLL, next to the module or in the system folder of Windows: a C function that the DLL does not have is a compile error (D137).
- A DLL next to a module is copied next to the executable, if that is in another folder (D138).
- When compiling for Linux, `extern "name"` is the static library `libname.a` next to the module (D143). Shared libraries and the C library of Linux are not supported yet.
- They take and return numbers, `bool` and `addr`; a `ref` parameter is a C pointer to a number or to a struct of numbers, which have the layout of C (D129). The `BOOL` of Windows is an `i32`.
- C resources are wrapped in structs with a destructor (D42): see `examples/05_c_interop.cro`.
- Strings cannot be passed to C yet: it is an open question in [decisions.md](decisions.md).

## 15. Anonymous functions (D183–D186)

```cromo
i64 count(list<i64> values, func(i64) bool keep) {
    i64 n = 0
    for (i64 v in values) {
        if (keep(v)) {
            n++
        }
    }
    ret n
}

func(i64) i64 adder(i64 amount) {
    ret [amount](i64 x) i64 { ret x + amount }   // it keeps a copy of amount
}

i32 main() {
    list<i64> values
    i64 limit = 6
    i64 big = count(values, [limit](i64 x) bool { ret x > limit })
    i64 total = 0
    count(values, [ref total](i64 x) bool {          // it modifies total
        total += x
        ret true
    })
    func(i64) i64 plus3 = adder(3)
    ret i32(plus3(4))
}
```

- A lambda writes what it captures between square brackets, with the modes of Cromo: a name alone is read, `ref` modifies, `move` takes the value (D183). The parameters are written as in a function, and the type returned after them; without it the lambda returns nothing.
- A lambda that captures by address (with `ref`, or reading a value that is not simple) can only be passed as an argument; one that captures only with `move`, or values of simple types, is a value: it can be kept in variables, fields and lists, returned and moved (D184).
- The type of a function is `func(i32, ref T) bool` (D185). A variable or a field of that type is called as a function; the call is indirect (D186).
- Function values are moved and not copied; an empty one, as a variable without a value, is a panic when called (D189).

## 16. Arrays, maps and bytes (D196–D200)

```cromo
i32[4] scores                    // four i32, all 0, where the variable is
scores[0] = 10
for (i32 s in scores) { ... }
i64 n = scores.len()             // 4

map<string, i32> ages
ages["anna"] = 30                // adds or replaces
ages["anna"] += 1
i32 a = ages["anna"]             // a panic if the key is missing
bool known = ages.contains("bruno")
for (string name, i32 age in ages) { ... }

string word = "città"
u8 first = word[0]               // a byte: the length is in bytes (D12)
list<u8> bytes = word.bytes()
string again = string(bytes)     // throws when the bytes are not UTF-8
string part = word.slice(0, 3)   // the bytes from 0 to 3, 3 excluded
string line = format("{} is {}", "anna", a)
```

- An array is a value, as a struct: it is copied when its elements are simple, otherwise moved; `a[i]` is checked (D30). `i32[3][4]` is three arrays of four (D206).
- A `map` is a hash table. Its keys are the numbers, `bool`, `string` and the structs that implement `Key` with `u64 hash() const` and `bool equals(Self other) const` (D198).
- `format` writes into a string as `println` writes on the screen (D200).

## 17. The modules of the library (D201–D203)

```cromo
import std.fs
import std.time

i32 main() {
    time.Instant start = time.now()
    fs.writeText("notes.txt", "hello\n")
    string text = fs.readText("notes.txt")   // an exception if it cannot be read
    time.msleep(10)
    println("{} in {} ms, {}", text, start.elapsed().milliseconds(), time.localNow().text())
    ret 0
}
```

- The core (`list`, `string`, `map`, `format`, the numbers from text) is visible everywhere; `std.fs`, `std.time`, `std.os`, `std.thread`, `std.net` and `std.crypto` are imported (D201). The functions of the first three are in D211.
- Next to `fs.cro` a module can have `fs_windows.cro` and `fs_linux.cro`: only the file of the system the program is compiled for is used, and it shares the names of the module, also the private ones (D202). It holds for the library and for the programs.
- In the modules with `import unsafe`, `s.cstring()` gives the address of a copy of the string ending with a zero, for the C functions, until the end of the statement (D204).

## 18. Threads (D214–D217)

```cromo
import std.thread

i32 main() {
    list<i64> left = ...
    list<i64> right = ...
    thread.Thread t = thread.Thread([ref left]() { sort(ref left) })
    t.start()
    sort(ref right)          // at the same time as t
    t.join()                 // waits; an exception of t is thrown here
    merge(left, right)

    thread.Mutex<i64> total
    thread.Thread a = thread.Thread([total]() { total.lock([](ref i64 n) { n += 1 }) })
    thread.Thread b = thread.Thread([total]() { total.lock([](ref i64 n) { n += 2 }) })
    a.start()
    b.start()
    ret 0                    // a and b are waited for at the end of the block
}
```

- `thread.Thread` is made from a lambda, started with `start()` and waited for with `join()` (D214).
- A thread whose lambda captures by address is a thread of its block: it is waited for at the end of the block, and while it runs the block cannot use what the thread has with `ref`, nor modify what it reads (D215). The compiler checks it, so two threads never write the same data. A thread that captures only with `move` is free: it can be kept in a list and outlive its block.
- The data of a `thread.Mutex<T>` are reached only inside `lock`, which gives them with `ref` to one thread at a time (D216).
- `join()` throws again the exception of the thread; an exception that no `join()` takes stops the program (D217).

## 19. Network (D223–D226)

```cromo
import std.net
import std.thread

i32 main() {
    net.Listener server = net.listen("0.0.0.0", 8080)
    list<thread.Thread> workers
    for {
        net.Connection client = server.accept()   // waits for a client
        thread.Thread t = thread.Thread([move client]() {
            client.timeout(5000)                   // milliseconds, for each operation
            list<u8> request = client.read(4096)   // empty when the client has closed
            client.writeText("HTTP/1.0 200 OK\r\n\r\nhello")
        })
        t.start()
        workers.add(move t)
    }
}
```

- The calls block the thread that makes them, until they are done or their timeout; a server gives each connection to a thread (D223).
- `net.listen` gives a `Listener`, `accept()` and `net.connect(host, port)` give a `Connection`; the destructor closes the socket (D224). UDP has `net.udp(host, port)`.
- Errors and timeouts are exceptions (D225). Names and addresses of IPv4 and IPv6 work on Windows and on Linux, without the C library (D226).

### TLS (D233–D235, D243–D247)

```cromo
net.Connection site = net.connectTls("example.com", 443)
site.writeText("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n")
println("{}", string(site.read(4096)))

net.Listener server = net.listenTls("0.0.0.0", 8443, fs.readText("cert.pem"), fs.readText("key.pem"))
net.Connection client = server.accept()   // encrypted, as the connections of connectTls
```

- `net.connectTls` gives a `Connection` that encrypts, with the same `read`, `write` and timeouts. The certificate of the server must come from an authority of the system, and name the host: otherwise it is an exception, as `cannot connect to example.com:443 with TLS: the certificate has expired` (D234, D235).
- `net.listenTls` takes the certificate, followed by its chain, and the private key, as texts in PEM. A connection that it accepts makes its handshake with its first `read` or `write` (D244).
- `SSL_CERT_FILE` can name a file in PEM with the authorities to trust instead of those of the system (D247).
- TLS 1.2 and 1.3 come from mbedTLS, compiled into the runtime: the programs stay without dependencies (D233).

## 20. Cryptography (D236, D249)

```cromo
import std.crypto

list<u8> data = "hello".bytes()
println("{}", crypto.hex(crypto.sha256(data)))       // 2cf24dba5fb0a30e...
list<u8> key = crypto.random(32)
list<u8> mac = crypto.hmac(crypto.Hash.Sha256, key, data)

list<u8> nonce = crypto.random(12)                    // never twice with the same key
list<u8> sealed = crypto.encrypt(crypto.Cipher.AesGcm, key, nonce, data, list<u8>())
list<u8> plain = crypto.decrypt(crypto.Cipher.AesGcm, key, nonce, sealed, list<u8>())

list<u8> secret = crypto.random(32)
list<u8> signature = crypto.ed25519Sign(secret, data)
bool valid = crypto.ed25519Verify(crypto.ed25519PublicKey(secret), data, signature)
```

- Hashes: `md5`, `sha1`, `sha256`, `sha512`; `hmac` with the enum `Hash`; `Hasher` for data in parts.
- `random(n)`: bytes from the generator of the system. `hex`, `base64` and `base64Url`, with `fromHex`, `fromBase64` and `fromBase64Url`.
- `encrypt` and `decrypt` with AES-GCM or ChaCha20-Poly1305: the result has the tag of 16 bytes at the end, and `decrypt` throws if the data, or the extra data, are not those that were encrypted.
- Ed25519 signs (`ed25519PublicKey`, `ed25519Sign`, `ed25519Verify`) and X25519 exchanges keys (`x25519PublicKey`, `x25519`); the private keys are 32 random bytes.
- A key of the wrong size is an exception.

## 21. Still to be written

- The rest of the standard library: HTTP and JSON (M18).
