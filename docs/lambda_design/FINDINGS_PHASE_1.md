# Phase 1 Analysis: Canonical Function Type (`function<R(Args)>@`)

## 1. Token Sequences and Grammar (Item 1)

### 1.1 Existing Token Sequences in `asCParser::ParseType` (`as_parser.cpp:499-577`)

1. **`array<int>@`**:
   - `ParseDataType()` (`as_parser.cpp:521`):
     - Consumes `ttIdentifier` (`"array"`), creates `snDataType` (`tokenType = ttIdentifier`).
   - Line 525: Peeks token `ttLessThan` (`'<'`).
   - Line 534: `engine->IsTemplateType("array")` is `true`. Calls `ParseTemplTypeList(node)` (`as_parser.cpp:584`):
     - Consumes `ttLessThan` (`'<'`).
     - Calls `ParseType()` recursively for subtype -> produces `snDataType` (`"int"`).
     - Consumes `ttGreaterThan` (`'>'`), breaking token if `>>` or `>>>` (`as_parser.cpp:642`).
   - Line 543: Consumes `ttHandle` (`'@'`), creates `snUndefined` (`tokenType = ttHandle`).
   - **AST Result**: `snDataType` (array) with child `snDataType` (int), followed by child `snUndefined` (@).

2. **`dictionary@`**:
   - `ParseDataType()` (`as_parser.cpp:521`):
     - Consumes `ttIdentifier` (`"dictionary"`), creates `snDataType` (`tokenType = ttIdentifier`).
   - Line 525: Peeks token `ttHandle` (`'@'`). It is not `<`.
   - Line 543: Consumes `ttHandle` (`'@'`), creates `snUndefined` (`tokenType = ttHandle`).
   - **AST Result**: `snDataType` (dictionary) with child `snUndefined` (@).

3. **Nominal Funcdef Handle (e.g. `Callback@` where `funcdef void Callback();`)**:
   - `ParseDataType()` (`as_parser.cpp:521`):
     - Consumes `ttIdentifier` (`"Callback"`), creates `snDataType` (`tokenType = ttIdentifier`).
   - Line 525: Peeks token `ttHandle` (`'@'`).
   - Line 543: Consumes `ttHandle` (`'@'`), creates `snUndefined` (`tokenType = ttHandle`).
   - **AST Result**: `snDataType` (Callback) with child `snUndefined` (@).

### 1.2 Target Grammar for Canonical Function Types
```
CANONICAL_FUNC_TYPE ::= 'function' '<' RETURN_TYPE RETURN_TYPE_MOD? '(' PARAM_LIST? ')' '>' ('@' 'const'?)?
```
Where:
- `RETURN_TYPE` ::= `ParseType(true, false, false)`
- `RETURN_TYPE_MOD` ::= `ParseTypeMod(false)` (handles `&`, `const`)
- `PARAM_LIST` ::= `ParseParameterList()` (handles `( TYPE TYPEMOD IDENTIFIER? (',' TYPE TYPEMOD IDENTIFIER?)* )`)
- Closing delimiter: `'>'` (with token splitting for `>>` and `>>>`)
- Trailing handle: optional `'@'`, optional `'const'`

### 1.3 AST Representation Decision
- **Decision**: Extend `snDataType` rather than introducing a new node type (`snCanonicalFuncType`).
- **Justification with Code Evidence**:
  1. `asCBuilder::CreateDataTypeFromNode` begins with:
     ```cpp
     asASSERT(node->nodeType == snDataType || node->nodeType == snIdentifier || node->nodeType == snScope);
     ```
     (`as_builder.cpp:6446`). All type creation paths throughout the compiler assume the root of a parsed type is `snDataType`.
  2. In `snDataType`:
     - Child 1: `snDataType` for `"function"`.
     - Child 2: `snDataType` for return type.
     - Child 3: `snDataType` for return type modifier.
     - Child 4: `snParameterList` for parameter list.
     - Child 5+: `snUndefined` for `@` handle, `@const`, array brackets `[]`.
  3. When `CreateDataTypeFromNode` resolves `function<...>`, setting `n = paramListNode` lets lines `as_builder.cpp:6705-6750` automatically parse `@`, `@const`, and array dimensions (`function<void()>@[]`) using the existing, hardened builder logic.

---

## 2. Conflict & Disambiguation Analysis (Item 2)

