

# Custom Standard Template Library (STL) Vector Implementation | C++, Data Structures, Systems Programming

• Engineered a custom generic container matching std::vector behavior, using raw memory allocation (::operator new) and placement new to completely isolate object lifecycles from buffer allocation.

• Optimized allocation performance by implementing a dual-path pipeline using compile-time branching (if constexpr) and type traits (std::is_move_constructible_v), enabling safe, zero-copy resource moves during reallocations.

• Eliminated memory overhead and prevented resource leaks by applying the Rule of 5, choreographing low-level destructors (.~T()) and memory deallocators (::operator delete).

• Enforced strict type safety and blocked hazardous silent type casting by implementing explicit single-argument constructors and rvalue reference overloads (T&&) for perfect forwarding pathways.



# 1. whats the difference between `::operator delete(ptr); &ptr[i].~T();` ? why do we need both?

The core difference is that `ptr[i].~T()` manages the object, while ::operator delete(ptr) manages the raw memory those objects sit in.
In C++, creating an object involves two distinct phases: allocating raw bytes and initializing an object inside those bytes. Because our Vector controls this process manually using placement new, it must also separate the teardown process into two explicit stages.
Here is the exact breakdown of what each line does and why both are strictly required to avoid memory corruption:

1. `ptr[i].~T();` (The Destructor)

• What it does: It calls the explicit destructor of the object of type `T` located at index `i`.
• What it cleans up: It cleans up resources owned by the object itself, not the vector's array. For example, if `T` is a `std::string`, calling its destructor frees the internal heap memory that the string allocated for its text characters.
• Memory impact: It does not free the vector's underlying heap array pointer (ptr). The slot in the array remains allocated, but it is now considered "uninitialized raw memory."

2. `::operator delete(ptr);` (The Memory Deallocator)

• What it does: It releases the entire contiguous block of raw memory bytes pointed to by ptr back to the operating system or heap manager.
• What it cleans up: It frees the block of memory allocated via `::operator` new during a reserve() or assignment event.
• Memory impact: It does not know or care what types of objects were stored in that memory. It treats the buffer purely as a sequence of raw bytes. It will not call the destructors of your objects.

## What happens if you skip one?


|If you omit...|	The Consequence|	Why it happens|
| ------------ | ----------------- | ---------------- |
|`ptr[i].~T();`|	Resource/Memory Leak|	If `T` is a std::string or std::vector, its internal dynamically-allocated contents are orphaned in memory forever, even though the outer array space is freed.|
|`::operator delete(ptr);`|	Array Memory Leak|	The individual objects are destroyed properly, but the massive chunk of contiguous heap memory allocated for the vector's capacity stays hoarded by your application.|


## Visualizing the Lifecycle in a Vector

Imagine you have a `Vector<std::string>` with a capacity of 4, but only 2 elements inside:
```
ptr ──► [ "Hello" ] [ "World" ] [ Uninitialized ] [ Uninitialized ]
```
1. When you call `pop_back()`:
The vector only wants to remove "World". It calls `ptr[1].~T()`. The second slot becomes uninitialized raw memory again, but your capacity stays at 4. You cannot free the memory here because you need to keep the remaining slots intact.
2. When the vector is completely destroyed (`~Vector()`):
First, a loop destroys every active item up to size using `.~T()`. Finally, `::operator delete(ptr)` is called once to release the entire block of 4 slots back to the system.

To understand this, we need to look at how types like std::string or std::vector handle their own memory under the hood.
A std::string object doesn't actually store its text characters directly inside its own structure (unless the text is very short). Instead, it acts like a "manager." It holds a pointer to a completely separate block of heap memory where the actual text lives.

The Anatomy of a std::string

Think of a std::string object as a small box containing:
1. A pointer `(char*)` to a separate location in memory where the characters are stored.
2. A variable for the string's size.
3. A variable for the string's capacity.
Because of this, the string object itself always occupies a small, fixed size in memory (usually 24 or 32 bytes, regardless of whether it holds 5 characters or 5 million characters).


## Visualizing the Problem

Let's look at what happens in our custom `Vector<std::string>` when we store the word "HelloWorld!".

1. The Normal State

The Vector allocates enough bytes to hold the small 32-byte string manager box. That box, in turn, allocates its own separate memory for the actual text:
```text
Your Vector's Raw Memory Buffer (ptr)
┌────────────────────────────────────────┐
│  [ std::string box ]                   │
│   └──► Pointer member inside box  ───────────┐
└────────────────────────────────────────┘     │
                                               ▼
                                  Separate Dynamic Memory
                                  ┌───────────────────────┐
                                  │ "H" "e" "l" "l" "o"   │
                                  └───────────────────────┘
```

