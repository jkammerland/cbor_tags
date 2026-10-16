# Split decoder and visualization implementation headers

Status: design draft; no headers moved or APIs changed in this PR.
Baseline: `b751b0d` (0.25.0): `cbor_decoder.h` has 2,189 lines;
`extensions/cbor_visualization.h` has 2,480 lines.

## Objective

Make the decoder and visualization responsibilities easier to review while
preserving existing public include paths, templates, overload resolution, wire
behavior, status codes, and output text.

The existing `detail/cbor_argument.h`, `detail/cbor_item.h`,
`detail/cbor_raw_view_decode.h`, and `detail/cbor_variant_dispatch.h` already
provide useful boundaries. Extend these boundaries before adding new ones.

## Decoder boundaries

| Responsibility | Proposed home | Constraints |
| --- | --- | --- |
| Integer representability and segment availability helpers | `detail/cbor_decode_bounds.h` | Keep unsized input single-pass and reservation rules unchanged |
| Scalar, string, container and aggregate decoder member definitions | Small `detail/cbor_decode_*.h` headers as needed | Out-of-class definitions of the existing `decoder`; preserve access, constraints, and overload set |
| Variant dispatch | Existing `detail/cbor_variant_dispatch.h` plus member definitions | Preserve mismatch retry ordering and overload fast paths |
| Header/indefinite decoder mixins | `detail/cbor_header_decoder.h` | Retain existing mixin names and default composition |
| Public class declarations, aliases and factories | `cbor_decoder.h` | Remains the supported include and defines the same public types |

Start with non-member helpers and existing mixins. Extract member definitions
only after a prototype proves the constrained template syntax and include order
on supported compilers. Avoid a new inheritance hierarchy: changes to `Self`,
member lookup, layout, or overload visibility would exceed a mechanical split.
Do not splice anonymous `.inc` fragments into the middle of the class.

## Visualization boundaries

| Responsibility | Proposed home |
| --- | --- |
| Shared option structures and enums | `detail/cbor_visualization_options.h` |
| CDDL context, naming, type expressions and schema emission | `detail/cbor_cddl_schema.h` |
| Annotation byte formatting and `smart_annotator` | `detail/cbor_annotation.h` |
| Diagnostic scalar formatting, renderer and visitor | `detail/cbor_diagnostic.h` |
| Public entry points and includes | `extensions/cbor_visualization.h` |

The options retain their `cbor::tags` names, defaults and aggregate layout even
if their definitions move. Public entry points include `cddl_schema_to`,
`cddl_prelude_to`, `buffer_annotate`, `buffer_diagnostic`, and the existing
diagnostic visitor helpers. Preserve currently exposed detail types used as
template defaults, including `detail::CDDLContext`.

Each helper header has `#pragma once`, explicit dependencies, and namespace
boundaries. Include implementation headers outside namespace blocks. Shared
format helpers must have one definition with the same inline/template linkage.
Do not turn optional visualization dependencies into core decoder dependencies.

## Work sequence

1. Record baseline checks and representative preprocessing/compile timing with
   the normally installed toolchain. Header splitting alone does not promise a
   compile-time improvement.
2. Extract decoder bounds helpers and header mixins in a mechanical commit.
3. Extract CDDL, annotation and diagnostic responsibilities in separate commits.
4. Reassess decoder member size. Move coherent member-definition blocks only
   when the declarations remain readable; retain simple scalar definitions if
   moving them creates more duplication than useful separation.
5. Verify installed FILE_SET packaging includes every new header. Add compile
   probes for the supported public include paths and representative codec packs.
6. Run all required validation, compare results, and update this plan with the
   final header map before marking the implementation PR ready.

## Validation

- Run Debug CTest, formatting and clang-tidy. Build with supported GCC and Clang
  on Linux; use the repository Windows/macOS CI for portability evidence.
- Preserve exact diagnostic, annotation and CDDL snapshots, including malformed
  wire cases and non-default options. Test suites already provide the baseline.
- Compile multiple translation units to detect missing inline linkage and
  include-order dependencies. Exercise core-only includes independently of
  visualization, and visualization with named reflection backends where available.
- Check the installed consumer and source package contain the extracted headers.
- Run the standalone ASan fuzz suite against both revisions with the same
  persisted corpus. Compare semantic properties and coverage without requiring
  identical source-line numbers after file moves.
- Exercise unsized ranges, borrowed views, strict integers, extension overloads,
  variants, traversal and terminal incomplete input. Do not add prewalks or
  rollback while reorganizing code.

## Scope and review risks

No status taxonomy change, formatting output change, new codec API, renamed
public header, or generated reflection rewrite belongs in these commits. Keep
the generic status-code TODO effort separate so reviewers can attribute any
behavior change. Watch dependent-name lookup, constrained overload definitions,
default template arguments, cyclic includes, ADL, and macro configuration order.

This draft supplies an implementation and acceptance plan. Completion requires
the actual mechanical changes and validation evidence in a follow-up revision.