### 2.1 Potential Conflicts Today
1. **User variable named `function` in comparison expression**:
   ```as
   if (function < 10) ...
   bool b = function < x;
   ```
2. **User variable named `function` followed by template call or chained comparison**:
   ```as
   bool b = function < a && b > c;
   ```
3. **Variable declaration or assignment**:
   ```as
   int function = 42;
   function = 10;
   ```

### 2.2 Disambiguation Rule
`function` is a contextual identifier (`FUNCTION_TOKEN = "function"`, `as_tokendef.h:324`), not a reserved keyword.
It is treated as the start of a canonical function type **if and only if**:
1. The token is `function`.
2. The immediately following token is `<` (`ttLessThan`).
3. Lookahead function `IsFunctionType` (`as_parser.cpp:1541`) inspects the enclosed tokens without moving the parser stream and verifies:
   - `<` is followed by a valid return type (`IsDataType` or `ttVoid`).
   - Optional qualifiers/handle/reference on return type.
   - Followed **immediately** by `(` (`ttOpenParenthesis`).
   - Balanced parameter parentheses `(...)`.
   - Followed **immediately** by `>` (`script->code[t.pos] == '>'`).

**Why this is 100% collision-free**:
In AngelScript grammar, an expression `function < x` has `<` followed by an expression term, which is never immediately followed by `(...)` and then `>`. A function call `a(b)` inside `< >` would not have `< RetType ( Params ) >`. Thus, the syntax `< RetType ( Params ) >` is completely unique to function signatures and cannot be parsed as a valid comparison expression.

Furthermore:
- In `asCParser::CheckTemplateType`: returns `false` if `function` is not followed by `<...(...)>`.
- In `asCParser::IsDataType`: returns `false` when `checkValidTypes == true` unless followed by a valid `IsFunctionType` pattern.
- This ensures statements like `function = 5;` parse strictly as assignments to variables named `function`, preserving 100% backward compatibility.

---

## 3. Funcdef Creation: Builder vs Synthetic Engine-Owned `asCScriptFunction` (Item 3)

### 3.1 Existing Nominal Funcdefs
Nominal funcdefs (`funcdef void Callback(int);`) are processed in two passes:
1. `asCBuilder::RegisterFuncDef` (`as_builder.cpp:2055`): Allocates `sFuncDef` and registers nominal name in `module->m_funcDefs`.
2. `asCBuilder::CompleteFuncDef` (`as_builder.cpp:2116`): Parses return type, parameter types, in/out flags into `asCScriptFunction` (`asFUNC_FUNCDEF`). Sets shared traits, checks cross-module duplicates in `engine->funcDefs`.

### 3.2 Canonical Function Type Representation
Canonical types reuse `asCFuncdefType` (`as_typeinfo.h:236`) wrapping a synthetic `asCScriptFunction` (`as_scriptfunction.h:73`).

**Justification**:
1. In the VM (`as_context.cpp:4369`), indirect function calls (`asBC_CallPtr`) expect an object whose type has `asOBJ_FUNCDEF`.
2. In `asCCompiler::ImplicitConversion` (`as_compiler.cpp:8234-8248`), structural subtyping between funcdefs relies on:
   ```cpp
   if( toFunc->IsSignatureExceptNameEqual(fromFunc) )
   ```
   By representing canonical function types as `asCFuncdefType`, structural subtyping with nominal funcdefs is completely automatic and zero-cost.

**Exact fields populated in synthetic `asCScriptFunction`**:
- `engine`: Engine pointer (`this`).
- `module`: Target module (or 0 if engine-scoped).
- `funcType`: `asFUNC_FUNCDEF`.
- `name`: Normalized canonical name: `"function<" + returnType.Format(...) + "(" + params... + ")>"`.
- `nameSpace`: `engine->nameSpaces[0]` (root namespace `""`).
- `returnType`: Resolved `asCDataType` for return type.
- `parameterTypes`: `asCArray<asCDataType>` of resolved parameter types.
- `inOutFlags`: `asCArray<asETypeModifiers>` of parameter modifiers (`asTM_NONE`, `asTM_INREF`, `asTM_OUTREF`, `asTM_INOUTREF`).
- `traits.SetTrait(asTRAIT_SHARED, isShared)`: True if engine-scoped, false if module-scoped.
- `id`: Allocated via `engine->GetNextScriptFunctionId()`. Added via `engine->AddScriptFunction(fd)`.

