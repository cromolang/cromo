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
- **Overflow** (D30): in debug it stops the program, in release the value wraps around from the other end of the range. `wrapAdd(a, b)`, `wrapSub(a, b)` and `wrapMul(a, b)` wrap around always, for the hashes and the checks of the data (D396).
- **Operators** (D59, D193–D195): from the strongest, `* / %`, `+ -`, `<< >>`, `&`, `^`, `|`, the comparisons, `&&`, `||`. The bitwise operators bind more than the comparisons, so `flags & MASK == 0` is `(flags & MASK) == 0`. `!` negates a `bool` and inverts the bits of an integer. A shift beyond the width of the type is a panic in debug, and in release its count is taken modulo the width; `>>` keeps the sign of a signed integer.
- **Numbers from text** (D199): `i64("42")`, `f64("2.5")` read a number, and throw an exception when the text is not a number of the type.
- **The bits of a floating-point number** (D397): `x.bits()` is a `u64` for an `f64` and a `u32` for an `f32`; `f64.fromBits(b)` and `f32.fromBits(b)` make the number again.

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
- The initial value of a field is any expression, `list<string> names = ["a"]`, `i64 size = 4 * 1024`, a call: it is computed each time the default value of the struct is made, in the order of the fields, after the structs inside it; it cannot read the other fields, nor throw an exception (D360, D363, D364). A field moved away has the default of its type (D78, D362).
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

## 10. Errors and exceptions (D16, D36, D338–D348)

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

```
panic: index out of bounds: the index is 5 but the length is 3 at shapes.cro:14:19
    at Point.at (shapes.cro:14)
    at deep (shapes.cro:22)
    at deep (shapes.cro:24)
    ... the same 2 more times
    at main (shapes.cro:29)
```

- A panic, an exception that nobody catches and a stack overflow print the calls that led there, from the innermost, also with `--release`: the function, the file and the line, with the functions that the optimizer put inside others (D339, D342). A recursion is written once, with how many times it repeats; the frames of the runtime and of C are left out. The program does not need its debug information for it: the compiler writes a compact table in the executable.
- The names are those of the source: `main`, `Point.area`, `list<i64>.add`, `Point.Point` for a constructor, `Point.~Point` for a destructor, `main.lambda` for a lambda written in `main`, `fs.copy` for a function of another module (D343). The files are written as in the panics: as given to `cromo`, and `cromo:core.cro` for the library (D345).
- The stack of an exception is the one of its `throw`, which keeps 63 return addresses with the object; `throw` alone, in a `catch`, keeps the stack of the first one (D340, D347).
- A stack overflow is a panic, `panic: stack overflow`, with exit code 101 (D344).
- Without `--release` the executable has the debug information for a debugger: the lines, the functions and the variables with their types, in a PDB next to the `.exe` on Windows and in DWARF on Linux (D338). The object of a method is `this`; a `string` or a `list` shows its fields (D346). With `-O2` some variables can be "optimized out"; `-O0` keeps them.

## 11. Printing and formatting (D21, D38, D318–D323)

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
- A struct or an enum with `string text() const` is printed by `{}` with its text, and the format applies to it as to a string (D318).
- `{:?}` prints the form of debug of any value but a function or an interface, generated by the compiler: `Point { x: 1, y: 2 }`, `Circle(2.5)`, `[1, 2]`, `{"anna": 30}`, a string between quotes, a float with its point. A generic prints its `T` with `{:?}` when `T : Debug` (D319).

```cromo
struct Point {
    i64 x
    i64 y
    public string text() const { ret format("({}, {})", x, y) }
}
println("{} {:>10} {:?}", p, p, p)   // (1, 2)     (1, 2) Point { x: 1, y: 2 }
```

## 12. Interfaces and generics (D39, D40, D92, D324, D325, D357–D361)

There is no inheritance: polymorphism goes through interfaces. The syntax of interfaces (D49) and of generic constraints (D52) is decided. Inside an interface, `Self` is the type that implements it (D84).

A method of a struct can have type parameters of its own, written and deduced as those of a function: `numbers.map<string>(f)`, `counter.pick(a, b)`. A method of an interface cannot, since its table has one entry for each method (D92). `list<T>` has `map<U>(func(T) U f)` and `filter(func(T) bool keep)`.

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

