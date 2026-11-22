# Ids and handles: pointing to another object

A folder knows its parent, an order its customer, a node of a graph its neighbours. In C++ each of them holds a pointer or a reference to the other object. Cromo has no pointers, and a `ref` lives only during a call or a block ([Ownership](ownership.md)): an object cannot hold a reference to another one.

This chapter explains why, and what a Cromo program writes in its place: an **id**, the index of the other object in a list. It is the first thing that a C++ programmer asks about Cromo, and the answer is short. The objects live in a list, and who points to one of them keeps its number.

The examples use what the chapters before this one explained: structs with their methods ([Structs](structs.md)) and lists ([Lists, maps and text](collections.md)). The section on threads uses `std.thread` and anonymous functions, which come later, in [Anonymous functions](lambdas.md) and [Threads, tasks and channels](concurrency.md): it can be read again after them.

## Why a reference cannot be kept

A reference is an address in memory. An address says where an object is now, and nothing keeps the object there. This is C++, and it compiles without a warning:

```cpp
std::vector<Token> tokens = read();
Token* first = &tokens[0];   // the address of the first token, inside the vector
tokens.push_back(more);      // the vector grows, and moves its elements elsewhere
print(first->text);          // first is the address of memory that was freed
```

The last line reads memory that no longer belongs to the vector. The program may print the right text, print something else, or stop, and it may do so only on some days. The same happens when the object is destroyed at the end of its block, or moved into another variable, while somebody still keeps its address.

A language that lets a program keep references has three ways to stop this:

- **Lifetimes**, as Rust. The compiler proves that every reference ends before its object. It works, and it asks for annotations as `'a` in the types and in the signatures, and for a borrow checker that follows every reference through the whole program.
- **A garbage collector**, as Go and Java. An object stays alive as long as somebody points to it. The program pays with a runtime that stops and scans the memory, and no longer decides when an object ends.
- **Unchecked pointers**, as C and C++. The programmer is trusted, and the mistakes are found when the program runs, or not at all.

Cromo takes none of the three: it has no lifetimes to write, no garbage collector, and no memory used after it was freed. What it gives up is the reference that is kept. A `ref` is the parameter of a call, the capture of a lambda that does not outlive its block, the element of a `for` or the data of a `case`: it ends with the call or with the block, and the object it refers to is still there, because the compiler does not let that object be moved, destroyed or changed by someone else meanwhile. A reference in a field, in a variable or in a returned value could outlive its object, so it cannot be written:

```cromo error
struct Folder {
    string name
    ref Folder parent
}

i32 main() {
    ret 0
}
```

```output
error: expected a type, found 'ref'
```

## An id is an index into a list

All the objects of a kind go into a list, and the list has an owner. An object that points to another one keeps its **id**: the index of that object in the list, a plain `i64`. This is a tree of folders, where each folder knows its parent:

```cromo
struct Folder {
    string name
    i64 parent  // the id of the parent: an index into folders, -1 for none
}

struct Tree {
    list<Folder> folders

    // Adds a folder and gives its id.
    i64 add(string name, i64 parent) {
        Folder folder
        folder.name = name
        folder.parent = parent
        folders.add(folder)
        ret folders.len() - 1
    }

    // The path of a folder: from it up to the root, an id at a time.
    string path(i64 id) const {
        string text = ""
        i64 at = id
        for (at >= 0) {
            text = "/" + folders[at].name + text
            at = folders[at].parent
        }
        ret text
    }
}

i32 main() {
    Tree tree
    i64 home = tree.add("home", -1)
    i64 anna = tree.add("anna", home)
    i64 photos = tree.add("photos", anna)
    println("{}", tree.path(photos))

    // The list grows and moves in memory: the ids are still good.
    for (i64 i = 0; i < 1000; i++) {
        tree.add("more", home)
    }
    println("{}", tree.path(photos))

    // Who has the id finds the folder as it is now.
    tree.folders[anna].name = "andrea"
    println("{}", tree.path(photos))
    ret 0
}
```

```output
/home/anna/photos
/home/anna/photos
/home/andrea/photos
```

`i64 parent` stands where C++ would write `Folder* parent`, and `-1` where it would write `nullptr`. The difference is in what the number means. An address is a place in memory; an id is a position in a list, and it is turned into a place only when it is used, by `folders[at]`:

