# The unsafe dialect

Cromo checks that a program never reads memory that it does not own: there are no pointers, every index is checked, and the compiler follows each value from its creation to its end. Some code, though, must work below those rules: the code that manages memory by hand, as a list does with its elements, and the code that talks to C. For it, Cromo has a dialect, which a module turns on with `import unsafe`.

The standard library is written in this dialect: `list`, `string` and `map` are structs of Cromo that manage their memory with the functions of this chapter, behind a safe interface, and so is most of the runtime, as the heap and the threads. A program rarely needs it, except to call C, as in [Calling C](c-interop.md). This chapter shows what the dialect allows, so that you can read the library and write the few lines that need it.

## import unsafe

`import unsafe` is written with the other imports, at the top of a file, and holds for that file: in a module made of several files, as a folder or the files of a system as `clock_windows.cro`, each file that uses the dialect writes it. It allows:

| What | Shown in |
|---|---|
| `extern` blocks of C functions | [Calling C](c-interop.md) |
| the type `addr`, an address of memory | this chapter |
| the built-in functions of raw memory, as `alloc` and `free` | this chapter |
| `s.cstring()` and `string(data, length)`, the strings of C | [Calling C](c-interop.md) |
| module variables, outside the functions | this chapter |
| the attributes `[[export("name")]]` and `[[cold]]` | this chapter |

Everything else keeps its checks: ownership, the indices of lists, the overflow of integers. What the dialect adds is what the compiler cannot check, and that is the responsibility of the author of the module.

Without the import, these things do not exist:

```cromo error
i32 main() {
    addr place
    i64 size = sizeOf<i64>()
    ret 0
}
```

```output
error: 'addr' belongs to the unsafe dialect: the module must write 'import unsafe'
error: 'sizeOf' belongs to the unsafe dialect: the module must write 'import unsafe'
```

The other modules use only what the module makes public, and the compiler checks that this interface is safe: a public function, field or method that uses `addr` is the error `function 'block' is public, but its type addr is only for the modules with 'import unsafe': offer a safe type instead`. Keeping `addr` and the module's raw memory out of its public interface is the rule that makes the dialect stay inside its module, and a search for `import unsafe` then finds every place where the safety of the program depends on the care of its author.

## addr

`addr` is an address of memory, without a type: it says where, not what. Its default value is the null address, and two addresses are compared with `==` and `!=`:

```cromo
import unsafe

i32 main() {
    addr none
    addr block = alloc(64)
    println("{} {}", none == addr(0), block != none)
    i64 number = i64(block)
    println("aligned to 16: {}", number % 16 == 0)
    println("{:#x}", u64(addr(4096)))
    free(block)
    ret 0
}
```

```output
true true
aligned to 16: true
0x1000
```

- `addr(n)` makes an address from an `i64` or a `u64`, and `addr(0)` is the null address.
- `i64(p)` and `u64(p)` give the number of an address, for alignments and for the values that the system gives as numbers.
- No other type converts to or from `addr`; there is no arithmetic on it, except `offset`, shown below; it is printed only as a number, `i64(p)`.

```cromo error
import unsafe

i32 main() {
    addr a = alloc(16)
    addr b = a + 8
    u32 low = u32(a)
    println("{}", a < b)
    ret 0
}
```

```output
error: a number cannot be combined with a value of type addr
error: cannot convert addr to u32: an addr converts to i64 or u64, and back
error: '<' cannot compare addr values
```

An `addr` can hold any number: the compiler never checks that it points to memory of the program. Reading or writing through a wrong address is undefined, as in C.

## Memory: alloc, resize and free

The heap is used with three built-in functions:

| Function | What it does |
|---|---|
| `addr alloc(i64 bytes)` | allocates `bytes` bytes, aligned to 16; their content is not set |
| `addr resize(addr p, i64 bytes)` | gives a block of `bytes` bytes with the content of `p`, up to the smaller size; `p` is no longer valid |
| `void free(addr p)` | gives the block back; `free` of the null address does nothing |

