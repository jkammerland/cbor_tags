# CDDL Standard Coverage

This document tracks how `cddl_schema_to` maps C++ types to CDDL grammar from
RFC 8610 and the RFC 9682 updated ABNF.

## Scope

`cddl_schema_to` generates CDDL for the CBOR shapes emitted by the default
encoder and for explicitly documented transform/extension shapes. Named
reflection builds can also generate and validate schemas for explicit named-map
transforms such as `as_named_map<T>`.

Generated schemas intentionally describe type shapes, not value refinements.
For example, a dynamic tag field can be rendered as `#6(payload)` because the
runtime tag number is not available from the static C++ type alone.

## Coverage Matrix

| CDDL / CBOR feature | Standard reference | Support | Test anchor | Notes |
|---|---|---:|---|---|
| CDDL document root rule | RFC 8610/RFC 9682 `cddl`, `rule` | Yes | `CDDL supports root expressions for anonymous schema roots` | Aggregate roots use their generated type name. Tuple/container/tag-marker roots default to `root = ...`; `CDDLOptions::root_name` can override it. |
| CDDL identifiers | RFC 8610/RFC 9682 `typename = id`, `id` | Partial | `CDDL gives colliding C++ short names distinct rule names` | Generated names are sanitized and collision-safe. Exact generated names are not API-stable. |
| Integer major types | RFC 8610 prelude model for `uint`, `nint`, `int` | Yes | `CDDL no columns`; `CDDL supports always_inline and enum underlying integer shapes` | C++ signed integers map to `int`, unsigned integers map to `uint`, and `negative` maps to `nint`. Enums use `uint`/`int` by default and when named enum metadata is unavailable. |
| Text and byte strings | RFC 8610 `tstr`, `bstr` | Yes | `CDDL no columns`; `CDDL emits typed containers and registers nested definitions once` | `std::string` maps to `tstr`; byte ranges map to `bstr`. |
| Floating point and simple values | RFC 8610/RFC 9682 major type 7 | Yes | `CDDL no columns`; `cddl helpers generate prelude and schemas` | `float16_t`, `float`, `double`, `bool`, and `nullptr_t` map to prelude-style names. Generic `simple` maps to `#7`. |
| Aggregate arrays | RFC 8610/RFC 9682 `[ group ]` | Yes | `CDDL emits RFC 8610 shapes for aggregate arrays and tag payloads` | Multi-field aggregates render as fixed arrays. Single-field aggregates render as the single payload shape, matching encoder behavior. |
| Named-map structs | RFC 8610 §3.5.1 Figures 1, 3, 4, 5, 7 | Named reflection | `named-map CDDL covers RFC 8610 map and group examples`; `named-map CDDL covers RFC 8610 group factorization and personal data examples` | `as_named_map<T>` uses reflected member names as text-string keys. `std::optional<T>` fields render as optional entries and are omitted on encode when empty. |
| Named groups | RFC 8610 §2.1, §3.5.1 Figures 2, 3, 6 | Named reflection | `named-map CDDL covers RFC 8610 map and group examples` | `as_named_group<T>` emits reusable CDDL groups and flattens group members into containing named maps. |
| Sequence containers | RFC 8610/RFC 9682 occurrence `*` in array groups | Yes | `CDDL emits typed containers and registers nested definitions once` | Variable sequences render as `[* value]`; fixed-size `std::array`/static `std::span` render as `[N*N value]`. |
| Maps | RFC 8610/RFC 9682 `{ group }`, `memberkey =>` | Yes | `CDDL emits typed containers and registers nested definitions once`; `CDDL groups choices in map keys and repeated item positions` | Map keys and values are typed. Choice keys are parenthesized so they fit `memberkey = type1 =>`. |
| Typed extension entries | RFC 8610 §3.5.1 Figure 7 | Named reflection | `named-map CDDL covers RFC 8610 group factorization and personal data examples`; `named-map codec handles optionals, groups, and typed extensions` | `as_named_extension<std::map<std::string, T>>` renders as `* tstr => T` and captures unmatched text keys during decode. Exact arbitrary `any` values are not modeled yet. |
| Type choices | RFC 8610/RFC 9682 `type = type1 *( "/" type1 )` | Yes | `CDDL groups choices in map keys and repeated item positions` | `std::variant` renders as `A / B`; `std::optional<T>` renders as `T / null`. Choices are grouped when embedded in positions that require `type1`. Variants whose alternatives contain nullable smart pointers are intentionally rejected as a conservative generator limitation. |
| Nullable smart pointers | Extension shape using RFC 8610/RFC 9682 type choices | Yes | `CDDL emits nullable pointer shapes for the smart pointer codec` | `std::unique_ptr<T>` and `std::shared_ptr<T>` with default-initializable, non-const, non-void, non-array pointees render as `[0] / [1, T]`, matching the opt-in `nullable_ptr_codec` wire shape. Pointer identity and `shared_graph_codec` tag 28/29 semantics are not expressed in CDDL. |
| Named enum choices | RFC 8610/RFC 9682 `&(...)` enumeration expressions | Optional | `CDDL can emit named enum choices`; `CDDL reuses named enum definitions inside aggregate schemas` | Requires `CBOR_TAGS_USE_STD_REFLECTION=ON` or `CBOR_TAGS_USE_MAGIC_ENUM_NAMES=ON`, plus `CDDLOptions::enum_mode = CDDLEnumMode::named_values`. Output is stricter than the decoder's current underlying-integer enum policy. |
| Static tags | RFC 8610/RFC 9682 `#6.n(type)` | Yes | `CDDL emits RFC 8610 shapes for aggregate arrays and tag payloads`; `cddl helpers cover tuple and tagged tuple schemas` | Static tag members and tagged tuples render exact tag numbers. |
| Dynamic tags | RFC 8610/RFC 9682 `#6(type)` | Partial | `CDDL emits RFC 8610 shapes for aggregate arrays and tag payloads` | Runtime tag number is not known from the type; schema constrains only "some tag with this payload shape". |
| Recursive type rules | RFC 8610 matching rules allow rule names to be used in themselves | Yes | `CDDL supports recursive aggregate containers` | Recursive aggregates emit named rules, e.g. `Node = [* Node]`. `always_inline` keeps recursive references named. |
| Anonymous roots | RFC 8610 first rule defines document semantics | Yes | `CDDL supports root expressions for anonymous schema roots` | Non-aggregate roots are wrapped in a rule named `root` unless `root_name` is set. |
| Empty aggregates | CBOR data item model | Unsupported | `CDDL supports root expressions for anonymous schema roots` | The default encoder emits no CBOR item for an empty aggregate; there is no CDDL data-item shape to describe. |
| CDDL generics | RFC 8610/RFC 9682 `genericparm`, `genericarg` | Unsupported | None | Out of scope for generated schemas. |
| CDDL control operators | RFC 8610/RFC 9682 `ctlop` | Unsupported | None | Constraints such as `.size`, `.bits`, `.regexp`, and `.cbor` are not generated. |
| CDDL group choices | RFC 8610/RFC 9682 `//` | Unsupported | None | The generator emits type choices with `/`, not reusable group choices. |
| Value literals and ranges | RFC 8610/RFC 9682 `value`, `rangeop` | Partial | `CDDL can emit named enum choices` | Named enum mode emits integer value literals for declared enumerators. The generator does not otherwise infer literal values or numeric bounds from C++ types. |

## Regression Targets

The current standard-sensitive regression tests live primarily in
`test/test_cddl.cpp`:

- `CDDL emits RFC 8610 shapes for aggregate arrays and tag payloads`
- `CDDL emits typed containers and registers nested definitions once`
- `CDDL emits nullable pointer shapes for the smart pointer codec`
- `cddl_variant_nullable_pointer_compile_fails`
- `CDDL supports recursive aggregate containers`
- `CDDL gives colliding C++ short names distinct rule names`
- `CDDL groups choices in map keys and repeated item positions`
- `CDDL supports root expressions for anonymous schema roots`
- `CDDL supports always_inline and enum underlying integer shapes`
- `CDDL can emit named enum choices`
- `CDDL reuses named enum definitions inside aggregate schemas`
- `named-map CDDL covers RFC 8610 map and group examples`
- `named-map CDDL covers RFC 8610 group factorization and personal data examples`
- `named-map codec handles optionals, groups, and typed extensions`

Tuple and tagged-tuple schema/encoding alignment is covered in
`test/test_visualization_coverage.cpp`.

## Review Rule

When adding new generated CDDL syntax, update this matrix in the same change as
the implementation and regression test. If support is intentionally partial,
document the exact edge case and why it is partial.
