# Phase 5 Analysis & Findings: Implicit Conversion Between function<R(Args)> and Nominal funcdefs

**Document Status**: Complete Read-Only Analysis (Awaiting User Plan Approval)  
**Branch**: `feature/lambda-closures`

---

## 1. Type Equality & Comparison Matrix

In AngelScript, type comparison and compatibility checking happen in several specific places in the parser, compiler, and runtime engine.

### 1.1 Compiler & Engine Comparison Sites
1. **`asCDataType::operator==` (`as_datatype.cpp:511`)**:
   - Compares `tokenType`, `typeInfo`, `isReference`, `isReadOnly`, and `isConstHandle`.
   - Requires identical `typeInfo` pointers.
2. **`asCDataType::IsEqualExceptRefAndConst` (`as_datatype.cpp:530`)**:
   - Used in declaration matching and type validation.
   - Requires identical `typeInfo` pointers (`typeInfo == dt.typeInfo`).
3. **`asCCompiler::ImplicitConversion` (`as_compiler.cpp:8715-8728`)**:
   - Checks if `to.IsFuncdef()` and `from.IsFuncdef()`.
   - Calls `toFunc->IsSignatureExceptNameEqual(fromFunc)`.
   - If signatures match, rewrites expression typeInfo (`ctx->type.dataType.SetTypeInfo(to.GetTypeInfo())`) and returns `asCC_REF_CONV` (cost 10).
4. **`asCCompiler::MatchArgument` (`as_compiler.cpp:15903-15905`)**:
   - Evaluates implicit conversion cost for function arguments during overload resolution.
   - Exact match returns cost `asCC_NONE` (0).
   - Funcdef conversion returns cost `asCC_REF_CONV` (10).
5. **`asCScriptEngine::RefCastObject` (`as_scriptengine.cpp:5404-5417`)**:
   - Runtime execution of handle casts (`asBC_Cast`).
   - If both types are `asOBJ_FUNCDEF`, compares `fromFunc->funcdef->IsSignatureExceptNameEqual(toFunc->funcdef)`.
   - If signatures match, returns `asSUCCESS` and AddRefs the handle.
6. **`asCCompiler::CompileConversion` (`as_compiler.cpp:13150-13170`)**:
   - Compiles explicit cast expressions `cast<T>(expr)`.
   - Calls `ImplicitConversion` first. If compatible, succeeds at compile time without emitting runtime cast instructions.
7. **Handle Identity (`is` / `!is`)**:
   - Compares raw pointer addresses in registers (`asBC_CmpPtr`). Works across all handle types regardless of typeInfo.

### 1.2 Funcdef Comparison Matrix

| Operation | Canonical / Canonical | Canonical / Nominal | Nominal / Nominal (Same) | Nominal / Nominal (Diff Name, Same Sig) | Nominal / Nominal (Diff Sig) |
|---|---|---|---|---|---|
| **`operator==` (Type Equality)** | **TRUE** (shared `typeInfo`) | **FALSE** (distinct `typeInfo`) | **TRUE** (identical `typeInfo`) | **FALSE** (distinct `typeInfo`) | **FALSE** |
| **`ImplicitConversion` (Assignment, Args, Return)** | **Exact Match** (cost 0) | **Compatible** (`asCC_REF_CONV`, cost 10) | **Exact Match** (cost 0) | **Compatible** (`asCC_REF_CONV`, cost 10)* | **INCOMPATIBLE** (error) |
| **Overload Ranking (passing type X)** | **Cost 0** (Exact) | **Cost 10** (`asCC_REF_CONV`) | **Cost 0** (Exact) | **Cost 10** (`asCC_REF_CONV`) | **No Match** (-1) |
| **`cast<T>(expr)` (Explicit Cast)** | **Valid** (No-op) | **Valid** (`asCC_REF_CONV`) | **Valid** (No-op) | **Valid** (`asCC_REF_CONV`) | **Compile Error** |
| **Runtime `RefCastObject`** | **`asSUCCESS`** | **`asSUCCESS`** | **`asSUCCESS`** | **`asSUCCESS`** | **`asERROR`** (null) |
| **Handle Comparison (`is`, `!is`)** | **Valid** | **Valid** | **Valid** | **Valid** | **Valid** |