- **The list can move.** After a thousand more folders the list has moved its elements several times. A pointer to `photos` taken before would be the address of freed memory; the id `photos` still finds the same folder.
- **A wrong id is found.** `folders[at]` checks the index: an id that is out of the list stops the program with a panic that says the index and the size, instead of reading the memory of something else.
- **An id is a simple value.** It is copied freely, kept in a field, in another list or in a map, sent to another thread and written in a file. None of the rules of ownership apply to it, because it owns nothing.

To follow an id the program needs the list: `path` is a method of `Tree`, not of `Folder`, because a folder alone cannot reach its parent. This is the pattern of the whole chapter: the object that owns the list has the functions that follow the ids.

## Objects of different kinds

With several kinds of objects, each kind has its list, and an id is an index into the list of its kind. Here an order points to a customer and to a product:

```cromo
struct Customer {
    string name
    string city
}

struct Product {
    string name
    i64 price
}

struct Order {
    i64 customer  // an index into customers
    i64 product   // an index into products
    i64 quantity
}

struct Shop {
    list<Customer> customers
    list<Product> products
    list<Order> orders

    i64 addCustomer(string name, string city) {
        Customer customer
        customer.name = name
        customer.city = city
        customers.add(customer)
        ret customers.len() - 1
    }

    i64 addProduct(string name, i64 price) {
        Product product
        product.name = name
        product.price = price
        products.add(product)
        ret products.len() - 1
    }

    void order(i64 customer, i64 product, i64 quantity) {
        Order made
        made.customer = customer
        made.product = product
        made.quantity = quantity
        orders.add(made)
    }

    // What a customer has spent: its orders, each with the price of its product.
    i64 spent(i64 customer) const {
        i64 total = 0
        for (Order o in orders) {
            if (o.customer == customer) {
                total += o.quantity * products[o.product].price
            }
        }
        ret total
    }

    void print() const {
        for (Order o in orders) {
            println("{} of {}: {} x {}", customers[o.customer].name, customers[o.customer].city, o.quantity, products[o.product].name)
        }
    }
}

i32 main() {
    Shop shop
    i64 anna = shop.addCustomer("Anna", "Torino")
    i64 luca = shop.addCustomer("Luca", "Bari")
    i64 pen = shop.addProduct("pen", 2)
    i64 book = shop.addProduct("book", 15)

    shop.order(anna, book, 2)
    shop.order(luca, pen, 10)
    shop.order(anna, pen, 3)
    shop.print()
    println("Anna has spent {}", shop.spent(anna))

    // One customer and one product, and several orders that point to them.
    shop.customers[anna].city = "Milano"
    shop.products[pen].price = 3
    shop.print()
    println("Anna has spent {}", shop.spent(anna))
    ret 0
}
```

```output
Anna of Torino: 2 x book
Luca of Bari: 10 x pen
Anna of Torino: 3 x pen
Anna has spent 36
Anna of Milano: 2 x book
Luca of Bari: 10 x pen
Anna of Milano: 3 x pen
Anna has spent 39
```

Two orders point to the same Anna and two to the same pen. The city and the price are changed in one place, and every order sees the new value: the objects are shared, as with a pointer, and nothing was copied.

It is the shape of a database. `customer` and `product` in `Order` are foreign keys, the three lists are tables, and `products[o.product].price` is a join. A program written so is easy to save and to load: the ids mean the same in the file as in memory, where addresses would have to be translated.

The elements of a `for` are read in place, without a copy, and `for (ref Order o in orders)` lets the loop modify them: inside a loop, the element has a short name and the index is not written at all.

## An id of each kind has its type

An id is an `i64`, and so is every other id. A call with two ids in the wrong order compiles, and finds the wrong objects when it runs:

```cromo ignore
shop.order(pen, anna, 2)  // the product where the customer goes: it compiles
```

Where the kinds are many, each kind of id gets a struct of its own, with the index as its only field. A struct with one `i64` takes the same room as an `i64`, and the compiler then tells the ids apart:

```cromo error
struct CustomerId {
    i64 index
}

struct ProductId {
    i64 index
}

void order(CustomerId customer, ProductId product, i64 quantity) {
    println("customer {}, product {}, {} of them", customer.index, product.index, quantity)
}

i32 main() {
    CustomerId anna
    anna.index = 0
    ProductId pen
    pen.index = 3
    order(pen, anna, 2)
    ret 0
}
```