`resize` of the null address is an `alloc`, so a block can start null and grow. When the memory runs out, or the size is negative, the program stops with `panic: out of memory` and the exit code 101: an allocation never returns null.

Two more functions measure and move:

| Function | What it does |
|---|---|
| `i64 sizeOf<T>()` | the bytes that a value of type `T` takes, as a field or an element |
| `addr offset(addr p, i64 bytes)` | the address `bytes` bytes after `p`, or before it when negative |

```cromo
import unsafe

struct Point {
    i32 x
    i32 y
}

i32 main() {
    println("{} {} {} {}", sizeOf<i64>(), sizeOf<Point>(), sizeOf<bool>(), sizeOf<string>())
    addr block = alloc(4 * sizeOf<i64>())
    for (i64 i = 0; i < 4; i++) {
        put<i64>(offset(block, i * 8), (i + 1) * 10)
    }
    block = resize(block, 8 * sizeOf<i64>())
    println("{}", copyAt<i64>(offset(block, 24)))
    free(block)
    ret 0
}
```

```output
8 8 1 24
40
```

A `string` takes 24 bytes because it is a struct of the library: the address of its bytes, its length and its capacity. The bytes are on the heap, which the string frees in its destructor.

The compiler does not follow the blocks: freeing a block twice, using it after `free`, or forgetting to free it are not found. The usual answer is the one of C++: the block belongs to a struct, whose destructor frees it.

## Values in memory: put, take, copyAt and destroy

Four functions read and write a value of a type `T` at an address. They know the type, so they do what Cromo does with that value: they move it, copy it with `copy()`, or destroy it.

| Function | What it does |
|---|---|
| `void put<T>(addr p, move T value)` | moves the value into the memory at `p`, without destroying what was there |
| `T take<T>(addr p)` | moves the value out of the memory at `p`, which then holds nothing |
| `T copyAt<T>(addr p)` | gives a copy of the value at `p`, which stays there |
| `void destroy<T>(addr p)` | destroys the value at `p`, which then holds nothing; the memory stays allocated |

`put` and `take` treat the memory as empty: `put` does not destroy an old value, and after `take` the value belongs to the variable that received it, so the memory must not be destroyed again. A value given to `put` without `move` is copied, as for any `move` parameter, and a value that cannot be copied is then an error, as it is for `copyAt`. This struct says when it is copied and destroyed:

```cromo
import unsafe

struct Noisy {
    string name

    ~Noisy() {
        println("destroy {}", name)
    }

    Noisy copy() const {
        println("copy {}", name)
        Noisy other
        other.name = name + "'"
        ret other
    }
}

i32 main() {
    addr slot = alloc(sizeOf<Noisy>())
    Noisy a
    a.name = "a"
    put<Noisy>(slot, move a)
    Noisy b = copyAt<Noisy>(slot)
    Noisy c = take<Noisy>(slot)
    println("{} {}", b.name, c.name)
    put<Noisy>(slot, move c)
    destroy<Noisy>(slot)
    free(slot)
    println("end of main")
    ret 0
}
```

```output
copy a
a' a
destroy a
end of main
destroy a'
```

`copyAt` called `copy()`, `take` moved the value out without a copy, and `destroy` ran the destructor of the value put back. For the numbers and the other simple types, these functions only read and write bytes, and `destroy` does nothing.

The type is not checked against the memory: `take<string>` of a block that holds an `i64` reads garbage, as a cast of C would.

## Bytes: copyMemory and compareMemory

| Function | What it does |
|---|---|
| `void copyMemory(addr dest, addr source, i64 bytes)` | copies the bytes; the two areas can overlap |
| `i32 compareMemory(addr a, addr b, i64 bytes)` | compares the bytes: negative, zero or positive, as `memcmp` of C |