list<Shape> shapes = [Circle(1.0), Square(2.0)]   // objects of different types in the same list
Shape biggest = Circle(3.0)                         // the circle moves to the heap
```

- A read or `ref` parameter of an interface takes a struct where it is, by reference, without allocating anything, and also the value of an interface (D49).
- The value of an interface can be a variable, a field, a value returned, a `move` parameter, an element of a list or a value of a map: a struct becomes one by moving to the heap with its method table, copied when it can be copied, otherwise with `move`, and its value destroys it (D357). It is not copied, only moved (D358), and only the methods of the interface are called on it (D359).
- The default value of an interface is empty: a method called on it is a panic, `the value of Shape is empty: it was moved, or never given` (D361).
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
- `[[cold]]` before a function says that it runs rarely, as `#[cold]` of Rust: the compiler keeps it out of its callers and optimizes them for the other path (D398).
- `[[export("name")]]` before a function gives it that name and the convention of C, so that C calls it: its parameters and its result are numbers, `bool` and `addr`, and it cannot let an exception out (D395). The runtime written in Cromo uses it (D393).
- `addr(n)` makes an address from an `i64` or a `u64`, and `i64(p)` and `u64(p)` give the number of an address; `addr(0)` is the null address, the default value of `addr` (P13).
- A module with `import unsafe` can declare variables outside its functions, for the state of the whole program (D394), as `i64 count`, `u8[4096] buffer` or `bool ready = true`. Only that module sees them; they hold numbers, `bool`, `addr`, and arrays and structs of them whose fields have no initial values, and start from their default value or from a literal, without any code before `main`. `variableAddress(x)` gives the `addr` of one of them, or of a field or an element of one, as for the atomics or for a C function.
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
- A parameter `const func(...)` receives a function that does not modify what it captures, as a lambda of C++ without `mutable`: a lambda without `ref` captures, a named function, or a function value. Several threads can call it at once, as the handler of `http.serve` (D253). A thread cannot call a parameter `func` that is not `const`.

## 16. Arrays, maps, lists, text and bytes (D196–D200, D304–D317)

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

list<string> names = ["anna", "bruno"]      // a literal of a list
list<string> fields = " a, b ,c ".trim().split(",")
i64 at = line.find("is")                    // -1 if it is missing
string shout = line.upper().replace(" ", "_")
string all = join(names, ", ")
```

- An array is a value, as a struct: it is copied when its elements are simple, otherwise moved; `a[i]` is checked (D30). `i32[3][4]` is three arrays of four (D206).
- A `map` is a hash table. Its keys are the numbers, `bool`, `string` and the structs that implement `Key` with `u64 hash() const` and `bool equals(Self other) const` (D198).
- `format` writes into a string as `println` writes on the screen (D200).
- `[a, b, c]` makes a list: the type of its elements is the one expected, or the one of the first element, and an empty `[]` needs the type expected. The elements are given to the list as to `add()`: a variable that is not simple is written `move x`, or copied (D308).
- The functions of text are methods of `string` (D304): `find`, `findFrom`, `findLast`, `contains`, `startsWith`, `endsWith`, `split`, `trim`, `trimStart`, `trimEnd`, `replace`, `upper`, `lower`, `repeat`, and `join(parts, separator)`. The indices are in bytes, and a text not found is -1 (D305); `upper` and `lower` change only the letters of ASCII (D306).

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

- The core (`list`, `string`, `map`, `format`, `join`, the numbers from text) is visible everywhere; `std.fs`, `std.time`, `std.os`, `std.process`, `std.thread`, `std.net`, `std.crypto`, `std.json`, `std.jwt`, `std.http`, `std.gzip`, `std.zip`, `std.tar` and `std.websocket` are imported (D201). The functions of the first three are in D211.
- `std.fs` has also `isFile`, `isLink`, `size`, `modified` (the seconds from 1970), `rename`, `copy`, `createFolders` with the folders above, `removeAll`, which never follows a link, and the parts of a path: `joinPath`, `parent`, `fileName` and `extension`.
- `fs.createLink(target, path)` makes a link to a folder, a symbolic link on Linux and a junction on Windows, which needs no administrator; `fs.readLink(path)` gives where it points, and `fs.makeExecutable(path)` lets a file run on Linux (P18).
- Next to `fs.cro` a module can have `fs_windows.cro` and `fs_linux.cro`: only the file of the system the program is compiled for is used, and it shares the names of the module, also the private ones (D202). It holds for the library and for the programs.
- In the same way a file can be for an architecture, `fs_arm64.cro` or `fs_x86_64.cro`, or for a system and an architecture, `fs_linux_arm64.cro` (D408, P23).
- In the modules with `import unsafe`, `s.cstring()` gives the address of a copy of the string ending with a zero, for the C functions, until the end of the statement (D204).

```cromo
import std.process