```output
error: a value of type ProductId cannot be used as CustomerId
error: a value of type CustomerId cannot be used as ProductId
```

## When an object is removed

An id says a position, and removing an element from a list moves those after it one place back. The ids kept elsewhere then find another object:

```cromo
i32 main() {
    list<string> names = ["Anna", "Luca", "Sara"]
    i64 luca = 1
    println("{}", names[luca])
    names.remove(0)
    println("{}", names[luca])
    ret 0
}
```

```output
Luca
Sara
```

This is not memory that was freed: the program reads a real string of its own list, or stops with a panic if the id is now past the end. It is a mistake in the logic of the program, and the compiler does not find it. There are three ways to avoid it, from the simplest:

- **Never remove.** The list only grows, and an object that is no longer needed is marked, with a `bool removed` among its fields. The ids stay good for ever. It is right for what lives as long as its owner: the nodes of a syntax tree, the rows loaded from a file, the pieces of a document.
- **Reuse the places.** A removed object leaves its place free, and the next one that is added takes it. The list does not grow, but an old id now finds the new object, with no sign that it is another one.
- **A generation in the id.** Each place counts how many times it was freed, and an id keeps the count it saw when it was given. An id whose count is not the one of its place is known to be old.

The third way makes an id of two numbers, and such an id is usually called a **handle**:

```cromo
// A handle: the place of a name, and the generation of the place when it was given.
struct Handle {
    i64 index
    i64 generation
}

struct Place {
    string name
    i64 generation
    bool used
}

struct Names {
    list<Place> places

    // Adds a name in a place that is free, or in a new one.
    Handle add(string name) {
        i64 at = -1
        for (i64 i = 0; i < places.len(); i++) {
            if (!places[i].used) {
                at = i
                break
            }
        }
        if (at < 0) {
            Place place
            places.add(place)
            at = places.len() - 1
        }
        places[at].name = name
        places[at].used = true
        Handle handle
        handle.index = at
        handle.generation = places[at].generation
        ret handle
    }

    // The place is free again, and the handles given for it are no longer good.
    void remove(Handle handle) {
        if (has(handle)) {
            places[handle.index].used = false
            places[handle.index].generation++
        }
    }

    bool has(Handle handle) const {
        ret places[handle.index].used && places[handle.index].generation == handle.generation
    }
}

i32 main() {
    Names names
    Handle anna = names.add("Anna")
    Handle luca = names.add("Luca")
    names.remove(anna)
    Handle sara = names.add("Sara")

    println("the place of Anna now holds {}", names.places[anna.index].name)
    println("anna {}, luca {}, sara {}", names.has(anna), names.has(luca), names.has(sara))
    ret 0
}
```

```output
the place of Anna now holds Sara
anna false, luca true, sara true
```

Sara took the place that Anna left, and the handle of Anna has the same index as that of Sara. Its generation is the old one, so `has` tells them apart: a function that receives a handle checks it, and answers that the object is no longer there instead of working on another one.

The library works so where a program keeps what it does not own. A window and a component of `std.gui` are handles ([Windows and apps](gui.md)): simple values, copied into every lambda that needs them, and a handle of a window that was closed gives a panic, not the memory of another window. A `thread.Shared<T>` is a handle too, as the next section shows.

## An object changed by several threads

Two threads that change the same object need more than an id: each must wait while the other is at work. `thread.Shared<T>` keeps a value together with its lock, and gives a handle to it ([Threads, tasks and channels](concurrency.md)). The handle is copied into each thread, as an id would be, and the object is reached only through `lock`:

```cromo
import std.thread

struct Account {
    string owner
    i64 balance
    i64 operations

    void deposit(i64 amount) {
        balance += amount
        operations++
    }

    void withdraw(i64 amount) {
        balance -= amount
        operations++
    }
}

i32 main() {
    Account opened
    opened.owner = "Anna"
    opened.balance = 1000
    thread.Shared<Account> account = thread.Shared<Account>(move opened)

    thread.Thread salary = thread.Thread([account]() {
        for (i64 i = 0; i < 100000; i++) {
            account.lock([](ref Account a) { a.deposit(3) })
        }
    })
    thread.Thread rent = thread.Thread([account]() {
        for (i64 i = 0; i < 100000; i++) {
            account.lock([](ref Account a) { a.withdraw(2) })
        }
    })
    salary.start()
    rent.start()
    salary.join()
    rent.join()

    account.lock([](ref Account a) { println("{}: balance {}, operations {}", a.owner, a.balance, a.operations) })
    ret 0
}
```