2. What happens if you ONLY call `::operator delete(ptr)`?

If you skip the destructor `(~T())` and immediately tell the system to free the vector's raw buffer (ptr), this happens:

```text
The system deletes your Vector's buffer. 
The [ std::string box ] is instantly vaporized.

                                               (Broken Link)
                                               
                                               ▼
                                  Separate Dynamic Memory
                                  ┌───────────────────────┐
                                  │ "H" "e" "l" "l" "o"   │  <── ORPHANED!
                                  └───────────────────────┘
```

The system successfully freed the 32 bytes belonging to the vector's array. However, because the string box was vaporized without its destructor being called, nobody ever told the system to free the separate text memory ("Hello").
That text memory is now orphaned. It is trapped in the heap, taking up space, and your program has lost the pointer to it, creating a classic memory leak.

## Why the Destructor Fixes This

When we call `ptr[i].~T()`, we are telling the string box: "Hey, you are about to be destroyed. Clean up after yourself."
The string's destructor runs first, freeing the separate "Hello" memory. Only after that is done do we safely use ::operator delete`(ptr)` to free the vector's buffer itself.

# 2. Explanation for `std::is_move_constructible_`

```cpp            
            if constexpr (std::is_move_constructible_v<T>) {
                ::new (&new_data[i]) T(std::move(ptr[i]));
            } else {
                ::new (&new_data[i]) T(ptr[i]);
            }
```
This specific piece of code handles the process of moving or copying existing elements into a brand new memory buffer whenever your vector needs to grow (reallocate).
To understand why it is written this way, we have to look at two advanced C++ features working together: Move Semantics and Compile-time if (if constexpr).

1. The Core Logic: Move vs. Copy

When a vector runs out of capacity and needs to reallocate a larger block of memory, it has to shift all its current elements from the old memory buffer (ptr) to the new memory buffer (new_data).
There are two ways to shift an element:
• The Copy Way (else branch): This duplicates the data. If the element is a heavy object (like a large std::string or another vector), a brand new heap allocation happens to duplicate its inner contents. The old element remains fully intact.
• The Move Way (if branch): This steals the resources. Instead of allocating new internal memory for a string's text, the new slot simply takes ownership of the old string's memory pointer. It is incredibly fast because no deep copying happens.
Naturally, we always prefer moving elements during reallocation because it is massively faster.

2. The Problem: Not every type can be moved

What happens if someone creates a `Vector<User>` using a custom User class that explicitly disables moving (for example, a class holding a fixed hardware resource lock)?
If we blindly wrote: 
```cpp
::new (&new_data[i]) T(std::move(ptr[i])); // Force a move 
``` 
The code would fail to compile the moment someone tried to use our vector with a non-moveable type.


3. The Solution: `std::is_move_constructible_v<T>`

This is a standard library type trait. It acts like a question to the compiler: "Can this type T be safely constructed via move semantics?"
• If `T` can be moved, it evaluates to true.
• If `T` cannot be moved, it evaluates to false.

4. The Magic: Why if constexpr instead of a regular if?

A regular if statement is evaluated at runtime (while the program is running). A regular if requires both branches to be valid, compilable code. If the if branch contains a std::move on a non-moveable object, the compiler will throw an error and refuse to build your program, even if the execution path would never hit it.
if constexpr is evaluated at compile-time.
When the compiler generates the code for your specific vector type, it checks the condition and completely discards the branch that evaluates to false. It acts like an automated code-cutter:
   
 Scenario A: You make a `Vector<std::string>` (Moveable)
The compiler checks `std::is_move_constructible_v<std::string>`, which is true. It compiles only the move code and completely erases the else block from the final application. 
```cpp
// What the compiler actually builds for std::string:
::new (&new_data[i]) T(std::move(ptr[i]));
```

 Scenario B: You make a `Vector<NonMoveableType>` 
    
The compiler checks the trait, sees it is false, and completely erases the if block, compiling only the safe copy fallback code. Your project builds perfectly without errors!
```cpp
// What the compiler actually builds for NonMoveableType:
::new (&new_data[i]) T(ptr[i]);
```

# 3. which objects should be NOT move constructible?

Classes and objects should be made not move-constructible if moving them breaks their internal logic, causes safety issues, or violates the nature of the resource they manage.
The most common types of classes that you should explicitly restrict from being moved include:

1. Identity-Fixed Classes (Polymorphic Bases)

If a class is meant to be a base class in an inheritance hierarchy, allowing it to be moved or copied can lead to a dangerous bug called object slicing. If you try to move a derived child class object into a base class slot, the child's extended properties are sliced off and discarded.
• The Fix: Delete both copy and move operations on your base interfaces.
```cpp
class Shape {
public:
    virtual ~Shape() = default;
    Shape(Shape&&) = delete; // Blocks moving base slots
};
```

2. Classes Managing Address-Dependent Internals

If an object contains a pointer or reference that points to itself or to an internal sub-member within its own memory layout, moving that object to a new location in the heap will completely corrupt it.
• Why: The old object is shifted to a new memory address, but the internal pointer is still staring at the old, now-destroyed memory location.
• Examples: Network loopback handles or deeply nested data structures with cross-linked parent/child references.

3. Strict Single-Instance Singletons

A Singleton pattern guarantees that exactly one instance of a class exists throughout the entire application lifecycle.
• Why: While moving doesn't create a new active instance (it voids the original), allowing objects to be moved around your system breaks the concept of a single, reliable global access point.
```cpp
class Singleton {
public:
    Singleton(Singleton&&) = delete; // Enforces a static, non-transferable lifecycle
};
```

4. Hardware and System Resource Wrappers

Some objects map directly to a physical hardware state or a strictly singular OS utility that cannot be duplicated or generalized into an abstract "transferable resource."
• Examples: `std::mutex` (thread synchronization locks), active hardware asset handlers (like audio channel tracks or hardware input buses), and active UI windows bound to OS pipelines. Moving a threading lock while multiple threads are waiting on its exact location would trigger an immediate crash.

Summary Checklist for Vector Safety

When designing your own custom types, use this simple rule of thumb:

|Resource| Ownership|	Move Strategy|	Example|
|--------|----------|----------------|---------|
|Dynamic| Data Manager|	Make Moveable|	std::string, Vector|
|Unique| System Handle|	Move-Only (No Copy)|	std::unique_ptr, std::file|
|Fixed| Memory / Hardware Asset|	Delete Move & Copy|	std::mutex, Thread Workers|


# 5. why are we using ::operator new 2 times in lines 
```cpp
::new (&new_data[i]) T(std::move(ptr[i])); 
 pointer new_data = static_cast<pointer>(::operator new(new_cap * sizeof(T)));
```

It looks like a slight confusion in the syntax, but you are actually seeing two entirely different operators that happen to share the word new!
They serve completely different purposes: one allocates memory bytes, and the other initializes an object. Here is the breakdown:

1. `::operator new(bytes)` — The Memory Allocator

```cpp
pointer new_data = static_cast<pointer>(::operator new(new_cap * sizeof(T)));
```
• What it is: This is a raw memory allocation function (very similar to malloc in C).
• What it does: It goes to the operating system and requests a giant, continuous block of raw, uninitialized memory bytes `(new_cap * sizeof(T))`.
• The key detail: It does not construct any objects. It just gives you the empty space. If T is a std::string, no string objects exist yet; it is just a blank template of bytes.

2. `::new (&new_data[i]) T(...)` — The "Placement New" Operator

``` cpp
::new (&new_data[i]) T(std::move(ptr[i]));
```
• What it is: This is called Placement New. The syntax is new (address) Type(arguments).
• What it does: It tells the compiler: "Do not allocate any new memory. Instead, look at the memory address I provided `(&new_data[i])`, and construct an object of type T exactly inside that specific spot."
• The key detail: This does the actual initialization work. It calls the move constructor (or copy constructor) of your type T to bring the object to life inside the raw buffer space we allocated in step 1.

Summary of the Lifecycle

We use both because a custom Vector must separate the creation process into two distinct steps:
1. `::operator` new builds the empty parking lot (allocates raw bytes).
2. Placement new parks a specific car into slot i of that parking lot (calls the constructor to initialize an object).
If we just used standard `new T[capacity]`, C++ would immediately force both steps to happen at once for every single slot, which would break our vector's efficiency and prevent us from using types without default constructors.


# 6. pop_back()

We purposely do not delete the raw memory of that last item because of how a vector is designed to optimize performance.
If we deleted the memory slot every time you called `pop_back()`, the vector would lose its efficiency advantages. Here is exactly why we keep that raw memory slot alive:

1. To Keep `pop_back()` Fast (O(1) Time)

Releasing memory back to the operating system or heap manager is a very slow operation. If `pop_back()` deallocated memory every time it was called, it would turn a lightning-fast operation into a sluggish one.
By only running the destructor `(.~T())`, we quickly shut down the object, which takes almost no time.

2. To Keep the Vector's "Capacity" Intact

There is a fundamental rule in C++ vectors: `pop_back()` reduces the `size()`, but it NEVER reduces the `capacity()`.
The memory slot is kept alive so that if you decide to call push_back() immediately afterward, the vector doesn't have to go back to the operating system to ask for new memory. The empty slot is already sitting there, warm and ready to be used.

Visualizing a `pop_back()` and `push_back()` Cycle

Imagine you have a vector with 3 elements, and its capacity is 3:
```text
ptr ──► [ "A" ] [ "B" ] [ "C" ]   (size = 3, capacity = 3)
```

When you call pop_back(), we change size to 2 and run `~T()` on the last item. The slot is now raw, uninitialized memory, but the memory itself is still part of our vector's allocated block:
```text
ptr ──► [ "A" ] [ "B" ] [ Raw Memory Slot ]   (size = 2, capacity = 3)
```
Because we kept that memory slot, if you instantly call push_back("D"), we don't have to allocate anything! We use placement new to build "D" right into that existing slot:
```text
ptr ──► [ "A" ] [ "B" ] [ "D" ]   (size = 3, capacity = 3)
```

When does that memory actually get deleted?

The memory for that last slot (and all other slots) is finally deleted when the entire vector is destroyed (in the `~Vector()` destructor) or if you explicitly call a function that forces a reallocation resize (like `shrink_to_fit()`).
Until then, keeping the raw bytes allocated is a feature, not a bug—it's what makes vectors so incredibly fast!

# 7. Explicit keyword in constructor.

The explicit keyword is needed to prevent the compiler from performing implicit conversions that can lead to highly confusing, silent bugs.
In C++, any constructor that can be called with a single argument acts as an implicit conversion operator by default. Without explicit, the compiler thinks it is allowed to secretly turn a single size_type (like an integer) into a full Vector object whenever it feels it helps make a function call work.
Here is exactly how things go wrong without it, and how explicit saves you.

The Danger: What happens WITHOUT explicit

Imagine you have a function designed to calculate total stats from a Vector, and someone makes a simple typo:
```cpp
void calculateStats(const Vector<int>& data) {
    // Processes the vector...
}

int main() {
    // Oops! The programmer meant to pass a vector variable, 
    // but made a typo and accidentally passed the integer 100.
    calculateStats(100); 
}
```

• Without `explicit`: This code compiles perfectly without any warnings. The compiler sees that calculateStats expects a `Vector<int>`, but received an `int (100)`. It searches your `class, finds Vector(size_type count)`, and says: "Aha! I can convert this integer 100 into a brand new, temporary Vector containing 100 default-initialized items!"
• The Performance Nightmare: Your program will silently allocate heap memory for 100 elements, pass that temporary vector into the function, run the function, and then instantly delete it when the line finishes.
This makes typos incredibly hard to catch because the compiler hides them by doing extra, invisible memory work.

The Fix: What happens WITH `explicit`

When you add explicit to the constructor:
```cpp
explicit Vector(size_type count);
```
You are telling the compiler: "You are only allowed to use this constructor if the programmer explicitly asks for a Vector creation."
Now, look at how the compiler responds to different code patterns:
```cpp
Vector<int> v1(5);       // OK: Explicitly creating a vector of size 5
Vector<int> v2 = 5;      // ERROR: Direct implicit conversion blocked!

calculateStats(100);     // ERROR: Compiler blocks this and says: 
                         // "Cannot convert 'int' to 'Vector<int>'"
```

Visualizing the Rule of Thumb

In modern C++, you should almost always mark single-argument constructors as explicit unless you are intentionally creating a wrapper type where automatic conversion makes logical sense (like converting a raw `const char*` into a `std::string`).
For a container like a `Vector`, an integer represents a capacity or size count, not an alternate form of a vector, making explicit absolutely mandatory to keep your code clean, readable, and performant.

# 8. `::new (&ptr[i]) T(other.ptr[i]);`

 `::new (&ptr[i])` is pinpointing the exact address in raw memory where the new object should be created.
The second part, `T(other.ptr[i])`, tells the compiler how to initialize that object. It explicitly calls the Copy Constructor of your type `T` to duplicate the data from the source vector.
Here is the exact breakdown of how these two parts work together in harmony:

Breaking Down the Syntax

Think of the placement new syntax as a formula: `::new (address) Type(constructor_arguments)`;
```text
  ::new    (&ptr[i])       T        (other.ptr[i]);
  └──┬──┘  └─────┬────┘    ───┬───     └───────┬────────┘
     │           │            │                │
"Don't allocate" │      "Create an object"     │
"memory!"        │      "of this class type"   │
                 ▼                             ▼
        "Go to this exact address"    "Pass the source item into"
        "in our raw buffer block"     "the Copy Constructor"
```

What is happening step-by-step?

Let's look at what the computer physically does when this line executes, assuming `T` is a `std::string` and we are copying the word "Hello":
1. Find the Destination Address `(&ptr[i])`:
The compiler calculates the exact byte address of slot i in your vector's uninitialized raw memory buffer. No memory is requested from the operating system.
2. Read the Source Object `(other.ptr[i])`:
The compiler looks at slot i of the other vector and sees the existing "Hello" string object sitting there.
3. Trigger the Copy Constructor `(T(...))`:
The compiler wakes up the Copy Constructor of the class `(std::string(const std::string& other))`. It tells it: "Construct a brand new string box directly inside the memory slot we found in Step 1. As for its contents, look at the string box from Step 2, allocate a fresh block of text memory, and duplicate the word 'Hello' into it."

Why can't we just use normal assignment `(ptr[i] = other.ptr[i])`?

This is the most common point of confusion when writing a vector. Why write that long placement new line instead of a simple equals sign?
• What = does: The normal assignment operator `(operator=)` assumes that an object already exists and is alive in that slot. It tries to clean up old values before putting the new value in.
• The Problem: In our vector's fresh buffer, the memory at `ptr[i]` is completely raw and uninitialized. There is no object alive there yet.
• The Crash: If you use =, a type like std::string will try to read its own (garbage) internal pointers to free its "old text." This causes an immediate, catastrophic memory crash.

Summary

The line `::new (&ptr[i]) T(other.ptr[i]);` bridges the gap. It allows you to build a brand new object in uninitialized space while safely using the copy constructor to clone the data out of the other vector, leaving the original data completely untouched.

# 9. Two push_back() functions

The fundamental difference between these two functions is how they transfer the data into your `vector`: the first one copies the data, while the second one moves (steals) the data.
This is the standard C++ implementation for supporting Move Semantics, which eliminates expensive memory allocations when adding temporary values to a container.
Here is a direct breakdown of how they compare:

1. `void push_back(const T& value)` (Copy Version)

• The Parameter: Takes an lvalue reference `(const T&)`. It accepts variables that have a name and a lasting lifespan in your program.
• What it does: It performs a deep copy. It allocates new internal resources for the vector element and duplicates the data from value. The original value remains completely unchanged.
• Use case: When you want to keep using the original object after adding it to the vector.
```cpp
std::string str = "Hello World";
my_vector.push_back(str); // Calls the copy version.
// 'str' is still "Hello World" and perfectly usable here.
```

2. `void push_back(T&& value)` (Move Version)

• The Parameter: Takes an rvalue reference `(T&&)`. It accepts temporary, nameless objects (like a value returned from a function) or objects explicitly cast using std::move.
• What it does: It performs a move construction via `std::move(value)`. Instead of allocating new internal memory and copying data, it steals the internal pointers and resources directly out of value and plugs them into the vector slot.
• Use case: When the source object is temporary, or you no longer need it. This is incredibly fast for heavy objects like strings or other vectors because it avoids allocating new heap memory.
```cpp
// Example A: Passing a temporary object
my_vector.push_back(std::string("Temporary")); // Calls move version automatically.

// Example B: Explicitly moving an existing object
std::string str = "Heavy String Data";
my_vector.push_back(std::move(str)); // Calls move version.
// 'str' is now empty (or in a valid but unspecified state). Do not read its data!
```

Comparison Summary


|Feature	|push_back(const T&)|	push_back(T&&)|
|-----------|-------------------|-----------------|
|Operation Type|	Deep Copy|	Resource Theft (Move)|
|Performance Cost|	High (Allocates memory, copies bytes)|	Extremely Low (Swaps a few pointers)|
|Original Object State|	Unchanged and safe to use|	Empty/gutted (Should not be re-used)|
|Triggered By|	Named variables (str)|	Temporaries or std::move(str)|