---

## 4. Canonical Cache Design, Ownership & Refcount Rules (Item 4)

### 4.1 Key Format & Signature Matching
Rather than string lookups, matching uses AngelScript's internal structural equality:
```cpp
funcDef->funcdef->IsSignatureExceptNameEqual(returnType, parameterTypes, inOutFlags, 0, false, false);
```
(`as_scriptfunction.cpp:1126`). This compares exact return type, constness, reference, handle flags, parameter types, and parameter in/out modifiers, ignoring nominal names and namespaces.

### 4.2 Handling of Types Belonging to Discardable Modules (Module-Scoped vs Engine-Scoped)
Consider:
```as
// In Module A
class LocalClass {}
function<void(LocalClass@)>@ cb;
```
If `function<void(LocalClass@)>` were engine-scoped:
- When Module A is discarded, `LocalClass` is destroyed.
- An engine-scoped funcdef would hold a dangling pointer to `LocalClass` in its parameter list.

**The Safe Rule (Matching AngelScript's Shared Type Semantics)**:
1. When resolving a canonical funcdef:
   - Check if `returnType.GetTypeInfo() && !returnType.GetTypeInfo()->IsShared()`.
   - Check if any `paramType.GetTypeInfo() && !paramType.GetTypeInfo()->IsShared()`.
2. **If ANY mentioned type is non-shared (module-scoped)**:
   - The canonical funcdef is **Module-Scoped** (`isShared = false`, `funcDef->module = module`).
   - Stored in `module->m_funcDefs` only.
   - When the module is discarded, `module->Discard()` destroys the funcdef and its script classes together. No leaks, no dangling pointers.
3. **If ALL mentioned types are shared or primitive / application-registered**:
   - The canonical funcdef is **Engine-Scoped** (`isShared = true`, `SetShared(true)`).
   - Stored in `engine->funcDefs`.
   - Reused across all modules.
   - Tracked in `module->m_funcDefs` of referencing modules with `AddRefInternal()`.

### 4.3 Refcount & Release Rules
- `asCFuncdefType` inherits from `asCTypeInfo` and uses `AddRefInternal()` / `ReleaseInternal()`.
- Engine-scoped: `engine->funcDefs` retains a reference until engine shutdown (`asCScriptEngine::ClearInternal` releases all `funcDefs`).
- Module-scoped: Owned by `module->m_funcDefs`. Discarding the module calls `ReleaseInternal()`.

---

## 5. Enumeration and Nominal Assumptions (Item 5)

| API / System Location | Nominal Assumption | Canonical Visibility Requirement |
|---|---|---|
| `asIScriptEngine::GetFuncdefCount()` / `GetFuncdefByIndex()` | Returns `registeredFuncDefs.GetLength()` (`as_scriptengine.cpp:6228`). Only application-registered funcdefs via C++ API. | Canonical types are **NOT** enumerated here (they are not registered via `engine->RegisterFuncdef`). |
| `asIScriptModule::GetObjectTypeCount()` / `GetObjectTypeByIndex()` | Returns `m_classTypes` (`as_module.cpp:1121`). Only class/interface types. | Canonical types are **NOT** enumerated here. |
| `asIScriptModule::GetTypeIdByDecl()` / `GetTypeInfoByDecl()` | Calls `asCBuilder::ParseDataType` (`as_module.cpp:1159`). | **YES**. `mod->GetTypeIdByDecl("function<int(int)>@")` must return the canonical typeId. |
| `asCScriptFunction::GetDeclaration()` | Used by reflection, debuggers, error messages. | **YES**. Must format as `"function<Ret(Params)>"` without nominal prefixes. |
| `as_restore.cpp` (Bytecode Save/Load) | Serializes `module->m_funcDefs` and matches shared types on reload via `IsSignatureExceptNameEqual()`. | **YES**. Canonical funcdefs must be stored in `module->m_funcDefs` so bytecode save/load serializes and restores them seamlessly. |

---

## 6. Public API Impact
- **No changes to public API headers (`angelscript.h`)**.
- Fully backward compatible with existing nominal funcdefs and user code.
- All additions are internal to `as_parser`, `as_builder`, `as_scriptengine`, and `as_scriptfunction`.