process.Output r = process.run("git", ["status", "--short"])
println("{} {}", r.code, r.out)

process.Process p = process.start("sort", [])
p.write("b\na\n")
p.closeInput()
println("{}", p.readAll())
p.wait()
```

- `process.run` waits for the end of the program and gives its exit code, its output and its errors; `process.start` gives a `Process` with `write`, `closeInput`, `read`, `readAll`, `wait` and `kill`, whose errors go where the ones of this program go (D307). A program is found in the folders of `PATH`. An exit code other than 0 is a value; a program that cannot start is an exception.

```cromo
import std.fs
import std.gzip
import std.tar
import std.zip

for (zip.Entry entry in zip.read(fs.readBytes("photos.zip"))) {
    println("{}: {} bytes", entry.name, entry.data.len())
}
tar.extract(gzip.decompress(fs.readBytes("site.tar.gz")), "site")
```

- `zip.read` and `tar.read` give the entries of an archive, each with `name`, `folder`, `link`, `executable` and `data`; `zip.extract` and `tar.extract` write its files and folders in a folder, without the links, and refuse a name that would leave it, as `../x` (D404, P21). A zip can be stored, with deflate or Zip64; a tar ustar, of GNU or of pax.

## 18. Threads, tasks and channels (D214–D217, D269–D284, D326–D337)

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
- A thread that calls a function received as a parameter needs it `const func`, which does not modify what it captures (D253).

```cromo
thread.Mutex<Stats> stats
thread.Group connections
for {
    net.Connection c = listener.accept()
    connections.start([move c, stats]() { serve(ref c, stats) })
}
```

- `thread.Task` has the interface and the rules of `thread.Thread`, but many tasks run on a few threads of the system, one for each processor (D269). A task that waits for the network, for `time.sleep`, for a mutex or for `join()` leaves its thread to the others: the code is the same, without `async` or `await`. `thread.Thread` stays a thread of the system, for C code and for long work that blocks.
- A task has 1 MB of stack reserved, and takes the memory page by page: an idle task costs a few KB (D272).
- A call that blocks the thread of a task, as a file or a C function, keeps that thread; after about a millisecond the runtime gives the other tasks another thread (D271).
- `thread.Group` starts tasks one by one, also in a loop (D273). Its tasks can read the variables declared before the group, which stay constant until the end of its block, and take others with `move`; not with `ref`. At the end of its block the group waits for all its tasks; `g.wait()` waits for them earlier and throws again the first exception of a task.

```cromo
thread.Channel<string> lines = thread.Channel<string>(16)
thread.Atomic<i64> count
thread.Group readers
for (i64 i = 0; i < 4; i++) {
    readers.start([lines, count]() {
        for (string line in lines) {   // until close() and the last value
            count.add(1)
        }
    })
}
lines.send("a")
lines.send("b")
lines.close()
readers.wait()
println("{}", count.load())   // 2
```

- `thread.Channel<T>(n)` passes values from task to task and from thread to thread, in the order they are sent: it holds up to `n` of them, `send()` waits while it is full and `receive()` while it is empty, leaving the thread to the other tasks. With capacity 0, the default value, the sender waits until a receiver has the value (D326, D329). A negative capacity is a panic (D330).
- `close()` ends it: the values already sent can still be received, then `receive()` throws; `send()` to a closed channel throws, also when it was waiting; a second `close()` does nothing (D327, D331, D334). `next(ref v)` is `receive()` without the exception: false once the channel is closed and empty (D332).
- `for (T v in channel)` receives until the end: `v` belongs to the body, which can move it elsewhere; the channel is a variable or a field (D333). The values left in a channel end with it.
- `thread.Atomic<T>` holds an integer or a bool, 0 or `false` by default, or the value of `thread.Atomic<T>(x)`; it cannot be copied (D335). `load`, `store`, `add` and `exchange`, which return the value before, and `compareExchange(expected, desired)`, true if it wrote, are all sequentially consistent (D328, D336). Another `T`, or `add` of a bool, is an error where it is written (D337).

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

- Hashes: `md5`, `sha1`, `sha256`, `sha384`, `sha512`; `hmac` with the enum `Hash`; `Hasher` for data in parts.
- `random(n)`: bytes from the generator of the system. `hex`, `base64` and `base64Url`, with `fromHex`, `fromBase64` and `fromBase64Url`.
- `encrypt` and `decrypt` with AES-GCM or ChaCha20-Poly1305: the result has the tag of 16 bytes at the end, and `decrypt` throws if the data, or the extra data, are not those that were encrypted.
- Ed25519 signs (`ed25519PublicKey`, `ed25519Sign`, `ed25519Verify`) and X25519 exchanges keys (`x25519PublicKey`, `x25519`); the private keys are 32 random bytes.
- A key of the wrong size is an exception.

## 21. JSON (D251, D255–D257, D264–D266, D365, D369–D371)

```cromo
import std.json