*\*Note on Nominal/Nominal (Different Name, Same Signature)*:  
As verified in Phase 0 audit (`FINDINGS_PHASE_0.md` section 2.4) and `test_functionptr.cpp:2348-2353`, AngelScript 2.31+ already supports implicit conversion between nominal funcdefs with identical signatures via `IsSignatureExceptNameEqual`. This behavior is preserved to maintain 100% baseline regression compatibility. Nominal funcdefs with differing signatures remain strictly incompatible (compile error).

---

## 2. Precise Definition of "Identical Signature"

In AngelScript, signature equality between function definitions is governed by `asCScriptFunction::IsSignatureExceptNameEqual` (`as_scriptfunction.cpp:1124-1160`).

### 2.1 Specification of Identical Signature
Two funcdefs $F_1$ and $F_2$ have an **identical signature** if and only if all of the following conditions hold:
1. **Return Type**:
   - `F1.returnType == F2.returnType` (base type, object type, handle flag, constness, reference modifier must match exactly).
2. **Parameter Count**:
   - `F1.parameterTypes.GetLength() == F2.parameterTypes.GetLength()`.
3. **Parameter Types**:
   - For every parameter index $i \in [0, N-1]$:
     - `F1.parameterTypes[i] == F2.parameterTypes[i]` (base type, object type, handle flag, constness must match).
4. **Parameter In/Out Modifiers**:
   - For every parameter index $i \in [0, N-1]$:
     - `F1.inOutFlags[i] == F2.inOutFlags[i]` (exact match of `asTM_NONE`, `asTM_INREF`, `asTM_OUTREF`, `asTM_INOUTREF`).
5. **Const Modifier**:
   - `F1.IsReadOnly() == F2.IsReadOnly()`.
6. **Receiver / Object Type**:
   - `(F1.objectType != 0) == (F2.objectType != 0)`.
7. **Variadic Flag**:
   - `F1.IsVariadic() == F2.IsVariadic()`.

### 2.2 Default Arguments
- `defaultArgs` are **ignored** for compatibility.
- *Rationale*: A funcdef handle represents a callable function pointer interface. Default parameter expressions belong to call-site syntax bindings, not the underlying function calling convention or type interface. This matches the behavior of `IsSignatureExceptNameEqual`.

---

## 3. Runtime Representation & VM Interoperability

### 3.1 Handle Interchangeability at Runtime
- In AngelScript, all function handles (whether nominal funcdef `Callback@` or canonical `function<R(Args)>@`) are represented uniformly at runtime as a pointer to `asCScriptFunction`:
  - Plain functions: `asCScriptFunction*` with `funcType == asFUNC_SCRIPT` or `asFUNC_SYSTEM`.
  - Closures / delegates: `asCScriptFunction*` with `funcType == asFUNC_DELEGATE`.
- Because the runtime representation is an identical pointer type, assigning between canonical and nominal handles is a **direct pointer copy** (`asBC_PshVPtr`, `asBC_STOREOBJ`, `asBC_REFCPY`).
- **No VM bytecode translation or wrapper objects are needed at runtime.**

### 3.2 Ref Cast Verification (`cast<T>`)
- Compile-time explicit cast (`cast<Nominal>(canHandle)`):
  - In `CompileConversion` (`as_compiler.cpp:13152`), `ImplicitConversion` returns `asCC_REF_CONV`.
  - The compiler converts the expression type directly at compile time.
- Runtime dynamic cast (`asBC_Cast`):
  - Evaluated by `asCScriptEngine::RefCastObject` (`as_scriptengine.cpp:5404`).
  - Directly compares `fromFunc->funcdef->IsSignatureExceptNameEqual(toFunc->funcdef)`.
  - Reinterprets the pointer and calls `AddRefScriptObject`, returning `asSUCCESS`.

---

## 4. Overload Resolution Ranking & Cost Rules

### 4.1 Cost Ranking Structure
AngelScript uses `enum EConvCost` (`as_compiler.h:201`):
```
asCC_NO_CONV                  = 0   (Exact match)
asCC_CONST_CONV               = 1   (Add/remove const)
...
asCC_REF_CONV                 = 10  (Funcdef signature conversion / Reference conversion)
asCC_OBJ_TO_PRIMITIVE_CONV    = 12  (opConv to primitive)
asCC_TO_OBJECT_CONV           = 14  (Constructor / opImplConv)
```

