<p align="center"><img src="https://repository-images.githubusercontent.com/1036687907/6cd0e5bb-3856-4ad1-ac4c-da02be8e3ed7" alt="angelscript logo" width="50%" height="50%"></p>

The AngelCode Scripting Library, or AngelScript as it is also known, is an extremely flexible cross-platform scripting library designed to allow applications to extend their functionality through external scripts. It has been designed from the beginning to be an easy to use component, both for the application programmer and the script writer.

The official repository for AngelScript is: https://github.com/anjo76/angelscript  
Please go to https://www.angelcode.com/angelscript for more information.

---

# Experimental Prototype: First-Class Lambdas and Closures

> [!NOTE]
> **AI-Assisted Prototype Notice**
> 
> This fork introduces an **experimental prototype** adding first-class **Anonymous Functions (Lambdas)**, **Stateful Closures (Variable Captures)**, and **Canonical Function Types (`function<R(...)>@`)** to AngelScript.
> 
> - **Developed with AI Assistance**: This implementation was developed collaboratively using AI assistance (Google DeepMind / Antigravity pair programming), following AngelScript's internal architecture, conventions, and strict C++98 standards.
> - **Status and Scope**: While the prototype compiles cleanly across MSVC (Windows) and GCC (Linux) and passes 100% of the regression and test suites with zero memory leaks, **it is an experimental prototype that still requires deeper, in-depth testing and edge-case validation**. However, it provides a practical and modern capability for anonymous functions in AngelScript.

---

## Motivation and Benefits

In standard AngelScript, passing callbacks requires considerable boilerplate:
1. You must declare a nominal `funcdef` ahead of time (`funcdef bool Predicate(int);`).
2. You must define a separate named global or member function.
3. If you need local state or context, you must manually write a wrapper class or use global variables.

With this prototype, you can write concise inline logic, capture local variables directly, and pass callbacks naturally.

---

## Practical Examples

### 1. Inline Anonymous Callback (No funcdef Boilerplate)
Instead of declaring a `funcdef` and an auxiliary global function, pass anonymous functions directly:

```asc
// Before: needed a global funcdef + named helper function
// Now: inline lambda expression
void ProcessNumbers() {
    array<int> numbers = { 1, 2, 3, 4, 5, 6 };

    // Pass inline lambda directly as a filter or comparator
    numbers.sort(function(int a, int b) {
        return a > b; // Descending order
    });
}
```

---

### 2. Capturing Local Variables (Closures)
Capture local variables by value into an isolated closure instance:

```asc
void SetupMultiplier() {
    int factor = 10;

    // Capture 'factor' inside square brackets [factor]
    auto@ multiply = function[factor](int x) {
        return x * factor;
    };

    int result = multiply(5); // Returns 50
}
```

---

### 3. Stateful Counters & Accumulators
Stateful closures retain and modify their own captured state across invocations without polluting the global scope:

```asc
function<int()>@ MakeCounter(int start) {
    int count = start;
    return function[count]() {
        count++;
        return count;
    };
}

void Test() {
    auto@ counter = MakeCounter(10);
    print(counter()); // Prints 11
    print(counter()); // Prints 12
    print(counter()); // Prints 13
}
```

---

### 4. Capturing [this] in Class Methods
Easily capture the current class instance to create self-contained event handlers and callbacks:

```asc
class Player {
    int health = 100;

    function<void(int)>@ GetHealAction() {
        // Captures 'this' reference via an internal delegate
        return function[this](int amount) {
            this.health += amount;
            print("Current HP: " + this.health);
        };
    }
}

void Main() {
    Player hero;
    auto@ heal = hero.GetHealAction();
    heal(25); // Current HP: 125
}
```

---

### 5. Seamless Interoperability with Existing funcdef APIs
Existing APIs that expect nominal `funcdef` handles can accept anonymous lambdas transparently through automatic implicit conversion:

```asc
// Existing engine or host API funcdef:
funcdef void OnTimerCallback(int elapsedMs);

void StartTimer(int delay, OnTimerCallback@ callback) {
    callback(delay);
}

void Run() {
    // Implicit conversion from lambda to OnTimerCallback@:
    StartTimer(1000, function(int elapsedMs) {
        print("Timer triggered after " + elapsedMs + " ms");
    });
}
```

---

### 6. Canonical Function Types & auto@
You can declare function variables explicitly using canonical function types, or let `auto@` infer the type automatically:

```asc
// Explicit canonical function signature:
function<string(int, string)>@ formatter = function(int id, string name) {
    return "[" + id + "] " + name;
};

// Automatic signature inference:
auto@ square = function(int x) {
    return x * x;
};
```

---

## Technical Highlights

- **100% C++98 Compliant**: Written entirely in standard C++98 with no C++11 dependencies. Tested under MSVC (Windows) and GCC 11+ with `-std=c++98 -Wall -Wextra`.
- **Bytecode Serialization**: Fully compatible with `asIScriptModule::SaveByteCode()` and `LoadByteCode()`.
- **Garbage Collection (GC)**: Stateful closures (`$Closure`) automatically implement cycle detection and register with AngelScript's GC.
- **High Performance**: Canonical function types are cached globally and per-module; lookups use fast signature hashes and bitwise traits (`asTRAIT_CANONICAL`, `asTRAIT_LAMBDA`) instead of linear string parsing.
- **Zero Breaking Changes**: Existing AngelScript scripts and host bindings remain 100% compatible.

---

## Testing and Verification

The prototype includes dedicated test suites under `sdk/tests/test_feature/source/test_lambda.cpp`:
- Anonymous lambdas, nested lambdas, and recursion.
- Value captures, handle captures, and `[this]` delegate captures.
- Bytecode save and reload roundtrips.
- Garbage collection cycle collection.
- Nominal `funcdef` compatibility.

To run the test suite on Linux (GCC):
```bash
cd sdk/tests/test_feature/projects/gnuc
make
./testgnuc
```

To run the test suite on Windows (MSVC):
```cmd
cd sdk/tests/test_feature/projects/msvc2019
msbuild /p:Configuration=Release
bin/test_feature.exe
```