struct User {
    [[json("user_id")]] i64 id        // its name in JSON (D256)
    string name
    list<string> roles
    [[json(skip)]] string password    // left out
}

User u = json.decode<User>("{\"user_id\": 7, \"name\": \"Ada\", \"roles\": null}")
string text = json.encode(u)          // {"user_id":7,"name":"Ada","roles":[]}
string indented = json.pretty(u)      // on several lines

json.Value v = json.parse("{\"items\": [1, 2.5]}")
f64 second = v.get("items").at(1).asFloat()
switch (v.get("items").at(0)) {
    case Int(i64 n) { ... }           // integers and floats apart (D255)
    default { ... }
}
```

- `json.encode`, `json.pretty` and `json.decode<T>` work on the numbers, `bool`, `string`, `json.Value`, `list<T>`, `map<string, T>` and the fixed arrays of them, the enums, and the structs whose fields can be converted: the compiler generates the conversion of a struct the first time it is needed, as `copy()`, without a declaration (D251). A field that cannot be converted is a compile error that names it: `Config.log.flush has type func()`. A generic writes `<T : json.Data>`.
- An object has a member for each field, in their order, also the private ones; the keys of a map are written in order. A member that is missing, or `null`, leaves the default value (D257); a member without a field is ignored.
- `[[json("name")]]` before a field gives it another name in JSON, `[[json(skip)]]` leaves it out: the attributes are written with the syntax of C++11 (D256).
- A case of an enum without data is its name, `"Empty"`; one with data is an object whose only member is the case, `{"Circle": {"radius": 2.5}}`, as serde. `[[json("circle")]]` before a case changes its name (D365, D371). A fixed array is an array of JSON with exactly its elements (D370).
- `json.Value` is an enum: `Null`, `Bool`, `Int(i64)`, `UInt(u64)` for the integers above the `i64` (D369), `Float(f64)`, `Text`, `Array`, `Object`, with the members in their order. Its methods read it (`asInt`, `asFloat`, `asText`, `get`, `at`, `has`, `keys`, `len`, `isNull`) and build it (`json.object()`, `json.array()`, `set`, `add`); `json.fromValue<T>` and `json.toValue` convert it.
- The errors are exceptions that say where: `cannot read JSON: line 3, column 7: expected ',' or '}'`, `cannot decode JSON: at users[2].age: expected an integer, found a string`.

### JSON Web Tokens (D399–D402)

```cromo
import std.jwt
import std.time

struct Session {
    string sub
    string role
    i64 exp
}

jwt.Key key = jwt.secret(jwt.Algorithm.Hs256, secret)  // at least 32 bytes
Session s
s.sub = "anna"
s.exp = time.unix() + 3600
string token = jwt.sign(s, key)
Session back = jwt.verify<Session>(token, key)         // the signature, exp and nbf