### 4.2 Behavior on Overloaded Functions
1. **Target Specificity**:
   ```angelscript
   void Process(Callback@ cb);            // (1)
   void Process(function<void(int)>@ f);  // (2)
   ```
   - When called with `function<void(int)>@`:
     - Overload (2) matches with cost 0 (exact match).
     - Overload (1) matches with cost 10 (`asCC_REF_CONV`).
     - **Result**: Overload (2) is chosen cleanly without ambiguity.
   - When called with `Callback@`:
     - Overload (1) matches with cost 0 (exact match).
     - Overload (2) matches with cost 10 (`asCC_REF_CONV`).
     - **Result**: Overload (1) is chosen cleanly without ambiguity.
2. **Compromised Ambiguity (Lambda Argument)**:
   - When called with untyped lambda: `Process(function(int x) {})`:
     - Both overloads convert via `ImplicitConvLambdaToFunc` with cost `asCC_CONST_CONV` (1).
     - Because costs are identical, AngelScript correctly detects and reports an ambiguous call:
       `Multiple matching signatures to 'Process(...)'`.
3. **Single Funcdef Overload with Other Types**:
   ```angelscript
   void Dispatch(Callback@ cb);
   void Dispatch(int x);
   ```
   - When called with `function<void(int)>@`:
     - Matches `Dispatch(Callback@)` with cost 10.
     - Does not match `Dispatch(int)`.
     - **Result**: Successfully calls `Dispatch(Callback@)`.

---

## 5. Host API & Registered Function Interactions

### 5.1 C++ Registered Functions with Nominal Funcdef Parameter
When C++ registers:
```cpp
engine->RegisterFuncdef("void MyCallback(int)");
engine->RegisterGlobalFunction("void HostRegister(MyCallback@ cb)", ...);
```
In AngelScript:
- A script passing `function<void(int)>@` to `HostRegister` compiles successfully via `asCC_REF_CONV`.
- A script passing a lambda `function(int x) { ... }` compiles successfully via `ImplicitConvLambdaToFunc`.
- The C++ host receives the underlying `asIScriptFunction*` (script function or delegate).

### 5.2 C++ Registered Functions with Canonical Function Type Parameter
When C++ registers:
```cpp
engine->RegisterGlobalFunction("void HostRegister(function<void(int)>@ cb)", ...);
```
- In `asCBuilder::ParseFunctionDeclaration`:
  - `asCParser::ParseType` contextually recognizes `function<void(int)>`.
  - `CreateDataTypeFromNode` queries `engine->GetCanonicalFuncdef` and binds the parameter as the canonical funcdef type.
- Script passing nominal `MyCallback@` converts cleanly via `asCC_REF_CONV`.

### 5.3 Script Callback Invocation from Host
- Host prepares context: `ctx->Prepare(fn)`.
- Host sets argument: `ctx->SetArgDWord(0, 42)`.
- Host executes: `ctx->Execute()`.
- Standard AddRef/Release refcount semantics apply uniformly to both nominal and canonical handles.

---

## 6. Token Splitting in Explicit Cast (`cast<function<...>>`)

### 6.1 Issue Identified
In `as_parser.cpp:1780` (`ParseCast`):
```cpp
// BNF:12: CAST ::= 'cast' '<' TYPE '>' '(' ASSIGN ')'
GetToken(&t1);
if( t1.type != ttGreaterThan )
{
    Error(ExpectedToken(">"), &t1);
    ...
}
```
When parsing:
```angelscript
auto f = cast<function<void()>>(nomHandle);
```
The closing `>` of `function<void()>` and the closing `>` of `cast<...>` are adjacent (`>>`). The tokenizer produces `ttRightShift`. Because `ParseCast` did not split `>>`, this produced a syntax error `Expected token '>' (Instead found '>>')`.

### 6.2 Solution
In `ParseCast`:
If `script->code[t1.pos] == '>'`:
Split the token by calling `SetPos(t1.pos + 1)`.
This matches the existing token splitting logic in `ParseTemplTypeList` (`as_parser.cpp:641`), allowing `cast<function<R(Args)>>(handle)` to parse without requiring artificial whitespace.