```cromo
import unsafe

i32 main() {
    string first = "hello"
    string second = "help!"
    addr a = alloc(5)
    addr b = alloc(5)
    copyMemory(a, first.cstring(), 5)
    copyMemory(b, second.cstring(), 5)
    println("{} {}", compareMemory(a, b, 3) == 0, compareMemory(a, b, 4) < 0)
    copyMemory(offset(a, 1), a, 4)
    println("{}", string(a, 5))
    free(a)
    free(b)
    ret 0
}
```

```output
true true
hhell
```

They move bytes, not values: a value that owns resources, as a string, is moved with them only when the old place is then treated as empty, as `list` does when it inserts an element in the middle.

## checkIndex

`checkIndex(index, length)` stops the program with the panic of an index out of a list when `index` is not between 0 and `length - 1`. It is the check that `v[i]` makes, for the structs that manage their elements by hand, as the stack below.

## How a list is written

Together, these functions are enough to write a container. This stack keeps its elements one after the other on the heap, as `list` does:

```cromo file=stack.cro
import unsafe

// A stack of values on the heap, written as list is.
struct Stack<T> {
    addr data
    i64 count
    i64 capacity

    ~Stack() {
        for (i64 i = 0; i < count; i++) {
            destroy<T>(at(i))
        }
        free(data)
    }

    // The place of the element i.
    addr at(i64 i) const {
        ret offset(data, i * sizeOf<T>())
    }

    void push(move T value) {
        if (count == capacity) {
            capacity = capacity == 0 ? 4 : capacity * 2
            data = resize(data, capacity * sizeOf<T>())
        }
        put<T>(at(count), move value)
        count++
    }

    T pop() {
        checkIndex(count - 1, count)
        count--
        ret take<T>(at(count))
    }

    T top() const {
        checkIndex(count - 1, count)
        ret copyAt<T>(at(count - 1))
    }

    i64 len() const {
        ret count
    }
}

i32 main() {
    Stack<string> words
    words.push("one")
    words.push("two")
    words.push("three")
    println("{} on top, {} in all", words.top(), words.len())
    string last = words.pop()
    println("popped {}, {} left", last, words.len())
    Stack<i64> numbers
    numbers.pop()
    ret 0
}
```

```output
three on top, 3 in all
popped three, 2 left
panic: index out of bounds: the index is -1 but the length is 0 at stack.cro:31:9
    at Stack<i64>.pop (stack.cro:31)
    at main (stack.cro:55)
```

- The default value of the struct is an empty stack with a null `data`, which `resize` turns into a block at the first `push`.
- The destructor destroys the elements still there, then frees the block.
- `Stack` has a destructor and no `copy()`, so it cannot be copied, only moved: two stacks never free the same block. A container that can be copied writes `copy()`, which makes a new block and copies each element with `copyAt`, as `list` does.

The struct is safe to use from other modules: its fields are private, and its public methods, when it has them, check the indices. This is the shape of all the code in the dialect: a few lines that the compiler cannot check, inside a struct that the rest of the program uses with all the checks.

## Module variables

A file with `import unsafe` can declare variables outside the functions: the state of the whole program, as the heap of the runtime and the queue of its tasks. The normal code has none, so it has neither global state nor data races between threads.

They are written as local variables, at the level of the declarations, and every function of the module reads and changes them:

```cromo
import unsafe

i64 calls
i64[4] slots
bool ready = true
i64 start = -5

void count() {
    calls++
}

i32 main() {
    count()
    count()
    println("{} {} {}", calls, ready, start)
    slots[2] = 42
    addr place = variableAddress(slots[2])
    println("{} {}", copyAt<i64>(place), place == offset(variableAddress(slots), 16))
    ret 0
}
```

```output
2 true -5
42 true
```

The rules:

- A module variable is private to its module, which sees it in all its files, also those without `import unsafe`. `public` and the attributes are errors.
- It holds numbers, `bool`, `char`, `addr`, and fixed-size arrays and structs of them, whose fields have no initial values.
- Its value at the start is its default value, or a literal: a number, with `-` in front when negative, a character, `true` or `false`. The value is written in the executable: no code runs before `main` and none after it, so a module variable has no destructor, and a type that needs code to be made or destroyed, as `string` or `list`, is an error.
- A local variable or a parameter cannot have its name: `a variable named 'count' is already visible here`, as for the other variables.
- Nothing protects a module variable from threads: two threads that write it at the same time are a data race, which the compiler does not see. A program that shares data between threads uses the types of `std.thread`, as `thread.Mutex<T>` and `thread.Atomic<T>`, in normal code: see [Threads, tasks and channels](concurrency.md).

```cromo error
import unsafe

string name
i64 limit = 2 * 512

i32 main() {
    ret 0
}
```

```output
error: a module variable holds numbers, bool, addr, or arrays and structs of them whose fields have no initial values, not string
error: the initial value of a module variable is a number, a character, true or false
```

```cromo error
import unsafe

public i64 total

i32 main() {
    ret 0
}
```

```output
error: a module variable has no attributes and is not public: only its module sees it
```

Without `import unsafe`, a variable outside the functions is an error. The constants of a module, `const i64 limit = 1024`, hold no state and are in every module: see [Common concepts](common-concepts.md).

**variableAddress.** `variableAddress(x)` gives the `addr` of a module variable, or of a field or an element of one, for the atomics and for the C functions that keep an address. It stays the same for the whole program. A local variable has none, because its place ends with its block:

```cromo error
import unsafe

i32 main() {
    i64 local = 5
    addr place = variableAddress(local)
    ret 0
}
```

```output
error: variableAddress() takes a module variable, or a field or an element of one
```

## [[export("name")]]

A function of a module with `import unsafe` can be exported with a fixed name and the calling convention of C: the C code linked with the program, as a static library of an `extern` block, then calls it by that name. The runtime of Cromo uses it for the functions that the generated code and the C of the runtime call, as `cromo_alloc`.

```cromo
import unsafe

[[export("example_twice")]]
i64 twice(i64 x) {
    ret x * 2
}

[[export("example_positive")]]
bool positive(i32 x) {
    ret x > 0
}

i32 main() {
    println("{} {}", twice(21), positive(-3))
    ret 0
}
```

```output
42 false
```

C declares it as `long long example_twice(long long x);`, while Cromo still calls it by its own name, `twice`. An example with a C library is in [Calling C](c-interop.md). The rules:

- The name is made of letters, digits and `_`, and does not start with a digit. It is a symbol of the whole program, as a function of C, so two exported functions cannot have the same name, and the names of the runtime, which start with `cromo_`, as `cromo_alloc`, make the linker report a duplicate symbol.
- The parameters are numbers, `bool` or `addr`, passed by value; the result is one of them, or `void`.
- It cannot be generic, and it cannot let an exception out, because C has no exceptions: it catches them inside.
- It has no other attributes, but `[[cold]]`.

```cromo error
import unsafe

[[export("count_words")]]
i64 countWords(string text) {
    ret text.len()
}

[[export("check_value")]]
i64 check(i64 x) {
    if (x < 0) {
        throw Error("negative")
    }
    ret x
}

[[export("9lives")]]
void lives() {
}

i32 main() {
    ret 0
}
```

```output
error: the parameters of an exported function are numbers, bool or addr, passed by value
error: the exported function 'check' can throw, and C has no exceptions: catch them inside it
error: [[export]] takes the name of the function in C, as [[export("cromo_alloc")]]
```

## [[cold]]

`[[cold]]` before a function says that it runs rarely, as `#[cold]` of Rust and `__attribute__((cold))` of GCC. The compiler keeps it out of the functions that call it, and lays them out for the other path, which makes the common path shorter. The runtime uses it for the slow paths, as the one where the heap asks the system for more memory.