jwt.Verifier verifier                                  // the tokens of a provider
verifier.keys = jwt.keySet(jwks)                       // its JWK Set, as text
verifier.issuer = "https://auth.example.com"
verifier.audience = "api"
try {
    Session user = verifier.verify<Session>(token)
} catch (jwt.Invalid e) {
    // e.reason: Expired, BadSignature, ...
}
```

- HS256, HS384 and HS512 with `jwt.secret`; RS256, RS384, RS512, ES256, ES384 and EdDSA with `jwt.publicKey` and `jwt.privateKey`, from PEM (D399). The algorithm is the one of the key, never the one of the header: `none`, and a token in HS256 for a key of RSA, are refused (D402).
- The claims are a struct, written and read as with `json.encode` and `json.decode<T>`; the library reads `exp`, `nbf`, `iss` and `aud` by itself. `exp` and `nbf` are checked when they are there, with the tolerance `verifier.leeway` in seconds; `iss` and `aud` when the verifier names them (D400, D402).
- `jwt.keySet` reads a JWK Set, as Keycloak, Auth0 and Google publish them; a token takes the key with its `kid`, and one without a `kid` the first key of its algorithm. Downloading the set is up to the program, with `http.Client` (D401).
- A refused token is a `jwt.Invalid`, whose `reason` is `Malformed`, `WrongAlgorithm`, `UnknownKey`, `BadSignature`, `Expired`, `NotYetValid`, `WrongIssuer` or `WrongAudience` (P15). A secret shorter than its hash, a key of RSA under 2048 bits and a key of another curve than its algorithm are refused when the key is made (P16).
- A key keeps what mbedTLS has read, once: it is moved, not copied (P14). On x64 a verification takes about 4 µs in HS256, 90 µs in RS256, 130 µs in EdDSA and 1 ms in ES256, whose curve mbedTLS computes slowly.

## 22. HTTP, gzip and WebSocket (D252–D254, D258–D263, D270, D366–D368, D372–D374)

```cromo
import std.http
import std.json
import std.thread

thread.Mutex<i64> hits
http.serve("0.0.0.0", 8080, [hits](ref http.Request req, ref http.Response res) {
    hits.lock([](ref i64 n) { n++ })
    if (req.matches("GET /users/{id}")) {
        res.writeJson(json.encode(findUser(i64(req.param("id")))))
    } else if (req.matches("POST /users")) {
        User u = json.decode<User>(req.text())
        res.status = 201
    } else {
        res.status = 404
    }
})

http.Response r = http.get("https://api.example.com/users/7")
User u = json.decode<User>(r.text())
```

- `http.serve(host, port, handler)` gives every request to one handler, a `const func`, each connection on a task of its own (D270): it runs on several threads at once, and what the requests share is in a mutex (D252, D253). `http.listen` and `http.listenTls` give a `Server` with `port()` and `serve(handler)`.
- The handler chooses the route with patterns: `req.matches("GET /users/{id}")`, then `req.param("id")`; `{rest...}` takes the rest of the path, and GET takes also HEAD. `req.queryValue("page")` reads the query. An exception of the handler gives the status 500.
- `http.get(url)`, `http.post(url, body, type)` and `http.send(request)` open a connection for each request, with TLS for `https`, and follow the redirects (D254). A status of error is a response; an exception is a failure of the network.
- `Request` and `Response` have `headers`, compared without case, `body`, `text()`, and `write`, `writeJson` and `writeBytes`, which also set the `Content-Type`.
- `http.Client client` has `get`, `post` and `send`, and keeps up to 8 connections for each host, to use them for the next requests, also from several tasks; a kept connection that the server closed meanwhile is replaced, and the request goes again (D366, D372).
- The client asks for gzip and decompresses the body by itself, when the request does not name an `Accept-Encoding` (D367, D373). `std.gzip` has `compress`, `decompress` and `decompressUpTo(data, limit)`, for data that come from the network, and `inflate(data, limit)` and `crc32(data)` for deflate without gzip, as in a zip (P20).
- A response is at most 16 MB, also once decompressed, unless `client.bodyLimit` says another (P19).

```cromo
http.serve("0.0.0.0", 8080, [](ref http.Request req, ref http.Response res) {
    if (req.matches("GET /chat")) {
        req.webSocket([](ref websocket.Socket ws) {
            for {
                switch (ws.receive()) {           // throws websocket.Closed at the end
                    case Text(string text) { ws.send("echo " + text) }
                    case Binary(list<u8> bytes) { ws.sendBytes(bytes) }
                }
            }
        })
    }
})