```output
Anna: balance 101000, operations 200000
```

Two things are passed here, in two different ways:

- **`[account]` is a copy.** The lambda of each thread captures the handle by value. The handle is not the account: it only says where the account and its lock are, so the two copies reach the same account, and the account itself is never copied.
- **`ref Account a` is a reference.** `lock` takes the lock, gives the real account to the inner lambda with `ref`, and releases the lock when the lambda ends, also when an exception goes through. This reference is an ordinary `ref` parameter: it lives only during that call, which is to say only while the lock is held.

`move opened` puts the account inside the `Shared`, and `opened` can no longer be used: after that line there is no way to reach the account without its lock, so the lock cannot be forgotten. The reference that lasts is the one inside `Shared`, written once in the library, in the unsafe dialect; the program sees a handle that it can copy and a `ref` that cannot leave the lock.

The value of a `Shared` stays until the end of the program, and its destructor never runs: it is for a state that lives as long as the program, not for objects made and thrown away in a loop.

Without `Shared`, two threads that capture the same variable by address are refused by the compiler:

```cromo error
import std.thread

i32 main() {
    i64 balance = 1000
    thread.Thread a = thread.Thread([ref balance]() { balance += 3 })
    thread.Thread b = thread.Thread([ref balance]() { balance -= 2 })
    a.start()
    b.start()
    a.join()
    b.join()
    println("{}", balance)
    ret 0
}
```

```output
error: the thread 'b' cannot start: 'balance' is used by the thread 'a' until a.join()
```

## What it costs

Ids are not free, and it is right to know their price before choosing them, though in Cromo there is no other choice for an object that is kept.

- **More to write.** `shop.customers[o.customer].name` where C++ writes `o.customer->name`, and the list must be at hand: the functions that follow ids are methods of the owner of the lists, or receive it as a parameter. A `for` over the list, and a `ref` parameter for the object to work on, keep the long names in few places.
- **The check of the index.** Every `list[i]` compares `i` with the size, also in a `--release` build. In a loop over the list the compiler usually removes the check, since it already knows that the index is inside; it stays where the index comes from the data, as in `products[o.product]`. It is a comparison and a jump that the processor almost always predicts: little, except in a short loop that does nothing else.
- **The address is computed.** A pointer is followed; an id is multiplied by the size of an element and added to the start of the list. They are a couple of instructions.
- **An id can grow old**, as the section above shows, where a pointer would be dangling: the mistake is of the same kind, and its effect is not. An old id finds a wrong object or a panic, never the memory of something else.

Against these there is what ids give:

- **The objects are next to each other.** A list keeps its elements in one block of memory, in order, and going over them uses the cache of the processor well. Objects allocated one by one are scattered, and following a pointer to each is often a cache miss, which costs far more than the check of an index.
- **Fewer allocations.** A thousand folders are one list that grows a few times, not a thousand allocations, and they are freed all at once.
- **Smaller ids.** Where the objects are less than two billion, an id can be an `i32`, half of a pointer: the structs are smaller, and more of them fit in the cache.

Compared with a C++ program that allocates each object and links them with pointers, the same program with ids is often faster. Compared with one that keeps its objects in a `std::vector` and points inside it, it is a little slower, by the check of the index, and it cannot break when the vector grows.

## Summary

- An object cannot hold a reference to another one: a reference that is kept could outlive its object, and Cromo has neither lifetimes nor a garbage collector to prevent it.
- The objects of a kind live in a list, and who points to one of them keeps its id, the index in that list: a simple value, copied and stored freely.
- An id stays good when the list grows and moves, and a wrong one is a panic, not memory read by mistake.
- The functions that follow ids belong to the owner of the lists.
- A struct with one field for each kind of id lets the compiler tell them apart.
- Removing from a list changes what the ids after it find: never remove, or use a handle, an index with the generation of its place.
- A window, a component and a `thread.Shared<T>` are handles of the library. A `Shared` is copied into each thread and gives its value with `ref` only inside `lock`.
- The price is longer names and the check of each index; the gain is objects that are next to each other in memory, and fewer allocations.