```cromo
import unsafe

i64 slowCalls

[[cold]]
i64 slow(i64 x) {
    slowCalls++
    ret x * 2
}

i64 fast(i64 x) {
    if (x % 50 == 49) {
        ret slow(x) - x
    }
    ret x
}

i32 main() {
    i64 sum = 0
    for (i64 i = 0; i < 100; i++) {
        sum += fast(i)
    }
    println("{} after {} slow calls", sum, slowCalls)
    ret 0
}
```

```output
4950 after 2 slow calls
```

The function works as any other: `[[cold]]` changes only how fast the code around it is. It takes no arguments, is written once, can go with `[[export]]`, and is only for the modules with `import unsafe`:

```cromo error
[[cold]]
void rarely() {
}

i32 main() {
    rarely()
    ret 0
}
```

```output
error: [[cold]] is only for the modules with 'import unsafe'
```

## The names of the dialect

The standard library uses a few more built-in functions of the dialect, for its own work: the threads and the tasks, the atomics, the arguments of the program, the environment and the system calls of Linux. They serve only its modules and can change, so they are not described here.

In a file with `import unsafe` all these names are built-in functions:

- `alloc`, `resize`, `free`, `sizeOf`, `offset`, `take`, `put`, `destroy`, `copyAt`, `copyMemory`, `compareMemory`, `checkIndex` and `variableAddress`, of this chapter;
- `argumentCount`, `argumentLength`, `argumentData`, `environment`, `exitProcess`, `systemCall`, `decimalToFloat`, `hashBytes`, `equalAt`, `jsonWrite`, `jsonRead`, `debugWrite`, `selfAddress`, `threadStart`, `threadWait`, `threadRethrow`, `threadUncaught`, `threadFree`, `taskStart`, `groupStart`, `mutexLock`, `mutexUnlock`, `atomicLoad`, `atomicStore`, `atomicAdd`, `atomicExchange`, `atomicCompareExchange`, `atomicLoadRelaxed`, `atomicStoreRelaxed` and `atomicAddRelaxed`, of the library.

A function of the module, or a C function, with one of these names would never be called there, so it is an error where it is declared: give your functions other names. In the files without `import unsafe` these names are free.

## What the compiler no longer checks

The dialect, with the C functions that it calls, is the only place where a Cromo program can have undefined behavior. These mistakes compile, and the program may crash or, worse, go on with wrong data:

- reading or writing through an address that is null, freed, or outside its block;
- freeing a block twice, or never;
- `take` of a place that holds nothing, `put` over a value that is then never destroyed, a type `T` that is not the one in the memory;
- the `addr` of `cstring()` kept after its statement;
- a module variable written by two threads at the same time.

So the code in the dialect stays short, and is wrapped as soon as possible in a struct with a destructor and a safe interface. The rest of the program then keeps all the guarantees of Cromo.

## Summary

- `import unsafe` turns on the dialect in the file that writes it: `extern` blocks, `addr`, raw memory, module variables, `[[export]]` and `[[cold]]`. The other modules use its public interface.
- `addr` is an address without a type: null by default, compared with `==` and `!=`, converted only to and from `i64` and `u64`.
- `alloc`, `resize` and `free` manage the heap; `sizeOf<T>()` and `offset` measure and move; `put<T>`, `take<T>`, `copyAt<T>` and `destroy<T>` move, copy and destroy values in memory; `copyMemory` and `compareMemory` work on bytes; `checkIndex` checks an index.
- Module variables hold numbers, `bool`, `addr`, and arrays and structs of them, from a literal or the default value, without code before `main`; `variableAddress(x)` gives their address.
- `[[export("name")]]` gives a function the name and the convention of C; `[[cold]]` marks a function that runs rarely.
- The standard library is written in this dialect; a program uses it to call C, and keeps it small and wrapped.