websocket.Socket ws = websocket.connect("wss://example.com/chat")
ws.send("hello")
```

- `req.webSocket(work)` answers 101 and gives the WebSocket to `work`, which runs inside the handler, on the task of the connection, and can read what the handler reads; a request that is not one of WebSocket gets 400 (D368, D374). `websocket.connect(url)` opens one, with TLS for `wss`.
- `send(text)`, `sendBytes(bytes)`, and `receive()`, which gives a `websocket.Message`, `Text` or `Binary`, joins the fragments, answers the pings and throws `websocket.Closed`, with `code` and `reason`, once the other side has closed or the connection has ended (1006). `close()` sends the close and waits for the answer. Without `timeout(ms)` a WebSocket waits as long as it takes; a message is at most 16 MB (D374).

## 23. Tests (D349–D356)

```cromo
// shapes.cro
f64 area(f64 side) {
    ret side * side
}

[[test]]
void areaOfASquare() {
    assert(area(2.0) == 4.0)
}
```

```
> cromo test shapes.cro
ok    areaOfASquare
FAIL  areaOfTheUnit
      panic: assertion failed: total == 7 (total is 6) at shapes_test.cro:9:5
          at areaOfTheUnit (shapes_test.cro:9)
1 passed, 1 failed
```

- A test is a function `[[test]] void name()`, without parameters (D349). It can be in the file of the code, or in a file `shapes_test.cro` next to it, which is a part of the module and sees its private names (D350); `cromo build` leaves them out.
- `assert(condition)` is a built-in function, as `panic`: when the condition is false it stops with a panic that writes the condition as written and, for a comparison of numbers or bools, the values of its sides that are not literals, each computed once (D351, D353). It works in any function, also with `--release`.
- `cromo test [file.cro] [names]` builds one executable with the tests of the program and of its modules, and runs each in a process of its own, as many at a time as the processors: a panic, an exception or a stack overflow stops only its test (D352). It writes `ok` or `FAIL` for each test, in the order of the files, with the output of the tests that failed, and the count; its exit code is 1 if one failed (D354). The names after the file choose the tests whose name contains one of them (D355). The tests of a module are named after it: `geometry.areaOfSquare` (D356).

## 24. Packages (D376–D391)

*Decided, not implemented yet: see the milestones in [compiler.md](compiler.md).*

A package is a Git repository, named by its URL, and a version is a tag (D376). The dependencies of a project are lines of `cromo.mod`, next to `main.cro` (D377, D384):

```
// cromo.mod
module github.com/anna/app

require geometry github.com/anna/geometry v1.2.0
require geometry2 github.com/anna/geometry v2.0.1   // v1 and v2 together (D382)
require json5 github.com/bob/json5 v0.3.1
```

```cromo
// main.cro
import geometry                    // the root folder of github.com/anna/geometry (D383)
import json5.parser               // the module parser of github.com/bob/json5

i32 main() {
    Circle c = Circle(2.0)
    println("{}", area(c))
    ret 0
}
```

- **Versions:** of each package the program uses the highest of the minimal versions that the project and its dependencies require, the same every time, without a lock file (D378). `v1` and `v2` are two packages (D382).
- **Integrity:** `cromo.sum` keeps the SHA-256 of each version used, written the first time and checked always; a tag moved on the server is an error (D380). The hash is of the files, not of the archive (D387).
- **Downloads:** `cromo` downloads the archive of the tag over HTTPS from GitHub, GitLab or Codeberg and opens it itself; the other hosts and the private repositories go through `git` (D379, D386). The versions are kept in `~/.cromo/packages` (D388).
- **Commands:** `cromo get url[@version] [name]` adds a dependency or raises its version; `cromo build`, `run` and `test` download what is missing (D381, D385, D389):

```
cromo get github.com/anna/geometry                  // the last version
cromo get github.com/anna/geometry@v1.3.0           // that version
cromo get github.com/anna/geometry@v2 geometry2     // the last v2, with its name (D389)
```

- **Development:** `require geometry ../geometry` uses a local folder in place of a version (D390).

## 25. Still to be written

- The rest of the standard library.
