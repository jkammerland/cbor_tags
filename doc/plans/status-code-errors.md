# Replace generic decoder errors

Status: design draft; no runtime behavior changes in this PR.
Baseline: `b751b0d` (0.25.0).

## Problem

`decoder::operator()` in `cbor_decoder.h` catches `std::exception` and
returns `status_code::error` with a `TODO: placeholder`. The same status also
represents malformed arguments, unsupported representations, aliasing, and
internal fallback paths. Callers cannot distinguish these cases.

The catch is only part of the work. Replacing it with one differently named
error would still lose the original parse reason.

## Inventory to complete before implementation

| Source | Current generic paths | Proposed classification |
| --- | --- | --- |
| `cbor_decoder.h`, `operator()` | Unclassified standard exception | Keep `error` as compatibility fallback for trusted customization failures |
| `decode_unsigned` and `get_and_validate_header` | Invalid additional info, wrong major type | Malformed argument versus the existing type-specific mismatch status |
| String payload and container helpers | Unrepresentable size, fixed extent mismatch, input/output overlap | Size/extent or aliasing status after reviewing each existing guarantee |
| `decode_encoded_view` | Scan failures and default expected-major status | Propagate the actual scanner status; distinguish the unused success-path default |
| Header/simple/indefinite decoders | Reserved encodings and invalid item structure | Malformed argument or structure status |
| `detail/cbor_argument.h`, `detail/cbor_item.h` | Invalid headers, unexpected break/chunk, malformed containers | Shared wire-error taxonomy |
| `cbor_traversal.h` | Visitor failure, structure/depth failure, exception fallback | Preserve returned visitor status; separate library parse/limit errors |
| Encoder and extension boundaries | Existing `error` returns | Audit for consistency; change only where the source is known |

Use `rg -n 'status_code::error|throw |TODO' include/cbor_tags` to maintain this
inventory against the implementation branch. Record every migrated site and
every intentionally retained fallback in the implementation PR.

## Proposed approach

1. Append narrowly defined statuses after existing enum values. Preserve all
   existing numeric values, the `uint8_t` representation, public return types,
   and the contiguous range used for retriable variant mismatches.
2. Propose names such as `invalid_additional_info`, `malformed_structure`,
   `input_output_aliasing`, and `unsupported_extent` only after mapping concrete
   sites. Reuse `incomplete`, `size_limit_exceeded`, `unexpected_group_size`, and
   existing mismatch statuses wherever their contracts already fit.
3. Add an internal typed parse failure carrying `status_code` where a primitive
   currently has a value-returning API. Catch it before `std::exception` at
   public boundaries. Prefer direct status propagation where it already exists.
   Do not change codec overload signatures just to remove exception transport.
4. Keep allocation and terminal incomplete classifications intact. Keep `error`
   for exceptions from application codecs that the library cannot classify.
   Do not infer a parse reason by matching `what()` text.
5. Share argument/structure classification with scanners and traversal without
   introducing a scanner or prewalk into typed decoding.
6. Update status messages and document which observable errors become more
   precise. Consumers comparing specifically to `error` need a release note.

An example acceptance case is an invalid integer additional-info encoding:
today it reaches the catch-all; afterward it should report a documented parse
status. A truncated valid integer remains `incomplete`. A codec throwing an
arbitrary `std::runtime_error` remains an unclassified customization failure.

## Invariants

- One-shot failure remains terminal. No cursor or destination rollback.
- Unsized non-contiguous input consumes a declared segment once; no prewalk.
- Reservation still requires the existing range-provided availability check.
- Variant dispatch retries only genuine type mismatches. Structural errors must
  stop dispatch rather than accidentally becoming retriable.
- Public `noexcept` and trusted-code ownership rules remain explicit. The
  separate policy for non-standard exceptions requires an intentional decision;
  this TODO does not authorize silently adding a catch-all.
- Do not classify application schema recursion or allocator behavior as a
  library vulnerability.

## Validation and completion criteria

- Add literal wire tests for each changed classification: reserved additional
  info, truncated arguments, wrong major type, invalid break/chunk, fixed extent
  mismatch, size bound, aliasing, and encoded-view failure.
- Exercise contiguous, sized non-contiguous, and unsized non-contiguous input.
  Assert status and documented prefix consumption; do not require rollback.
- Test variant mismatch retry versus terminal parse failure, allocation failure,
  and a contract-conforming codec returning a status or throwing a standard
  exception. Test message mapping for each newly introduced status directly.
- Keep semantic round trips separate from exact wire fixtures.
- Run the repository Debug CTest, format and tidy gates, followed by the
  standalone ASan fuzz suite. Compare fuzz coverage for argument and variant
  dispatch code before and after the change.
- Remove the placeholder TODO only once the inventory is resolved. Explain
  retained generic fallbacks in code and the release note.

## Review decisions

The implementation PR must settle exact enum names and exception transport.
It should precede or follow the header split as its own behavioral change,
never be hidden among file moves. This draft is ready for design review, not
for merging as an implemented fix.
